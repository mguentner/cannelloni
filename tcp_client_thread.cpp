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
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace cannelloni;

TCPClientThread::TCPClientThread(const struct debugOptions_t &debugOptions,
                                 const struct TCPThreadParams &params)
  : TCPThread(debugOptions, params)
{
}

bool TCPClientThread::attempt_connect() {
  m_socket = socket(m_addressFamily, SOCK_STREAM, 0);
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
  linfo << "Connecting to " << formatSocketAddress(getSocketAddress(&m_remoteAddr)) << "..." << std::endl;
  if (connect(m_socket, (struct sockaddr *)&m_remoteAddr, sizeof(m_remoteAddr)) < 0) {
    close(m_socket);
    linfo << "Connect failed." << std::endl;
    return false;
  }
  linfo << "Connected!" << std::endl;
  return true;
}

void TCPClientThread::cleanup() {}
