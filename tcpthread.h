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
#include <chrono>
#include <mutex>
#include <vector>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

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
  
  struct TCPThreadParams {
    struct sockaddr_storage &remoteAddr;
    struct sockaddr_storage &localAddr;
    int addressFamily;
  };

  struct TCPConnection {
    int socket;
    ConnectState state;
    Decoder decoder;
    /* The part of the current protocol chunk received so far */
    uint8_t buffer[MAX_TRANSMIT_BUFFER_SIZE_BYTES];
    ssize_t bufferedBytes;
  };

  class TCPThread : public ConnectionThread {
    public:
      TCPThread(const struct debugOptions_t &debugOptions,
                const struct TCPThreadParams &params);
      virtual ~TCPThread();

      virtual int start();
      virtual void cleanup() = 0;
      virtual void run();

      virtual void transmitFrame(canfd_frame *frame);

    protected:
      bool setupSocket(int socket);
      /*
       * Socket that signals incoming connections, or -1 when no more
       * connections are accepted. With no connection open and nothing
       * to listen on, run() calls attempt_connect() to connect actively.
       */
      virtual int listenSocket();
      /* Returns the socket of a new connection, or -1 on failure */
      virtual int attempt_connect() = 0;

    private:
      void waitForEvents();
      void addConnection(int socket);
      void closeConnection(TCPConnection &connection);
      void receive(TCPConnection &connection);
      void checkProtocol(TCPConnection &connection);
      void decodeChunk(TCPConnection &connection, ssize_t len);
      void forwardFrame(canfd_frame *frame);
      void sendFrame(canfd_frame *frame);
      void flushFrameBuffer();
      bool setupPipe();

    protected:
      struct debugOptions_t m_debugOptions;
      int m_serverSocket;
      std::vector<TCPConnection> m_connections;
      Timer m_blockTimer;
      uint64_t m_rxCount;
      uint64_t m_txCount;
      std::recursive_mutex m_socketWriteMutex;

      struct sockaddr_storage m_localAddr;
      struct sockaddr_storage m_remoteAddr;
      int m_addressFamily;

      int m_framebufferHasDataPipe[2] = { -1, -1 };
  };

  struct TCPServerThreadParams {
    struct sockaddr_storage &remoteAddr;
    struct sockaddr_storage &localAddr;
    int addressFamily;
    bool checkPeer;
    
    public:

    TCPThreadParams toTCPThreadParams() const {
      return TCPThreadParams{
        .remoteAddr = remoteAddr,
        .localAddr = localAddr,
        .addressFamily = addressFamily
      };
    }
  };

  class TCPServerThread : public TCPThread  {
    public:
      TCPServerThread(const struct debugOptions_t &debugOptions,
                      const struct TCPServerThreadParams &params);

      virtual int start();
      virtual int listenSocket();
      virtual int attempt_connect();
      virtual void cleanup();

    private:
      bool m_checkPeerConnect;
  };

  class TCPClientThread : public TCPThread {
  public:
    TCPClientThread(const struct debugOptions_t &debugOptions,
                    const struct TCPThreadParams &params);
    virtual int attempt_connect();
    virtual void cleanup();

  private:
    std::chrono::steady_clock::time_point m_lastAttempt;
  };
}
