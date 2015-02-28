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

#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/timerfd.h>

#include <net/if.h>
#include <arpa/inet.h>

#include "udpthread.h"
#include "logging.h"

UDPThread::UDPThread(const struct debugOptions_t &debugOptions,
                     const struct sockaddr_in &remoteAddr,
                     const struct sockaddr_in &localAddr,
                     bool sort)
  : ConnectionThread()
  , m_udpSocket(0)
  , m_sequenceNumber(0)
  , m_timeout(100)
  , m_rxCount(0)
  , m_txCount(0)
  , m_sort(sort)
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
  m_timer.adjust(m_timeout, 1);

  linfo << "UDPThread up and running" << std::endl;
  while (m_started) {
    /* Prepare readfds */
    FD_ZERO(&readfds);
    FD_SET(m_udpSocket, &readfds);
    FD_SET(m_timer.getFd(), &readfds);
    int ret = select(std::max(m_udpSocket,m_timer.getFd())+1, &readfds, NULL, NULL, NULL);
    if (ret < 0) {
      lerror << "select error" << std::endl;
      break;
    }
    if (FD_ISSET(m_timer.getFd(), &readfds)) {
      if (m_timer.read() > 0) {
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
                  /* We got at least a complete canfd_frame header */
                  canfd_frame *frame = m_peerThread->getFrameBuffer()->requestFrame();
                  if (!frame) {
                    lerror << "Allocation error." << std::endl;
                    error = true;
                    break;
                  }
                  frame->can_id = ntohl(*((canid_t*)rawData));
                  /* += 4 */
                  rawData += sizeof(canid_t);
                  frame->len = *rawData;
                  /* += 1 */
                  rawData += sizeof(frame->len);
                  /* If this is a CAN FD frame, also retrieve the flags */
                  if (frame->len & CANFD_FRAME) {
                    frame->flags = *rawData;
                    /* += 1 */
                    rawData += sizeof(frame->flags);
                  }
                  /* RTR Frames have no data section although they have a dlc */
                  if ((frame->can_id & CAN_RTR_FLAG) == 0) {
                    /* Check again now that we know the dlc */
                    if (rawData-buffer+canfd_len(frame) > receivedBytes) {
                      lerror << "Received incomplete packet / can header corrupt!" << std::endl;
                      error = true;
                      break;
                    }
                    memcpy(frame->data, rawData, canfd_len(frame));
                    rawData += canfd_len(frame);
                  }
                  m_peerThread->transmitFrame(frame);
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

void UDPThread::transmitFrame(canfd_frame *frame) {
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
        if (timeout < m_timer.getValue()) {
          if (m_debugOptions.timer) {
            linfo << "Found timeout entry for ID " << can_id << ". Adjusting timer." << std::endl;
          }
          /* Let buffer expire in timeout ms */
          m_timer.adjust(m_timeout, timeout);
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
  if (m_sort)
    m_frameBuffer->sortIntermediateBuffer();

  const std::list<canfd_frame*> *buffer = m_frameBuffer->getIntermediateBuffer();

  data = packetBuffer+UDP_DATA_PACKET_BASE_SIZE;
  for (auto it = buffer->begin(); it != buffer->end(); it++) {
    canfd_frame *frame = *it;
    /* Check for packet overflow */
    if ((data-packetBuffer
          +CANNELLONI_FRAME_BASE_SIZE
          +canfd_len(frame)
          +((frame->len & CANFD_FRAME)?sizeof(frame->flags):0)) > UDP_PAYLOAD_SIZE) {
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
    /* += 4 */
    data += sizeof(canid_t);
    *data = frame->len;
    /* += 1 */
    data += sizeof(frame->len);
    /* If this is a CAN FD frame, also send the flags */
    if (frame->len & CANFD_FRAME) {
      *data = frame->flags;
      /* += 1 */
      data += sizeof(frame->flags);
    }
    if ((frame->can_id & CAN_RTR_FLAG) == 0) {
      memcpy(data, frame->data, canfd_len(frame));
      data+=canfd_len(frame);
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

void UDPThread::fireTimer() {
  /* Instant expiry (so 1us) */
  m_timer.adjust(m_timeout, 1);
}


