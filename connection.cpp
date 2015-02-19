/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
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
#include <iostream>
#include <iomanip>
#include <vector>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>

#include <net/if.h>
#include <arpa/inet.h>

#include "connection.h"
#include "logging.h"

using namespace cannelloni;

Thread::Thread()
  : m_started(false)
  , m_running(false)
  , m_privThread(NULL)
{ }

Thread::~Thread() {
  if (m_started)
    stop();
}

int Thread::start() {
  m_started = true;
  m_privThread = new std::thread(&Thread::privRun, this);
  m_running = true;
  return 0;
}

void Thread::stop() {
  m_started = false;
}

void Thread::join() {
  if (m_privThread)
    m_privThread->join();
  delete m_privThread;
}

bool Thread::isRunning() {
  return m_running;
}

void Thread::privRun() {
  run();
  m_running = false;
  m_started = false;
}


UDPThread::UDPThread(const struct debugOptions_t &debugOptions,
                     const struct sockaddr_in &remoteAddr,
                     const struct sockaddr_in &localAddr)
  : Thread()
  , m_canThread(NULL)
  , m_frameBuffer(NULL)
  , m_sequenceNumber(0)
  , m_timeout(100)
  , m_rxCount(0)
  , m_txCount(0)
{
  memcpy(&m_debugOptions, &debugOptions, sizeof(struct debugOptions_t));
  memcpy(&m_remoteAddr, &remoteAddr, sizeof(struct sockaddr_in));
  memcpy(&m_localAddr, &localAddr, sizeof(struct sockaddr_in));
}

int UDPThread::start() {
  /* Setup our connection */
  m_udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (m_udpSocket < 0) {
    lerror << "socket Error" << std::endl;
    return -1;
  }
  if (bind(m_udpSocket, (struct sockaddr *)&m_localAddr, sizeof(m_localAddr)) < 0) {
    lerror << "Could not bind to address" << std::endl;
    return -1;
  }
  /* Create timerfd */
  m_timerfd = timerfd_create(CLOCK_REALTIME, 0);
  if (m_timerfd < 0) {
    lerror << "timerfd_create error" << std::endl;
    return -1;
  }
  return Thread::start();
}

void UDPThread::stop() {
  Thread::stop();
  /* m_started is now false, we need to wake up the thread */
  fireTimer();
}

void UDPThread::run() {
  fd_set readfds;
  ssize_t receivedBytes;
  uint8_t buffer[RECEIVE_BUFFER_SIZE];
  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen = sizeof(struct sockaddr_in);
  char clientAddrStr[INET_ADDRSTRLEN];

  /* Set interval to m_timeout and an immediate timeout */
  adjustTimer(m_timeout, 1);

  linfo << "UDPThread up and running" << std::endl;
  while (m_started) {
    /* Prepare readfds */
    FD_ZERO(&readfds);
    FD_SET(m_udpSocket, &readfds);
    FD_SET(m_timerfd, &readfds);
    int ret = select(std::max(m_udpSocket,m_timerfd)+1, &readfds, NULL, NULL, NULL);
    if (ret < 0) {
      lerror << "select error" << std::endl;
      break;
    }
    if (FD_ISSET(m_timerfd, &readfds)) {
      ssize_t readBytes;
      uint64_t numExp;
      /* read from timer */
      readBytes = read(m_timerfd, &numExp, sizeof(uint64_t));
      if (readBytes != sizeof(uint64_t)) {
        lerror << "timerfd read error" << std::endl;
        break;
      }
      if (numExp) {
        if (m_frameBuffer->getFrameBufferSize())
          transmitBuffer();
      }
    }
    if (FD_ISSET(m_udpSocket, &readfds)) {
      /* Clear buffer */
      memset(buffer, 0, RECEIVE_BUFFER_SIZE);
      receivedBytes = recvfrom(m_udpSocket, buffer, RECEIVE_BUFFER_SIZE,
          0, (struct sockaddr *) &clientAddr, &clientAddrLen);
      if (receivedBytes < 0) {
        lerror << "recvfrom error." << std::endl;
        return;
      } else if (receivedBytes > 0) {
        if (inet_ntop(AF_INET, &clientAddr.sin_addr, clientAddrStr, INET_ADDRSTRLEN) == NULL) {
          lwarn << "Could not convert client address" << std::endl;
        } else {
            if (memcmp(&(clientAddr.sin_addr), &(m_remoteAddr.sin_addr), sizeof(struct in_addr)) != 0) {
              lwarn << "Received a packet from " << clientAddrStr
                    << ", which is not set as a remote." << std::endl;
            } else {
              bool drop = false;
              struct UDPDataPacket *data;
              /* Check for OP Code */
              data = (struct UDPDataPacket*) buffer;
              if (data->version != CANNELLONI_FRAME_VERSION) {
                lwarn << "Received wrong version" << std::endl;
                drop = true;
              }
              if (data->op_code != DATA) {
                lwarn << "Received wrong OP code" << std::endl;
                drop = true;
              }
              if (ntohs(data->count) == 0) {
                linfo << "Received empty packet" << std::endl;
                drop = true;
              }
              if (!drop) {
                uint8_t *rawData = buffer+UDP_DATA_PACKET_BASE_SIZE;
                bool error;
                if (m_debugOptions.udp) {
                  linfo << "Received " << std::dec << receivedBytes << " Bytes from Host " << clientAddrStr
                    << ":" << ntohs(clientAddr.sin_port) << std::endl;
                }
                m_rxCount++;
                for (uint16_t i=0; i<ntohs(data->count); i++) {
                  if (rawData-buffer+CANNELLONI_FRAME_BASE_SIZE > receivedBytes) {
                    lerror << "Received incomplete packet" << std::endl;
                    error = true;
                    break;
                  }
                  /* We got at least a complete can_frame header */
                  can_frame *frame = m_canThread->getFrameBuffer()->requestFrame();
                  if (!frame) {
                    lerror << "Allocation error." << std::endl;
                    error = true;
                    break;
                  }
                  frame->can_id = ntohl(*((canid_t*)rawData));
                  rawData+=4;
                  frame->can_dlc = *rawData;
                  rawData+=1;
                  /* RTR Frames have no data section although they have a dlc */
                  if ((frame->can_id & CAN_RTR_FLAG) == 0) {
                    /* Check again now that we know the dlc */
                    if (rawData-buffer+frame->can_dlc > receivedBytes) {
                      lerror << "Received incomplete packet / can header corrupt!" << std::endl;
                      error = true;
                      break;
                    }
                    memcpy(frame->data, rawData, frame->can_dlc);
                    rawData+=frame->can_dlc;
                  }
                  m_canThread->transmitCANFrame(frame);
                  if (m_debugOptions.can) {
                    printCANInfo(frame);
                  }
                }
              }
            }
        }
      }
    }
  }
  if (m_debugOptions.buffer) {
    m_frameBuffer->debug();
  }
  /* free all entries in m_framePool */
  m_frameBuffer->clearPool();
  linfo << "Shutting down. UDP Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << std::endl;
  shutdown(m_udpSocket, SHUT_RDWR);
  close(m_udpSocket);
}

void UDPThread::setCANThread(CANThread *thread) {
  m_canThread = thread;
}

CANThread* UDPThread::getCANThread() {
  return m_canThread;
}

void UDPThread::setFrameBuffer(FrameBuffer *buffer) {
  m_frameBuffer = buffer;
}

FrameBuffer* UDPThread::getFrameBuffer() {
  return m_frameBuffer;
}

void UDPThread::sendCANFrame(can_frame *frame) {
  m_frameBuffer->insertFrame(frame);
  if( m_frameBuffer->getFrameBufferSize() + UDP_DATA_PACKET_BASE_SIZE >= UDP_PAYLOAD_SIZE) {
    fireTimer();
  } else {
    /* Check whether we have custom timeout for this frame */
    std::map<uint32_t,uint32_t>::iterator it;
    uint32_t can_id;
    if (frame->can_id & CAN_EFF_FLAG)
      can_id = frame->can_id & CAN_EFF_MASK;
    else
      can_id = frame->can_id & CAN_SFF_MASK;
    it = m_timeoutTable.find(can_id);
    if (it != m_timeoutTable.end()) {
      uint32_t timeout = it->second;
      if (timeout < m_timeout) {
        if (timeout < getTimerValue()) {
          if (m_debugOptions.timer) {
            linfo << "Found timeout entry for ID " << can_id << ". Adjusting timer." << std::endl;
          }
          /* Let buffer expire in timeout ms */
          adjustTimer(m_timeout, timeout);
        }
      }

    }

  }
}

void UDPThread::setTimeout(uint32_t timeout) {
  m_timeout = timeout;
}

uint32_t UDPThread::getTimeout() {
  return m_timeout;
}

void UDPThread::setTimeoutTable(std::map<uint32_t,uint32_t> &timeoutTable) {
  m_timeoutTable = timeoutTable;
}

std::map<uint32_t,uint32_t>& UDPThread::getTimeoutTable() {
  return m_timeoutTable;
}

void UDPThread::transmitBuffer() {
  uint8_t *packetBuffer = new uint8_t[UDP_PAYLOAD_SIZE];
  uint8_t *data;
  ssize_t transmittedBytes = 0;
  uint16_t frameCount = 0;
  struct UDPDataPacket *dataPacket;
  struct timeval currentTime;

  m_frameBuffer->swapBuffers();
  m_frameBuffer->sortIntermediateBuffer();

  const std::list<can_frame*> *buffer = m_frameBuffer->getIntermediateBuffer();

  data = packetBuffer+UDP_DATA_PACKET_BASE_SIZE;
  for (auto it = buffer->begin(); it != buffer->end(); it++) {
    can_frame *frame = *it;
    /* Check for packet overflow */
    if ((data-packetBuffer+CANNELLONI_FRAME_BASE_SIZE+frame->can_dlc) > UDP_PAYLOAD_SIZE) {
      dataPacket = (struct UDPDataPacket*) packetBuffer;
      dataPacket->version = CANNELLONI_FRAME_VERSION;
      dataPacket->op_code = DATA;
      dataPacket->seq_no = m_sequenceNumber++;
      dataPacket->count = htons(frameCount);
      transmittedBytes = sendto(m_udpSocket, packetBuffer, data-packetBuffer, 0,
            (struct sockaddr *) &m_remoteAddr, sizeof(m_remoteAddr));
      if (transmittedBytes != data-packetBuffer) {
        lerror << "UDP Socket error. Error while transmitting" << std::endl;
      } else {
        m_txCount++;
      }
      data = packetBuffer+UDP_DATA_PACKET_BASE_SIZE;
      frameCount = 0;
    }
    *((canid_t *) (data)) = htonl(frame->can_id);
    data+=4;
    *data = frame->can_dlc;
    data+=1;
    if ((frame->can_id & CAN_RTR_FLAG) == 0) {
      memcpy(data, frame->data, frame->can_dlc);
      data+=frame->can_dlc;
    }
    frameCount++;
  }
  dataPacket = (struct UDPDataPacket*) packetBuffer;
  dataPacket->version = CANNELLONI_FRAME_VERSION;
  dataPacket->op_code = DATA;
  dataPacket->seq_no = m_sequenceNumber++;
  dataPacket->count = htons(frameCount);
  transmittedBytes = sendto(m_udpSocket, packetBuffer, data-packetBuffer, 0,
        (struct sockaddr *) &m_remoteAddr, sizeof(m_remoteAddr));
  if (transmittedBytes != data-packetBuffer) {
    lerror << "UDP Socket error. Error while transmitting" << std::endl;
  } else {
    m_txCount++;
  }
  m_frameBuffer->unlockIntermediateBuffer();
  m_frameBuffer->mergeIntermediateBuffer();
  delete[] packetBuffer;
}

inline uint32_t UDPThread::getTimerValue() {
  struct itimerspec ts;
  timerfd_gettime(m_timerfd, &ts);
  return ts.it_value.tv_sec*1000000 + ts.it_value.tv_nsec/1000;
}

inline void UDPThread::adjustTimer(uint32_t interval, uint32_t value) {
  struct itimerspec ts;
  ts.it_interval.tv_sec = interval/1000000;
  ts.it_interval.tv_nsec = (interval%1000000)*1000;
  ts.it_value.tv_sec = value/1000000;
  ts.it_value.tv_nsec = (value%1000000)*1000;
  timerfd_settime(m_timerfd, 0, &ts, NULL);
  if (m_debugOptions.timer) {
    linfo << "Timer has been adjusted. Interval:" << interval << " Value:" << value << std::endl;
  }
}

void UDPThread::fireTimer() {
  /* Instant expiry (so 1us) */
  adjustTimer(m_timeout, 1);
}

CANThread::CANThread(const struct debugOptions_t &debugOptions,
                     const std::string &canInterfaceName = "can0")
  : Thread()
  , m_canInterfaceName(canInterfaceName)
  , m_udpThread(NULL)
  , m_rxCount(0)
  , m_txCount(0)
{
  memcpy(&m_debugOptions, &debugOptions, sizeof(struct debugOptions_t));
}

int CANThread::start() {
  struct timeval timeout;
  struct ifreq canInterface;
  /* Setup our socket */
  m_canSocket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (m_canSocket < 0) {
    lerror << "socket Error" << std::endl;
    return -1;
  }
  /* Determine the index of m_canInterfaceName */
  strcpy(canInterface.ifr_name, m_canInterfaceName.c_str());
  if (ioctl(m_canSocket, SIOCGIFINDEX, &canInterface) < 0) {
    lerror << "Could get index of interface >" << m_canInterfaceName << "<" << std::endl;
    return -1;
  }
  m_localAddr.can_ifindex = canInterface.ifr_ifindex;
  m_localAddr.can_family = AF_CAN;

  if (bind(m_canSocket, (struct sockaddr *)&m_localAddr, sizeof(m_localAddr)) < 0) {
    lerror << "Could not bind to interface" << std::endl;
    return -1;
  }
  /* Create timerfd */
  m_timerfd = timerfd_create(CLOCK_REALTIME, 0);
  if (m_timerfd < 0) {
    lerror << "timerfd_create error" << std::endl;
    return -1;
  }
  return Thread::start();
}

void CANThread::stop() {
  Thread::stop();
  fireTimer();
}

void CANThread::run() {
  fd_set readfds;
  ssize_t receivedBytes;
  struct itimerspec ts;

  linfo << "CANThread up and running" << std::endl;

  /* Prepare timerfd for the first time*/
  ts.it_interval.tv_sec = CAN_TIMEOUT/1000;
  ts.it_interval.tv_nsec = (CAN_TIMEOUT%1000)*1000000;
  ts.it_value.tv_sec = CAN_TIMEOUT/1000;
  ts.it_value.tv_nsec = (CAN_TIMEOUT%1000)*1000000;
  timerfd_settime(m_timerfd, 0, &ts, NULL);

  while (m_started) {
    /* Prepare readfds */
    FD_ZERO(&readfds);
    FD_SET(m_canSocket, &readfds);
    FD_SET(m_timerfd, &readfds);

    int ret = select(std::max(m_canSocket,m_timerfd)+1, &readfds, NULL, NULL, NULL);
    if (ret < 0) {
      lerror << "select error" << std::endl;
      break;
    }
    if (FD_ISSET(m_timerfd, &readfds)) {
      ssize_t readBytes;
      uint64_t numExp;
      /* read from timer */
      readBytes = read(m_timerfd, &numExp, sizeof(uint64_t));
      if (readBytes != sizeof(uint64_t)) {
        lerror << "timerfd read error" << std::endl;
        break;
      }
      if (numExp) {
        /* We transmit our buffer */
        if (m_frameBuffer->getFrameBufferSize())
          transmitBuffer();
      }
    }
    if (FD_ISSET(m_canSocket, &readfds)) {
      /* Request frame from frameBuffer */
      struct can_frame *frame = m_udpThread->getFrameBuffer()->requestFrame();
      if (frame == NULL) {
        continue;
      }
      receivedBytes = recv(m_canSocket, frame, sizeof(struct can_frame), 0);
      if (receivedBytes < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
          /* Timeout occured */
          m_udpThread->getFrameBuffer()->insertFramePool(frame);
          continue;
        } else {
          m_udpThread->getFrameBuffer()->insertFramePool(frame);
          lerror << "CAN read error" << std::endl;
          return;
        }
      } else if (receivedBytes < sizeof(struct can_frame) || receivedBytes == 0) {
        lwarn << "Incomplete CAN frame" << std::endl;
      } else if (receivedBytes) {
        m_rxCount++;
        if (m_udpThread != NULL) {
          m_udpThread->sendCANFrame(frame);
        }
        if (m_debugOptions.can) {
          printCANInfo(frame);
        }
      }
    }
  }
  if (m_debugOptions.buffer) {
    m_frameBuffer->debug();
  }
  /* free all entries in m_framePool */
  m_frameBuffer->clearPool();
  linfo << "Shutting down. CAN Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << std::endl;
  shutdown(m_canSocket, SHUT_RDWR);
  close(m_canSocket);
}

void CANThread::setUDPThread(UDPThread *thread) {
  m_udpThread = thread;
}

UDPThread* CANThread::getUDPThread() {
  return m_udpThread;
}

void CANThread::setFrameBuffer(FrameBuffer *buffer) {
  m_frameBuffer = buffer;
}

FrameBuffer* CANThread::getFrameBuffer() {
  return m_frameBuffer;
}

void CANThread::transmitCANFrame(can_frame* frame) {
  m_frameBuffer->insertFrame(frame);
  fireTimer();
}

void CANThread::transmitBuffer() {
  ssize_t transmittedBytes = 0;
  /* Swap buffers */
  m_frameBuffer->swapBuffers();
  /* TODO: Should a sort happen here again? */
  const std::list<can_frame*> *buffer = m_frameBuffer->getIntermediateBuffer();
  for (auto it = buffer->begin(); it != buffer->end(); it++) {
    can_frame *frame = *it;
    transmittedBytes = write(m_canSocket, frame, sizeof(*frame));
    if (transmittedBytes != sizeof(*frame)) {
      lerror << "CAN write failed" << std::endl;
    } else {
      m_txCount++;
    }
  }
  m_frameBuffer->unlockIntermediateBuffer();
  m_frameBuffer->mergeIntermediateBuffer();
}

void CANThread::fireTimer() {
  struct itimerspec ts;
  /* Reset the timer of m_timerfd to m_timeout */
  ts.it_interval.tv_sec = CAN_TIMEOUT/1000;
  ts.it_interval.tv_nsec = (CAN_TIMEOUT%1000)*1000000;
  ts.it_value.tv_sec = 0;
  ts.it_value.tv_nsec = 1000;
  timerfd_settime(m_timerfd, 0, &ts, NULL);
}
