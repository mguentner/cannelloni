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

#pragma once

#include <string>
#include <stdint.h>
#include <chrono>

#include "connection.h"
#include "timer.h"

namespace cannelloni {

#define CAN_TIMEOUT 2000000 /* 2 sec in us */
#define CAN_TX_RETRY_MIN 25 /* us, initial retry backoff on a busy bus */
#define CAN_TX_RETRY_MAX 4000 /* us, maximum retry backoff */

class CANThread : public ConnectionThread {
  public:
    CANThread(const struct debugOptions_t &debugOptions,
              const std::string &canInterfaceName);
    virtual ~CANThread();
    virtual int start();
    virtual void stop();
    virtual void run();

    virtual void transmitFrame(canfd_frame *frame);

    /* Drop frames that have been undeliverable on a busy bus for longer than
     * timeout_us. 0 (default) disables the staleness drop. */
    void setTxStaleTimeout(uint32_t timeout_us);

  private:
    void transmitBuffer();
    void fireTimer();
    /* Updates m_busUsable based on a received CAN error frame */
    void handleErrorFrame(canfd_frame *frame);

  private:
    struct debugOptions_t m_debugOptions;
    int m_canSocket;
    bool m_canfd;
    /* Whether the controller currently reports the bus as usable.
     * Set to false on bus-off / error-passive, true again on recovery.
     */
    bool m_busUsable;
    Timer m_timer;

    std::string m_canInterfaceName;

    /* Performance Counters */
    uint64_t m_rxCount;
    uint64_t m_txCount;
    uint64_t m_txDropCount;
    /* Current retry backoff (us) while the bus is usable but busy */
    uint64_t m_retryInterval;
    /* Staleness timeout (us). Frames that stay undeliverable on a busy bus
     * (ENOBUFS/deferral, no bus-off error frame) longer than this are dropped.
     * 0 disables. */
    uint32_t m_txStaleTimeout;
    /* Whether CANThread currently failing consecutivly in writing to the bus, and when
     * that run started. Used to bound staleness against m_txStaleTimeout. */
    bool m_txStuck;
    std::chrono::steady_clock::time_point m_txStuckSince;
    /* Warn-once throttle for the staleness drop, reset on a successful write. */
    bool m_txStaleWarned;
};

}
