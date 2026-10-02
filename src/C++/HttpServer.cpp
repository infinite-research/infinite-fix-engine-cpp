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

#include "HttpServer.h"

namespace FIX {
Mutex HttpServer::s_mutex;
int HttpServer::s_count = 0;
HttpServer *HttpServer::s_pServer = 0;

void HttpServer::startGlobal(const SessionSettings &s) EXCEPT(ConfigError, RuntimeError) {
  if (s.get().has(HTTP_ACCEPT_PORT)) {
    throw ConfigError("HttpAcceptPort is no longer supported; use an authenticated external control plane");
  }
}

void HttpServer::stopGlobal() {}

HttpServer::HttpServer(const SessionSettings &settings) EXCEPT(ConfigError)
    : m_pServer(0),
      m_settings(settings),
      m_threadid(0),
      m_port(0),
      m_stop(false) {}

void HttpServer::onConfigure(const SessionSettings &) EXCEPT(ConfigError) {}

void HttpServer::onInitialize(const SessionSettings &) EXCEPT(RuntimeError) {}

void HttpServer::start() EXCEPT(ConfigError, RuntimeError) {
  throw ConfigError("HttpAcceptPort is no longer supported; use an authenticated external control plane");
}

void HttpServer::stop() {}

void HttpServer::onStart() {}

bool HttpServer::onPoll() { return false; }

void HttpServer::onStop() {}

void HttpServer::onConnect(SocketServer &, socket_handle, socket_handle) {}

void HttpServer::onWrite(SocketServer &, socket_handle) {}

bool HttpServer::onData(SocketServer &, socket_handle) { return false; }

void HttpServer::onDisconnect(SocketServer &, socket_handle) {}

void HttpServer::onError(SocketServer &) {}

void HttpServer::onTimeout(SocketServer &) {}

THREAD_PROC HttpServer::startThread(void *) { return 0; }

} // namespace FIX
