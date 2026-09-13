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
#include <atomic>
#include <cstdlib>
#include <future>
#ifdef __linux__
#include <fcntl.h>
#include <filesystem>
#endif
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

template <typename Transport> struct CallbackStopAcceptor : Transport {
  using Transport::Transport;
  std::atomic<int> callbacks{0};
  void onConnect(SocketServer &server, socket_handle, socket_handle socket) override {
    server.getMonitor().addRead(socket);
  }
  bool onData(SocketServer &, socket_handle) override { return false; }
  void onDisconnect(SocketServer &, socket_handle) override {
    this->stop(true);
    ++callbacks;
  }
};

template <typename Transport> struct ExternalStopAcceptor : Transport {
  using Transport::Transport;
  std::promise<void> entered;
  std::future<void> proceed;
  void onTimeout(SocketServer &server) override {
    entered.set_value();
    proceed.wait();
    server.getMonitor().numSockets();
  }
};

struct ThreadedCallbackStopApplication : TestApplication {
  Acceptor *acceptor = nullptr;
  std::promise<void> entered;
  std::promise<void> returned;

  void onLogon(const SessionID &) override {
    entered.set_value();
    acceptor->stop(true);
    returned.set_value();
  }
};

int listenerPort(socket_handle socket) {
  sockaddr_in address{};
  socklen_t size = sizeof(address);
  REQUIRE(getsockname(socket, reinterpret_cast<sockaddr *>(&address), &size) == 0);
  return ntohs(address.sin_port);
}

std::string addressInUseError() {
#ifdef _MSC_VER
  return error_wsaerror(WSAEADDRINUSE);
#else
  int code = EADDRINUSE;
  return error_strerror(code);
#endif
}

ListenerSocket loopbackListener(int port) {
  const socket_handle socket = socket_createAcceptor("127.0.0.1", port, true);
  if (socket == INVALID_SOCKET_HANDLE) {
    const std::string error = socket_error();
    if (error == addressInUseError()) {
      throw RuntimeError("Unable to create, bind, or listen to port " + std::to_string(port) + " (" + error + ")");
    }
  }
  REQUIRE(socket != INVALID_SOCKET_HANDLE);
  return ListenerSocket(socket);
}

int availableLoopbackPort() {
  ListenerSocket reservation = loopbackListener(0);
  return listenerPort(reservation.value);
}

bool isLoopbackBindCollision(const RuntimeError &error, int port) {
  const std::string prefix = "Unable to create, bind, or listen to port " + std::to_string(port) + " (";
  const std::string message = error.what();
  return message.find(prefix) != std::string::npos && message.find(addressInUseError()) != std::string::npos;
}

template <typename Scenario> void withLoopbackPort(Scenario scenario) {
  for (int attempt = 0; attempt < 10; ++attempt) {
    const int port = availableLoopbackPort();
    try {
      scenario(port);
      return;
    } catch (const RuntimeError &error) {
      if (attempt == 9 || !isLoopbackBindCollision(error, port)) {
        throw;
      }
    }
  }
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

TEST_CASE("Acceptor synchronous start errors propagate") {
  struct ThrowingAcceptor : Acceptor {
    using Acceptor::Acceptor;
    bool cleaned = false;
    void onStart() override { throw RuntimeError("synchronous startup failure"); }
    bool onPoll() override { return false; }
    void onStop() override { cleaned = true; }
  };
  TestApplication application;
  MemoryStoreFactory stores;
  ThrowingAcceptor acceptor(application, stores, listenerSettings({{0, "127.0.0.1"}}));
  CHECK_THROWS_AS(acceptor.block(), RuntimeError);
  CHECK(acceptor.isStopped());
  CHECK(acceptor.cleaned);
}

TEST_CASE("Acceptor asynchronous start cleanup contains exceptions") {
  struct ThrowingAcceptor : Acceptor {
    using Acceptor::Acceptor;
    void onStart() override { throw RuntimeError("asynchronous startup failure"); }
    bool onPoll() override { return false; }
    void onStop() override { throw RuntimeError("asynchronous cleanup failure"); }
  };
  TestApplication application;
  MemoryStoreFactory stores;
  ThrowingAcceptor acceptor(application, stores, listenerSettings({{0, "127.0.0.1"}}));
  acceptor.start();
  for (int retry = 0; retry < 1000 && !acceptor.isStopped(); ++retry) {
    process_sleep(0.001);
  }
  CHECK(acceptor.isStopped());
  CHECK_NOTHROW(acceptor.stop(true));
}

TEST_CASE("Acceptor rollback restores processing after stop failure") {
  struct ThrowingStopAcceptor : Acceptor {
    using Acceptor::Acceptor;
    int stops = 0;
    void onStart() override {}
    bool onPoll() override { return false; }
    void onStop() override {
      if (stops++ == 0) {
        throw RuntimeError("rollback cleanup failure");
      }
    }
  };
  TestApplication application;
  MemoryStoreFactory stores;
  auto settings = listenerSettings({{0, "127.0.0.1"}});
  Dictionary defaults = settings.get();
  defaults.setInt(HTTP_ACCEPT_PORT, 0);
  settings.set(defaults);
  ThrowingStopAcceptor acceptor(application, stores, settings);
  CHECK_THROWS_AS(acceptor.start(), RuntimeError);
  CHECK(acceptor.isStopped());
  CHECK(acceptor.stops == 1);
  CHECK_NOTHROW(acceptor.stop(true));
}

TEST_CASE("SocketAcceptor callback stop defers dispatch teardown") {
  const std::string mode = GENERATE("start", "block", "poll");
#if HAVE_SSL && !defined(_MSC_VER)
  const bool tls = GENERATE(false, true);
#else
  const bool tls = false;
#endif
  CAPTURE(mode, tls);
  withLoopbackPort([&](int port) {
    TestApplication application;
    MemoryStoreFactory stores;
    auto settings = listenerSettings({{port, "127.0.0.1"}});
    auto exercise = [&](auto &acceptor) {
      auto peer = std::async(std::launch::async, [port]() {
        for (int retry = 0; retry < 1000; ++retry) {
          ListenerSocket socket(socket_createConnector());
          if (socket_connect(socket.value, "127.0.0.1", port) == 0) {
            return socket_send(socket.value, "x", 1) == 1;
          }
          process_sleep(0.001);
        }
        return false;
      });
      if (mode == "block") {
        acceptor.block();
      } else {
        if (mode == "start") {
          acceptor.start();
        }
        for (int retry = 0; retry < 1000 && !acceptor.callbacks; ++retry) {
          if (mode == "poll") {
            acceptor.poll();
          }
          process_sleep(0.001);
        }
      }
      acceptor.stop(true);
      CHECK(peer.get());
      CHECK(acceptor.callbacks == 1);
      ListenerSocket replacement = loopbackListener(port);
    };
#if HAVE_SSL && !defined(_MSC_VER)
    if (tls) {
      CallbackStopAcceptor<SSLSocketAcceptor> acceptor(application, stores, settings);
      exercise(acceptor);
    } else
#endif
    {
      CallbackStopAcceptor<SocketAcceptor> acceptor(application, stores, settings);
      exercise(acceptor);
    }
  });
}

TEST_CASE("SocketAcceptor external stop waits for dispatch") {
  const bool polling = GENERATE(false, true);
#if HAVE_SSL && !defined(_MSC_VER)
  const bool tls = GENERATE(false, true);
#else
  const bool tls = false;
#endif
  CAPTURE(polling, tls);
  withLoopbackPort([&](int port) {
    TestApplication application;
    MemoryStoreFactory stores;
    const auto settings = listenerSettings({{port, "127.0.0.1"}});
    auto exercise = [&](auto &acceptor) {
      std::promise<void> proceed;
      acceptor.proceed = proceed.get_future();
      auto entered = acceptor.entered.get_future();
      auto worker = std::async(std::launch::async, [&]() {
        if (polling) {
          acceptor.poll();
        } else {
          acceptor.block();
        }
      });
      while (entered.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
        if (worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
          worker.get();
          FAIL("acceptor dispatch returned before the timeout callback");
        }
      }
      auto stopper = std::async(std::launch::async, [&]() { acceptor.stop(true); });
      while (!acceptor.isStopped()) {
        process_sleep(0.001);
      }
      CHECK(stopper.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
      proceed.set_value();
      stopper.get();
      worker.get();
      ListenerSocket replacement = loopbackListener(port);
    };
#if HAVE_SSL && !defined(_MSC_VER)
    if (tls) {
      ExternalStopAcceptor<SSLSocketAcceptor> acceptor(application, stores, settings);
      exercise(acceptor);
    } else
#endif
    {
      ExternalStopAcceptor<SocketAcceptor> acceptor(application, stores, settings);
      exercise(acceptor);
    }
  });
}

TEST_CASE("Threaded acceptor callback stop is an external completion barrier") {
#if HAVE_SSL && !defined(_MSC_VER)
  const bool tls = GENERATE(false, true);
#else
  const bool tls = false;
#endif
  CAPTURE(tls);
  withLoopbackPort([&](int port) {
    ThreadedCallbackStopApplication application;
    MemoryStoreFactory stores;
    const auto settings = listenerSettings({{port, "127.0.0.1"}});
    std::unique_ptr<Acceptor> acceptor;
#if HAVE_SSL && !defined(_MSC_VER)
    if (tls) {
      acceptor = std::make_unique<ThreadedSSLSocketAcceptor>(application, stores, settings);
    } else
#endif
    {
      acceptor = std::make_unique<ThreadedSocketAcceptor>(application, stores, settings);
    }
    application.acceptor = acceptor.get();
    auto entered = application.entered.get_future();
    auto returned = application.returned.get_future();
    acceptor->start();

    ListenerSocket client(socket_createConnector());
    bool connected = false;
    for (int retry = 0; retry < 1000 && !connected; ++retry) {
      connected = socket_connect(client.value, "127.0.0.1", port) == 0;
      if (!connected) {
        process_sleep(0.001);
      }
    }
    REQUIRE(connected);

    const SessionID &sessionID = *acceptor->getSessions().begin();
    FIX42::Logon logon(EncryptMethod(0), HeartBtInt(30));
    logon.getHeader().set(SenderCompID(sessionID.getTargetCompID()));
    logon.getHeader().set(TargetCompID(sessionID.getSenderCompID()));
    logon.getHeader().set(MsgSeqNum(1));
    logon.getHeader().set(SendingTime::now());
    logon.setField(ResetSeqNumFlag(true));
    const std::string wire = logon.toString();
#if HAVE_SSL && !defined(_MSC_VER)
    auto context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(SSL_CTX_new(TLS_method()), SSL_CTX_free);
    auto secure = std::unique_ptr<SSL, decltype(&SSL_free)>(SSL_new(context.get()), SSL_free);
    if (tls) {
      REQUIRE(SSL_set_fd(secure.get(), client.value) == 1);
      REQUIRE(SSL_connect(secure.get()) == 1);
      REQUIRE(SSL_write(secure.get(), wire.data(), static_cast<int>(wire.size())) == static_cast<int>(wire.size()));
    } else
#endif
    {
      REQUIRE(socket_send(client.value, wire.data(), static_cast<int>(wire.size())) == static_cast<int>(wire.size()));
    }

    REQUIRE(entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    for (int retry = 0; retry < 2000 && !acceptor->isStopped(); ++retry) {
      process_sleep(0.001);
    }
    REQUIRE(acceptor->isStopped());
    auto externalStop = std::async(std::launch::async, [&]() { acceptor->stop(true); });
    CHECK(externalStop.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    shutdown(client.value, 2);
    REQUIRE(returned.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    externalStop.get();

    { ListenerSocket replacement = loopbackListener(port); }
    acceptor->start();
    acceptor->stop(true);
  });
}

TEST_CASE("Acceptor restart fails while cleanup remains deferred") {
  struct DeferredStopAcceptor : Acceptor {
    using Acceptor::Acceptor;
    bool deferCleanup = true;
    void onStart() override {}
    bool onPoll() override { return false; }
    void onStop() override {
      if (deferCleanup) {
        deferStopCleanup();
      }
    }
  };
  TestApplication application;
  MemoryStoreFactory stores;
  DeferredStopAcceptor acceptor(application, stores, listenerSettings({{0, "127.0.0.1"}}));
  acceptor.start();
  acceptor.stop(true);
  CHECK_THROWS_AS(acceptor.start(), RuntimeError);
  acceptor.deferCleanup = false;
  acceptor.stop(true);
}

#ifdef __linux__
TEST_CASE("SocketAcceptor accepted poll connection teardown") {
  const bool promoted = GENERATE(false, true);
#if HAVE_SSL && !defined(_MSC_VER)
  const bool tls = GENERATE(false, true);
#else
  const bool tls = false;
#endif
  CAPTURE(tls, promoted);
  withLoopbackPort([&](int port) {
    TestApplication application;
    MemoryStoreFactory stores;
    auto acceptor = listenerAcceptor(false, tls, application, stores, listenerSettings({{port, "127.0.0.1"}}));
    const bool listening = acceptor->poll();
    REQUIRE(listening);
    ListenerSocket client(socket_createConnector());
    REQUIRE(socket_connect(client.value, "127.0.0.1", port) == 0);
    timeval timeout{2, 0};
    REQUIRE(setsockopt(client.value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    REQUIRE(setsockopt(client.value, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
#if HAVE_SSL && !defined(_MSC_VER)
    auto context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(SSL_CTX_new(TLS_method()), SSL_CTX_free);
    auto secure = std::unique_ptr<SSL, decltype(&SSL_free)>(SSL_new(context.get()), SSL_free);
    SSL_set_fd(secure.get(), client.value);
    std::future<int> handshake;
    if (tls) {
      handshake = std::async(std::launch::async, [&]() { return SSL_connect(secure.get()); });
    }
#endif
    REQUIRE(acceptor->poll());
#if HAVE_SSL && !defined(_MSC_VER)
    if (tls) {
      REQUIRE(handshake.get() == 1);
    }
#endif
    if (promoted) {
      REQUIRE(acceptor->poll());
    }
    socket_handle accepted = INVALID_SOCKET_HANDLE;
    const int clientPort = listenerPort(client.value);
    for (const auto &entry : std::filesystem::directory_iterator("/proc/self/fd")) {
      const int candidate = std::stoi(entry.path().filename().string());
      sockaddr_in address{};
      socklen_t size = sizeof(address);
      if (getpeername(candidate, reinterpret_cast<sockaddr *>(&address), &size) == 0
          && ntohs(address.sin_port) == clientPort) {
        accepted = candidate;
        break;
      }
    }
    REQUIRE(accepted != INVALID_SOCKET_HANDLE);
    acceptor->stop(true);
    CHECK(fcntl(accepted, F_GETFD) == -1);
    ListenerSocket first = loopbackListener(port);
    if (first.value != accepted) {
      REQUIRE(dup2(first.value, accepted) == accepted);
      socket_close(first.value);
      first.value = accepted;
    }
    acceptor.reset();
    CHECK(listenerPort(first.value) == port);
  });
}
#endif

#if HAVE_SSL && defined(__linux__)
TEST_CASE("SSLSocketConnection and monitor share descriptor ownership") {
  const bool monitorFirst = GENERATE(false, true);
  auto monitor = std::make_unique<SocketMonitor>();
  const socket_handle socket = socket_createConnector();
  REQUIRE(socket != INVALID_SOCKET_HANDLE);
  REQUIRE(monitor->addRead(socket));
  auto context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(SSL_CTX_new(TLS_method()), SSL_CTX_free);
  auto connection = std::make_unique<SSLSocketConnection>(
      socket,
      SSL_new(context.get()),
      SSLSocketConnection::Sessions{},
      monitor.get());
  if (monitorFirst) {
    REQUIRE(monitor->drop(socket));
  } else {
    connection.reset();
  }
  ListenerSocket replacement(socket_createAcceptor("127.0.0.1", 0, true));
  REQUIRE(replacement.value != INVALID_SOCKET_HANDLE);
  if (replacement.value != socket) {
    REQUIRE(dup2(replacement.value, socket) == socket);
    socket_close(replacement.value);
    replacement.value = socket;
  }
  connection.reset();
  monitor.reset();
  CHECK(listenerPort(replacement.value) != 0);
}
#endif

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
    if (reservation.value == INVALID_SOCKET_HANDLE) {
      SKIP("configured-address isolation requires a second local loopback address");
    }
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
    ListenerSocket occupied(socket_createAcceptor("127.0.0.1", 0, true));
    REQUIRE(occupied.value != INVALID_SOCKET_HANDLE);
    const int occupiedPort = listenerPort(occupied.value);
    withLoopbackPort([&](int firstPort) {
      auto acceptor = listenerAcceptor(
          threaded,
          tls,
          application,
          stores,
          listenerSettings({{firstPort, "127.0.0.1"}, {occupiedPort, "127.0.0.1"}}));
      bool failed = false;
      try {
        acceptor->start();
      } catch (const RuntimeError &error) {
        if (isLoopbackBindCollision(error, firstPort)) {
          throw;
        }
        failed = true;
      }
      CHECK(failed);
      ListenerSocket replacement = loopbackListener(firstPort);
      acceptor->stop(true);
    });
  }

  SECTION("startup failure after initialization releases the listener") {
    withLoopbackPort([&](int port) {
      auto settings = listenerSettings({{port, "127.0.0.1"}});
      Dictionary defaults = settings.get();
      defaults.setInt(HTTP_ACCEPT_PORT, 0);
      settings.set(defaults);
      auto acceptor = listenerAcceptor(threaded, tls, application, stores, settings);
      bool failed = false;
      try {
        acceptor->start();
      } catch (const ConfigError &) {
        failed = true;
      }
      CHECK(failed);
      CHECK(acceptor->isStopped());
      acceptor.reset();
      ListenerSocket replacement = loopbackListener(port);
    });
  }

  SECTION("poll stop and destruction release the listener") {
    if (!threaded) {
      withLoopbackPort([&](int port) {
        auto acceptor = listenerAcceptor(threaded, tls, application, stores, listenerSettings({{port, "127.0.0.1"}}));
        const bool listening = acceptor->poll();
        CHECK(listening);
        acceptor->stop(true);
        ListenerSocket replacement = loopbackListener(port);
        acceptor.reset();
        CHECK(listenerPort(replacement.value) == port);
      });
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
    CHECK(failed);
  } else if (worker) {
    CHECK_THROWS_AS(acceptor->block(), RuntimeError);
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
  bool restarted = false;
  for (int retry = 0; retry < 1000 && !restarted; ++retry) {
    try {
      acceptor->start();
      restarted = true;
    } catch (const RuntimeError &) {
      process_sleep(0.001);
    }
  }
  CHECK(restarted);
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
