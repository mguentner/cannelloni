/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
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


UDPThread::UDPThread(const struct sockaddr_in &remoteAddr,
                     const struct sockaddr_in &localAddr)
  : Thread()
  , m_canThread(NULL)
  , m_frameBufferSize(0)
  , m_frameBufferSize_trans(0)
  , m_frameBuffer(new std::multiset<can_frame, can_frame_comp>)
  , m_frameBuffer_trans(new std::multiset<can_frame, can_frame_comp>)
  , m_sequenceNumber(0)
  , m_timeout(100)
  , m_rxCount(0)
  , m_txCount(0)
{
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
  linfo << "Shutting down. UDP Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << std::endl;
  /* Close socket to interrupt recvfrom in run() */
  shutdown(m_udpSocket, SHUT_RDWR);
  close(m_udpSocket);
  Thread::stop();
}

void UDPThread::run() {
  fd_set readfds;
  ssize_t receivedBytes;
  uint8_t buffer[RECEIVE_BUFFER_SIZE];
  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen = sizeof(struct sockaddr_in);
  char clientAddrStr[INET_ADDRSTRLEN];

  struct itimerspec ts;

  linfo << "UDPThread up and running" << std::endl;
  /* Prepare timerfd for the first time*/
  ts.it_interval.tv_sec = m_timeout/1000;
  ts.it_interval.tv_nsec = (m_timeout%1000)*1000000;
  ts.it_value.tv_sec = m_timeout/1000;
  ts.it_value.tv_nsec = (m_timeout%1000)*1000000;
  timerfd_settime(m_timerfd, 0, &ts, NULL);

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
      if (numExp > 1) {
        lwarn << "timer overflow" << std::endl;
      }
      if (numExp) {
#if TIMER_DEBUG
        struct timeval currentTime;
        gettimeofday(&currentTime, NULL);
        linfo << "Timer exp:" << currentTime.tv_sec << " " << currentTime.tv_usec << std::endl;
#endif
        if (m_frameBuffer->size()) {
          transmitBuffer();
        }
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
              /* We expect the packet to be sorted, so we can just use a vector */
              std::vector<can_frame> canFrames;
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
              if (data->count == 0) {
                linfo << "Received empty packet" << std::endl;
                drop = true;
              }
              if (!drop) {
                uint8_t *rawData = buffer+UDP_DATA_PACKET_BASE_SIZE;
                bool error;
#if UDP_DEBUG
                linfo << "Received " << receivedBytes << " Bytes from Host " << clientAddrStr
                  << ":" << ntohs(clientAddr.sin_port) << std::endl;
#endif
                m_rxCount++;
                for (uint8_t i=0; i<data->count; i++) {
                  if (rawData-buffer+CANNELLONI_FRAME_BASE_SIZE > receivedBytes) {
                    lerror << "Received incomplete packet" << std::endl;
                    error = true;
                    break;
                  }
                  /* We got at least a complete can_frame header */
                  can_frame frame;
                  frame.can_id = ntohl(*((uint32_t*)rawData));
                  rawData+=4;
                  frame.can_dlc = *rawData;
                  rawData+=1;
                  /* Check again now that we know the dlc */
                  if (rawData-buffer+frame.can_dlc > receivedBytes) {
                    lerror << "Received incomplete packet / can header corrupt!" << std::endl;
                    error = true;
                    break;
                  }
                  memcpy(frame.data, rawData, frame.can_dlc);
                  canFrames.push_back(frame);
                  rawData+=frame.can_dlc;
#if CAN_DEBUG
                  if (frame.can_id & CAN_EFF_FLAG)
                    std::cout << "EFF Frame ID[" << (frame.can_id & CAN_EFF_MASK)
                                                 << "]\t Length:" << (int) frame.can_dlc << "\t";
                  else
                    std::cout << "SFF Frame ID[" << (frame.can_id & CAN_SFF_MASK)
                                                 << "]\t Length:" << (int) frame.can_dlc << "\t";
                  for (uint8_t i=0; i<frame.can_dlc; i++)
                    std::cout << std::setbase(16) << " " << int(frame.data[i]);
                  std::cout << std::endl;
#endif
                  }
                  m_canThread->transmitCANFrames(canFrames);
              }
            }
        }
      }
    }
  }
}

void UDPThread::setCANThread(CANThread *thread) {
  m_canThread = thread;
}

CANThread* UDPThread::getCANThread() {
  return m_canThread;
}

void UDPThread::sendCANFrame(const can_frame &frame) {
  m_bufferMutex.lock();
  m_frameBuffer->insert(frame);
  m_frameBufferSize += CANNELLONI_FRAME_BASE_SIZE + frame.can_dlc;
  if (m_frameBufferSize + UDP_DATA_PACKET_BASE_SIZE >= UDP_PAYLOAD_SIZE) {
    fireTimer();
  }
  m_bufferMutex.unlock();
}

void UDPThread::setTimeout(uint32_t timeout) {
  m_timeout = timeout;
}

uint32_t UDPThread::getTimeout() {
  return m_timeout;
}

void UDPThread::transmitBuffer() {
  uint8_t *packetBuffer = new uint8_t[UDP_PAYLOAD_SIZE];
  uint8_t *data;
  uint16_t transmittedBytes = 0;
  uint8_t frameCount;
  struct UDPDataPacket *dataPacket;
  struct timeval currentTime;


  dataPacket = (struct UDPDataPacket*) packetBuffer;
  dataPacket->version = CANNELLONI_FRAME_VERSION;
  dataPacket->op_code = DATA;
  dataPacket->seq_no = m_sequenceNumber++;
  /* We initialize dataPacket->count later */
  data = packetBuffer+UDP_DATA_PACKET_BASE_SIZE;
  m_bufferMutex.lock();
  std::swap(m_frameBufferSize, m_frameBufferSize_trans);
  std::swap(m_frameBuffer, m_frameBuffer_trans);
  m_bufferMutex.unlock();

  for (auto it = m_frameBuffer_trans->begin(); it != m_frameBuffer_trans->end();) {
    auto &frame = *it;
    /* Check for packet overflow */
    if ((data-packetBuffer+CANNELLONI_FRAME_BASE_SIZE+frame.can_dlc) > UDP_PAYLOAD_SIZE) {
      break;
    } else {
      *((canid_t *) (data)) = htonl(frame.can_id);
      data+=4;
      *data = frame.can_dlc;
      data+=1;
      memcpy(data, frame.data, frame.can_dlc);
      data+=frame.can_dlc;
      frameCount++;
      m_frameBufferSize_trans -= (CANNELLONI_FRAME_BASE_SIZE+frame.can_dlc);
      /* Attention: This is our increment function...we need to remove this! */
      it = m_frameBuffer_trans->erase(it);
    }
  }
  dataPacket->count = frameCount;
  transmittedBytes = sendto(m_udpSocket, packetBuffer, data-packetBuffer, 0,
      (struct sockaddr *) &m_remoteAddr, sizeof(m_remoteAddr));
  if (transmittedBytes != data-packetBuffer) {
    lerror << "UDP Socket error. Error while transmitting" << std::endl;
  } else {
    m_txCount++;
  }
  /* We are done with sending, now swap the buffers back */
  m_bufferMutex.lock();
  std::swap(m_frameBufferSize, m_frameBufferSize_trans);
  std::swap(m_frameBuffer, m_frameBuffer_trans);
  /* The buffers are swapped, we now need to merge */
  m_frameBufferSize += m_frameBufferSize_trans;
  m_frameBuffer->insert(m_frameBuffer_trans->begin(), m_frameBuffer_trans->end());
  /* Both frameBuffers are now merged again, clear one */
  m_frameBuffer_trans->clear();
  m_frameBufferSize_trans = 0;
  m_bufferMutex.unlock();
  delete[] packetBuffer;
}

void UDPThread::fireTimer() {
  struct itimerspec ts;
  /* Reset the timer of m_timerfd to m_timeout */
  ts.it_interval.tv_sec = m_timeout/1000;
  ts.it_interval.tv_nsec = (m_timeout%1000)*1000000;
  ts.it_value.tv_sec = 0;
  ts.it_value.tv_nsec = 1000;
  timerfd_settime(m_timerfd, 0, &ts, NULL);
}

CANThread::CANThread(const std::string &canInterfaceName = "can0")
  : Thread()
  , m_canInterfaceName(canInterfaceName)
  , m_udpThread(NULL)
  , m_frameBuffer(new std::vector<can_frame>)
  , m_frameBuffer_trans(new std::vector<can_frame>)
  , m_rxCount(0)
  , m_txCount(0)
{
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
  linfo << "Shutting down. CAN Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << std::endl;
  /*
   * shutdown socket to interrupt recvfrom in run()
   * NOTE: This does currently not work, there we use a timeout on
   * the socket to interrupt the loop
   */
  shutdown(m_canSocket, SHUT_RDWR);
  fireTimer();
  close(m_canSocket);
  Thread::stop();
}

void CANThread::run() {
  fd_set readfds;
  ssize_t receivedBytes;
  struct can_frame frame;
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
      if (numExp > 1) {
        lwarn << "timer overflow" << std::endl;
      }
      if (numExp) {
        /* We transmit our buffer */
        transmitBuffer();
      }
    }
    if (FD_ISSET(m_canSocket, &readfds)) {
      receivedBytes = recv(m_canSocket, &frame, sizeof(struct can_frame), 0);
      if (receivedBytes < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
          /* Timeout occured */
          continue;
        } else {
          lerror << "CAN read error" << std::endl;
          return;
        }
      } else if (receivedBytes < sizeof(struct can_frame) || receivedBytes == 0) {
        lwarn << "Incomplete CAN frame" << std::endl;
      } else if (receivedBytes) {
#if CAN_DEBUG
        if (frame.can_id & CAN_EFF_FLAG)
          std::cout << "EFF Frame ID[" << (frame.can_id & CAN_EFF_MASK)
                                       << "]\t Length:" << (int) frame.can_dlc << "\t";
        else
          std::cout << "SFF Frame ID[" << (frame.can_id & CAN_SFF_MASK)
                                       << "]\t Length:" << (int) frame.can_dlc << "\t";
        for (uint8_t i=0; i<frame.can_dlc; i++)
          std::cout << std::setbase(16) << " " << int(frame.data[i]);
        std::cout << std::endl;
#endif
        m_rxCount++;
        if (m_udpThread != NULL)
          m_udpThread->sendCANFrame(frame);
      }
    }
  }
}

void CANThread::setUDPThread(UDPThread *thread) {
  m_udpThread = thread;
}

UDPThread* CANThread::getUDPThread() {
  return m_udpThread;
}

void CANThread::transmitCANFrames(const std::vector<can_frame> &frames) {
  m_bufferMutex.lock();
  m_frameBuffer->insert(m_frameBuffer->end(), frames.begin(), frames.end());
  m_bufferMutex.unlock();
  fireTimer();
}

void CANThread::transmitBuffer() {
  ssize_t transmittedBytes = 0;
  /* Swap buffers */
  m_bufferMutex.lock();
  std::swap(m_frameBuffer, m_frameBuffer_trans);
  m_bufferMutex.unlock();
  for (can_frame frame : *m_frameBuffer_trans) {
    transmittedBytes = write(m_canSocket, &frame, sizeof(frame));
    if (transmittedBytes != sizeof(frame)) {
      lerror << "CAN write failed" << std::endl;
    } else {
      m_txCount++;
    }
  }
  m_frameBuffer_trans->clear();
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
