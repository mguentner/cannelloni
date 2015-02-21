/*
 * This file is part of cannelloni, a SocketCAN over Ethernet tunnel.
 *
 * Copyright (C) 2014-2015 Maximilian Güntner <maximilian.guentner@gmail.com>
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


#include "framebuffer.h"
#include "can.h"
#include "logging.h"

using namespace cannelloni;

FrameBuffer::FrameBuffer(size_t size, size_t max) :
  m_buffer(new std::list<can_frame*>),
  m_intermediateBuffer(new std::list<can_frame*>),
  m_bufferSize(0),
  m_intermediateBufferSize(0),
  m_maxAllocCount(max)
{
  resizePool(size);
}

FrameBuffer::~FrameBuffer() {
  /* delete all frames */
  clearPool();
  delete m_buffer;
  delete m_intermediateBuffer;
}

can_frame* FrameBuffer::requestFrame() {
  std::lock_guard<std::recursive_mutex> lock(m_poolMutex);
  if (m_framePool.empty()) {
    bool resizePoolResult;
    if (m_maxAllocCount > 0) {
      if (m_maxAllocCount <= m_totalAllocCount) {
        lerror << "Maximum of allocated frames reached." << std::endl;
        return NULL;
      }
      resizePoolResult = resizePool(std::min(m_maxAllocCount-m_totalAllocCount,m_totalAllocCount));
    } else {
      /* If m_maxAllocCount is 0, we just grow the pool */
      resizePoolResult = resizePool(m_totalAllocCount);
    }
    if (!resizePoolResult) {
      lerror << "Allocation failed. Not enough memory available." << std::endl;
      /* Test whether a partial alloc was possible */
      if (m_framePool.empty()) {
        /* We have no frames available and return NULL */
        lerror << "Frame Pool is depleted!!!." << std::endl;
        return NULL;
      }
    }
  }
  /* If we reach this point, m_framePool is not depleted */
  can_frame *ret = m_framePool.front();
  /*
   * In a benchmark, splicing between three lists showed no
   * performance improvement over front() and pop_front(),
   * it even was 33% slower
   */
  m_framePool.pop_front();
  return ret;
}

void FrameBuffer::insertFramePool(can_frame *frame) {
  std::lock_guard<std::recursive_mutex> lock(m_poolMutex);

  m_framePool.push_back(frame);
}

void FrameBuffer::insertFrame(can_frame *frame) {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);

  m_buffer->push_back(frame);
  m_bufferSize += CANNELLONI_FRAME_BASE_SIZE + frame->can_dlc;
}

void FrameBuffer::returnFrame(can_frame *frame) {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);

  m_buffer->push_front(frame);
  m_bufferSize += CANNELLONI_FRAME_BASE_SIZE + frame->can_dlc;
}

can_frame* FrameBuffer::requestBufferFront() {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);
  if (m_buffer->empty()) {
    return NULL;
  }
  else {
    can_frame *ret = m_buffer->front();
    m_buffer->pop_front();
    m_bufferSize -= (CANNELLONI_FRAME_BASE_SIZE + ret->can_dlc);
    return ret;
  }
}

void FrameBuffer::swapBuffers() {
  std::unique_lock<std::recursive_mutex> lock1(m_bufferMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock2(m_intermediateBufferMutex, std::defer_lock);
  std::lock(lock1, lock2);

  std::swap(m_bufferSize, m_intermediateBufferSize);
  std::swap(m_buffer,     m_intermediateBuffer);
}

void FrameBuffer::sortIntermediateBuffer() {
  std::lock_guard<std::recursive_mutex> lock(m_intermediateBufferMutex);

  m_intermediateBuffer->sort(can_frame_comp());
}

void FrameBuffer::mergeIntermediateBuffer() {
  std::unique_lock<std::recursive_mutex> lock1(m_poolMutex, std::defer_lock);
  std::unique_lock<std::recursive_mutex> lock2(m_intermediateBufferMutex, std::defer_lock);
  std::lock(lock1, lock2);

  m_framePool.merge(*m_intermediateBuffer);
  m_intermediateBufferSize = 0;
}


const std::list<can_frame*>* FrameBuffer::getIntermediateBuffer() {
  /* We need to lock m_intermediateBuffer here */
  m_intermediateBufferMutex.lock();
  return m_intermediateBuffer;
}

void FrameBuffer::unlockIntermediateBuffer() {
  m_intermediateBufferMutex.unlock();
}

void FrameBuffer::debug() {
  linfo << "FramePool: " << m_framePool.size() << std::endl;
  linfo << "Buffer: " << m_buffer->size() << " (elements) "
        << m_bufferSize << " (bytes)" <<  std::endl;
  linfo << "intermediateBuffer: " << m_intermediateBuffer->size() << std::endl;
}

void FrameBuffer::clearPool() {
  std::lock_guard<std::recursive_mutex> lock(m_poolMutex);
  for (can_frame *f : m_framePool) {
    delete f;
  }
  for (can_frame *f : *m_intermediateBuffer) {
    delete f;
  }
  for (can_frame *f : *m_buffer) {
    delete f;
  }
  m_framePool.clear();
  m_totalAllocCount = 0;
}

size_t FrameBuffer::getFrameBufferSize() {
  std::lock_guard<std::recursive_mutex> lock(m_bufferMutex);
  return m_bufferSize;
}

bool FrameBuffer::resizePool(std::size_t size) {
  std::lock_guard<std::recursive_mutex> lock(m_poolMutex);
  for (size_t i=0; i<size; i++) {
    can_frame *frame = new can_frame;
    if (frame) {
      m_framePool.push_back(frame);
    } else {
      m_totalAllocCount += i;
      m_poolMutex.unlock();
      return false;
    }
  }
  m_totalAllocCount += size;
  linfo << "New Poolsize:" << m_totalAllocCount << std::endl;
  return true;
}
