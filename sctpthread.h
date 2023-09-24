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
#include <netinet/sctp.h>
#include <sys/socket.h>

namespace cannelloni {

/* The common header + one Chunk Header */
#define SCTP_HEADER_SIZE 12
#define SCTP_PAYLOAD_SIZE ETHERNET_MTU-IP_HEADER_SIZE-SCTP_HEADER_SIZE

enum SCTPThreadRole {SCTP_SERVER, SCTP_CLIENT};

struct SCTPThreadParams  {
  struct sockaddr_storage &remoteAddr;
  struct sockaddr_storage &localAddr;
  int addressFamily;
  bool sortFrames;
  bool checkPeer;
  SCTPThreadRole role;

  public:
   UDPThreadParams toUDPThreadParams() const {
    return UDPThreadParams{
      .remoteAddr = remoteAddr,
      .localAddr = localAddr,
      .addressFamily = addressFamily,
      .sortFrames = sortFrames,
      .checkPeer = checkPeer,
    };
   }
};

class SCTPThread : public UDPThread {
  public:
    SCTPThread(const struct debugOptions_t &debugOptions,
               const struct SCTPThreadParams &params);

    virtual int start();
    virtual void run();

    virtual void transmitFrame(canfd_frame *frame);

  protected:
    virtual ssize_t sendBuffer(uint8_t *buffer, uint16_t len);
  private:
    bool isConnected();

  private:
    sctp_assoc_t m_assoc_id;
    SCTPThreadRole m_role;
    bool m_checkPeerConnect;
    bool m_connected;
    int m_serverSocket;
};

}
