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
#include <algorithm>

#include <linux/can/raw.h>

#include <net/if.h>
#include <arpa/inet.h>
#include <string>

#include "connection.h"
#include "logging.h"

using namespace cannelloni;

#define EMPTY_ADDR_STRING 			"0.0.0.0"
#define DEFAULT_CLIENT_TIMEOUT_SEC 		  60

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
                     const struct sockaddr_in &localAddr,
                     bool sort)
  : Thread()
  , m_canThread(NULL)
  , m_frameBuffer(NULL)
  , m_sequenceNumber(0)
  , m_timeout(100)
  , m_rxCount(0)
  , m_txCount(0)
  , m_sort(sort)
  ,m_bind2firstConnection(false)
  ,m_clientConnectionTimeoutSec(DEFAULT_CLIENT_TIMEOUT_SEC)
{
  memcpy(&m_debugOptions, &debugOptions, sizeof(struct debugOptions_t));
  memcpy(&m_remoteAddr, &remoteAddr, sizeof(struct sockaddr_in));
  memcpy(&m_localAddr, &localAddr, sizeof(struct sockaddr_in));
  memset(&m_currentClientAddr, 0, sizeof(struct sockaddr_in));
  if( EMPTY_ADDR_STRING == getAddressString(&m_remoteAddr) )  {
    linfo << "Remote address is empty, cannelloni will bind to first connection" << std::endl;
    m_bind2firstConnection = true;
  }

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
  m_timerFdUdp = timerfd_create(CLOCK_REALTIME, 0);
  if (m_timerFdUdp < 0) {
    lerror << "timerfd_create error" << std::endl;
    return -1;
  }
  /* Create timerfd */
  m_timerFdClientConnection = timerfd_create(CLOCK_REALTIME, 0);
  if (m_timerFdClientConnection < 0) {
    lerror << "Client Connection error" << std::endl;
    return -1;
  }
  linfo << "timerFd->" << std::dec << m_timerFdClientConnection << std::dec <<
		 " ," <<  m_timerFdUdp << std::endl;
  return Thread::start();
}

void UDPThread::stop() {
  Thread::stop();
  /* m_started is now false, we need to wake up the thread */
  fireTimer();
}

std::string UDPThread::getAddressString( struct sockaddr_in * address ) {
  char clientAddrStr[INET_ADDRSTRLEN];
  std::string addr_string;

  if (inet_ntop(AF_INET, &address->sin_addr, clientAddrStr, INET_ADDRSTRLEN) == NULL)	{
   lwarn << "Could not convert client address" << std::endl;
  } 	else {
   addr_string.append(clientAddrStr);
  }
  return addr_string;
}

bool
UDPThread::setClientConnectionTimeoutSec(uint32_t sec) {
  bool retval = false;
  if (m_bind2firstConnection) {
    m_clientConnectionTimeoutSec = sec;
    retval= true;
  }
  return retval;
}

void UDPThread::run() {
  fd_set readfds;
  ssize_t receivedBytes;
  uint8_t buffer[RECEIVE_BUFFER_SIZE];
  socklen_t clientAddrLen = sizeof(struct sockaddr_in);

  /* Set interval to m_timeout and an immediate timeout */
  adjustTimer(m_timerFdUdp, m_timeout, 1);

  linfo << "UDPThread up and listenning on port: " << std::dec << m_localAddr.sin_port << std::endl;
  linfo << "Remote Address is set to " << getAddressString(&m_remoteAddr) << std::endl;

  while (m_started) {
    /* Prepare readfds */
    FD_ZERO(&readfds);
    FD_SET(m_udpSocket, &readfds);
    FD_SET(m_timerFdUdp, &readfds);
    FD_SET(m_timerFdClientConnection, &readfds);
    int ret = select(std::max({m_udpSocket,m_timerFdClientConnection,m_timerFdUdp})+1, &readfds, NULL, NULL, NULL);
    if (ret < 0) {
      lerror << "select error" << std::endl;
      break;
    }
    if (FD_ISSET(m_timerFdUdp, &readfds)) {
      ssize_t readBytes;
      uint64_t numExp;
      /* read from timer */
      readBytes = read(m_timerFdUdp, &numExp, sizeof(uint64_t));
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
      bzero(buffer,RECEIVE_BUFFER_SIZE);

      receivedBytes = recvfrom(m_udpSocket, buffer, RECEIVE_BUFFER_SIZE,
         0, (struct sockaddr *) &m_currentClientAddr, &clientAddrLen);
      if (receivedBytes < 0) {
        lerror << "recvfrom error." << std::endl;
        return;
      } else if (receivedBytes > 0) {

        if( m_bind2firstConnection && getAddressString(&m_remoteAddr) == EMPTY_ADDR_STRING ) {
          /*Bind to this first client incoming, now the remote is the current..waiting a timeout*/
          memcpy(&m_remoteAddr,&m_currentClientAddr,sizeof(m_currentClientAddr));
          linfo << "Bound to remote Client: " << getAddressString(&m_remoteAddr)  << std::endl;
        }
        std::string addrString = getAddressString(&m_remoteAddr);

        if ( addrString.empty() ) {
         lwarn << "Could not convert client address" << std::endl;
        } else {
          if (memcmp(&(m_currentClientAddr.sin_addr), &(m_remoteAddr.sin_addr), sizeof(struct in_addr)) != 0) {
           lwarn << "Received a packet from " << getAddressString(&m_currentClientAddr)
                 << ", which is not set as a remote." << std::endl;
          } else {

            if ( handleMessage(buffer , receivedBytes) ) {
              /*If message is correct start timeout*/
              if( m_bind2firstConnection ) {
                /*disable and re-enable timeout*/
                adjustTimer(m_timerFdClientConnection,0, m_clientConnectionTimeoutSec*1000*1000);
              } else {
                lwarn << "error handlinf Frame" << std::endl;
              }
            }
          }
        }
      } /*end byte rx > 0*/
    }
    if (FD_ISSET(m_timerFdClientConnection, &readfds)) {
          linfo << "Client Connection Timeout Expired"  << std::endl;
          bzero(&m_remoteAddr,sizeof(m_remoteAddr));
          /*stop timer*/
          adjustTimer(m_timerFdClientConnection,0, 0);
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


bool
UDPThread::handleMessage(uint8_t * buffer , size_t bufferSize) {

  bool drop = false;
  struct UDPDataPacket *data = NULL;
  bool frameHandled =false;
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

    if (m_debugOptions.udp) {
      linfo << "Received " << std::dec << bufferSize << " Bytes from Host " << getAddressString (&m_remoteAddr)
        << ":" << ntohs(m_remoteAddr.sin_port) << std::endl;
    }
    m_rxCount++;
    for (uint16_t i=0; i<ntohs(data->count); i++) {
      if (rawData-buffer+CANNELLONI_FRAME_BASE_SIZE > bufferSize) {
        lerror << "Received incomplete packet" << std::endl;
        break;
      }
      /* We got at least a complete canfd_frame header */
      canfd_frame *frame = m_canThread->getFrameBuffer()->requestFrame();
      if (!frame) {
        lerror << "Allocation error." << std::endl;
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
        if (rawData-buffer+canfd_len(frame) > bufferSize) {
          lerror << "Received incomplete packet / can header corrupt!" << std::endl;
        }
        memcpy(frame->data, rawData, canfd_len(frame));
        rawData += canfd_len(frame);
      }
      m_canThread->transmitCANFrame(frame);
      if (m_debugOptions.can) {
        printCANInfo(frame);
      }
      frameHandled = true;
    }
  } else {
    if(m_bind2firstConnection) {
      linfo << "remote ip " << getAddressString(&m_remoteAddr) << " NOT handled as remote  " << std::endl;
      /*reset value*/
      bzero(&m_remoteAddr,sizeof(m_remoteAddr));
    }
  }
  return frameHandled;
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

void UDPThread::sendCANFrame(canfd_frame *frame) {
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
        if (timeout < getTimerValue(m_timerFdUdp)) {
          if (m_debugOptions.timer) {
            linfo << "Found timeout entry for ID " << can_id << ". Adjusting timer." << std::endl;
          }
          /* Let buffer expire in timeout ms */
          adjustTimer(m_timerFdUdp,m_timeout, timeout);
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

inline uint32_t UDPThread::getTimerValue(int timerFd) {
  struct itimerspec ts;
  timerfd_gettime(timerFd, &ts);
  return ts.it_value.tv_sec*1000000 + ts.it_value.tv_nsec/1000;
}

inline void UDPThread::adjustTimer(int timerFd , uint32_t interval, uint32_t value) {
  struct itimerspec ts;
  /* Setting the value to 0 disables the timer */
  if (value == 0)
    //value = 1;
  ts.it_interval.tv_sec = interval/1000000;
  ts.it_interval.tv_nsec = (interval%1000000)*1000;
  ts.it_value.tv_sec = value/1000000;
  ts.it_value.tv_nsec = (value%1000000)*1000;
  timerfd_settime(timerFd, 0, &ts, NULL);
  if (m_debugOptions.timer) {
    linfo << "Timer has been adjusted. Interval:" << interval << " Value:" << value << std::endl;
  }
}

void UDPThread::fireTimer() {
  /* Instant expiry (so 1us) */
  adjustTimer(m_timerFdUdp,m_timeout, 1);
}

CANThread::CANThread(const struct debugOptions_t &debugOptions,
                     const std::string &canInterfaceName = "can0")
  : Thread()
  , m_canInterfaceName(canInterfaceName)
  , m_udpThread(NULL)
  , m_rxCount(0)
  , m_txCount(0)
  , m_canfd(false)
{
  memcpy(&m_debugOptions, &debugOptions, sizeof(struct debugOptions_t));
}

int CANThread::start() {
  struct timeval timeout;
  struct ifreq canInterface;
  uint32_t canfd_on = 1;
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
  /* Check MTU of interface */
  if (ioctl(m_canSocket, SIOCGIFMTU, &canInterface) < 0) {
    lerror << "Could get MTU of interface >" << m_canInterfaceName << "<" <<  std::endl;
  }
  /* Check whether CAN_FD is possible */
  if (canInterface.ifr_mtu == CANFD_MTU) {
    /* Try to switch into CAN_FD mode */
    if (setsockopt(m_canSocket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof(canfd_on))) {
      lerror << "Could not enable CAN_FD." << std::endl;
    } else {
      m_canfd = true;
    }

  } else {
    lerror << "CAN_FD is not supported on >" << m_canInterfaceName << "<" << std::endl;
  }

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

  adjustTimer(CAN_TIMEOUT, CAN_TIMEOUT);

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
      struct canfd_frame *frame = m_udpThread->getFrameBuffer()->requestFrame();
      if (frame == NULL) {
        continue;
      }
      receivedBytes = recv(m_canSocket, frame, sizeof(struct canfd_frame), 0);
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
      } else if (receivedBytes == CAN_MTU || receivedBytes == CANFD_MTU) {
        m_rxCount++;
        /* If it is a CAN FD frame, encode this in len */
        if (receivedBytes == CANFD_MTU) {
          frame->len |= CANFD_FRAME;
        } else {
          frame->len &= ~(CANFD_FRAME);
        }
        if (m_udpThread != NULL) {
          m_udpThread->sendCANFrame(frame);
        }
        if (m_debugOptions.can) {
          printCANInfo(frame);
        }
      } else {
        lwarn << "Incomplete/Invalid CAN frame" << std::endl;
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

void CANThread::transmitCANFrame(canfd_frame* frame) {
  m_frameBuffer->insertFrame(frame);
  fireTimer();
}

void CANThread::transmitBuffer() {
  ssize_t transmittedBytes = 0;
  /* Loop here until buffer is empty or we cannot write anymore */
  while(1) {
    canfd_frame *frame = m_frameBuffer->requestBufferFront();
    if (frame == NULL)
      break;
    /* Check whether we are operating on a CAN FD socket */
    if (m_canfd) {
      /* Clear the CANFD_FRAME bit in len */
      frame->len &= ~(CANFD_FRAME);
      transmittedBytes = write(m_canSocket, frame, CANFD_MTU);
    } else {
      /* First check the length of the frame */
      if (frame->len & CANFD_FRAME) {
        /* Something is wrong with the setup */
        lwarn << "Received a CAN FD for a socket that only supports (CAN 2.0)." << std::endl;
        frame->len &= ~(CANFD_FRAME);
        m_frameBuffer->insertFramePool(frame);
        continue;
      } else {
        /* No CAN FD socket, use legacy MTU */
        transmittedBytes = write(m_canSocket, frame, CAN_MTU);
      }
    }
    if (transmittedBytes == CANFD_MTU || transmittedBytes == CAN_MTU) {
      /* Put frame back into pool */
      m_frameBuffer->insertFramePool(frame);
      m_txCount++;
    } else {
      /* Put frame back into buffer */
      m_frameBuffer->returnFrame(frame);
      /* Revisit this function after 25 us */
      adjustTimer(CAN_TIMEOUT, 25);
      if (m_debugOptions.can)
        linfo << "CAN write failed." << std::endl;
      break;
    }
  }
}

inline uint32_t CANThread::getTimerValue() {
  struct itimerspec ts;
  timerfd_gettime(m_timerfd, &ts);
  return ts.it_value.tv_sec*1000000 + ts.it_value.tv_nsec/1000;
}

inline void CANThread::adjustTimer(uint32_t interval, uint32_t value) {
  struct itimerspec ts;
  /* Setting the value to 0 disables the timer */
//  if (value == 0)
//    value = 1;
  ts.it_interval.tv_sec = interval/1000000;
  ts.it_interval.tv_nsec = (interval%1000000)*1000;
  ts.it_value.tv_sec = value/1000000;
  ts.it_value.tv_nsec = (value%1000000)*1000;
  timerfd_settime(m_timerfd, 0, &ts, NULL);
  if (m_debugOptions.timer) {
    linfo << "Timer has been adjusted. Interval:" << interval << " Value:" << value << std::endl;
  }
}

void CANThread::fireTimer() {
  /* Instant expiry (so 1us) */
  adjustTimer(CAN_TIMEOUT, 1);
}
