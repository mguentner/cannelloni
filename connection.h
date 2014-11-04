/*
 * This file is part of cannelloni, a SocketCAN over ethernet tunnel.
 *
 * Copyright (C) 2014 Maximilian Güntner <maximilian.guentner@gmail.com>
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
#include <set>
#include <vector>
#include <mutex>
#include <sys/types.h>
#include <netinet/in.h>
#include "can.h"

namespace cannelloni {

#define RECEIVE_BUFFER_SIZE 1500
#define UDP_PAYLOAD_SIZE 1472
#define CAN_SOCKET_TIMEOUT 2
#define CAN_DEBUG 0
#define UDP_DEBUG 0

class Thread {
  public:
    Thread();
    ~Thread();
    virtual int start();
    virtual void stop();
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
    UDPThread(const struct sockaddr_in &remoteAddr,
              const struct sockaddr_in &localAddr);

    virtual int start();
    virtual void stop();
    virtual void run();

    void setCANThread(CANThread *thread);
    CANThread* getCANThread();

    void sendCANFrame(const can_frame &frame);
    void setTimeout(uint32_t timeout);
    uint32_t getTimeout();

  private:
    /* This function transmits m_frameBufferSize */
    void transmitBuffer();

  private:
    int m_udpSocket;
    CANThread *m_canThread;
    struct sockaddr_in m_localAddr;
    struct sockaddr_in m_remoteAddr;
    /*
     * This is the local buffer of frames for this
     * Thread. Once a timeout occurs or a packet is
     * full, this buffer gets sent to remoteAddr
     */
    std::multiset<can_frame, can_frame_comp> *m_frameBuffer;
    std::multiset<can_frame, can_frame_comp> *m_frameBuffer_trans;
    /* When swapping the buffers we currently need a mutex */
    std::mutex m_bufferMutex;
    /* Track current frame buffer size */
    uint16_t m_frameBufferSize;
    uint16_t m_frameBufferSize_trans;
    uint8_t m_sequenceNumber;
    /* Timeout variables */
    uint64_t m_lastTransmit;
    uint32_t m_timeout;
};

class CANThread : public Thread {
  public:
    CANThread(const std::string &canInterfaceName);

    virtual int start();
    virtual void stop();
    virtual void run();

    void setUDPThread(UDPThread *thread);
    UDPThread* getUDPThread();

    void transmitCANFrames(const std::vector<can_frame> &frames);

  private:
    int m_canSocket;
    struct sockaddr_can m_localAddr;
    std::string m_canInterfaceName;
    UDPThread *m_udpThread;
};

}
