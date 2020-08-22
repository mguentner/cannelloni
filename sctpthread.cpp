/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
 *
 * Copyright (C) 2014-2017 Maximilian Güntner <code@sourcediver.org>
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

#include <chrono>
#include <cstdio>
#include <algorithm>

#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/timerfd.h>


#include <net/if.h>
#include <arpa/inet.h>

#include <netinet/sctp.h>

#include "logging.h"
#include "sctpthread.h"

SCTPThread::SCTPThread(const struct debugOptions_t &debugOptions,
                       const struct sockaddr_in &remoteAddr,
                       const struct sockaddr_in &localAddr,
                       bool sort,
                       bool checkPeer,
                       SCTPThreadRole role)
  : UDPThread(debugOptions, remoteAddr, localAddr, sort, checkPeer)
  , m_assoc_id(0)
  , m_role(role)
  , m_checkPeerConnect(checkPeer)
  , m_connected(false)
{
  m_payloadSize = SCTP_PAYLOAD_SIZE;
}

int SCTPThread::start() {
  /*
   * Since we are currently not using multihoming and/or
   * one-to-many connections, we can also use SOCK_STREAM
   * instead of SOCK_SEQPACKET
   */
  if (m_role == SERVER) {
    m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (m_serverSocket < 0) {
      lerror << "socket error" << std::endl;
      return -1;
    }
    if (bind(m_serverSocket, (struct sockaddr *) &m_localAddr, sizeof(m_localAddr)) < 0) {
      lerror << "Could not bind to address" << std::endl;
      return -1;
    }
  }
  /*
   * UDPThread::parsePacket will check the remote address. Using SCTP, a packet
   * might arrive from a different interface than expected.
   * We just set checkPeer to false and only check whether connects are valid
   */
  m_checkPeer = false;
  return Thread::start();
}

void SCTPThread::run() {
  fd_set readfds;
  ssize_t receivedBytes;
  uint8_t buffer[RECEIVE_BUFFER_SIZE];
  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen = sizeof(struct sockaddr_in);

  /* Set interval to m_timeout */
  m_transmitTimer.adjust(m_timeout, m_timeout);
  m_blockTimer.adjust(SELECT_TIMEOUT, SELECT_TIMEOUT);

  while (m_started) {
    if (!m_connected) {
      if (m_role == SERVER) {
        struct sockaddr_in connAddr;
        char connAddrStr[INET_ADDRSTRLEN];
        socklen_t connAddrLen = sizeof(connAddr);
        fd_set readfds;
        struct timeval timeout;
        const int nagle = 0;

        listen(m_serverSocket, 1);
        FD_ZERO(&readfds);
        FD_SET(m_serverSocket, &readfds);

        linfo << "Waiting for a client to connect." << std::endl;
        /* Set Timeout to 1 second */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int ret = select(m_serverSocket+1, &readfds, NULL, NULL, &timeout);
        if (ret < 0) {
          lerror << "select error" << std::endl;
          continue;
        } else if (ret == 0) {
          /* Timeout occurred, checking whether m_started changed */
          continue;
        } /* else */
        m_socket = accept(m_serverSocket,(struct sockaddr*) &connAddr, &connAddrLen);
        /* Reject all further connection attempts */
        listen(m_serverSocket, 0);
        if (m_socket == -1) {
          lerror << "Error while accepting." << std::endl;
          continue;
        }
        if (inet_ntop(AF_INET, &connAddr.sin_addr, connAddrStr, INET_ADDRSTRLEN) == NULL) {
          lwarn << "Could not convert client address" << std::endl;
          close(m_socket);
          continue;
        }
        /*
         * We have a connection, now check whether it matches the one
         * the user specified as the peer unless m_checkPeerConnect is false
         */
        if (m_checkPeerConnect) {
          if (memcmp(&(connAddr.sin_addr), &(m_remoteAddr.sin_addr), sizeof(struct in_addr)) != 0) {
            lwarn << "Got a connection from " << connAddrStr
                  << ", which is not set as a remote." << std::endl;
            close(m_socket);
            /* Wait here for some time */
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
          }
        } else {
          linfo << "Got a connection from " << connAddrStr << std::endl;
        }
        /* At this point we have a valid connection */
        m_connected = true;
        /* Clear the old entries in frameBuffer */
        m_frameBuffer->reset();
        /* Disable Nagle for this connection */
        if (setsockopt(m_socket, IPPROTO_SCTP, SCTP_NODELAY, &nagle, sizeof(nagle))) {
          lerror << "Could not disable Nagle." << std::endl;
        }
      } else {
        const int nagle = 0;
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
        if (m_socket < 0) {
          lerror << "socket error" << std::endl;
          continue;
        }
        if (setsockopt(m_socket, IPPROTO_SCTP, SCTP_NODELAY, &nagle, sizeof(nagle))) {
          lerror << "Could not disable Nagle." << std::endl;
        }
        linfo << "Connecting..." << std::endl;
        if (sctp_connectx(m_socket, (struct sockaddr *) &m_remoteAddr, 1, &m_assoc_id) < 0) {
          close(m_socket);
          linfo << "Connect failed." << std::endl;
          /* Wait here for some time */
          std::this_thread::sleep_for(std::chrono::seconds(2));
          continue;
        } else {
          linfo << "Connected!" << std::endl;
          m_connected = true;
        }
      }
    } else { /* m_connected == true */
      /* Prepare readfds */
      FD_ZERO(&readfds);
      FD_SET(m_socket, &readfds);
      FD_SET(m_transmitTimer.getFd(), &readfds);
      FD_SET(m_blockTimer.getFd(), &readfds);
      int ret = select(std::max({m_socket, m_transmitTimer.getFd(), m_blockTimer.getFd()})+1,
        &readfds, NULL, NULL, NULL);
      if (ret < 0) {
        if (errno == EOF) {
          m_connected = false;
          close(m_socket);
        }
        /* Check whether the remote has terminated the connection */
        lerror << "select error" << std::endl;
        continue;
      }
      if (FD_ISSET(m_transmitTimer.getFd(), &readfds)) {
        if (m_transmitTimer.read() > 0) {
          if (m_frameBuffer->getFrameBufferSize())
            prepareBuffer();
          else
            m_transmitTimer.disable();
        }
      }
      if (FD_ISSET(m_blockTimer.getFd(), &readfds)) {
        m_blockTimer.read();
      }
      if (FD_ISSET(m_socket, &readfds)) {
        struct sctp_sndrcvinfo sinfo;
        int flags = 0;
        bzero(&sinfo, sizeof(sinfo));
        /* Clear buffer */
        memset(buffer, 0, RECEIVE_BUFFER_SIZE);
        receivedBytes = sctp_recvmsg(m_socket, buffer, RECEIVE_BUFFER_SIZE,
                        (struct sockaddr *) &clientAddr, &clientAddrLen, &sinfo, &flags);
        if (receivedBytes < 0) {
          lerror << "recvfrom error." << std::endl;
          /* close connection */
          m_connected = false;
          close(m_socket);
          continue;
        } else if (receivedBytes > 0) {
          parsePacket(buffer, receivedBytes, clientAddr);
        } else {
          m_connected  = false;
          close(m_socket);
          continue;
        }
      }
    }
  }
  if (m_debugOptions.buffer) {
    m_frameBuffer->debug();
  }
  linfo << "Shutting down. SCTP Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << std::endl;
  m_connected = false;
  close(m_socket);
  if (m_role == SERVER) {
    close(m_serverSocket);
  }
}

void SCTPThread::transmitFrame(canfd_frame *frame) {
  if (m_connected) {
    UDPThread::transmitFrame(frame);
  } else {
    /* We need to drop that frame, since we are not connected */
    m_frameBuffer->insertFramePool(frame);
    if (m_debugOptions.udp) {
      linfo << "Not connected. Dropping frame" << std::endl;
    }
  }
}

ssize_t SCTPThread::sendBuffer(uint8_t *buffer, uint16_t len) {
  struct sctp_sndrcvinfo sinfo;
  bzero(&sinfo, sizeof(sinfo));
  sinfo.sinfo_stream = 0;
  sinfo.sinfo_assoc_id = m_assoc_id;
  return sctp_send(m_socket, buffer, len, &sinfo, 0);
}
