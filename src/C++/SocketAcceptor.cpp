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

#include "Exceptions.h"
#include "Session.h"
#include "Settings.h"
#include "SocketAcceptor.h"
#include "Utility.h"

#include <memory>

namespace FIX {
SocketAcceptor::SocketAcceptor(Application &application, MessageStoreFactory &factory, const SessionSettings &settings)
    EXCEPT(ConfigError)
    : Acceptor(application, factory, settings),
      m_pServer(nullptr) {}

SocketAcceptor::SocketAcceptor(
    Application &application,
    MessageStoreFactory &factory,
    const SessionSettings &settings,
    LogFactory &logFactory) EXCEPT(ConfigError)
    : Acceptor(application, factory, settings, logFactory),
      m_pServer(nullptr) {}

SocketAcceptor::~SocketAcceptor() {
  SocketConnections::iterator iter;
  for (iter = m_connections.begin(); iter != m_connections.end(); ++iter) {
    delete iter->second;
  }
}

void SocketAcceptor::onConfigure(const SessionSettings &sessionSettings) EXCEPT(ConfigError) {
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

void SocketAcceptor::onInitialize(const SessionSettings &sessionSettings) EXCEPT(RuntimeError) {
  uint16_t port = 0;

  try {
    std::unique_ptr<SocketServer> server(new SocketServer(1));
    PortToSessions portToSessions;
    SessionToPort sessionToPort;

    for (const SessionID &sessionID : sessionSettings.getSessions()) {
      const Dictionary &settings = sessionSettings.get(sessionID);
      port = (short)settings.getInt(SOCKET_ACCEPT_PORT);
      const std::string address = settings.has(SOCKET_ACCEPT_ADDRESS) ? settings.getString(SOCKET_ACCEPT_ADDRESS) : "";

      const bool reuseAddress = settings.has(SOCKET_REUSE_ADDRESS) ? settings.getBool(SOCKET_REUSE_ADDRESS) : true;

      const bool noDelay = settings.has(SOCKET_NODELAY) ? settings.getBool(SOCKET_NODELAY) : false;

      const int sendBufSize = settings.has(SOCKET_SEND_BUFFER_SIZE) ? settings.getInt(SOCKET_SEND_BUFFER_SIZE) : 0;

      const int rcvBufSize = settings.has(SOCKET_RECEIVE_BUFFER_SIZE) ? settings.getInt(SOCKET_RECEIVE_BUFFER_SIZE) : 0;

      socket_handle acceptSocket = server->add(address, port, reuseAddress, noDelay, sendBufSize, rcvBufSize);
      portToSessions[socket_hostport(acceptSocket)].insert(sessionID);
      sessionToPort[sessionID] = socket_hostport(acceptSocket);
    }

    m_portToSessions.swap(portToSessions);
    m_sessionToPort.swap(sessionToPort);
    m_pServer = std::move(server);
  } catch (SocketException &e) {
    throw RuntimeError(
        "Unable to create, bind, or listen to port " + IntConvertor::convert((unsigned short)port) + " (" + e.what()
        + ")");
  }
}

void SocketAcceptor::onStart() {
  while (!isStopped() && m_pServer && m_pServer->block(*this)) {}

  if (!m_pServer) {
    return;
  }

  time_t start = 0;
  time_t now = 0;

  ::time(&start);
  while (isLoggedOn()) {
    m_pServer->block(*this);
    if (::time(&now) - 5 >= start) {
      break;
    }
  }

  onStop();
}

bool SocketAcceptor::onPoll() {
  if (!m_pServer) {
    return false;
  }

  time_t start = 0;
  time_t now = 0;

  if (isStopped()) {
    if (start == 0) {
      ::time(&start);
    }
    if (!isLoggedOn()) {
      start = 0;
      return false;
    }
    if (::time(&now) - 5 >= start) {
      start = 0;
      return false;
    }
  }

  m_pServer->block(*this, true);
  if (isStopped()) {
    onStop();
    return false;
  }
  return true;
}

void SocketAcceptor::onStop() {
  joinStartThread();
  if (m_pServer && m_pServer->isDispatching()) {
    return;
  }
  while (!m_connections.empty()) {
    SocketAcceptor::onDisconnect(*m_pServer, m_connections.begin()->first);
  }
  m_pServer.reset();
}

void SocketAcceptor::onConnect(SocketServer &server, socket_handle a, socket_handle s) {
  if (!socket_isValid(s)) {
    return;
  }
  SocketConnections::iterator i = m_connections.find(s);
  if (i != m_connections.end()) {
    return;
  }
  uint16_t port = server.socketToPort(a);
  Sessions sessions = m_portToSessions[port];
  m_connections[s] = new SocketConnection(s, sessions, &server.getMonitor());

  std::stringstream stream;
  stream << "Accepted connection from " << socket_peername(s) << " on port " << port;

  if (getLog()) {
    getLog()->onEvent(stream.str());
  }
}

void SocketAcceptor::onWrite(SocketServer &server, socket_handle s) {
  SocketConnections::iterator i = m_connections.find(s);
  if (i == m_connections.end()) {
    return;
  }
  SocketConnection *pSocketConnection = i->second;
  if (pSocketConnection->processQueue()) {
    pSocketConnection->unsignal();
  }
}

bool SocketAcceptor::onData(SocketServer &server, socket_handle s) {
  SocketConnections::iterator i = m_connections.find(s);
  if (i == m_connections.end()) {
    return false;
  }
  SocketConnection *pSocketConnection = i->second;
  return pSocketConnection->read(*this, server);
}

void SocketAcceptor::onDisconnect(SocketServer &, socket_handle s) {
  SocketConnections::iterator i = m_connections.find(s);
  if (i == m_connections.end()) {
    return;
  }
  SocketConnection *pSocketConnection = i->second;

  Session *pSession = pSocketConnection->getSession();
  if (pSession) {
    pSession->disconnectIfConnected();
  }

  delete pSocketConnection;
  m_connections.erase(s);
}

void SocketAcceptor::onError(SocketServer &) {}

void SocketAcceptor::onTimeout(SocketServer &) {
  SocketConnections::iterator i;
  for (i = m_connections.begin(); i != m_connections.end(); ++i) {
    i->second->onTimeout();
  }
}
} // namespace FIX
