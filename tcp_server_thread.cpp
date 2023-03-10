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

#include "logging.h"
#include "tcpthread.h"
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>

using namespace cannelloni;

TCPServerThread::TCPServerThread(const struct debugOptions_t &debugOptions,
                                 const struct sockaddr_in &remoteAddr,
                                 const struct sockaddr_in &localAddr,
                                 bool checkPeer)
  : TCPThread(debugOptions, remoteAddr, localAddr),
  m_checkPeerConnect(checkPeer)
{

}

int TCPServerThread::start() {
  m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (m_serverSocket < 0) {
    lerror << "socket error" << std::endl;
    return -1;
  }

  const int option = 1;
  setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  if (bind(m_serverSocket, (struct sockaddr *)&m_localAddr,
           sizeof(m_localAddr)) < 0) {
    lerror << "Could not bind to address" << std::endl;
    return -1;
  }
  return TCPThread::start();
}

bool TCPServerThread::attempt_connect() {
  struct sockaddr_in connAddr;
  char connAddrStr[INET_ADDRSTRLEN];
  socklen_t connAddrLen = sizeof(connAddr);
  fd_set readfds;
  struct timeval timeout;

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
    return false;
  } else if (ret == 0) {
    /* Timeout occurred, checking whether m_started changed */
    return false;
  } /* else */
  m_socket = accept(m_serverSocket,(struct sockaddr*) &connAddr, &connAddrLen);
  /* Reject all further connection attempts */
  listen(m_serverSocket, 0);
  if (m_socket == -1) {
    lerror << "Error while accepting." << std::endl;
    return false;
  }
  if (inet_ntop(AF_INET, &connAddr.sin_addr, connAddrStr, INET_ADDRSTRLEN) == NULL) {
    lwarn << "Could not convert client address" << std::endl;
    close(m_socket);
    return false;
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
      return false;
    }
  }
  linfo << "Got a connection from " << connAddrStr << std::endl;
  /* Clear the old entries in frameBuffer */
  m_frameBuffer->reset();
  m_decoder.reset();
  /* At this point we have a valid connection */
  if (!setupSocket()) {
    return false;
  }
  if (!setupPipe()) {
    return false;
  }
  return true;
}

void TCPServerThread::cleanup() { close(m_serverSocket); }
