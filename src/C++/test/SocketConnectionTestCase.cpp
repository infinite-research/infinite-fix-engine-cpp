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
#pragma warning(disable : 4503 4355 4786)
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "TestHelper.h"
#include <Application.h>
#include <Dictionary.h>
#include <MessageStore.h>
#include <SessionSettings.h>
#include <SocketAcceptor.h>
#include <SocketConnection.h>
#include <SocketInitiator.h>
#include <SocketServer.h>
#include <ThreadedSocketConnection.h>
#include <Utility.h>
#include <fix42/Logon.h>
#include <fix42/NewOrderSingle.h>
#include <fix42/SequenceReset.h>
#include <set>
#if HAVE_SSL
#include <SSLSocketAcceptor.h>
#include <SSLSocketConnection.h>
#include <ThreadedSSLSocketConnection.h>
#include <future>
#endif

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("connection admission preserves rejected session state", "[admission]") {
  const bool threaded = GENERATE(false, true);
#if HAVE_SSL
  const bool tls = GENERATE(false, true);
#else
  const bool tls = false;
#endif
  const std::string reason = GENERATE(
      "reset",
      "reject",
      "address",
      "listener",
      "authentication",
      "valid",
      "duplicate",
      "established-reject");
  const bool established = reason == "duplicate" || reason == "established-reject";
  const bool admitted = reason == "valid" || established;
  CAPTURE(threaded, tls, reason);
  struct Application : NullApplication, Responder {
    void fromAdmin(const Message &, const SessionID &id) override {
      ++authenticationCalls;
      CHECK(Session::isSessionRegistered(id) == expectedRegistered);
      CHECK(session->getExpectedSenderNum() == expectedSender);
      CHECK(session->getExpectedTargetNum() == expectedTarget);
      if (reject) {
        throw RejectLogon();
      }
    }
    void fromApp(const Message &, const SessionID &) override { ++appCalls; }
    void toAdmin(Message &, const SessionID &) override { ++outgoingCalls; }
    void onLogon(const SessionID &) override { ++logonCalls; }
    void onLogout(const SessionID &) override { ++logoutCalls; }
    bool send(const std::string &) override { return true; }
    void disconnect() override { ++disconnectCalls; }
    Session *session = nullptr;
    bool reject = false;
    bool expectedRegistered = false;
    int expectedSender = 7, expectedTarget = 9, logoutCalls = 0;
    int authenticationCalls = 0, appCalls = 0, outgoingCalls = 0, logonCalls = 0, disconnectCalls = 0;
  } application;
  SessionID id("FIX.4.2", "ADMISSION", "PEER");
  FileStoreFactory stores("store");
  SessionSettings settings;
  Dictionary dictionary;
  dictionary.setString(CONNECTION_TYPE, "acceptor");
  dictionary.setString(START_TIME, "00:00:00");
  dictionary.setString(END_TIME, "00:00:00");
  dictionary.setString(USE_DATA_DICTIONARY, "N");
  settings.set(id, dictionary);
  {
    std::unique_ptr<Acceptor> acceptor;
#if HAVE_SSL
    if (tls) {
      acceptor.reset(new SSLSocketAcceptor(application, stores, settings));
    } else
#endif
    {
      acceptor.reset(new SocketAcceptor(application, stores, settings));
    }
    Session *session = Session::lookupSession(id);
    REQUIRE(session);
    application.session = session;
    application.reject = reason == "authentication";
    session->setResponder(&application);
    session->setNextSenderMsgSeqNum(7);
    session->setNextTargetMsgSeqNum(9);
    session->setRefreshOnLogon(true);
    session->setResetOnLogon(true);
    {
      FileStore external(UtcTimeStamp::now(), "store", id);
      external.setNextSenderMsgSeqNum(11);
      external.setNextTargetMsgSeqNum(13);
    }
    if (reason == "address") {
      session->setAllowedRemoteAddresses({"192.0.2.1"});
    }
    FIX42::Logon logon(EncryptMethod(0), HeartBtInt(30));
    logon.getHeader().setField(SenderCompID("PEER"));
    logon.getHeader().setField(TargetCompID("ADMISSION"));
    logon.getHeader().setField(MsgSeqNum(1));
    logon.getHeader().setField(SendingTime(UtcTimeStamp::now()));
    logon.setField(ResetSeqNumFlag(true));
    FIX42::SequenceReset reset(NewSeqNo(50));
    reset.getHeader() = logon.getHeader();
    reset.getHeader().setField(MsgType(MsgType_SequenceReset));
    Message first = logon;
    if (reason == "reset") {
      first = reset;
    } else if (reason == "reject") {
      first.getHeader().setField(MsgType(MsgType_Reject));
    }
    const std::string wire = first.toString() + (admitted ? "" : reset.toString());
    auto sockets = socket_createpair();
    REQUIRE(sockets.first != INVALID_SOCKET_HANDLE);
    REQUIRE(sockets.second != INVALID_SOCKET_HANDLE);
#if HAVE_SSL
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_method()), SSL_CTX_free);
    std::unique_ptr<SSL, decltype(&SSL_free)> client(nullptr, SSL_free);
    SSL *ssl = nullptr;
    if (tls) {
      REQUIRE(context);
      REQUIRE(SSL_CTX_set_max_proto_version(context.get(), TLS1_2_VERSION) == 1);
      REQUIRE(SSL_CTX_set_cipher_list(context.get(), "PSK-AES128-CBC-SHA") == 1);
      SSL_CTX_set_psk_client_callback(
          context.get(),
          [](SSL *, const char *, char *identity, unsigned int size, unsigned char *key, unsigned int keySize)
              -> unsigned int {
            if (size < 10 || keySize < 16) {
              return 0;
            }
            std::copy_n("admission", 10, identity);
            std::fill_n(key, 16, 42);
            return 16;
          });
      SSL_CTX_set_psk_server_callback(
          context.get(),
          [](SSL *, const char *, unsigned char *key, unsigned int keySize) -> unsigned int {
            if (keySize < 16) {
              return 0;
            }
            std::fill_n(key, 16, 42);
            return 16;
          });
      client.reset(SSL_new(context.get()));
      ssl = SSL_new(context.get());
      REQUIRE(client);
      REQUIRE(ssl);
      SSL_set_quiet_shutdown(ssl, 1);
      REQUIRE(SSL_set_fd(client.get(), sockets.first) == 1);
      REQUIRE(SSL_set_fd(ssl, sockets.second) == 1);
      auto handshake = std::async(std::launch::async, [&]() { return SSL_connect(client.get()); });
      REQUIRE(SSL_accept(ssl) == 1);
      REQUIRE(handshake.get() == 1);
      REQUIRE(SSL_write(client.get(), wire.data(), static_cast<int>(wire.size())) == static_cast<int>(wire.size()));
    } else
#endif
    {
      REQUIRE(socket_send(sockets.first, wire.data(), wire.size()) == static_cast<ssize_t>(wire.size()));
    }
    std::set<SessionID> listeners;
    if (reason != "listener") {
      listeners.insert(id);
    }
    SocketServer server;
    auto exercise = [&](auto &connection, auto read) {
      read();
      CHECK((connection.getSession() != nullptr) == admitted);
      if (established) {
        REQUIRE(session->isLoggedOn());
        application.expectedRegistered = true;
        application.expectedSender = 2;
        application.expectedTarget = 2;
        application.reject = reason == "established-reject";
        auto rejected = logon;
        if (reason == "duplicate") {
          rejected.removeField(FIELD::ResetSeqNumFlag);
        }
        rejected.getHeader().setField(MsgSeqNum(2));
        const auto buffered = rejected.toString() + reset.toString() + logon.toString();
#if HAVE_SSL
        if (tls) {
          REQUIRE(
              SSL_write(client.get(), buffered.data(), static_cast<int>(buffered.size()))
              == static_cast<int>(buffered.size()));
        } else
#endif
        {
          REQUIRE(
              socket_send(sockets.first, buffered.data(), buffered.size()) == static_cast<ssize_t>(buffered.size()));
        }
        read();
        CHECK_FALSE(session->receivedLogon());
        CHECK_FALSE(session->sentLogon());
        CHECK(application.logoutCalls == 1);
        CHECK(application.authenticationCalls == (reason == "duplicate" ? 1 : 2));
        CHECK(session->getExpectedTargetNum() == 2);
        if (session->receivedLogon()) {
          session->disconnect();
        }
      } else if (admitted) {
        session->disconnect();
      }
    };
#if HAVE_SSL
    if (tls && threaded) {
      {
        ThreadedSSLSocketConnection connection(sockets.second, ssl, listeners, nullptr);
        exercise(connection, [&]() { connection.read(); });
      }
      SSL_free(ssl);
    } else if (tls) {
      SSLSocketConnection connection(sockets.second, ssl, listeners, &server.getMonitor());
      exercise(connection, [&]() { connection.read(static_cast<SSLSocketAcceptor &>(*acceptor), server); });
    } else
#endif
        if (threaded) {
      ThreadedSocketConnection connection(sockets.second, listeners, nullptr);
      exercise(connection, [&]() { connection.read(); });
    } else {
      SocketConnection connection(sockets.second, listeners, &server.getMonitor());
      exercise(connection, [&]() { connection.read(static_cast<SocketAcceptor &>(*acceptor), server); });
      socket_close(sockets.second);
    }
    socket_close(sockets.first);
    CHECK_FALSE(Session::isSessionRegistered(id));
    CHECK(
        application.authenticationCalls
        == (reason == "established-reject" ? 2 : ((reason == "authentication" || admitted) ? 1 : 0)));
    CHECK(application.appCalls == 0);
    CHECK(application.outgoingCalls == (admitted ? 1 : 0));
    CHECK(application.logonCalls == (admitted ? 1 : 0));
    CHECK(session->getExpectedSenderNum() == (admitted ? 2 : 7));
    CHECK(session->getExpectedTargetNum() == (admitted ? 2 : 9));
    session->disconnect();
    CHECK(application.disconnectCalls == (admitted ? 0 : 1));
    if (established) {
      application.reject = false;
      application.expectedRegistered = false;
      REQUIRE(session->acceptLogon(logon.toString(), application));
      CHECK(session->isLoggedOn());
      CHECK(session->getExpectedTargetNum() == 2);
      CHECK(application.authenticationCalls == (reason == "duplicate" ? 2 : 3));
      session->disconnect();
      Session::unregisterSession(id);
    }
  }
  FileStore reopened(UtcTimeStamp::now(), "store", id);
  CHECK(reopened.getNextSenderMsgSeqNum() == (admitted ? 2 : 11));
  CHECK(reopened.getNextTargetMsgSeqNum() == (admitted ? 2 : 13));
}

TEST_CASE("SocketConnectionTests") {
  struct TestSocketMonitor : public SocketMonitor {
    virtual void signal(int socket) { signaledSocket = socket; }

    virtual void unsignal(int socket) { unsignalSocket = socket; }

    virtual bool drop(int socket) {
      dropSocket = socket;
      return true;
    }

    int signaledSocket = -1;
    int unsignalSocket = -1;
    int dropSocket = -1;
  };

  struct TestApplication : public FIX::NullApplication {
    void fromApp(const FIX::Message &m, const FIX::SessionID &)
        EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) {
      count++;
    }

    int count = 0;
  };

  struct NullLogFactory : public LogFactory {
  public:
    virtual ~NullLogFactory() {}
    virtual Log *create() { return 0; }
    virtual Log *create(const SessionID &) { return 0; }
    virtual void destroy(Log *) {}
  };

  struct TestSession : public Session {
    TestSession(
        Application &app,
        MessageStoreFactory &factory,
        const SessionID &sessionId,
        DataDictionaryProvider &provider,
        const TimeRange &timeRange,
        int heartBtInt)
        : Session(
              []() { return UtcTimeStamp::now(); },
              app,
              factory,
              sessionId,
              provider,
              timeRange,
              heartBtInt,
              nullptr) {};
    ~TestSession() {};

    virtual void next(const std::string &, const UtcTimeStamp &timeStamp, bool queued = false) {
      if (nextThrowInvalidMsg) {
        throw InvalidMessage();
      }
    }

    SocketConnection *pConnection = nullptr;
    bool nextThrowInvalidMsg = false;
  };

  struct TestSocketInitiator : public SocketInitiator {
    TestSocketInitiator(
        Application &app,
        MessageStoreFactory &factory,
        const SessionSettings &settings,
        Session *session)
        : SocketInitiator(app, factory, settings),
          pSession(session) {};

    virtual ~TestSocketInitiator() {};

    virtual Session *getSession(const SessionID &, Responder &responder) {
      pSession->setResponder(&responder);
      return pSession;
    }

    Session *pSession;
  };

  struct TestSocketAcceptor : public SocketAcceptor {
    TestSocketAcceptor(
        Application &app,
        MessageStoreFactory &factory,
        const SessionSettings &settings,
        Session *session,
        LogFactory &log)
        : SocketAcceptor(app, factory, settings, log),
          pSession(session) {};

    virtual ~TestSocketAcceptor() {};

    virtual Session *getSession(const std::string &msg, Responder &responder) {
      pSession->setResponder(&responder);
      return pSession;
    }

    Session *pSession;
  };

  struct BaseSocketConnection {
    BaseSocketConnection() {
      initiatorSessionID = SessionID(BeginString("FIX.4.2"), SenderCompID("INITIATOR"), TargetCompID("ACCEPT"));

      acceptorSessionID = SessionID(BeginString("FIX.4.2"), SenderCompID("ACCEPT"), TargetCompID("INITIATOR"));

      initiatorProvider.addTransportDataDictionary(
          initiatorSessionID.getBeginString(),
          FIX::TestSettings::pathForSpec("FIX42"));
      acceptorProvider.addTransportDataDictionary(
          acceptorSessionID.getBeginString(),
          FIX::TestSettings::pathForSpec("FIX42"));

      sessionTime.reset(new TimeRange(UtcTimeOnly(), UtcTimeOnly(), 0, 31));

      dictionaryInitiator.setString("ConnectionType", "initiator");
      dictionaryInitiator.setString("FileStorePath", "store");
      dictionaryInitiator.setString(USE_DATA_DICTIONARY, "N");
      dictionaryInitiator.setString(START_TIME, "12:00:00");
      dictionaryInitiator.setString(END_TIME, "12:00:00");
      dictionaryInitiator.setString(START_DAY, "Sun");
      dictionaryInitiator.setString(END_DAY, "Mon");
      dictionaryInitiator.setString(HEARTBTINT, "30");

      settingsInitiator.set(initiatorSessionID, dictionaryInitiator);

      dictionaryAcceptor.setString("ConnectionType", "acceptor");
      dictionaryAcceptor.setString("FileStorePath", "store");
      dictionaryAcceptor.setString(USE_DATA_DICTIONARY, "N");
      dictionaryAcceptor.setString(START_TIME, "12:00:00");
      dictionaryAcceptor.setString(END_TIME, "12:00:00");
      dictionaryAcceptor.setString(START_DAY, "Sun");
      dictionaryAcceptor.setString(END_DAY, "Mon");

      settingsAcceptor.set(acceptorSessionID, dictionaryAcceptor);
      acceptorSession.reset(
          new TestSession(application, factory, acceptorSessionID, acceptorProvider, *sessionTime, 1));
      acceptor.reset(new TestSocketAcceptor(application, factory, settingsAcceptor, acceptorSession.get(), logFactory));

      initiator.reset(new SocketInitiator(application, factory, settingsInitiator));
      initiatorSession.reset(
          new TestSession(application, factory, initiatorSessionID, initiatorProvider, *sessionTime, 1));
      testInitiator.reset(new TestSocketInitiator(application, factory, settingsInitiator, initiatorSession.get()));
    };

    ~BaseSocketConnection() { socket_close(socket); };

    DataDictionaryProvider initiatorProvider;
    DataDictionaryProvider acceptorProvider;

    SessionID initiatorSessionID;
    Dictionary dictionaryInitiator;
    SessionSettings settingsInitiator;

    SessionID acceptorSessionID;
    Dictionary dictionaryAcceptor;
    SessionSettings settingsAcceptor;

    TestSocketMonitor monitor;
    TestApplication application;
    FIX::MemoryStoreFactory factory;
    NullLogFactory logFactory;
    SocketServer server;

    std::unique_ptr<TimeRange> sessionTime;
    std::unique_ptr<TestSession> initiatorSession;
    std::unique_ptr<TestSession> acceptorSession;

    std::unique_ptr<SocketInitiator> initiator;
    std::unique_ptr<TestSocketInitiator> testInitiator;

    std::unique_ptr<SocketAcceptor> acceptor;
    int socket = 101;
  };

  struct TestSessionConnection : public BaseSocketConnection {
    TestSessionConnection()
        : BaseSocketConnection() {
      pSocketConnection.reset(new SocketConnection(*testInitiator, initiatorSessionID, socket, &monitor));
    };

    ~TestSessionConnection() {};

    Session *getSession() { return pSocketConnection->getSession(); }

    std::unique_ptr<SocketConnection> pSocketConnection;
  };

  SECTION("socketTimeout_SessionIsNotNull") {
    TestSessionConnection connection;
    connection.pSocketConnection->onTimeout();
    Session *session = connection.pSocketConnection->getSession();
    CHECK(nullptr != session);
  }

  SECTION("ReadSocket_SessionMissing_NoMessagesRead") {
    TestSessionConnection connection;
    std::set<SessionID> sessions;
    connection.pSocketConnection.reset(new SocketConnection(connection.socket, sessions, &connection.monitor));

    SocketConnector connector;
    CHECK(!connection.pSocketConnection->read(connector));
  }

  SECTION("ReadSocket_SocketException_ReadFailed") {
    TestSessionConnection connection;
    Session *session = connection.getSession();
    CHECK(nullptr != session);
    session->disconnect();

    SocketConnector connector;
    CHECK(!connection.pSocketConnection->read(connector));
  }
}
