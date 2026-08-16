/*
 * This file is part of cannelloni, a SocketCAN over ethernet tunnel.
 *
 * Copyright (C) 2014-2026 Maximilian Güntner <code@mguentner.de>
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
#include <linux/can/error.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include "canthread.h"
#include "cannelloni.h"
#include "logging.h"

using namespace cannelloni;

CANThread::CANThread(const struct debugOptions_t &debugOptions, const std::string &canInterfaceName)
  : ConnectionThread()
  , m_canSocket(0)
  , m_canfd(false)
  , m_busUsable(true)
  , m_canInterfaceName(canInterfaceName)
  , m_rxCount(0)
  , m_txCount(0)
  , m_txDropCount(0)
  , m_retryInterval(CAN_TX_RETRY_MIN)
  , m_txStaleTimeout(0)
  , m_txStuck(false)
  , m_txStaleWarned(false)
{
  memcpy(&m_debugOptions, &debugOptions, sizeof(struct debugOptions_t));
}

CANThread::~CANThread() {}

void CANThread::setTxStaleTimeout(uint32_t timeout_us) {
  m_txStaleTimeout = timeout_us;
}

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
  strncpy(canInterface.ifr_name, m_canInterfaceName.c_str(), IFNAMSIZ-1);
  canInterface.ifr_name[IF_NAMESIZE-1] = '\0';
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

  /*
   * Subscribe to error frames so we can tell a broken bus (bus-off /
   * error-passive) apart from a busy one.
   * Only drop frames in the when the bus is broken, retry aggressivly on
   * a busy one.
   *
   * Note: this depends on the error case, a broken bus may also just
   * result in a controller deferring transmission and not accepting
   * more frames in the tx queue. Error handling thus is best-effort here
   */
  can_err_mask_t err_mask = CAN_ERR_BUSOFF | CAN_ERR_CRTL | CAN_ERR_RESTARTED;
  if (setsockopt(m_canSocket, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                 &err_mask, sizeof(err_mask)) < 0) {
    lwarn << "Could not enable CAN error frames on >" << m_canInterfaceName
          << "<; bus-off detection disabled." << std::endl;
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
        } else if (errno == ENETDOWN || errno == ENODEV) {
          /* the interface is down, continue can come back later */
          m_peerThread->getFrameBuffer()->insertFramePool(frame);
          continue;
        } else {
          m_peerThread->getFrameBuffer()->insertFramePool(frame);
          lerror << "CAN read error" << std::endl;
          break;
        }
      } else if (receivedBytes == CAN_MTU || receivedBytes == CANFD_MTU) {
        /* Error frames are delivered on the same socket */
        if (frame->can_id & CAN_ERR_FLAG) {
          handleErrorFrame(frame);
          m_peerThread->getFrameBuffer()->insertFramePool(frame);
          continue;
        }
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
  linfo << "Shutting down. CAN Transmission Summary: TX: " << m_txCount << " RX: " << m_rxCount << " DROP: " << m_txDropCount << std::endl;
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
    /* If the controller reports the bus as unusable (bus-off / error-passive)
     * the frame is undeliverable. Drop it instead of retrying forever and
     * flushing stale data once the bus recovers. */
    if (!m_busUsable) {
      frame->len &= ~(CANFD_FRAME);
      m_frameBuffer->insertFramePool(frame);
      m_txDropCount++;
      continue;
    }
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
      /* If we had been dropping stale frames, the bus is now usable again. */
      if (m_txStaleWarned) {
        linfo << "Bus on >" << m_canInterfaceName
              << "< operational again, resuming transmission." << std::endl;
      }
      /* Successful write, reset the retry backoff and staleness tracking */
      m_retryInterval = CAN_TX_RETRY_MIN;
      m_txStuck = false;
      m_txStaleWarned = false;
    } else {
      int writeErrno = errno;
      /* ENETDOWN: the interface is down, the frame cannot be transmitted at all, drop it. */
      if (writeErrno == ENETDOWN) {
        frame->len &= ~(CANFD_FRAME);
        m_frameBuffer->insertFramePool(frame);
        m_txDropCount++;
        continue;
      }
      /* Bus is usable but busy (e.g. lost arbitration, or the controller is
       * deferring on a bus that never reports bus-off): the frame is
       * (probably) still deliverable; back off and retry.
       * record the timea to drop later after m_txStaleTimeout
       */
      auto now = std::chrono::steady_clock::now();
      if (!m_txStuck) {
        m_txStuck = true;
        m_txStuckSince = now;
      }
      /* If we have been undeliverable longer than the configured staleness
       * timeout, drop this frame instead of buffering it forever and flushing
       * stale data on recovery. Every write above still runs, so each drop
       * doubles as a recovery probe (a success resets m_txStuck). */
      if (m_txStaleTimeout > 0 &&
          std::chrono::duration_cast<std::chrono::microseconds>(now - m_txStuckSince)
              .count() > static_cast<int64_t>(m_txStaleTimeout)) {
        frame->len &= ~(CANFD_FRAME);
        m_frameBuffer->insertFramePool(frame);
        m_txDropCount++;
        if (!m_txStaleWarned) {
          lwarn << "Frames undeliverable on >" << m_canInterfaceName << "< for > "
                << m_txStaleTimeout << " us, dropping (bus stuck?)." << std::endl;
          m_txStaleWarned = true;
        }
        continue;
      }
      /* If it was a CAN FD frame, encode this in len again before putting it back into buffer */
      if (frameIsCANFD) {
        frame->len |= CANFD_FRAME;
      }
      /* Put frame back into buffer and retry after the backoff interval. */
      m_frameBuffer->returnFrame(frame);
      m_timer.adjust(CAN_TIMEOUT, m_retryInterval);
      if (m_debugOptions.can)
        linfo << "CAN write failed, retry in " << m_retryInterval << " us." << std::endl;
      m_retryInterval *= 2;
      if (m_retryInterval > CAN_TX_RETRY_MAX)
        m_retryInterval = CAN_TX_RETRY_MAX;
      break;
    }
  }
}

void CANThread::fireTimer() {
  /* Instant expiry (so 1us) */
  m_timer.adjust(CAN_TIMEOUT, 1);
}

void CANThread::handleErrorFrame(canfd_frame *frame) {
  bool wasUsable = m_busUsable;
  /* bus-off: the controller cannot transmit at all. */
  if (frame->can_id & CAN_ERR_BUSOFF) {
    m_busUsable = false;
  }
  /*
   * error-passive means frames are not getting acknowledged (broken/lonely bus)
   * error-active means we can transmit again
   */
  if (frame->can_id & CAN_ERR_CRTL) {
    uint8_t status = frame->data[1];
    if (status & (CAN_ERR_CRTL_TX_PASSIVE | CAN_ERR_CRTL_RX_PASSIVE))
      m_busUsable = false;
    else if (status & CAN_ERR_CRTL_ACTIVE)
      m_busUsable = true;
  }
  /* controller was restarted after a bus-off. */
  if (frame->can_id & CAN_ERR_RESTARTED) {
    m_busUsable = true;
  }
  if (wasUsable != m_busUsable) {
    linfo << "CAN bus on >" << m_canInterfaceName << "< is now "
          << (m_busUsable ? "usable, resuming transmission"
                          : "unusable (bus-off/error-passive), dropping backlog")
          << std::endl;
    fireTimer();
  }
}
