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

#ifdef _MSC_VER
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "Settings.h"
#include "ThreadedSocketAcceptor.h"
#include "Utility.h"
#include "scope_guard.hpp"

namespace FIX {
ThreadedSocketAcceptor::ThreadedSocketAcceptor(
    Application &application,
    MessageStoreFactory &factory,
    const SessionSettings &settings) EXCEPT(ConfigError)
    : Acceptor(application, factory, settings) {
  socket_init();
}

ThreadedSocketAcceptor::ThreadedSocketAcceptor(
    Application &application,
    MessageStoreFactory &factory,
    const SessionSettings &settings,
    LogFactory &logFactory) EXCEPT(ConfigError)
    : Acceptor(application, factory, settings, logFactory) {
  socket_init();
}

ThreadedSocketAcceptor::~ThreadedSocketAcceptor() {
  if (hasDeferredStopCleanup()) {
    stop(true);
  }
  socket_term();
}

void ThreadedSocketAcceptor::onConfigure(const SessionSettings &sessionSettings) EXCEPT(ConfigError) {
  std::map<int, unsigned long> addresses;
  for (const SessionID &sessionID : sessionSettings.getSessions()) {
    const Dictionary &settings = sessionSettings.get(sessionID);
    const int port = settings.getInt(SOCKET_ACCEPT_PORT);
    const std::string address = settings.has(SOCKET_ACCEPT_ADDRESS) ? settings.getString(SOCKET_ACCEPT_ADDRESS) : "";
    const unsigned long host = address.empty() ? INADDR_ANY : inet_addr(address.c_str());
    if (host == INADDR_NONE) {
      throw ConfigError(std::string(SOCKET_ACCEPT_ADDRESS) + " must be empty or a numeric IPv4 address");
    }
    const auto result = addresses.emplace(port, host);
    if (!result.second && result.first->second != host) {
      throw ConfigError(
          std::string("Sessions sharing ") + SOCKET_ACCEPT_PORT + " must use the same " + SOCKET_ACCEPT_ADDRESS);
    }
    if (settings.has(SOCKET_REUSE_ADDRESS)) {
      settings.getBool(SOCKET_REUSE_ADDRESS);
    }
    if (settings.has(SOCKET_NODELAY)) {
      settings.getBool(SOCKET_NODELAY);
    }
  }
}

void ThreadedSocketAcceptor::onInitialize(const SessionSettings &sessionSettings) EXCEPT(RuntimeError) {
  short port = 0;
  std::set<int> ports;
  Sockets sockets;
  PortToSessions portToSessions;
  SocketToPort socketToPort;
  auto cleanup = sg::make_scope_guard([&]() {
    for (const socket_handle socket : sockets) {
      socket_close(socket);
    }
  });

  for (const SessionID &sessionID : sessionSettings.getSessions()) {
    const Dictionary &settings = sessionSettings.get(sessionID);
    port = (short)settings.getInt(SOCKET_ACCEPT_PORT);
    const std::string address = settings.has(SOCKET_ACCEPT_ADDRESS) ? settings.getString(SOCKET_ACCEPT_ADDRESS) : "";

    portToSessions[port].insert(sessionID);

    if (ports.find(port) != ports.end()) {
      continue;
    }
    ports.insert(port);

    const bool reuseAddress = settings.has(SOCKET_REUSE_ADDRESS) ? settings.getBool(SOCKET_REUSE_ADDRESS) : true;

    const bool noDelay = settings.has(SOCKET_NODELAY) ? settings.getBool(SOCKET_NODELAY) : false;

    const int sendBufSize = settings.has(SOCKET_SEND_BUFFER_SIZE) ? settings.getInt(SOCKET_SEND_BUFFER_SIZE) : 0;

    const int rcvBufSize = settings.has(SOCKET_RECEIVE_BUFFER_SIZE) ? settings.getInt(SOCKET_RECEIVE_BUFFER_SIZE) : 0;

    socket_handle socket = socket_createAcceptor(address, port, reuseAddress);
    if (socket == INVALID_SOCKET_HANDLE) {
      SocketException e;
      throw RuntimeError(
          "Unable to create, bind, or listen to port " + IntConvertor::convert((unsigned short)port) + " (" + e.what()
          + ")");
    }
    auto socketCleanup = sg::make_scope_guard([&]() { socket_close(socket); });
    if (noDelay) {
      socket_setsockopt(socket, TCP_NODELAY);
    }
    if (sendBufSize) {
      socket_setsockopt(socket, SO_SNDBUF, sendBufSize);
    }
    if (rcvBufSize) {
      socket_setsockopt(socket, SO_RCVBUF, rcvBufSize);
    }

    socketToPort[socket] = port;
    sockets.insert(socket);
    socketCleanup.dismiss();
  }

  m_portToSessions.swap(portToSessions);
  m_socketToPort.swap(socketToPort);
  m_sockets.swap(sockets);
  cleanup.dismiss();
}

void ThreadedSocketAcceptor::onStart() {
  Locker l(m_mutex);
  if (isStopped()) {
    return;
  }
  for (const Sockets::value_type &socket : m_sockets) {
    int port = m_socketToPort[socket];
    auto info = std::make_unique<AcceptorThreadInfo>(this, socket, port);
    auto worker = m_threads.emplace(socket, thread_id{}).first;
    auto cleanup = sg::make_scope_guard([&]() { m_threads.erase(worker); });
    if (!thread_spawn(&socketAcceptorThread, info.get(), worker->second)) {
      throw RuntimeError("Unable to spawn acceptor listener thread");
    }
    info.release();
    cleanup.dismiss();
  }
}

bool ThreadedSocketAcceptor::onPoll() { return false; }

void ThreadedSocketAcceptor::onStop() {
  joinStartThread();
  Sockets sockets;
  SocketToThread threads;
  SocketToThread::iterator i;
  bool calledFromWorker = false;

  {
    Locker l(m_mutex);

    time_t start = 0;
    time_t now = 0;

    ::time(&start);
    while (isLoggedOn()) {
      if (::time(&now) - 5 >= start) {
        break;
      }
    }

    for (const auto &socketWithThread : m_threads) {
      if (thread_is_current(socketWithThread.second)) {
        calledFromWorker = true;
        break;
      }
    }
    if (calledFromWorker) {
      if (!m_sockets.empty()) {
        sockets.swap(m_sockets);
        threads = m_threads;
      }
      deferStopCleanup();
    } else {
      sockets.swap(m_sockets);
      threads.swap(m_threads);
    }
  }

  for (const socket_handle socket : sockets) {
    socket_close(socket);
  }
  if (!sockets.empty()) {
    for (i = threads.begin(); i != threads.end(); ++i) {
      if (sockets.find(i->first) == sockets.end()) {
        socket_close(i->first);
      }
    }
  }
  if (calledFromWorker) {
    return;
  }
  for (i = threads.begin(); i != threads.end(); ++i) {
    thread_join(i->second);
  }
}

void ThreadedSocketAcceptor::addThread(socket_handle s, thread_id t) {
  Locker l(m_mutex);

  m_threads[s] = t;
}

void ThreadedSocketAcceptor::removeThread(socket_handle s) {
  Locker l(m_mutex);
  SocketToThread::iterator i = m_threads.find(s);
  if (i != m_threads.end()) {
    thread_detach(i->second);
    m_threads.erase(i);
  }
}

THREAD_PROC ThreadedSocketAcceptor::socketAcceptorThread(void *p) {
  AcceptorThreadInfo *info = reinterpret_cast<AcceptorThreadInfo *>(p);

  ThreadedSocketAcceptor *pAcceptor = info->m_pAcceptor;
  socket_handle s = info->m_socket;
  int port = info->m_port;
  delete info;

  int noDelay = 0;
  int sendBufSize = 0;
  int rcvBufSize = 0;
  socket_getsockopt(s, TCP_NODELAY, noDelay);
  socket_getsockopt(s, SO_SNDBUF, sendBufSize);
  socket_getsockopt(s, SO_RCVBUF, rcvBufSize);

  socket_handle socket = 0;
  while ((!pAcceptor->isStopped() && (socket = socket_accept(s)) != INVALID_SOCKET_HANDLE)) {
    if (noDelay) {
      socket_setsockopt(socket, TCP_NODELAY);
    }
    if (sendBufSize) {
      socket_setsockopt(socket, SO_SNDBUF, sendBufSize);
    }
    if (rcvBufSize) {
      socket_setsockopt(socket, SO_RCVBUF, rcvBufSize);
    }

    Sessions sessions = pAcceptor->m_portToSessions[port];

    ThreadedSocketConnection *pConnection = new ThreadedSocketConnection(socket, sessions, pAcceptor->getLog());

    ConnectionThreadInfo *info = new ConnectionThreadInfo(pAcceptor, pConnection);

    {
      Locker l(pAcceptor->m_mutex);

      if (pAcceptor->isStopped()) {
        delete info;
        delete pConnection;
        socket_close(socket);
        break;
      }

      std::stringstream stream;
      stream << "Accepted connection from " << socket_peername(socket) << " on port " << port;

      if (pAcceptor->getLog()) {
        pAcceptor->getLog()->onEvent(stream.str());
      }

      thread_id thread;
      if (!thread_spawn(&socketConnectionThread, info, thread)) {
        delete info;
        delete pConnection;
        socket_close(socket);
      } else {
        pAcceptor->addThread(socket, thread);
      }
    }
  }

  if (!pAcceptor->isStopped()) {
    pAcceptor->removeThread(s);
  }

  return 0;
}

THREAD_PROC ThreadedSocketAcceptor::socketConnectionThread(void *p) {
  ConnectionThreadInfo *info = reinterpret_cast<ConnectionThreadInfo *>(p);

  ThreadedSocketAcceptor *pAcceptor = info->m_pAcceptor;
  ThreadedSocketConnection *pConnection = info->m_pConnection;
  delete info;

  socket_handle socket = pConnection->getSocket();

  while (pConnection->read()) {}
  delete pConnection;
  if (!pAcceptor->isStopped()) {
    pAcceptor->removeThread(socket);
  }
  return 0;
}
} // namespace FIX
