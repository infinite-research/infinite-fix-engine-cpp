/* -*- C++ -*- */

/****************************************************************************
** Copyright (c) 2001-2014
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

#ifndef FIX_MUTEX_H
#define FIX_MUTEX_H

#include "Utility.h"

#ifndef _MSC_VER
#include <system_error>
#endif

namespace FIX {
/// Portable implementation of a mutex.
class Mutex {
public:
  Mutex() {
#ifdef _MSC_VER
    InitializeCriticalSection(&m_mutex);
#else
    pthread_mutexattr_t attributes;
    int result = pthread_mutexattr_init(&attributes);
    if (result != 0) {
      throw std::system_error(result, std::generic_category(), "pthread_mutexattr_init");
    }
    result = pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    if (result == 0) {
      result = pthread_mutex_init(&m_mutex, &attributes);
    }
    pthread_mutexattr_destroy(&attributes);
    if (result != 0) {
      throw std::system_error(result, std::generic_category(), "pthread recursive mutex initialization");
    }
#endif
  }

  ~Mutex() {
#ifdef _MSC_VER
    DeleteCriticalSection(&m_mutex);
#else
    pthread_mutex_destroy(&m_mutex);
#endif
  }

  void lock() {
#ifdef _MSC_VER
    EnterCriticalSection(&m_mutex);
#else
    pthread_mutex_lock(&m_mutex);
#endif
  }

  void unlock() {
#ifdef _MSC_VER
    LeaveCriticalSection(&m_mutex);
#else
    pthread_mutex_unlock(&m_mutex);
#endif
  }

private:
#ifdef _MSC_VER
  CRITICAL_SECTION m_mutex;
#else
  pthread_mutex_t m_mutex;
  // Preserve the prior private storage so layouts of public containing classes remain stable.
  pthread_t m_abiReservedThreadID{};
  int m_abiReservedCount{};
#endif
};

/// Locks/Unlocks a mutex using RAII.
class Locker {
public:
  Locker(Mutex &mutex)
      : m_mutex(mutex) {
    m_mutex.lock();
  }

  ~Locker() { m_mutex.unlock(); }

private:
  Mutex &m_mutex;
};

/// Does the opposite of the Locker to ensure mutex ends up in a locked state.
class ReverseLocker {
public:
  ReverseLocker(Mutex &mutex)
      : m_mutex(mutex) {
    m_mutex.unlock();
  }

  ~ReverseLocker() { m_mutex.lock(); }

private:
  Mutex &m_mutex;
};
} // namespace FIX

#endif
