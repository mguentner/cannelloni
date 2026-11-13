/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
 *
 * Copyright (C) 2014-2023 Maximilian Güntner <code@mguentner.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#ifdef __GLIBCXX__
#include <bits/stdc++.h>
#endif

#include <linux/can.h>
#include <string.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/timerfd.h>


#include <net/if.h>
#include <arpa/inet.h>

#include <netinet/tcp.h>

#include "cannelloni.h"
#include "connection.h"
#include "inet_address.h"
#include "logging.h"
#include "parser.h"
#include "tcpthread.h"

namespace {
  /* Lets the decoder report the size of the first chunk of the next frame */
  void startNextFrame(Decoder &decoder) {
    decoder.expectedBytes = decodeFrame(NULL, 0, &decoder.tempFrame, &decoder.state);
  }
}

TCPThread::TCPThread(const struct debugOptions_t &debugOptions,
                     const struct TCPThreadParams &params)
   : ConnectionThread()
  , m_debugOptions(debugOptions)
  , m_serverSocket(0)
  , m_rxCount(0)
  , m_txCount(0)
  , m_addressFamily(params.addressFamily)
{

  memcpy(&m_remoteAddr, &params.remoteAddr, sizeof(struct sockaddr_storage));
  memcpy(&m_localAddr, &params.localAddr, sizeof(struct sockaddr_storage));
}

/* Not closed in run(), as the CAN thread may signal the pipe until it is stopped */
TCPThread::~TCPThread() {
  close(m_framebufferHasDataPipe[SIGNAL_PIPE_READ]);
  close(m_framebufferHasDataPipe[SIGNAL_PIPE_WRITE]);
}

int TCPThread::start() {
  if (!setupPipe()) {
    return -1;
  }
  return Thread::start();
}

int TCPThread::listenSocket() {
  return -1;
}

void TCPThread::run() {
  /* Set interval to m_timeout */
  m_blockTimer.adjust(SELECT_TIMEOUT, SELECT_TIMEOUT);

  while (m_started) {
    if (m_connections.empty() && listenSocket() < 0) {
      int socket = attempt_connect();
      if (socket >= 0) {
        addConnection(socket);
      }
    } else {
      waitForEvents();
    }
    m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
                                       [](const TCPConnection &connection) { return connection.state == DISCONNECTED; }),
                        m_connections.end());
  }
  if (m_debugOptions.buffer) {
    m_frameBuffer->debug();
  }
  linfo << "Shutting down. TCP Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << std::endl;
  for (TCPConnection &connection : m_connections) {
    closeConnection(connection, "shutting down");
  }
  m_connections.clear();
  cleanup();
}

void TCPThread::waitForEvents() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(m_blockTimer.getFd(), &readfds);
  FD_SET(m_framebufferHasDataPipe[SIGNAL_PIPE_READ], &readfds);
  int maxFd = std::max(m_blockTimer.getFd(), m_framebufferHasDataPipe[SIGNAL_PIPE_READ]);
  int listenFd = listenSocket();
  if (listenFd >= 0) {
    FD_SET(listenFd, &readfds);
    maxFd = std::max(maxFd, listenFd);
  }
  for (const TCPConnection &connection : m_connections) {
    FD_SET(connection.socket, &readfds);
    maxFd = std::max(maxFd, connection.socket);
  }

  int ret = select(maxFd+1, &readfds, NULL, NULL, NULL);
  if (ret < 0) {
    lerror << "select error" << std::endl;
  } else {
    if (FD_ISSET(m_blockTimer.getFd(), &readfds)) {
      m_blockTimer.read();
      /*
         Let's flush out the frame buffer as well as there might be single frames left
         that have not been signaled through the pipe due to blocking
      */
      flushFrameBuffer();
    }
    if (FD_ISSET(m_framebufferHasDataPipe[SIGNAL_PIPE_READ], &readfds)) {
      int signal;
      ssize_t res = read(m_framebufferHasDataPipe[SIGNAL_PIPE_READ], &signal, sizeof(signal));
      if (res == sizeof(signal)) {
        flushFrameBuffer();
      }
    }
    /* Connections closed above have an invalid socket, skip them */
    for (TCPConnection &connection : m_connections) {
      if (connection.state != DISCONNECTED && FD_ISSET(connection.socket, &readfds)) {
        receive(connection);
      }
    }
    /* Accept last, so a new socket cannot reuse the number of one closed above */
    if (listenFd >= 0 && FD_ISSET(listenFd, &readfds)) {
      int socket = attempt_connect();
      if (socket >= 0) {
        addConnection(socket);
      }
    }
  }
}

void TCPThread::addConnection(int socket) {
  uint8_t protocolVersionBuffer[] = CANNELLONI_CONNECT_V1_STRING;
  ssize_t res = send(socket, protocolVersionBuffer, sizeof(protocolVersionBuffer)-1, MSG_NOSIGNAL);
  if (res == sizeof(protocolVersionBuffer)-1) {
    TCPConnection connection;
    connection.socket = socket;
    connection.state = CONNECTED;
    connection.bufferedBytes = 0;
    struct sockaddr_storage peerAddr;
    socklen_t peerAddrLen = sizeof(peerAddr);
    if (getpeername(socket, (struct sockaddr *) &peerAddr, &peerAddrLen) == 0) {
      connection.peer = formatSocketAddress(getSocketAddress(&peerAddr));
    }
    connection.rxCount = 0;
    connection.txCount = 0;
    m_connections.push_back(connection);
  } else {
    lerror << "write error could not announce protocol" << std::endl;
    close(socket);
  }
}

void TCPThread::closeConnection(TCPConnection &connection, const char *reason) {
  close(connection.socket);
  connection.socket = -1;
  connection.state = DISCONNECTED;
  linfo << "Connection to " << connection.peer << " closed: " << reason
        << ". TX: " << connection.txCount << " RX: " << connection.rxCount << std::endl;
}

void TCPThread::receive(TCPConnection &connection) {
  ssize_t chunkSize;
  if (connection.state == CONNECTED) {
    chunkSize = sizeof(CANNELLONI_CONNECT_V1_STRING)-1;
  } else {
    chunkSize = connection.decoder.expectedBytes;
  }
  ssize_t receivedBytes = read(connection.socket, connection.buffer + connection.bufferedBytes,
                               chunkSize - connection.bufferedBytes);
  if (receivedBytes < 0) {
    closeConnection(connection, strerror(errno));
  } else if (receivedBytes == 0) {
    closeConnection(connection, "closed by peer");
  } else {
    connection.bufferedBytes += receivedBytes;
    if (connection.bufferedBytes == chunkSize) {
      connection.bufferedBytes = 0;
      if (connection.state == CONNECTED) {
        checkProtocol(connection);
      } else {
        decodeChunk(connection, chunkSize);
      }
    }
  }
}

void TCPThread::checkProtocol(TCPConnection &connection) {
  if (memcmp(connection.buffer, CANNELLONI_CONNECT_V1_STRING, sizeof(CANNELLONI_CONNECT_V1_STRING)-1) == 0) {
    connection.state = NEGOTIATED;
    startNextFrame(connection.decoder);
  } else {
    closeConnection(connection, "invalid protocol");
  }
}

void TCPThread::decodeChunk(TCPConnection &connection, ssize_t len) {
  Decoder &decoder = connection.decoder;
  decoder.expectedBytes = decodeFrame(connection.buffer, len, &decoder.tempFrame, &decoder.state);
  if (decoder.expectedBytes == 0) {
    connection.rxCount++;
    m_rxCount++;
    forwardFrame(connection, &decoder.tempFrame);
    startNextFrame(decoder);
  } else if (decoder.expectedBytes == -1) {
    closeConnection(connection, "decoder error");
  }
}

void TCPThread::forwardFrame(TCPConnection &source, canfd_frame *frame) {
  canfd_frame *frameBufferFrame = m_peerThread->getFrameBuffer()->requestFrame(true, m_debugOptions.buffer);
  if (frameBufferFrame != NULL) {
    memcpy(frameBufferFrame, frame, sizeof(*frame));
    m_peerThread->transmitFrame(frameBufferFrame);
  } else {
    lerror << "Dropping frame due to framebuffer issue." << std::endl;
  }
  sendFrame(frame, &source);
}

void TCPThread::transmitFrame(canfd_frame *frame) {
  m_frameBuffer->insertFrame(frame);
  int signal = 1;
  ssize_t res = write(m_framebufferHasDataPipe[SIGNAL_PIPE_WRITE], &signal, sizeof(signal));
  if (res != sizeof(signal)) {
    /*
       when writing a lot of frames, the main loop might be too slow to consume the signals
       from the pipe which is not an error
    */
    if (errno != EWOULDBLOCK) {
      lwarn << "could not write to pipe " << res << std::endl;
    }
  }
  return;
}

void TCPThread::flushFrameBuffer() {
  /* Frames are dropped when no connection is negotiated, as nobody listens */
  m_frameBuffer->swapBuffers();
  std::list<canfd_frame*> *frames = m_frameBuffer->getIntermediateBuffer();
  for (auto it = frames->begin(); it != frames->end(); it++) {
    sendFrame(*it, NULL);
  }
  m_frameBuffer->unlockIntermediateBuffer();
  m_frameBuffer->mergeIntermediateBuffer();
}

void TCPThread::sendFrame(canfd_frame *frame, const TCPConnection *except) {
  uint8_t transmitBuffer[MAX_TRANSMIT_BUFFER_SIZE_BYTES];
  ssize_t encodedBytes = encodeFrame(transmitBuffer, frame);
  for (TCPConnection &connection : m_connections) {
    if (connection.state == NEGOTIATED && &connection != except) {
      ssize_t bytesWritten = send(connection.socket, transmitBuffer, encodedBytes, MSG_NOSIGNAL);
      if (bytesWritten == encodedBytes) {
        connection.txCount++;
        m_txCount++;
      } else {
        closeConnection(connection, bytesWritten >= 0 || errno == EAGAIN || errno == EWOULDBLOCK
                                    ? "send buffer full" : strerror(errno));
      }
    }
  }
}

bool TCPThread::setupSocket(int socket) {
  const int nagle = 0;
  const int min_window_size = 1;
  /*
   * Detect a dead peer. A side that only receives (e.g. the server feeding a vcan)
   * never writes, so if the peer's FIN/RST is lost (link down, ARP conflict) it keeps
   * the dead connection forever and the peer's reconnect waits in the accept queue.
   * Probe after 15 s of silence, every 2 s, and give up after 3 misses (about 21 s).
   */
  const int keepalive = 1;
  const int keepidle = 15;
  const int keepintvl = 2;
  const int keepcnt = 3;
  if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) ||
      setsockopt(socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) ||
      setsockopt(socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) ||
      setsockopt(socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt))) {
    lerror << "Could not enable TCP keepalive." << std::endl;
    return false;
  }
  if (setsockopt(socket, IPPROTO_TCP, TCP_WINDOW_CLAMP, &min_window_size, sizeof(min_window_size))) {
    lerror << "Could not set window size to " << min_window_size << std::endl;
    return false;
  }
  /* Disable Nagle for this connection */
  if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &nagle, sizeof(nagle))) {
    lerror << "Could not disable Nagle." << std::endl;
    return false;
  }
  return true;
}

bool TCPThread::setupPipe() {
  if (pipe(m_framebufferHasDataPipe) == -1) {
    lerror << "could not inititalize signal pipe" << std::endl;
    return false;
  }
  if (fcntl(m_framebufferHasDataPipe[SIGNAL_PIPE_WRITE], F_SETFL, O_NONBLOCK) <
      0) {
    lerror << "could not inititalize signal pipe" << std::endl;
    return false;
  }
  return true;
}
