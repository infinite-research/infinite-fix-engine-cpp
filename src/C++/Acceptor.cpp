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

#include "Acceptor.h"
#include "HttpServer.h"
#include "Session.h"
#include "SessionFactory.h"
#include "Utility.h"
#include "scope_guard.hpp"

#include <algorithm>
#include <fstream>
#include <memory>

namespace FIX {
namespace {
thread_local Acceptor *activeAcceptor = nullptr;
}
Acceptor::Acceptor(Application &application, MessageStoreFactory &messageStoreFactory, const SessionSettings &settings)
    EXCEPT(ConfigError)
    : m_threadid(0),
      m_application(application),
      m_messageStoreFactory(messageStoreFactory),
      m_settings(settings),
      m_pLogFactory(0),
      m_pLog(0),
      m_processing(false),
      m_firstPoll(true),
      m_stop(true) {
  initialize();
}

Acceptor::Acceptor(
    Application &application,
    MessageStoreFactory &messageStoreFactory,
    const SessionSettings &settings,
    LogFactory &logFactory) EXCEPT(ConfigError)
    : m_threadid(0),
      m_application(application),
      m_messageStoreFactory(messageStoreFactory),
      m_settings(settings),
      m_pLogFactory(&logFactory),
      m_pLog(0),
      m_processing(false),
      m_firstPoll(true),
      m_stop(true) {
  m_pLog = logFactory.create();
  auto logGuard = sg::make_scope_guard([&]() { m_pLogFactory->destroy(m_pLog); });
  initialize();
  logGuard.dismiss();
}

void Acceptor::initialize() EXCEPT(ConfigError) {
  auto cleanup = sg::make_scope_guard([&]() {
    for (auto &session : m_sessions) {
      delete session.second;
    }
    m_sessions.clear();
    m_sessionIDs.clear();
  });
  std::set<SessionID> sessions = m_settings.getSessions();
  std::set<SessionID>::iterator i;

  if (!sessions.size()) {
    throw ConfigError("No sessions defined");
  }

  SessionFactory factory(m_application, m_messageStoreFactory, m_pLogFactory);

  for (i = sessions.begin(); i != sessions.end(); ++i) {
    if (m_settings.get(*i).getString(CONNECTION_TYPE) == "acceptor") {
      auto session = std::unique_ptr<Session>(factory.create(*i, m_settings.get(*i)));
      auto inserted = m_sessions.emplace(*i, session.get());
      if (inserted.second) {
        session.release();
      }
      m_sessionIDs.insert(*i);
    }
  }

  if (!m_sessions.size()) {
    throw ConfigError("No sessions defined for acceptor");
  }
  cleanup.dismiss();
}

Acceptor::~Acceptor() {
  Sessions::iterator i;
  for (i = m_sessions.begin(); i != m_sessions.end(); ++i) {
    delete i->second;
  }

  if (m_pLogFactory && m_pLog) {
    m_pLogFactory->destroy(m_pLog);
  }
}

Session *Acceptor::getSession(const std::string &msg, Responder &responder) {
  Message message;
  if (!message.setStringHeader(msg)) {
    return 0;
  }

  try {
    auto const &beginString = message.getHeader().getField<BeginString>();
    auto const &clSenderCompID = message.getHeader().getField<SenderCompID>();
    auto const &clTargetCompID = message.getHeader().getField<TargetCompID>();
    auto const &msgType = message.getHeader().getField<MsgType>();
    if (msgType != MsgType_Logon) {
      return 0;
    }

    SenderCompID senderCompID(clTargetCompID);
    TargetCompID targetCompID(clSenderCompID);
    SessionID sessionID(beginString, senderCompID, targetCompID);

    Sessions::iterator i = m_sessions.find(sessionID);
    if (i != m_sessions.end()) {
      i->second->setResponder(&responder);
      return i->second;
    }
  } catch (FieldNotFound &) {}
  return 0;
}

Session *Acceptor::getSession(const SessionID &sessionID) const {
  Sessions::const_iterator i = m_sessions.find(sessionID);
  if (i != m_sessions.end()) {
    return i->second;
  } else {
    return 0;
  }
}

const Dictionary *const Acceptor::getSessionSettings(const SessionID &sessionID) const {
  try {
    return &m_settings.get(sessionID);
  } catch (ConfigError &) {
    return 0;
  }
}

void Acceptor::start() EXCEPT(ConfigError, RuntimeError) {
  if (m_processing) {
    throw RuntimeError("Acceptor::start called when already processing messages");
  }

  joinStartThread();
  m_processing = true;
  m_stop = false;

  bool initialized = false;
  try {
    onConfigure(m_settings);
    onInitialize(m_settings);
    initialized = true;

    HttpServer::startGlobal(m_settings);

    if (!thread_spawn(&startThread, this, m_threadid)) {
      throw RuntimeError("Unable to spawn thread");
    }
  } catch (...) {
    m_stop = true;
    Acceptor *previous = activeAcceptor;
    activeAcceptor = this;
    auto guard = sg::make_scope_guard([previous]() { activeAcceptor = previous; });
    if (initialized) {
      onStop();
    }
    HttpServer::stopGlobal();
    m_processing = false;
    throw;
  }
}

void Acceptor::block() EXCEPT(ConfigError, RuntimeError) {
  if (m_processing) {
    throw RuntimeError("Acceptor::block called when already processing messages");
  }

  joinStartThread();
  Acceptor *previous = activeAcceptor;
  activeAcceptor = this;
  auto guard = sg::make_scope_guard([this, previous]() {
    m_processing = false;
    activeAcceptor = previous;
  });
  m_processing = true;
  m_stop = false;
  bool initialized = false;
  try {
    onConfigure(m_settings);
    onInitialize(m_settings);
    initialized = true;
    onStart();
  } catch (...) {
    m_stop = true;
    if (initialized) {
      onStop();
    }
    HttpServer::stopGlobal();
    throw;
  }
}

bool Acceptor::poll() EXCEPT(ConfigError, RuntimeError) {
  if (m_processing) {
    throw RuntimeError("Acceptor::poll called when already processing messages");
  }

  Acceptor *previous = activeAcceptor;
  activeAcceptor = this;
  auto guard = sg::make_scope_guard([this, previous]() {
    m_processing = false;
    activeAcceptor = previous;
  });
  m_processing = true;
  bool initialized = !m_firstPoll;
  try {
    if (m_firstPoll) {
      m_stop = false;
      onConfigure(m_settings);
      onInitialize(m_settings);
      m_firstPoll = false;
      initialized = true;
    }
    return onPoll();
  } catch (...) {
    m_stop = true;
    if (initialized) {
      onStop();
    }
    throw;
  }
}

void Acceptor::stop(bool force) {
  if (isStopped()) {
    joinStartThread();
    return;
  }

  HttpServer::stopGlobal();

  std::vector<Session *> enabledSessions;

  Sessions sessions = m_sessions;
  Sessions::iterator i = sessions.begin();
  for (; i != sessions.end(); ++i) {
    Session *pSession = Session::lookupSession(i->first);
    if (pSession && pSession->isEnabled()) {
      enabledSessions.push_back(pSession);
      pSession->logout();
      Session::unregisterSession(pSession->getSessionID());
    }
  }

  if (!force) {
    for (int second = 1; second <= 10 && isLoggedOn(); ++second) {
      process_sleep(1);
    }
  }

  m_stop = true;
  onStop();
  joinStartThread();

  for (Session *session : enabledSessions) {
    session->logon();
  }
}

bool Acceptor::isLoggedOn() const {
  Sessions sessions = m_sessions;
  for (Sessions::value_type const &sessionIDWithSession : sessions) {
    if (sessionIDWithSession.second->isLoggedOn()) {
      return true;
    }
  }
  return false;
}

void Acceptor::joinStartThread() {
  if (activeAcceptor == this) {
    return;
  }
  if (m_threadid) {
    thread_join(m_threadid);
    m_threadid = 0;
  } else {
    while (m_processing) {
      process_sleep(0.001);
    }
  }
}

THREAD_PROC Acceptor::startThread(void *p) {
  Acceptor *pAcceptor = static_cast<Acceptor *>(p);
  Acceptor *previous = activeAcceptor;
  activeAcceptor = pAcceptor;
  auto guard = sg::make_scope_guard([pAcceptor, previous]() {
    pAcceptor->m_processing = false;
    activeAcceptor = previous;
  });
  auto log = [pAcceptor](const char *message) noexcept {
    try {
      pAcceptor->getLog()->onEvent(message);
    } catch (...) {}
  };
  auto stop = [pAcceptor, &log](const char *message) noexcept {
    pAcceptor->m_stop = true;
    try {
      pAcceptor->onStop();
    } catch (const std::exception &e) {
      log(e.what());
    } catch (...) {
      log("Unknown exception stopping acceptor start thread");
    }
    try {
      HttpServer::stopGlobal();
    } catch (const std::exception &e) {
      log(e.what());
    } catch (...) {
      log("Unknown exception stopping global HTTP server");
    }
    log(message);
  };
  try {
    pAcceptor->onStart();
  } catch (const std::exception &e) {
    stop(e.what());
  } catch (...) {
    stop("Unknown exception in acceptor start thread");
  }
  return 0;
}
} // namespace FIX
