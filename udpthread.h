/*
 * This file is part of cannelloni, a SocketCAN over ethernet tunnel.
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

#include <map>

#include <sys/types.h>
#include <netinet/in.h>

#include "connection.h"
#include "timer.h"


namespace cannelloni {

#define ETHERNET_MTU 1500
#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8

/* Block select max. for 500ms */
#define SELECT_TIMEOUT 500000

#define RECEIVE_BUFFER_SIZE ETHERNET_MTU
#define UDP_PAYLOAD_SIZE ETHERNET_MTU-IP_HEADER_SIZE-UDP_HEADER_SIZE

class UDPThread : public ConnectionThread {
  public:
    UDPThread(const struct debugOptions_t &debugOptions,
              const struct sockaddr_in &remoteAddr,
              const struct sockaddr_in &localAddr,
              bool sort,
              bool checkPeer);

    virtual int start();
    virtual void stop();
    virtual void run();
    bool parsePacket(uint8_t *buf, uint16_t len, struct sockaddr_in &clientAddr);
    virtual void transmitFrame(canfd_frame *frame);

    void setTimeout(uint32_t timeout);
    uint32_t getTimeout();

    void setTimeoutTable(std::map<uint32_t,uint32_t> &timeoutTable);
    std::map<uint32_t,uint32_t>& getTimeoutTable();

  protected:
    void prepareBuffer();
    virtual ssize_t sendBuffer(uint8_t *buffer, uint16_t len);

  protected:
    struct debugOptions_t m_debugOptions;
    bool m_sort;
    bool m_checkPeer;
    int m_socket;
    Timer m_blockTimer;
    Timer m_transmitTimer;

    struct sockaddr_in m_localAddr;
    struct sockaddr_in m_remoteAddr;

    uint8_t m_sequenceNumber;
    /* Timeout variables */
    uint32_t m_timeout;
    std::map<uint32_t,uint32_t> m_timeoutTable;
    /* Performance Counters */
    uint64_t m_rxCount;
    uint64_t m_txCount;

    uint32_t m_payloadSize;
};

}
