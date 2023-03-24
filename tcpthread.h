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

#pragma once

#include "connection.h"
#include "timer.h"
#include "decoder.h"
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define SELECT_TIMEOUT 500000
#define SIGNAL_PIPE_READ 0
#define SIGNAL_PIPE_WRITE 1

enum TCPThreadRole { TCP_SERVER, TCP_CLIENT };
/*
  DISCONNECTED: Waiting for a connection
  CONNECTED: TCP Connection established
  NEGOTIATED: A cannelloni peer has been found
 */
enum ConnectState { DISCONNECTED, CONNECTED, NEGOTIATED };

#define CANNELLONI_CONNECT_V1_STRING "CANNELLONIv1"

namespace cannelloni {

  class TCPThread : public ConnectionThread {
    public:
      TCPThread(const struct debugOptions_t &debugOptions,
                 const struct sockaddr_in &remoteAddr,
                 const struct sockaddr_in &localAddr);

      virtual int start();
      virtual void cleanup() = 0;
      virtual void run();

      virtual void transmitFrame(canfd_frame *frame);

    protected:
      bool isConnected();
      void flushFrameBuffer();
      void disconnect();
      bool setupSocket();
      bool setupPipe();
      virtual bool attempt_connect() = 0;

    protected:
      struct debugOptions_t m_debugOptions;
      int m_serverSocket;
      int m_socket;
      ConnectState m_connect_state;
      Timer m_blockTimer;
      uint64_t m_rxCount;
      uint64_t m_txCount;
      std::recursive_mutex m_socketWriteMutex;

      struct sockaddr_in m_localAddr;
      struct sockaddr_in m_remoteAddr;

      int m_framebufferHasDataPipe[2];
      Decoder m_decoder;
  };

  class TCPServerThread : public TCPThread  {
    public:
      TCPServerThread(const struct debugOptions_t &debugOptions,
                      const struct sockaddr_in &remoteAddr,
                      const struct sockaddr_in &localAddr,
                      bool checkPeer);

      virtual int start();
      virtual bool attempt_connect();
      virtual void cleanup();

    private:
      bool m_checkPeerConnect;
  };

  class TCPClientThread : public TCPThread {
  public:
    TCPClientThread(const struct debugOptions_t &debugOptions,
                    const struct sockaddr_in &remoteAddr,
                    const struct sockaddr_in &localAddr);
    virtual bool attempt_connect();
    virtual void cleanup();
  };
}
