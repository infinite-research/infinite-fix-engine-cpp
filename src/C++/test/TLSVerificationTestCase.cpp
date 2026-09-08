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

#include "config.h"
#if HAVE_SSL && !defined(_MSC_VER)
#include "catch_amalgamated.hpp"
#include <Application.h>
#include <MessageStore.h>
#include <SSLSocketAcceptor.h>
#include <SSLSocketInitiator.h>
#include <ThreadedSSLSocketAcceptor.h>
#include <ThreadedSSLSocketInitiator.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fix42/Logon.h>
#include <future>
#include <openssl/x509v3.h>
#include <thread>

using namespace FIX;
namespace {
std::string certificate(const std::string &name) { return std::string(QUICKFIX_TEST_PKI) + "/" + name; }
using Context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
using Secure = std::unique_ptr<SSL, decltype(&SSL_free)>;

template <typename Transport> struct TestInitiator : Transport {
  using Transport::Transport;
  bool disconnected(const SessionID &id) { return this->isDisconnected(id); }
};

template <typename Predicate> bool waitFor(Predicate done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return done();
}

struct Socket {
  socket_handle value;
  ~Socket() { socket_close(value); }
};

void timeout(socket_handle socket) {
  timeval interval{2, 0};
  REQUIRE(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &interval, sizeof(interval)) == 0);
  REQUIRE(setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &interval, sizeof(interval)) == 0);
}

int portOf(socket_handle socket) {
  sockaddr_in address{};
  socklen_t size = sizeof(address);
  REQUIRE(getsockname(socket, reinterpret_cast<sockaddr *>(&address), &size) == 0);
  return ntohs(address.sin_port);
}

Context peerContext(const std::string &cert, const std::string &key) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
  Context context(SSL_CTX_new(TLS_method()), SSL_CTX_free);
#else
  Context context(SSL_CTX_new(SSLv23_method()), SSL_CTX_free);
#endif
  REQUIRE(context);
  if (!cert.empty()) {
    REQUIRE(SSL_CTX_use_certificate_file(context.get(), certificate(cert).c_str(), SSL_FILETYPE_PEM) == 1);
    REQUIRE(SSL_CTX_use_PrivateKey_file(context.get(), certificate(key).c_str(), SSL_FILETYPE_PEM) == 1);
  }
  return context;
}

struct TLSApplication : NullApplication {
  void toAdmin(Message &, const SessionID &) override { ++outgoing; }
  void fromAdmin(const Message &, const SessionID &id) override {
    if (Session::isSessionRegistered(id)) {
      prematureRegistration = true;
    }
    ++authentication;
  }
  std::atomic<int> outgoing{0}, authentication{0};
  std::atomic<bool> prematureRegistration{false};
};

struct TLSStores : MemoryStoreFactory {
  struct Store : MemoryStore {
    Store(const UtcTimeStamp &now, std::atomic<int> &count)
        : MemoryStore(now),
          refreshes(count) {}
    void refresh() override {
      ++refreshes;
      MemoryStore::refresh();
    }
    std::atomic<int> &refreshes;
  };
  MessageStore *create(const UtcTimeStamp &now, const SessionID &) override { return new Store(now, refreshes); }
  std::atomic<int> refreshes{0};
};

SessionSettings settingsFor(bool server, const SessionID &id, int port, Dictionary defaults) {
  defaults.setString(CONNECTION_TYPE, server ? "acceptor" : "initiator");
  defaults.setString(START_TIME, "00:00:00");
  defaults.setString(END_TIME, "00:00:00");
  defaults.setBool(USE_DATA_DICTIONARY, false);
  defaults.setBool(REFRESH_ON_LOGON, true);
  defaults.setBool(RESET_ON_LOGON, true);
  defaults.setInt(HEARTBTINT, 30);
  defaults.setInt(RECONNECT_INTERVAL, 60);
  defaults.setInt(SOCKET_ACCEPT_PORT, port);
  defaults.setInt(SOCKET_CONNECT_PORT, port);
  if (!defaults.has(SOCKET_CONNECT_HOST)) {
    defaults.setString(SOCKET_CONNECT_HOST, "localhost");
  }
  defaults.setString(SERVER_CERTIFICATE_FILE, certificate("server.crt"));
  defaults.setString(SERVER_CERTIFICATE_KEY_FILE, certificate("server.key"));
  SessionSettings settings;
  settings.set(defaults);
  settings.set(id, defaults);
  return settings;
}

// Set the process-local OpenSSL defaults to a disposable unrelated root. This proves
// that explicitly configured private trust does not silently include default roots.
struct DefaultTrust {
  const char *oldFile = getenv("SSL_CERT_FILE");
  const char *oldDir = getenv("SSL_CERT_DIR");
  std::string file = oldFile ? oldFile : "", dir = oldDir ? oldDir : "";
  DefaultTrust() {
    setenv("SSL_CERT_FILE", certificate("unrelated.crt").c_str(), 1);
    setenv("SSL_CERT_DIR", certificate("empty").c_str(), 1);
  }
  ~DefaultTrust() {
    if (oldFile) {
      setenv("SSL_CERT_FILE", file.c_str(), 1);
    } else {
      unsetenv("SSL_CERT_FILE");
    }
    if (oldDir) {
      setenv("SSL_CERT_DIR", dir.c_str(), 1);
    } else {
      unsetenv("SSL_CERT_DIR");
    }
  }
};
} // namespace

TEST_CASE("TLSVerificationTests", "[tls]") {
  REQUIRE(std::filesystem::exists(certificate("ca.crt")));
  const bool threaded = GENERATE(false, true);
  CAPTURE(threaded);
  DefaultTrust defaults;
  TLSApplication application;
  TLSStores stores;
  Dictionary config;

  SECTION("initiator verifies the server before sending FIX") {
    config.setBool(RESET_ON_DISCONNECT, true);
    const std::string scenario = GENERATE(
        "valid-dns",
        "valid-ip",
        "missing-trust",
        "wrong-dns",
        "wrong-ip",
        "unrelated-root",
        "system-root",
        "revoked",
        "crl-valid",
        "crl-directory-unrelated",
        "crl-directory-valid",
        "crl-directory-revoked",
        "expired");
    CAPTURE(scenario);
    const bool accepted = scenario == "valid-dns" || scenario == "valid-ip" || scenario == "system-root"
                          || scenario == "crl-valid" || scenario == "crl-directory-valid";
    if (scenario != "missing-trust" && scenario != "system-root") {
      config.setString(CERTIFICATE_AUTHORITIES_FILE, certificate("ca.crt"));
    }
    if (scenario == "wrong-ip" || scenario == "valid-ip") {
      config.setString(SOCKET_CONNECT_HOST, "127.0.0.1");
    }
    if (scenario == "revoked" || scenario == "crl-valid") {
      config.setString(CERTIFICATE_REVOCATION_LIST_FILE, certificate("ca.crl"));
    }
    if (scenario.find("crl-directory-") == 0) {
      config.setString(CERTIFICATE_REVOCATION_LIST_DIRECTORY, certificate("crl-directory"));
    }
    std::string leaf = "server";
    if (scenario == "wrong-dns" || scenario == "wrong-ip" || scenario == "expired" || scenario == "revoked") {
      leaf = scenario;
    }
    if (scenario == "crl-directory-revoked") {
      leaf = "revoked";
    }
    if (scenario == "unrelated-root" || scenario == "system-root" || scenario == "crl-directory-unrelated") {
      leaf = "unrelated-server";
    }
    auto context = peerContext(leaf + ".crt", leaf == "unrelated-server" ? "server.key" : leaf + ".key");
    Socket listener{socket_createAcceptor(0, true)};
    REQUIRE(listener.value != INVALID_SOCKET_HANDLE);
    timeout(listener.value);
    SessionID id("FIX.4.2", "TLS-CLIENT", "TLS-SERVER");
    auto settings = settingsFor(false, id, portOf(listener.value), config);
    std::unique_ptr<Initiator> initiator;
    std::function<bool()> disconnected;
    if (threaded) {
      auto *transport = new TestInitiator<ThreadedSSLSocketInitiator>(application, stores, settings);
      disconnected = [&, transport]() { return transport->disconnected(id); };
      initiator.reset(transport);
    } else {
      auto *transport = new TestInitiator<SSLSocketInitiator>(application, stores, settings);
      disconnected = [&, transport]() { return transport->disconnected(id); };
      initiator.reset(transport);
    }
    Session *session = Session::lookupSession(id);
    REQUIRE(session);
    session->setNextSenderMsgSeqNum(7);
    session->setNextTargetMsgSeqNum(9);
    initiator->start();
    Socket socket{socket_accept(listener.value)};
    REQUIRE(socket.value != INVALID_SOCKET_HANDLE);
    timeout(socket.value);
    Secure ssl(SSL_new(context.get()), SSL_free);
    REQUIRE(SSL_set_fd(ssl.get(), socket.value) == 1);
    const int handshake = SSL_accept(ssl.get());
    char buffer[4096];
    const int bytes = handshake == 1 ? SSL_read(ssl.get(), buffer, sizeof(buffer)) : -1;
    SSL_set_quiet_shutdown(ssl.get(), 1);
    shutdown(socket.value, SHUT_RDWR);
    CHECK(waitFor(disconnected));
    initiator->stop(true);
    CHECK((bytes > 0) == accepted);
    CHECK((application.outgoing > 0) == accepted);
    CHECK((stores.refreshes > 0) == accepted);
    if (!accepted) {
      CHECK(session->getExpectedSenderNum() == 7);
      CHECK(session->getExpectedTargetNum() == 9);
    }
    if (bytes > 0) {
      CHECK(identifyType(std::string(buffer, bytes)) == MsgType_Logon);
    }
  }

  SECTION("acceptor authenticates certificate before FIX admission") {
    const std::string scenario = GENERATE(
        "server-only",
        "optional-absent",
        "required-absent",
        "required-valid",
        "wrong-eku",
        "unrelated-root",
        "crl-directory-unrelated",
        "crl-directory-valid",
        "binding-valid",
        "binding-ip",
        "binding-wrong",
        "binding-absent",
        "binding-wildcard",
        "binding-subdomain");
    CAPTURE(scenario);
    bool accepted = scenario == "server-only" || scenario == "optional-absent" || scenario == "required-valid"
                    || scenario == "crl-directory-valid";
#ifdef X509_CHECK_FLAG_NEVER_CHECK_SUBJECT
    accepted = accepted || scenario == "binding-valid" || scenario == "binding-ip";
#endif
    config.setString(CERTIFICATE_AUTHORITIES_FILE, certificate("ca.crt"));
    if (scenario.find("crl-directory-") == 0) {
      config.setString(CERTIFICATE_REVOCATION_LIST_DIRECTORY, certificate("crl-directory"));
    }
    config.setInt(
        CERTIFICATE_VERIFY_LEVEL,
        scenario == "server-only" ? 0 : (scenario == "optional-absent" || scenario == "binding-absent" ? 2 : 1));
    if (scenario.find("binding-") == 0) {
      config.setString(
          "CertificateAcceptedPeerName",
          scenario == "binding-wrong"       ? "other.example"
          : scenario == "binding-ip"        ? "127.0.0.1"
          : scenario == "binding-subdomain" ? ".example"
                                            : "client.example");
    }
    int port;
    {
      Socket reserved{socket_createAcceptor(0, true)};
      REQUIRE(reserved.value != INVALID_SOCKET_HANDLE);
      port = portOf(reserved.value);
    }
    SessionID id("FIX.4.2", "TLS-SERVER", "TLS-CLIENT");
    auto settings = settingsFor(true, id, port, config);
    std::unique_ptr<Acceptor> acceptor;
    if (threaded) {
      acceptor.reset(new ThreadedSSLSocketAcceptor(application, stores, settings));
    } else {
      acceptor.reset(new SSLSocketAcceptor(application, stores, settings));
    }
    Session *session = Session::lookupSession(id);
    REQUIRE(session);
    session->setNextSenderMsgSeqNum(7);
    session->setNextTargetMsgSeqNum(9);
    std::string leaf = "client";
    if (scenario == "server-only" || scenario == "optional-absent" || scenario == "required-absent"
        || scenario == "binding-absent") {
      leaf.clear();
    }
    if (scenario == "wrong-eku") {
      leaf = "wrong-client-eku";
    }
    if (scenario == "unrelated-root" || scenario == "crl-directory-unrelated") {
      leaf = "unrelated-client";
    }
    if (scenario == "binding-wildcard") {
      leaf = "wildcard-client";
    }
    auto context
        = peerContext(leaf.empty() ? "" : leaf + ".crt", leaf == "unrelated-client" ? "client.key" : leaf + ".key");
    acceptor->start();
    Socket socket{socket_createConnector()};
    timeout(socket.value);
    REQUIRE(socket_connect(socket.value, "127.0.0.1", port) >= 0);
    Secure ssl(SSL_new(context.get()), SSL_free);
    REQUIRE(SSL_set_fd(ssl.get(), socket.value) == 1);
    int bytes = -1;
    if (SSL_connect(ssl.get()) == 1) {
      FIX42::Logon logon(EncryptMethod(0), HeartBtInt(30));
      logon.setField(ResetSeqNumFlag(true));
      logon.getHeader().setField(SenderCompID("TLS-CLIENT"));
      logon.getHeader().setField(TargetCompID("TLS-SERVER"));
      logon.getHeader().setField(MsgSeqNum(1));
      logon.getHeader().setField(SendingTime(UtcTimeStamp::now()));
      const auto wire = logon.toString();
      if (SSL_write(ssl.get(), wire.data(), static_cast<int>(wire.size())) > 0) {
        char buffer[4096];
        bytes = SSL_read(ssl.get(), buffer, sizeof(buffer));
      }
    }
    CHECK((bytes > 0) == accepted);
    CHECK(application.authentication == (accepted ? 1 : 0));
    CHECK((stores.refreshes > 0) == accepted);
    if (!accepted) {
      CHECK(session->getExpectedSenderNum() == 7);
      CHECK(session->getExpectedTargetNum() == 9);
    }
    CHECK_FALSE(application.prematureRegistration);
    shutdown(socket.value, SHUT_RDWR);
    CHECK(waitFor([&]() { return !Session::isSessionRegistered(id); }));
    acceptor->stop(true);
    CHECK_FALSE(Session::isSessionRegistered(id));
  }

  SECTION("invalid security settings fail startup in all transports") {
    const bool server = GENERATE(false, true);
    const std::string scenario = GENERATE(
        "level-3",
        "level-negative",
        "level-text",
        "protocol",
        "old-protocol",
        "crl-missing",
        "ca-missing",
        "ca-empty");
    CAPTURE(server, scenario);
    config.setString(CERTIFICATE_AUTHORITIES_FILE, certificate("ca.crt"));
    if (scenario == "level-3") {
      config.setString(CERTIFICATE_VERIFY_LEVEL, "3");
    }
    if (scenario == "level-negative") {
      config.setString(CERTIFICATE_VERIFY_LEVEL, "-1");
    }
    if (scenario == "level-text") {
      config.setString(CERTIFICATE_VERIFY_LEVEL, "invalid");
    }
    if (scenario == "protocol") {
      config.setString(SSL_PROTOCOL, "TLSv1_2garbage");
    }
    if (scenario == "old-protocol") {
      config.setString(SSL_PROTOCOL, "TLSv1");
    }
    if (scenario == "crl-missing") {
      config.setString(CERTIFICATE_REVOCATION_LIST_FILE, certificate("missing.crl"));
    }
    if (scenario == "ca-missing") {
      config.setString(CERTIFICATE_AUTHORITIES_FILE, certificate("missing.crt"));
    }
    if (scenario == "ca-empty") {
      config.setString(CERTIFICATE_AUTHORITIES_FILE, "");
      config.setInt(CERTIFICATE_VERIFY_LEVEL, 1);
    }
    const auto settings = settingsFor(server, SessionID("FIX.4.2", "TLS-STARTUP", "PEER"), 0, config);
    const auto exercise = [&](auto &transport) {
      CHECK_THROWS(transport.start());
      transport.stop(true);
    };
    if (server && threaded) {
      ThreadedSSLSocketAcceptor transport(application, stores, settings);
      exercise(transport);
    } else if (server) {
      SSLSocketAcceptor transport(application, stores, settings);
      exercise(transport);
    } else if (threaded) {
      ThreadedSSLSocketInitiator transport(application, stores, settings);
      exercise(transport);
    } else {
      SSLSocketInitiator transport(application, stores, settings);
      exercise(transport);
    }
  }
}
#endif
