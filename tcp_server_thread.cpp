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

#include "inet_address.h"
#include "logging.h"
#include "tcpthread.h"
#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace cannelloni;

TCPServerThread::TCPServerThread(const struct debugOptions_t &debugOptions,
                                 const struct TCPServerThreadParams &params)
  : TCPThread(debugOptions, params.toTCPThreadParams()),
  m_checkPeerConnect(params.checkPeer)
{

}

int TCPServerThread::start() {
  m_serverSocket = socket(m_addressFamily, SOCK_STREAM, 0);
  if (m_serverSocket < 0) {
    lerror << "socket error" << std::endl;
    return -1;
  }

  const int option = 1;
  setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  if (m_addressFamily == AF_INET && bind(m_serverSocket, (struct sockaddr *)&m_localAddr, sizeof(sockaddr_in)) < 0) {
    lerror << "Could not bind to address" << std::endl;
    return -1;
  } else if (m_addressFamily == AF_INET6 && bind(m_serverSocket, (struct sockaddr *)&m_localAddr, sizeof(sockaddr_in6)) < 0) {
    lerror << "Could not bind to address" << std::endl;
    return -1;
  } else if (m_addressFamily != AF_INET && m_addressFamily != AF_INET6) {
    lerror << "Invalid address family" << m_addressFamily <<  std::endl;
    return -1;
  }
  if (listen(m_serverSocket, SOMAXCONN) < 0) {
    lerror << "Could not listen" << std::endl;
    return -1;
  }
  linfo << "Waiting for a client to connect." << std::endl;
  return TCPThread::start();
}

int TCPServerThread::listenSocket() {
  /* Further clients wait in the listen backlog until the connection closes */
  if (m_connections.empty()) {
    return m_serverSocket;
  }
  return -1;
}

int TCPServerThread::attempt_connect() {
  struct sockaddr_storage connAddr;
  socklen_t connAddrLen = sizeof(connAddr);

  int socket = accept(m_serverSocket, (struct sockaddr *) &connAddr, &connAddrLen);
  if (socket == -1) {
    lerror << "Error while accepting." << std::endl;
    return -1;
  }
  /*
   * We have a connection, now check whether it matches the one
   * the user specified as the peer unless m_checkPeerConnect is false
   */
  if (m_checkPeerConnect) {
    if ((m_addressFamily == AF_INET && (memcmp(&((struct sockaddr_in *) &connAddr)->sin_addr, &((struct sockaddr_in *) &m_remoteAddr)->sin_addr, sizeof(struct in_addr)) != 0)) || 
        (m_addressFamily == AF_INET6 && (memcmp(&((struct sockaddr_in6 *) &connAddr)->sin6_addr, &((struct sockaddr_in6 *) &m_remoteAddr)->sin6_addr, sizeof(struct in6_addr)) != 0))) {
      lwarn << "Got a connection attempt from " << formatSocketAddress(getSocketAddress(&connAddr))
            << ", which is not set as a remote. Restart with -p argument to override." << std::endl;
      close(socket);
      return -1;
     }
  }

  linfo << "Got a connection from " << formatSocketAddress(getSocketAddress(&connAddr)) << std::endl;
  /* At this point we have a valid connection */
  if (!setupSocket(socket)) {
    close(socket);
    return -1;
  }
  return socket;
}

void TCPServerThread::cleanup() { close(m_serverSocket); }
