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

#include <unistd.h>
#include <fcntl.h>

#include "timer.h"
#include "logging.h"

using namespace cannelloni;

Timer::Timer() {
  /* Create timerfd */
  m_timerfd = timerfd_create(CLOCK_REALTIME, 0);
  if (m_timerfd < 0) {
    lerror << "timerfd_create error" << std::endl;
  }
}

Timer::~Timer() {

}

uint64_t Timer::getValue() {
  struct itimerspec ts;
  timerfd_gettime(m_timerfd, &ts);
  return ts.it_value.tv_sec*1000000 + ts.it_value.tv_nsec/1000;
}

void Timer::adjust(uint64_t interval, uint64_t value) {
  struct itimerspec ts;
  /* Setting the interval/value to 0 disables the timer */
  if (value == 0)
    value = 1;
  if (interval == 0)
    interval = 1;
  ts.it_interval.tv_sec = interval/1000000;
  ts.it_interval.tv_nsec = (interval%1000000)*1000;
  ts.it_value.tv_sec = value/1000000;
  ts.it_value.tv_nsec = (value%1000000)*1000;
  timerfd_settime(m_timerfd, 0, &ts, NULL);
}

uint64_t Timer::read() {
  ssize_t readBytes;
  uint64_t numExp;
  /* read from timer */
  readBytes = ::read(m_timerfd, &numExp, sizeof(uint64_t));
  if (readBytes != sizeof(uint64_t)) {
    lerror << "timerfd read error" << std::endl;
    numExp = -1;
  }
  return numExp;
}

int Timer::getFd() {
  return m_timerfd;
}

void Timer::disable() {
  struct itimerspec ts;
  timerfd_gettime(m_timerfd, &ts);
  ts.it_value.tv_sec = 0;
  ts.it_value.tv_nsec = 0;
  timerfd_settime(m_timerfd, 0, &ts, NULL);
}

void Timer::enable() {
  struct itimerspec ts;
  timerfd_gettime(m_timerfd, &ts);
  ts.it_value.tv_sec = ts.it_interval.tv_sec;
  ts.it_value.tv_nsec = ts.it_interval.tv_nsec;
  timerfd_settime(m_timerfd, 0, &ts, NULL);
}

void Timer::fire() {
  struct itimerspec ts;
  timerfd_gettime(m_timerfd, &ts);
  adjust(ts.it_interval.tv_sec*1000+ts.it_interval.tv_nsec/1000, 1);
}

bool Timer::isEnabled() {
  if (getValue())
    return true;
  else
    return false;
}
