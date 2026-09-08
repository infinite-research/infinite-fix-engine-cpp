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
#include <poll.h>
#endif

#include "Session.h"
#include "ThreadedSocketAcceptor.h"
#include "ThreadedSocketConnection.h"
#include "ThreadedSocketInitiator.h"
#include "Utility.h"

namespace FIX {
ThreadedSocketConnection::ThreadedSocketConnection(socket_handle s, Sessions sessions, Log *pLog)
    : m_socket(s),
      m_pLog(pLog),
      m_sessions(sessions),
      m_pSession(0),
      m_disconnect(false) {
#if _MSC_VER
  FD_ZERO(&m_fds);
  FD_SET(m_socket, &m_fds);
#endif
}

ThreadedSocketConnection::ThreadedSocketConnection(
    const SessionID &sessionID,
    socket_handle s,
    const std::string &address,
    short port,
    Log *pLog,
    const std::string &sourceAddress,
    short sourcePort)
    : m_socket(s),
      m_address(address),
      m_port(port),
      m_sourceAddress(sourceAddress),
      m_sourcePort(sourcePort),
      m_pLog(pLog),
      m_pSession(Session::lookupSession(sessionID)),
      m_disconnect(false) {
#if _MSC_VER
  FD_ZERO(&m_fds);
  FD_SET(m_socket, &m_fds);
#endif
  if (m_pSession) {
    m_pSession->setResponder(this);
  }
}

ThreadedSocketConnection::~ThreadedSocketConnection() {
  if (m_pSession) {
    m_pSession->setResponder(0);
    Session::unregisterSession(m_pSession->getSessionID());
  }
}

bool ThreadedSocketConnection::send(const std::string &msg) {
  ssize_t totalSent = 0;
  while (totalSent < (int)msg.length()) {
    ssize_t sent = socket_send(m_socket, msg.c_str() + totalSent, msg.length());
    if (sent < 0) {
      return false;
    }
    totalSent += sent;
  }

  return true;
}

bool ThreadedSocketConnection::connect() {
  // do the bind in the thread as name resolution may block
  if (!m_sourceAddress.empty() || m_sourcePort) {
    socket_bind(m_socket, m_sourceAddress.c_str(), m_sourcePort);
  }

  return socket_connect(getSocket(), m_address.c_str(), m_port) >= 0;
}

void ThreadedSocketConnection::disconnect() {
  m_disconnect = true;
  socket_close(m_socket);
}

bool ThreadedSocketConnection::read() {
#if _MSC_VER
  struct timeval timeout = {1, 0};
  fd_set readset = m_fds;
#else
  int timeout = 1000; // 1000ms = 1 second
  struct pollfd pfd = {m_socket, POLLIN | POLLPRI, 0};
#endif

  try {
    // Wait for input (1 second timeout)
#if _MSC_VER
    int result = select(0, &readset, 0, 0, &timeout);
#else
    int result = poll(&pfd, 1, timeout);
#endif

    if (result > 0) // Something to read
    {
      // We can read without blocking
      ssize_t size = socket_recv(m_socket, m_buffer, sizeof(m_buffer));
      if (size <= 0) {
        throw SocketRecvFailed(size);
      }
      m_parser.addToStream(m_buffer, size);
    } else if (result == 0 && m_pSession) // Timeout
    {
      m_pSession->next(UtcTimeStamp::now());
    } else if (result < 0) // Error
    {
      throw SocketRecvFailed(result);
    }

    processStream();
    return !m_disconnect;
  } catch (MessageParseError &e) {
    Log *log = m_pSession ? m_pSession->getLog() : m_pLog;
    if (log) {
      log->onEvent(e.what());
    }
    if (m_pSession) {
      m_pSession->disconnect();
    } else {
      disconnect();
    }
    return false;
  } catch (SocketRecvFailed &e) {
    if (m_disconnect) {
      return false;
    }

    if (m_pSession) {
      m_pSession->getLog()->onEvent(e.what());
      m_pSession->disconnect();
    } else {
      disconnect();
    }

    return false;
  }
}

bool ThreadedSocketConnection::readMessage(std::string &msg) EXCEPT(SocketRecvFailed) {
  return m_parser.readFixMessage(msg);
}

void ThreadedSocketConnection::processStream() {
  std::string msg;
  while (readMessage(msg)) {
    if (!m_pSession) {
      if (!setSession(msg)) {
        disconnect();
        return;
      }
      if (!m_pSession->receivedLogon()) {
        return;
      }
      continue;
    }
    try {
      m_pSession->next(msg, UtcTimeStamp::now());
      if (m_disconnect) {
        return;
      }
    } catch (InvalidMessage &) {
      if (!m_pSession->isLoggedOn()) {
        disconnect();
        return;
      }
    }
  }
}

bool ThreadedSocketConnection::setSession(const std::string &msg) {
  Session *candidate = Session::lookupSession(msg, true);
  if (!candidate || identifyType(msg) != MsgType_Logon || m_sessions.count(candidate->getSessionID()) == 0
      || (!candidate->getAllowedRemoteAddresses().empty()
          && !candidate->inAllowedRemoteAddresses(socket_peername(m_socket)))) {
    if (m_pLog) {
      m_pLog->onEvent("Incoming connection was not admitted");
    }
    return false;
  }

  const SessionID &sessionID = candidate->getSessionID();

  // see if the session frees up within 5 seconds
  for (int i = 1; i <= 5; i++) {
    if (!Session::isSessionRegistered(sessionID)) {
      if (!candidate->acceptLogon(msg, *this)) {
        return false;
      }
      m_pSession = candidate;
      return true;
    }
    process_sleep(1);
  }

  return false;
}

} // namespace FIX
