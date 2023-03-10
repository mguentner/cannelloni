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

TCPClientThread::TCPClientThread(const struct debugOptions_t &debugOptions,
                                 const struct sockaddr_in &remoteAddr,
                                 const struct sockaddr_in &localAddr)
  : TCPThread(debugOptions, remoteAddr, localAddr)
{
}

bool TCPClientThread::attempt_connect() {
  m_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (m_socket < 0) {
    lerror << "socket error" << std::endl;
    return false;
  }
  if (!setupSocket()) {
    return false;
  }
  if (!setupPipe()) {
    return false;
  }
  linfo << "Connecting to " << inet_ntoa(m_remoteAddr.sin_addr) << ":"
        << ntohs(m_remoteAddr.sin_port) << "..." << std::endl;
  if (connect(m_socket, (struct sockaddr *)&m_remoteAddr, sizeof(m_remoteAddr)) < 0) {
    close(m_socket);
    linfo << "Connect failed." << std::endl;
    return false;
  }
  linfo << "Connected!" << std::endl;
  return true;
}

void TCPClientThread::cleanup() {}
