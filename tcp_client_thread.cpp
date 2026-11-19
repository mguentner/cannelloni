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
#include <chrono>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace cannelloni;

TCPClientThread::TCPClientThread(const struct debugOptions_t &debugOptions,
                                 const struct TCPThreadParams &params)
  : TCPThread(debugOptions, params)
{
}

int TCPClientThread::attempt_connect() {
  /* Also after a connection the server closed right away, e.g. a rejected peer */
  std::this_thread::sleep_until(m_lastAttempt + std::chrono::seconds(2));
  m_lastAttempt = std::chrono::steady_clock::now();
  int clientSocket = socket(m_addressFamily, SOCK_STREAM, 0);
  if (clientSocket < 0) {
    lerror << "socket error" << std::endl;
    return -1;
  }
  if (!setupSocket(clientSocket)) {
    close(clientSocket);
    return -1;
  }
  linfo << "Connecting to " << formatSocketAddress(getSocketAddress(&m_remoteAddr)) << "..." << std::endl;
  if (connect(clientSocket, (struct sockaddr *)&m_remoteAddr, sizeof(m_remoteAddr)) < 0) {
    close(clientSocket);
    linfo << "Connect failed." << std::endl;
    return -1;
  }
  linfo << "Connected!" << std::endl;
  return clientSocket;
}

void TCPClientThread::cleanup() {}
