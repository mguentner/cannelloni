/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
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

#include <stdint.h>
#include <sys/timerfd.h>

namespace cannelloni {

/*
 * A simple Timer class that wraps around the
 * timerfd API of the Linux Kernel.
 * Once created, the timer can be adjusted.
 * The FD returned by getFd() can then be used
 * in select() calls
 */

class Timer {
  public:
    Timer();
    ~Timer();

    uint64_t getValue();

    /* adjusts the interval and value of the Timer */
    void adjust(uint64_t interval, uint64_t value);
    /* read # of timeouts */
    uint64_t read();

    int getFd();

    /* disable timer */
    void disable();
    /* enable timer */
    void enable();
    /* trigger an immediate timeout */
    void fire();
    /* returns whether the timer is enabled */
    bool isEnabled();
  private:
    int m_timerfd;
};

}
