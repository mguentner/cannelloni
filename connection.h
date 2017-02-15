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

#include <linux/can/raw.h>
#include <stdint.h>

#include "thread.h"
#include "framebuffer.h"

namespace cannelloni {

struct debugOptions_t {
  uint8_t can    : 1;
  uint8_t udp    : 1;
  uint8_t buffer : 1;
  uint8_t timer  : 1;
};

class ConnectionThread : public Thread {
  public:
    ConnectionThread();
    virtual ~ConnectionThread();

    virtual void transmitFrame(canfd_frame *frame) = 0;
    void setFrameBuffer(FrameBuffer *buffer);
    FrameBuffer *getFrameBuffer();

    void setPeerThread(ConnectionThread *thread);
    ConnectionThread* getPeerThread();

  protected:
    FrameBuffer *m_frameBuffer;
    ConnectionThread *m_peerThread;
};

}
