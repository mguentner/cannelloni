/*
 * This file is part of cannelloni, a SocketCAN over ethernet tunnel.
 *
 * Copyright (C) 2014-2015 Maximilian Güntner <maximilian.guentner@gmail.com>
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

#include <thread>
#include <vector>
#include <list>
#include <mutex>
#include <map>

#include <stdint.h>

#include <sys/types.h>
#include <netinet/in.h>

#include "can.h"
#include "framebuffer.h"

namespace cannelloni {

#define RECEIVE_BUFFER_SIZE 1500
#define UDP_PAYLOAD_SIZE 1472
#define CAN_TIMEOUT 2000000 /* 2 sec in us */

struct debugOptions_t {
  uint8_t can    : 1;
  uint8_t udp    : 1;
  uint8_t buffer : 1;
  uint8_t timer  : 1;
};

class Thread {
  public:
    Thread();
    ~Thread();
    virtual int start();
    /* this is function tell the thread to stop */
    virtual void stop();
    /* joins the thread */
    void join();
    /* */
    bool isRunning();
    /* */
    virtual void run() = 0;
  private:
    /* thread control loop */
    void privRun();

  protected:
    /* determines when to break from run() */
    bool m_started;
  private:
    bool m_running;
    std::thread *m_privThread;
};

class CANThread;

class UDPThread : public Thread {
  public:
    UDPThread(const struct debugOptions_t &debugOptions,
              const struct sockaddr_in &remoteAddr,
              const struct sockaddr_in &localAddr,
              bool sort);

    virtual int start();
    virtual void stop();
    virtual void run();

    void setCANThread(CANThread *thread);
    CANThread* getCANThread();

    void setFrameBuffer(FrameBuffer *buffer);
    FrameBuffer *getFrameBuffer();

    void sendCANFrame(can_frame *frame);
    void setTimeout(uint32_t timeout);
    uint32_t getTimeout();

    void setTimeoutTable(std::map<uint32_t,uint32_t> &timeoutTable);
    std::map<uint32_t,uint32_t>& getTimeoutTable();

  private:
    /* This function transmits m_frameBuffer */
    void transmitBuffer();
    void fireTimer();
    /* Returns the current timer value in us */
    uint32_t getTimerValue();
    void adjustTimer(uint32_t interval, uint32_t value);

  private:
    struct debugOptions_t m_debugOptions;
    bool m_sort;
    int m_udpSocket;
    /*
     * We use the timerfd API of the Linux Kernel to send
     * m_frameBuffer periodically
     */
    int m_timerfd;

    CANThread *m_canThread;
    FrameBuffer *m_frameBuffer;
    struct sockaddr_in m_localAddr;
    struct sockaddr_in m_remoteAddr;

    uint8_t m_sequenceNumber;
    /* Timeout variables */
    uint32_t m_timeout;
    std::map<uint32_t,uint32_t> m_timeoutTable;
    /* Performance Counters */
    uint64_t m_rxCount;
    uint64_t m_txCount;
};

class CANThread : public Thread {
  public:
    CANThread(const struct debugOptions_t &debugOptions,
              const std::string &canInterfaceName);

    virtual int start();
    virtual void stop();
    virtual void run();

    void setUDPThread(UDPThread *thread);
    UDPThread* getUDPThread();

    void setFrameBuffer(FrameBuffer *buffer);
    FrameBuffer *getFrameBuffer();

    void transmitCANFrame(can_frame *frame);
  private:
    void transmitBuffer();
    void fireTimer();
    /* Returns the current timer value in us */
    uint32_t getTimerValue();
    void adjustTimer(uint32_t interval, uint32_t value);

  private:
    struct debugOptions_t m_debugOptions;
    int m_canSocket;
    /*
     * We use the timerfd API of the Linux Kernel to send
     * m_frameBuffer periodically
     */
    int m_timerfd;

    struct sockaddr_can m_localAddr;
    std::string m_canInterfaceName;
    UDPThread *m_udpThread;
    FrameBuffer *m_frameBuffer;

    /* Performance Counters */
    uint64_t m_rxCount;
    uint64_t m_txCount;
};

}
