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

#include <list>
#include <mutex>
#include "cannelloni.h"

namespace cannelloni {

/* Design Notes:
 *
 * This buffer contains canfd_frames received by CANThread or
 * UDPThread and stores them in a queue until an event occurs that leads to
 * flushing the buffer (e.g. timeout in UDPThread).
 *
 * When this happens, the buffers will be swapped to minimize the
 * time a Thread can be locked by a mutex.
 * All sorting should take place on the intermediate buffer to make
 * the whole operation as non-blocking as possible for the producer thread.
 *
 * Only the intermediate buffer is exposed and should only be accessed
 * by one party at a time.
 *
 * If the producer is a lot faster than the receiver, in our case
 * UDPThread >> CANThread, frames can also be extracted one at a time
 * if the interface blocks and writing is deferred.
 *
 * Currently there are a lot of mutex locks involved which need to be checked
 * again at a later time.
 *
 * The goal is to have FrameBuffer 100% thread-safe to support further
 * use-cases of cannelloni.
 */

class FrameBuffer {
  public:
    FrameBuffer(size_t size, size_t max);
    ~FrameBuffer();
    /* Locks m_poolMutex and takes a free frame from m_framePool,
     * will grow the buffer if no frame is available
     *
     * will return NULL if no memory is available and overwriteLast is false
     * will return the last frame in the buffer when overwriteLast is true
     *
     */
    canfd_frame* requestFrame(bool overwriteLast, bool debug = false);

    /* If a read fails we need to give the frame back */
    void insertFramePool(canfd_frame *frame);

    /* Inserts a frame into the frameBuffer (back) */
    void insertFrame(canfd_frame *frame);

    /* Inserts a frame into the frameBuffer (front) */
    void returnFrame(canfd_frame *frame);

    /* Instead of operating on the intermediateBuffer, we can
     * also request a frame from the buffer and put it back
     * using insertFramePool or returnFrame
     * This is useful when the consumer is a lot slower than
     * the producer (see Design Notes)
     */
    canfd_frame* requestBufferFront();

    canfd_frame* requestBufferBack();

    /* Swaps m_Buffer with m_intermediateBuffer */
    void swapBuffers();

    /* Sorts m_intermediateBuffer by canfd_frame->id */
    void sortIntermediateBuffer();

    /* merges m_intermediateBuffer back into m_poolMutex */
    void mergeIntermediateBuffer();

    /* merges parts of m_intermediateBuffer back into m_buffer */
    void returnIntermediateBuffer(std::list<canfd_frame*>::iterator start);

    /* This will return a pointer to the current intermediateBuffer.
     * Once the operation is done the caller MUST call
     * unlockIntermediateBuffer to unlock the mutex in order to
     * prevent a deadlock!
     */
    std::list<canfd_frame*>* getIntermediateBuffer();

    void unlockIntermediateBuffer();

    void debug();

    /* Moves all frames back into m_framePool and sets the size to 0 */
    void reset();

    void clearPool();

    size_t getFrameBufferSize();

  private:
    bool resizePool(std::size_t size, bool debug = false);

  private:
    std::list<canfd_frame*> m_framePool;
    std::list<canfd_frame*> m_buffer;
    std::list<canfd_frame*> m_intermediateBuffer;

    uint64_t m_totalAllocCount;
    /* When filling/swapping the buffers we currently need a mutex */
    std::recursive_mutex m_bufferMutex;
    std::recursive_mutex m_intermediateBufferMutex;
    std::recursive_mutex m_poolMutex;
    /* Track current frame buffer size */
    size_t m_bufferSize;
    size_t m_intermediateBufferSize;
    /*
     * This is the maximum of frames that will be
     * allocated. This guarantees that cannelloni stays
     * within a fixed memory bounds.
     *
     * a size of zero means that the buffer can grow
     * unlimited
     */
    size_t m_maxAllocCount;
};

}
