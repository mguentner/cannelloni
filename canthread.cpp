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

#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include "canthread.h"
#include "cannelloni.h"
#include "logging.h"

using namespace cannelloni;

CANThread::CANThread(const struct debugOptions_t &debugOptions,
                     const std::string &canInterfaceName = "can0")
  : ConnectionThread()
  , m_canSocket(0)
  , m_canfd(false)
  , m_canInterfaceName(canInterfaceName)
  , m_rxCount(0)
  , m_txCount(0)
{
  memcpy(&m_debugOptions, &debugOptions, sizeof(struct debugOptions_t));
}

CANThread::~CANThread() {}

int CANThread::start() {
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
  struct sockaddr_can localAddr;
  memset(&localAddr, 0, sizeof(localAddr));
  localAddr.can_ifindex = canInterface.ifr_ifindex;
  localAddr.can_family = AF_CAN;
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

  if (bind(m_canSocket, (struct sockaddr *)&localAddr, sizeof(localAddr)) < 0) {
    lerror << "Could not bind to interface" << std::endl;
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

  linfo << "CANThread up and running" << std::endl;

  m_timer.adjust(CAN_TIMEOUT, CAN_TIMEOUT);

  while (m_started) {
    /* Prepare readfds */
    FD_ZERO(&readfds);
    FD_SET(m_canSocket, &readfds);
    FD_SET(m_timer.getFd(), &readfds);

    int ret = select(std::max(m_canSocket,m_timer.getFd())+1, &readfds, NULL, NULL, NULL);
    if (ret < 0) {
      lerror << "select error" << std::endl;
      break;
    }
    if (FD_ISSET(m_timer.getFd(), &readfds)) {
      if (m_timer.read() > 0) {
        /* We transmit our buffer */
        if (m_frameBuffer->getFrameBufferSize())
          transmitBuffer();
      }
    }
    if (FD_ISSET(m_canSocket, &readfds)) {
      /* Request frame from frameBuffer */
      struct canfd_frame *frame = m_peerThread->getFrameBuffer()->requestFrame(true, m_debugOptions.buffer);
      if (frame == NULL) {
        continue;
      }
      receivedBytes = recv(m_canSocket, frame, sizeof(struct canfd_frame), 0);
      if (receivedBytes < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
          /* Timeout occurred */
          m_peerThread->getFrameBuffer()->insertFramePool(frame);
          continue;
        } else {
          m_peerThread->getFrameBuffer()->insertFramePool(frame);
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
        if (m_peerThread != NULL) {
          m_peerThread->transmitFrame(frame);
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
  linfo << "Shutting down. CAN Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << std::endl;
  shutdown(m_canSocket, SHUT_RDWR);
  close(m_canSocket);
}

void CANThread::transmitFrame(canfd_frame* frame) {
  m_frameBuffer->insertFrame(frame);
  fireTimer();
}

void CANThread::transmitBuffer() {
  ssize_t transmittedBytes = 0;
  /* Loop here until buffer is empty or we cannot write anymore */
  while(1) {
    canfd_frame *frame = m_frameBuffer->requestBufferFront();
    bool frameIsCANFD = false;
    if (frame == NULL)
      break;
    /* Check whether we are operating on a CAN FD socket */
    if (m_canfd) {
      if (frame->len & CANFD_FRAME) {
        frameIsCANFD = true;
        /* Clear the CANFD_FRAME bit in len */
        frame->len &= ~(CANFD_FRAME);
        transmittedBytes = write(m_canSocket, frame, CANFD_MTU);
      } else {
        frame->len &= ~(CANFD_FRAME);
        transmittedBytes = write(m_canSocket, frame, CAN_MTU);
      }
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
      /* If it was a CAN FD frame, encode this in len again before putting it back into buffer */
      if (frameIsCANFD) {
        frame->len |= CANFD_FRAME;
      }
      /* Put frame back into buffer */
      m_frameBuffer->returnFrame(frame);
      /* Revisit this function after 25 us */
      m_timer.adjust(CAN_TIMEOUT, 25);
      if (m_debugOptions.can)
        linfo << "CAN write failed." << std::endl;
      break;
    }
  }
}

void CANThread::fireTimer() {
  /* Instant expiry (so 1us) */
  m_timer.adjust(CAN_TIMEOUT, 1);
}
