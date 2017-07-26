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

#include <string>
#include <stdint.h>

#include "connection.h"
#include "timer.h"

namespace cannelloni {

#define CAN_TIMEOUT 2000000 /* 2 sec in us */

class CANThread : public ConnectionThread {
  public:
    CANThread(const struct debugOptions_t &debugOptions,
              const std::string &canInterfaceName);
    virtual ~CANThread();
    virtual int start();
    virtual void stop();
    virtual void run();

    virtual void transmitFrame(canfd_frame *frame);

  private:
    void transmitBuffer();
    void fireTimer();

  private:
    struct debugOptions_t m_debugOptions;
    int m_canSocket;
    bool m_canfd;
    Timer m_timer;

    std::string m_canInterfaceName;

    /* Performance Counters */
    uint64_t m_rxCount;
    uint64_t m_txCount;
};

}
