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

#include "thread.h"
#include "make_unique.h"

using namespace cannelloni;

Thread::Thread()
  : m_started(false)
  , m_running(false)
{ }

Thread::~Thread() {
  if (m_started)
    stop();
}

int Thread::start() {
  m_started = true;
  m_privThread = std::make_unique<std::thread>(&Thread::privRun, this);
  m_running = true;
  return 0;
}

void Thread::stop() {
  m_started = false;
}

void Thread::join() {
  if (m_privThread)
    m_privThread->join();
  m_privThread.reset(nullptr);
}

bool Thread::isRunning() {
  return m_running;
}

void Thread::privRun() {
  run();
  m_running = false;
  m_started = false;
}


