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
#include "logging.h"
#include "parser.h"
#include "tcpthread.h"

TCPThread::TCPThread(const struct debugOptions_t &debugOptions,
                     const struct TCPThreadParams &params)
   : ConnectionThread()
  , m_debugOptions(debugOptions)
  , m_serverSocket(0)
  , m_socket(0)
  , m_connect_state(DISCONNECTED)
  , m_rxCount(0)
  , m_txCount(0)
  , m_addressFamily(params.addressFamily)
{

  memcpy(&m_remoteAddr, &params.remoteAddr, sizeof(struct sockaddr_storage));
  memcpy(&m_localAddr, &params.localAddr, sizeof(struct sockaddr_storage));
}

int TCPThread::start() {
  return Thread::start();
}



void TCPThread::run() {
  fd_set readfds;
  uint8_t buffer[MAX_TRANSMIT_BUFFER_SIZE_BYTES];
  uint8_t protocolVersionBuffer[] = CANNELLONI_CONNECT_V1_STRING;

  /* Set interval to m_timeout */
  m_blockTimer.adjust(SELECT_TIMEOUT, SELECT_TIMEOUT);

  while (m_started) {
    if (m_connect_state == DISCONNECTED) {
      bool connect_successful = attempt_connect();
      if (connect_successful) {
        m_connect_state = CONNECTED;
        ssize_t res = write(m_socket, protocolVersionBuffer, sizeof(protocolVersionBuffer)-1);
        if (res != sizeof(protocolVersionBuffer)-1) {
          lerror << "write error could not announce protocol" << std::endl;
          disconnect();
          continue;
        }
      } else {
        /* Wait here for some time until the next attempt */
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    } else {
      /* Prepare readfds */
      FD_ZERO(&readfds);
      FD_SET(m_socket, &readfds);
      FD_SET(m_blockTimer.getFd(), &readfds);
      FD_SET(m_framebufferHasDataPipe[SIGNAL_PIPE_READ], &readfds);
      int ret = select(std::max({m_socket, m_blockTimer.getFd(), m_framebufferHasDataPipe[SIGNAL_PIPE_READ]})+1, &readfds, NULL, NULL, NULL);
      if (ret < 0) {
        if (errno == EOF) {
          disconnect();
        }
        /* Check whether the remote has terminated the connection */
        lerror << "select error" << std::endl;
        continue;
      }
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
      if (FD_ISSET(m_socket, &readfds)) {
        ssize_t receivedBytes = 0;
        ssize_t expectedBytes = 0;
        if (m_connect_state == CONNECTED) {
          expectedBytes = sizeof(CANNELLONI_CONNECT_V1_STRING)-1;
        } else {
          expectedBytes = m_decoder.expectedBytes;
        }
        if (expectedBytes != 0) {
          /* check whether we can read enough bytes */
          int available;
          if (ioctl(m_socket, FIONREAD, &available) == -1) {
            lerror << "ioctl failed" << std::endl;
            disconnect();
            continue;
          } else if (available > 0 && available < static_cast<int>(expectedBytes)) {
            /* not enough bytes are available, let's wait a bit */
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
          }

          receivedBytes = read(m_socket, buffer, expectedBytes);
          if (receivedBytes < 0) {
            lerror << "recvfrom error." << std::endl;
            /* close connection */
            disconnect();
            continue;
          } else if (receivedBytes == 0){
            disconnect();
            continue;
          }
        }
        if (m_connect_state == CONNECTED) {
          if (memcmp(buffer, protocolVersionBuffer, sizeof(CANNELLONI_CONNECT_V1_STRING)-1) == 0) {
            m_connect_state = NEGOTIATED;
            continue;
          } else {
            lwarn << "Invalid protocol detected" << std::endl;
            disconnect();
            continue;
          }
        } else {
          m_decoder.expectedBytes = decodeFrame(buffer, receivedBytes, &m_decoder.tempFrame, &m_decoder.state);
          if (m_decoder.expectedBytes == 0) {
            canfd_frame *frameBufferFrame = m_peerThread->getFrameBuffer()->requestFrame(true, m_debugOptions.buffer);
            if (frameBufferFrame != NULL) {
              memcpy(frameBufferFrame, &m_decoder.tempFrame, sizeof(m_decoder.tempFrame));
              m_peerThread->transmitFrame(frameBufferFrame);
            } else {
              lerror << "Dropping frame due to framebuffer issue." << std::endl;
            }
            m_rxCount++;
            continue;
          } else if (m_decoder.expectedBytes == -1) {
            lerror << "Decoder Error" << std::endl;
            disconnect();
            continue;
          }
        }
      }
    }
  }
  if (m_debugOptions.buffer) {
    m_frameBuffer->debug();
  }
  linfo << "Shutting down. TCP Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << std::endl;
  m_connect_state = DISCONNECTED;
  close(m_socket);
  cleanup();
}

void TCPThread::disconnect() {
  m_connect_state = DISCONNECTED;
  close(m_socket);
  close(m_framebufferHasDataPipe[SIGNAL_PIPE_READ]);
  close(m_framebufferHasDataPipe[SIGNAL_PIPE_WRITE]);
}

void TCPThread::transmitFrame(canfd_frame *frame) {
  if (m_connect_state != NEGOTIATED) {
    m_frameBuffer->insertFramePool(frame);
    return;
  }
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
  if (m_connect_state != NEGOTIATED) {
    return;
  }
  uint8_t transmitBuffer[MAX_TRANSMIT_BUFFER_SIZE_BYTES];
  m_frameBuffer->swapBuffers();
  std::list<canfd_frame*> *frames = m_frameBuffer->getIntermediateBuffer();
  for (auto it = frames->begin(); it != frames->end(); it++) {
    canfd_frame* frame = *it;
    ssize_t encodedBytes = encodeFrame(transmitBuffer, frame);
    ssize_t bytesWritten = send(m_socket, transmitBuffer, encodedBytes, 0);
    if (encodedBytes != bytesWritten) {
      disconnect();
      break;
    }
    m_txCount++;
  }
  m_frameBuffer->unlockIntermediateBuffer();
  m_frameBuffer->mergeIntermediateBuffer();
}

bool TCPThread::setupSocket() {
  const int nagle = 0;
  const int min_window_size = 1;
  if (setsockopt(m_socket, IPPROTO_TCP, TCP_WINDOW_CLAMP, &min_window_size, sizeof(min_window_size))) {
    lerror << "Could not set window size to " << min_window_size << std::endl;
    return false;
  }
  /* Disable Nagle for this connection */
  if (setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, &nagle, sizeof(nagle))) {
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
