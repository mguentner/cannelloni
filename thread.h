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

#include <memory>
#include <thread>

namespace cannelloni {

class Thread {
  public:
    Thread();
    virtual ~Thread();
    virtual int start();
    /* this is the function to tell the thread to stop */
    virtual void stop();
    /* joins the thread */
    void join();
    /* */
    bool isRunning();
    /* */
    virtual void run() = 0;
  private:
    /* thread control loop */
    void privRun();

  protected:
    /* determines when to break from run() */
    bool m_started;
  private:
    bool m_running;
    std::unique_ptr<std::thread> m_privThread;
};

}
