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
#include <SocketAcceptor.h>
#include <ThreadedSocketAcceptor.h>
#include <Utility.h>
#include <fix42/Logon.h>
#if HAVE_SSL && !defined(_MSC_VER)
#include <SSLSocketAcceptor.h>
#include <ThreadedSSLSocketAcceptor.h>
#endif
#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace FIX;

namespace {
struct ListenerSocket {
  explicit ListenerSocket(socket_handle value)
      : value(value) {}
  ~ListenerSocket() { socket_close(value); }

  socket_handle value;
};

int listenerPort(socket_handle socket) {
  sockaddr_in address{};
  socklen_t size = sizeof(address);
  REQUIRE(getsockname(socket, reinterpret_cast<sockaddr *>(&address), &size) == 0);
  return ntohs(address.sin_port);
}

SessionSettings listenerSettings(const std::vector<std::pair<int, std::string>> &listeners) {
  Dictionary defaults;
  defaults.setString(CONNECTION_TYPE, "acceptor");
  defaults.setBool(NON_STOP_SESSION, true);
  defaults.setBool(USE_DATA_DICTIONARY, false);
  defaults.setBool(SOCKET_REUSE_ADDRESS, true);
#if HAVE_SSL && !defined(_MSC_VER)
  defaults.setString(SERVER_CERTIFICATE_FILE, std::string(QUICKFIX_TEST_PKI) + "/server.crt");
  defaults.setString(SERVER_CERTIFICATE_KEY_FILE, std::string(QUICKFIX_TEST_PKI) + "/server.key");
#endif

  static int serial = 0;
  const std::string prefix = "TASK13-" + std::to_string(++serial) + "-";
  SessionSettings settings;
  settings.set(defaults);
  for (size_t index = 0; index < listeners.size(); ++index) {
    Dictionary session;
    session.setInt(SOCKET_ACCEPT_PORT, listeners[index].first);
    session.setString(SOCKET_ACCEPT_ADDRESS, listeners[index].second);
    settings.set(SessionID("FIX.4.2", prefix + static_cast<char>('A' + index), "PEER"), session);
  }
  return settings;
}

std::unique_ptr<Acceptor> listenerAcceptor(
    bool threaded,
    bool tls,
    TestApplication &application,
    MemoryStoreFactory &stores,
    const SessionSettings &settings) {
#if HAVE_SSL && !defined(_MSC_VER)
  if (tls) {
    if (threaded) {
      return std::make_unique<ThreadedSSLSocketAcceptor>(application, stores, settings);
    }
    return std::make_unique<SSLSocketAcceptor>(application, stores, settings);
  }
#else
  (void)tls;
#endif
  if (threaded) {
    return std::make_unique<ThreadedSocketAcceptor>(application, stores, settings);
  }
  return std::make_unique<SocketAcceptor>(application, stores, settings);
}
} // namespace

TEST_CASE("SocketAcceptor bind settings") {
  const bool threaded = GENERATE(false, true);
#if HAVE_SSL && !defined(_MSC_VER)
  const bool tls = GENERATE(false, true);
#else
  const bool tls = false;
#endif
  CAPTURE(threaded, tls);
  TestApplication application;
  MemoryStoreFactory stores;

  SECTION("configured address is forwarded") {
    ListenerSocket reservation(socket_createAcceptor("127.0.0.2", 0, true));
    REQUIRE(reservation.value != INVALID_SOCKET_HANDLE);
    const int port = listenerPort(reservation.value);
    auto acceptor = listenerAcceptor(threaded, tls, application, stores, listenerSettings({{port, "127.0.0.1"}}));
    if (threaded) {
      acceptor->block();
    } else {
      acceptor->start();
    }
    acceptor->stop(true);
  }

  SECTION("invalid address is rejected before listening") {
    const int port = 0;
    auto acceptor = listenerAcceptor(threaded, tls, application, stores, listenerSettings({{port, "localhost"}}));
    CHECK_THROWS_AS(acceptor->start(), ConfigError);
    ListenerSocket replacement(socket_createAcceptor("127.0.0.1", port, true));
    CHECK(replacement.value != INVALID_SOCKET_HANDLE);
    acceptor->stop(true);
  }

  SECTION("sessions sharing a port reject different addresses") {
    const int port = 0;
    auto acceptor
        = listenerAcceptor(threaded, tls, application, stores, listenerSettings({{port, ""}, {port, "127.0.0.1"}}));
    CHECK_THROWS_AS(acceptor->start(), ConfigError);
    ListenerSocket replacement(socket_createAcceptor("127.0.0.1", port, true));
    CHECK(replacement.value != INVALID_SOCKET_HANDLE);
    acceptor->stop(true);
  }

  SECTION("empty and explicit wildcard addresses share a listener") {
    const int port = 0;
    auto acceptor
        = listenerAcceptor(threaded, tls, application, stores, listenerSettings({{port, ""}, {port, "0.0.0.0"}}));
    CHECK_NOTHROW(acceptor->start());
    acceptor->stop(true);
  }

  SECTION("failed initialization publishes no listener") {
    ListenerSocket reservation(socket_createAcceptor("127.0.0.2", 0, true));
    REQUIRE(reservation.value != INVALID_SOCKET_HANDLE);
    const int firstPort = listenerPort(reservation.value);
    ListenerSocket occupied(socket_createAcceptor("", 0, true));
    REQUIRE(occupied.value != INVALID_SOCKET_HANDLE);
    const int occupiedPort = listenerPort(occupied.value);
    auto acceptor = listenerAcceptor(
        threaded,
        tls,
        application,
        stores,
        listenerSettings({{firstPort, "127.0.0.1"}, {occupiedPort, "127.0.0.1"}}));
    CHECK_THROWS_AS(acceptor->start(), RuntimeError);
    ListenerSocket replacement(socket_createAcceptor("127.0.0.1", firstPort, true));
    CHECK(replacement.value != INVALID_SOCKET_HANDLE);
    acceptor->stop(true);
  }

  SECTION("startup failure after initialization releases the listener") {
    ListenerSocket reservation(socket_createAcceptor("127.0.0.2", 0, true));
    REQUIRE(reservation.value != INVALID_SOCKET_HANDLE);
    const int port = listenerPort(reservation.value);
    auto settings = listenerSettings({{port, "127.0.0.1"}});
    Dictionary defaults = settings.get();
    defaults.setInt(HTTP_ACCEPT_PORT, 0);
    settings.set(defaults);
    auto acceptor = listenerAcceptor(threaded, tls, application, stores, settings);
    CHECK_THROWS_AS(acceptor->start(), ConfigError);
    CHECK(acceptor->isStopped());
    acceptor.reset();
    ListenerSocket replacement(socket_createAcceptor("127.0.0.1", port, true));
    CHECK(replacement.value != INVALID_SOCKET_HANDLE);
  }

  SECTION("poll stop and destruction release the listener") {
    if (!threaded) {
      ListenerSocket reservation(socket_createAcceptor("127.0.0.2", 0, true));
      REQUIRE(reservation.value != INVALID_SOCKET_HANDLE);
      const int port = listenerPort(reservation.value);
      auto acceptor = listenerAcceptor(threaded, tls, application, stores, listenerSettings({{port, "127.0.0.1"}}));
      CHECK(acceptor->poll());
      acceptor->stop(true);
      ListenerSocket replacement(socket_createAcceptor("127.0.0.1", port, true));
      CHECK(replacement.value != INVALID_SOCKET_HANDLE);
      acceptor.reset();
      CHECK(listenerPort(replacement.value) == port);
    }
  }
}

TEST_CASE("SocketAcceptor thread spawn failure", "[.]") {
  // Run with test/fail-thread-spawn.gdb, which fails exactly one thread_spawn call.
  const bool threaded = std::getenv("QUICKFIX_TEST_THREADED") != nullptr;
  const bool tls = std::getenv("QUICKFIX_TEST_TLS") != nullptr;
  const bool worker = std::getenv("QUICKFIX_TEST_WORKER") != nullptr;
  const bool asynchronous = std::getenv("QUICKFIX_TEST_ASYNC") != nullptr;
  TestApplication application;
  MemoryStoreFactory stores;
  ListenerSocket first(socket_createAcceptor("127.0.0.2", 0, true));
  ListenerSocket second(socket_createAcceptor("127.0.0.2", 0, true));
  REQUIRE(first.value != INVALID_SOCKET_HANDLE);
  REQUIRE(second.value != INVALID_SOCKET_HANDLE);
  const int firstPort = listenerPort(first.value);
  const int secondPort = listenerPort(second.value);
  auto acceptor = listenerAcceptor(
      threaded,
      tls,
      application,
      stores,
      listenerSettings({{firstPort, "127.0.0.1"}, {secondPort, "127.0.0.1"}}));
  if (asynchronous) {
    acceptor->start();
    for (int retry = 0; retry < 1000 && !acceptor->isStopped(); ++retry) {
      process_sleep(0.001);
    }
    const bool failed = acceptor->isStopped();
    acceptor->stop(true);
    CHECK(failed);
  } else if (worker) {
    acceptor->block();
  } else {
    CHECK_THROWS_AS(acceptor->start(), RuntimeError);
  }
  CHECK(acceptor->isStopped());
  {
    ListenerSocket replacement(socket_createAcceptor("127.0.0.1", firstPort, true));
    ListenerSocket other(socket_createAcceptor("127.0.0.1", secondPort, true));
    CHECK(replacement.value != INVALID_SOCKET_HANDLE);
    CHECK(other.value != INVALID_SOCKET_HANDLE);
  }
  CHECK_NOTHROW(acceptor->start());
  acceptor->stop(true);
}

TEST_CASE("SocketAcceptorTests") {
  TestApplication application;
  MemoryStoreFactory factory;
  socket_handle socket;

  SessionSettings settings;
  std::string input = "[DEFAULT]\n"
                      "ConnectionType=acceptor\n"
                      "SocketAcceptPort=0\n"
                      "SocketReuseAddress=Y\n"
                      "SendBufferSize=1024\n"
                      "ReceiveBufferSize=1024\n"
                      "StartTime=00:00:00\n"
                      "EndTime=00:00:00\n"
                      "UseDataDictionary=N\n"
                      "CheckLatency=N\n"
                      "[SESSION]\n"
                      "BeginString=FIX.4.2\n"
                      "SenderCompID=ISLD\n"
                      "TargetCompID=TW\n"
                      "[SESSION]\n"
                      "BeginString=FIX.4.1\n"
                      "SenderCompID=ISLD\n"
                      "TargetCompID=WT\n";
  std::stringstream stream(input);
  stream >> settings;

  SocketAcceptor object(application, factory, settings);
  object.poll();
  socket = createSocket(object.sessionToPort().find(SessionID("FIX.4.2", "ISLD", "TW"))->second, "127.0.0.1");
  object.poll();

  SECTION("receivePartialMessage") {
    std::string firstPart = "8=FIX.4.29=28235=834=2369=31450"
                            "52=20041209-15:35:32.68749=TW50=G56=ISLD"
                            "60=20041209-15:35:3259=055=GE54=148=BLA000060467"
                            "107=BLF5167=FUT44=9740.0041=040=239=038=10"
                            "37=20041209004077151=10150=020=09717=67960"
                            "17=0068712004120909353214=011=000679606=0432=20041209"
                            "1=1234567810=1458=F";

    std::string secondPart = "IX.4.29=34035=834=3369=31450"
                             "52=20041209-15:35:32.69249=TW50=G56=ISLD"
                             "60=20041209-15:35:3259=055=GE54=148=CME000060467"
                             "107=BLF5167=FUT44=9740.0041=040=239=238=10"
                             "37=20041209004077337=0C032=1031=9740.00151=0150=2"
                             "20=09717=6796017=00687220041209093532TN0002843"
                             "75=2004120914=011=00067960375=BLA030A16=0"
                             "432=200412091=1234567810=217";

    FIX42::Logon logon;
    logon.getHeader().set(SenderCompID("TW"));
    logon.getHeader().set(TargetCompID("ISLD"));
    logon.getHeader().set(MsgSeqNum(1));
    logon.getHeader().set(SendingTime::now());
    logon.set(HeartBtInt(30));

    CHECK(socket_send(socket, logon.toString().c_str(), (int)strlen(logon.toString().c_str())));
    object.poll();
    CHECK(socket_send(socket, firstPart.c_str(), (int)strlen(firstPart.c_str())));
    object.poll();
    CHECK(socket_send(socket, secondPart.c_str(), (int)strlen(secondPart.c_str())));
    object.poll();
  }

  object.stop(true);
  destroySocket(socket);
}
