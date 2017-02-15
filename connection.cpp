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

#include "connection.h"

using namespace cannelloni;

ConnectionThread::ConnectionThread()
  : Thread()
  , m_frameBuffer(0)
  , m_peerThread(0)
{

}

ConnectionThread::~ConnectionThread() {}

void ConnectionThread::setFrameBuffer(FrameBuffer *buffer) {
  m_frameBuffer = buffer;
}

FrameBuffer* ConnectionThread::getFrameBuffer() {
  return m_frameBuffer;
}

void ConnectionThread::setPeerThread(ConnectionThread *thread) {
  m_peerThread = thread;
}

ConnectionThread* ConnectionThread::getPeerThread() {
  return m_peerThread;
}
