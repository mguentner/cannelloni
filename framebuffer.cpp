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

#include <cstring>
#include "framebuffer.h"
#include "logging.h"

using namespace cannelloni;

FrameBuffer::FrameBuffer(size_t size, size_t max) :
  m_totalAllocCount(0),
  m_bufferSize(0),
  m_intermediateBufferSize(0),
  m_maxAllocCount(max)
{
  resizePool(size, false);
}

FrameBuffer::~FrameBuffer() {
  /* delete all frames */
  clearPool();
}

canfd_frame* FrameBuffer::requestFrame(bool overwriteLast, bool debug) {
  std::lock_guard<std::recursive_mutex> lock(m_poolMutex);
  if (m_framePool.empty()) {
    bool resizePoolResult;
    if (m_maxAllocCount > 0) {
      if (m_maxAllocCount <= m_totalAllocCount) {
        if (debug)
          lerror << "Maximum of allocated frames reached." << std::endl;
        resizePoolResult = false;
      } else {
        resizePoolResult = resizePool(std::min(m_maxAllocCount-m_totalAllocCount,m_totalAllocCount), debug);
      }
    } else {
      /* If m_maxAllocCount is 0, we just grow the pool */
      resizePoolResult = resizePool(m_totalAllocCount, debug);
    }
    if (!resizePoolResult && !overwriteLast) {
      if (debug)
        lerror << "Allocation failed. Not enough memory available." << std::endl;
      /* Test whether a partial alloc was possible */
      if (m_framePool.empty()) {
        /* We have no frames available and return NULL */
        if (debug)
          lerror << "Frame Pool is depleted!!!." << std::endl;
        return NULL;
      }
    } else if(!resizePoolResult && overwriteLast) {
      std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);
      /*
       * We did reach the limit but we are returning the last frame in the
       * buffer. (ringbuffer behaviour)
       */
      return requestBufferBack();
    }
  }
  /* If we reach this point, m_framePool is not depleted */
  canfd_frame *ret = m_framePool.front();
  /*
   * In a benchmark, splicing between three lists showed no
   * performance improvement over front() and pop_front(),
   * it even was 33% slower
   */
  m_framePool.pop_front();
  return ret;
}

void FrameBuffer::insertFramePool(canfd_frame *frame) {
  std::lock_guard<std::recursive_mutex> lock(m_poolMutex);

  m_framePool.push_back(frame);
}

void FrameBuffer::insertFrame(canfd_frame *frame) {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);

  m_buffer.push_back(frame);
  m_bufferSize += CANNELLONI_FRAME_BASE_SIZE + canfd_len(frame);

  /* We need one more byte for CAN_FD Frames */
  if (frame->len & CANFD_FRAME)
    m_bufferSize++;
}

void FrameBuffer::returnFrame(canfd_frame *frame) {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);

  m_buffer.push_front(frame);
  m_bufferSize += CANNELLONI_FRAME_BASE_SIZE + canfd_len(frame);
  /* We need one more byte for CAN_FD Frames */
  if (frame->len & CANFD_FRAME)
    m_bufferSize++;
}

canfd_frame* FrameBuffer::requestBufferFront() {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);
  if (m_buffer.empty()) {
    return NULL;
  }
  else {
    canfd_frame *ret = m_buffer.front();
    m_buffer.pop_front();
    m_bufferSize -= (CANNELLONI_FRAME_BASE_SIZE + canfd_len(ret));
    /* We need one more byte for CAN_FD Frames */
    if (ret->len & CANFD_FRAME)
      m_bufferSize--;
    return ret;
  }
}

canfd_frame* FrameBuffer::requestBufferBack() {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);
  if (m_buffer.empty()) {
    return NULL;
  }
  else {
    canfd_frame *ret = m_buffer.back();
    m_buffer.pop_back();
    m_bufferSize -= (CANNELLONI_FRAME_BASE_SIZE + canfd_len(ret));
    /* We need one more byte for CAN_FD Frames */
    if (ret->len & CANFD_FRAME)
      m_bufferSize--;
    return ret;
  }
}



void FrameBuffer::swapBuffers() {
  std::unique_lock<std::recursive_mutex> lock1(m_bufferMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock2(m_intermediateBufferMutex, std::defer_lock);
  std::lock(lock1, lock2);

  std::swap(m_bufferSize, m_intermediateBufferSize);
  m_buffer.swap(m_intermediateBuffer);
}

void FrameBuffer::sortIntermediateBuffer() {
  std::lock_guard<std::recursive_mutex> lock(m_intermediateBufferMutex);

  m_intermediateBuffer.sort(canfd_frame_comp());
}

void FrameBuffer::mergeIntermediateBuffer() {
  std::unique_lock<std::recursive_mutex> lock1(m_poolMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock2(m_intermediateBufferMutex, std::defer_lock);
  std::lock(lock1, lock2);

  m_framePool.splice(m_framePool.end(), m_intermediateBuffer);
  m_intermediateBufferSize = 0;
}

void FrameBuffer::returnIntermediateBuffer(std::list<canfd_frame*>::iterator start) {
  std::unique_lock<std::recursive_mutex> lock1(m_intermediateBufferMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock2(m_bufferMutex, std::defer_lock);
  std::lock(lock1,lock2);

  /* Don't splice since we need to keep track of the size */
  for (std::list<canfd_frame*>::iterator it = start;
                                         it != m_intermediateBuffer.end();) {
    canfd_frame *frame = *it;
    it = m_intermediateBuffer.erase(it);
    returnFrame(frame);
  }
}

std::list<canfd_frame*>* FrameBuffer::getIntermediateBuffer() {
  /* We need to lock m_intermediateBuffer here */
  m_intermediateBufferMutex.lock();
  return &m_intermediateBuffer;
}

void FrameBuffer::unlockIntermediateBuffer() {
  m_intermediateBufferMutex.unlock();
}

void FrameBuffer::debug() {
  linfo << "FramePool: " << m_framePool.size() << std::endl;
  linfo << "Buffer: " << m_buffer.size() << " (elements) "
        << m_bufferSize << " (bytes)" <<  std::endl;
  linfo << "intermediateBuffer: " << m_intermediateBuffer.size() << std::endl;
}

void FrameBuffer::reset() {
  std::unique_lock<std::recursive_mutex> lock1(m_poolMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock2(m_bufferMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock3(m_intermediateBufferMutex, std::defer_lock);
  std::lock(lock1, lock2, lock3);

  /* Splice everything back into the pool */
  m_framePool.splice(m_framePool.end(), m_intermediateBuffer);
  m_framePool.splice(m_framePool.end(), m_buffer);

  m_intermediateBufferSize = 0;
  m_bufferSize = 0;
}

void FrameBuffer::clearPool() {
  std::unique_lock<std::recursive_mutex> lock1(m_poolMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock2(m_bufferMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock3(m_intermediateBufferMutex, std::defer_lock);
  std::lock(lock1, lock2, lock3);

  for (canfd_frame *f : m_framePool) {
    delete f;
  }
  for (canfd_frame *f : m_intermediateBuffer) {
    delete f;
  }
  for (canfd_frame *f : m_buffer) {
    delete f;
  }
  m_framePool.clear();
  m_totalAllocCount = 0;
}

size_t FrameBuffer::getFrameBufferSize() {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);
  return m_bufferSize;
}

bool FrameBuffer::resizePool(std::size_t size, bool debug) {
  std::lock_guard<std::recursive_mutex> lock(m_poolMutex);
  for (size_t i=0; i<size; i++) {
      auto f = new canfd_frame;
      memset(f, 0, sizeof(*f));
      m_framePool.push_back(f);
  }
  m_totalAllocCount += size;
  if (debug)
    linfo << "New Poolsize:" << m_totalAllocCount << std::endl;
  return true;
}
