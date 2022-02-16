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

#pragma once

#include "udpthread.h"
#include <netinet/tcp.h>

namespace cannelloni {

/* The common header + one Chunk Header */
#define TCP_HEADER_SIZE 20
#define TCP_PAYLOAD_SIZE ETHERNET_MTU-IP_HEADER_SIZE-TCP_HEADER_SIZE

enum TCPThreadRole {TCP_SERVER, TCP_CLIENT};

class TCPThread : public UDPThread {
  public:
    TCPThread(const struct debugOptions_t &debugOptions,
               const struct sockaddr_in &remoteAddr,
               const struct sockaddr_in &localAddr,
               bool sort,
               bool checkPeer,
               TCPThreadRole role);

    virtual int start();
    virtual void run();

    virtual void transmitFrame(canfd_frame *frame);

  protected:
    virtual ssize_t sendBuffer(uint8_t *buffer, uint16_t len);
  private:
    bool isConnected();

  private:
    bool m_checkPeerConnect;
    int m_serverSocket;
    bool m_connected;
    TCPThreadRole m_role;
};

}
