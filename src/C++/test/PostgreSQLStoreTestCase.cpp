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

#ifdef HAVE_POSTGRESQL

#include "MessageStoreTestCase.h"
#include "TestHelper.h"
#include <PostgreSQLLog.h>
#include <PostgreSQLStore.h>
#include <Session.h>

#include <memory>

#include "catch_amalgamated.hpp"

using namespace FIX;

struct postgreSQLStoreFixture {
  postgreSQLStoreFixture(bool reset)
      : factory(TestSettings::sessionSettings.get()) {
    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));

    try {
      object.reset(factory.create(UtcTimeStamp::now(), sessionID));
    } catch (std::exception &e) {
      std::cerr << e.what() << std::endl;
      throw;
    }

    if (reset) {
      object->reset(UtcTimeStamp::now());
    }

    this->resetAfter = reset;
  }

  PostgreSQLStoreFactory factory;
  std::unique_ptr<MessageStore> object;
  bool resetAfter;
};

struct noResetPostgreSQLStoreFixture : postgreSQLStoreFixture {
  noResetPostgreSQLStoreFixture()
      : postgreSQLStoreFixture(false) {}
};

struct resetPostgreSQLStoreFixture : postgreSQLStoreFixture {
  resetPostgreSQLStoreFixture()
      : postgreSQLStoreFixture(true) {}
};

TEST_CASE_METHOD(resetPostgreSQLStoreFixture, "resetPostgreSQLStoreTests"){
    SECTION("setGet"){CHECK_MESSAGE_STORE_SET_GET}

    SECTION("setGetUint64"){CHECK_MESSAGE_STORE_SET_GET_UINT64}

    SECTION("setGetWithQuote"){CHECK_MESSAGE_STORE_SET_GET_WITH_QUOTE}

    SECTION("other"){CHECK_MESSAGE_STORE_OTHER}

    SECTION("otherUint64"){CHECK_MESSAGE_STORE_OTHER_UINT64}

    SET_SEQUENCE_NUMBERS}

TEST_CASE_METHOD(noResetPostgreSQLStoreFixture, "noResetPostgreSQLStoreTests") {
  SECTION("reload"){CHECK_MESSAGE_STORE_RELOAD}

  SECTION("refresh") {
    CHECK_MESSAGE_STORE_RELOAD
  }
}

TEST_CASE_METHOD(
    resetPostgreSQLStoreFixture,
    "PostgreSQL duplicate update preserves message and unrelated row",
    "[postgresql][database-update]") {
  const char replacementBytes[] = {'a', '\'', '"', '\\', '\r', '\n', '\1', '\xc3', '\xa9'};
  const std::string replacement(replacementBytes, sizeof(replacementBytes));
  REQUIRE(object->set(101, "safe"));
  REQUIRE(object->set(102, "old"));

  const Dictionary &settings = TestSettings::sessionSettings.get();
  const std::vector<std::pair<SessionID, std::string>> protectedRows{
      {SessionID("FIX.4.1", "SETGET", "TEST"), "begin"},
      {SessionID("FIX.4.2", "SETGET-OTHER", "TEST"), "sender"},
      {SessionID("FIX.4.2", "SETGET", "TEST-OTHER"), "target"},
      {SessionID("FIX.4.2", "SETGET", "TEST", "OTHER"), "qualifier"}};
  PostgreSQLStoreFactory otherFactory(settings);
  std::vector<std::unique_ptr<MessageStore>> protectedStores;
  for (const auto &[id, value] : protectedRows) {
    std::unique_ptr<MessageStore> store(otherFactory.create(UtcTimeStamp::now(), id));
    store->reset(UtcTimeStamp::now());
    REQUIRE(store->set(102, value));
    protectedStores.push_back(std::move(store));
  }

  REQUIRE(object->set(102, replacement));

  std::vector<std::string> messages;
  object->get(101, 102, messages);
  REQUIRE(messages.size() == 2);
  CHECK(messages[0] == "safe");
  CHECK(messages[1] == replacement);

  for (size_t index = 0; index != protectedRows.size(); ++index) {
    std::vector<std::string> messages;
    protectedStores[index]->get(102, 102, messages);
    REQUIRE(messages.size() == 1);
    CHECK(messages[0] == protectedRows[index].second);
  }
}

static int postgresDatabaseConnectionCount(const std::string &database = "postgres") {
  const Dictionary &settings = TestSettings::sessionSettings.get();
  short port = PostgreSQLStoreFactory::DEFAULT_PORT;
  if (settings.has(POSTGRESQL_STORE_PORT)) {
    port = static_cast<short>(settings.getInt(POSTGRESQL_STORE_PORT));
  }
  PostgreSQLConnection connection(
      settings.getString(POSTGRESQL_STORE_DATABASE),
      settings.getString(POSTGRESQL_STORE_USER),
      settings.getString(POSTGRESQL_STORE_PASSWORD),
      settings.getString(POSTGRESQL_STORE_HOST),
      port);
  PostgreSQLQuery query(
      "SELECT COUNT(*) FROM pg_stat_activity WHERE datname='" + database + "' AND usename=current_user");
  REQUIRE(connection.execute(query));
  REQUIRE(query.rows() == 1);
  return std::stoi(query.getValue(0, 0));
}

TEST_CASE("PostgreSQL log preserves supported special bytes", "[postgresql][database-log]") {
  const Dictionary &settings = TestSettings::sessionSettings.get();
  short port = PostgreSQLLogFactory::DEFAULT_PORT;
  if (settings.has(POSTGRESQL_STORE_PORT)) {
    port = static_cast<short>(settings.getInt(POSTGRESQL_STORE_PORT));
  }
  const SessionID sessionID("FIX.4.2", "LOG-BYTES", "TEST");
  PostgreSQLLog log(
      sessionID,
      settings.getString(POSTGRESQL_STORE_DATABASE),
      settings.getString(POSTGRESQL_STORE_USER),
      settings.getString(POSTGRESQL_STORE_PASSWORD),
      settings.getString(POSTGRESQL_STORE_HOST),
      port);
  log.clear();
  const char valueBytes[] = {'a', '\'', '"', '\\', '\r', '\n', '\1', '\x7f', '\xc3', '\xa9'};
  log.onEvent(std::string(valueBytes, sizeof(valueBytes)));

  PostgreSQLConnection connection(
      settings.getString(POSTGRESQL_STORE_DATABASE),
      settings.getString(POSTGRESQL_STORE_USER),
      settings.getString(POSTGRESQL_STORE_PASSWORD),
      settings.getString(POSTGRESQL_STORE_HOST),
      port);
  PostgreSQLQuery query(
      "SELECT encode(convert_to(text, 'UTF8'), 'hex') FROM event_log WHERE beginstring='FIX.4.2' "
      "AND sendercompid='LOG-BYTES' AND targetcompid='TEST' ORDER BY id DESC LIMIT 1");
  REQUIRE(connection.execute(query));
  REQUIRE(query.rows() == 1);
  CHECK(std::string(query.getValue(0, 0)) == "6127225c0d0a017fc3a9");
}

TEST_CASE("PostgreSQL database construction releases ownership on failure", "[postgresql][database-ownership]") {
  const Dictionary &settings = TestSettings::sessionSettings.get();
  const std::string user = settings.getString(POSTGRESQL_STORE_USER);
  const std::string password = settings.getString(POSTGRESQL_STORE_PASSWORD);
  const std::string host = settings.getString(POSTGRESQL_STORE_HOST);
  short port = PostgreSQLStoreFactory::DEFAULT_PORT;
  if (settings.has(POSTGRESQL_STORE_PORT)) {
    port = static_cast<short>(settings.getInt(POSTGRESQL_STORE_PORT));
  }
  const SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("OWNERSHIP"), TargetCompID("TEST"));

  SECTION("direct store releases connection when cache population fails") {
    const int before = postgresDatabaseConnectionCount();
    CHECK_THROWS_AS(
        std::make_unique<PostgreSQLStore>(UtcTimeStamp::now(), sessionID, "postgres", user, password, host, port),
        ConfigError);
    CHECK(postgresDatabaseConnectionCount() == before);
  }

  SECTION("pooled store releases connection when cache population fails") {
    const int before = postgresDatabaseConnectionCount();
    PostgreSQLConnectionPool pool(true);
    const DatabaseConnectionID id("postgres", user, password, host, port);
    CHECK_THROWS_AS(std::make_unique<PostgreSQLStore>(UtcTimeStamp::now(), sessionID, id, &pool), ConfigError);
    CHECK(postgresDatabaseConnectionCount() == before);
  }

  SECTION("failed native and log connections remain repeatable") {
    const std::string missingDatabase = "quickfix_missing_raii_database";
    for (int attempt = 0; attempt != 3; ++attempt) {
      CHECK_THROWS_AS(PostgreSQLConnection(missingDatabase, user, password, host, port), ConfigError);
    }
    CHECK_THROWS_AS(
        std::make_unique<PostgreSQLLog>(sessionID, missingDatabase, user, password, host, port),
        ConfigError);
    CHECK_THROWS_AS(std::make_unique<PostgreSQLLog>(missingDatabase, user, password, host, port), ConfigError);

    Dictionary logSettings;
    logSettings.setString(POSTGRESQL_LOG_DATABASE, missingDatabase);
    logSettings.setString(POSTGRESQL_LOG_USER, user);
    logSettings.setString(POSTGRESQL_LOG_PASSWORD, password);
    logSettings.setString(POSTGRESQL_LOG_HOST, host);
    logSettings.setInt(POSTGRESQL_LOG_PORT, port);
    logSettings.setString(CONNECTION_TYPE, "initiator");
    SessionSettings sessionSettings;
    sessionSettings.set(logSettings);
    sessionSettings.set(sessionID, logSettings);
    PostgreSQLLogFactory logFactory(sessionSettings);
    CHECK_THROWS_AS(std::unique_ptr<Log>(logFactory.create()), ConfigError);
    CHECK_THROWS_AS(std::unique_ptr<Log>(logFactory.create(sessionID)), ConfigError);
  }

  SECTION("session destroys valid store when log creation fails") {
    const std::string database = settings.getString(POSTGRESQL_STORE_DATABASE);
    const int before = postgresDatabaseConnectionCount(database);
    PostgreSQLStoreFactory storeFactory(settings);

    Dictionary logSettings;
    logSettings.setString(POSTGRESQL_LOG_DATABASE, "quickfix_missing_raii_database");
    logSettings.setString(POSTGRESQL_LOG_USER, user);
    logSettings.setString(POSTGRESQL_LOG_PASSWORD, password);
    logSettings.setString(POSTGRESQL_LOG_HOST, host);
    logSettings.setInt(POSTGRESQL_LOG_PORT, port);
    logSettings.setString(CONNECTION_TYPE, "initiator");
    SessionSettings sessionSettings;
    sessionSettings.set(sessionID, logSettings);
    PostgreSQLLogFactory logFactory(sessionSettings);
    TestApplication application;
    DataDictionaryProvider dictionaries;

    CHECK(Session::lookupSession(sessionID) == nullptr);
    CHECK_THROWS_AS(
        std::make_unique<Session>(
            []() { return UtcTimeStamp::now(); },
            application,
            storeFactory,
            sessionID,
            dictionaries,
            TimeRange(UtcTimeOnly(), UtcTimeOnly()),
            0,
            &logFactory),
        ConfigError);
    CHECK(postgresDatabaseConnectionCount(database) == before);
    CHECK(Session::lookupSession(sessionID) == nullptr);
  }
}

TEST_CASE("PostgreSQL database factories require explicit credentials", "[postgresql][database-security]") {
  const SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("CREDENTIALS"), TargetCompID("TEST"));

  SECTION("store requires user") {
    Dictionary settings;
    settings.setString(POSTGRESQL_STORE_PASSWORD, "");
    PostgreSQLStoreFactory factory(settings);
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(factory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(POSTGRESQL_STORE_USER));
  }

  SECTION("store rejects empty user") {
    Dictionary settings;
    settings.setString(POSTGRESQL_STORE_USER, "");
    settings.setString(POSTGRESQL_STORE_PASSWORD, "");
    PostgreSQLStoreFactory factory(settings);
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(factory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(POSTGRESQL_STORE_USER));
  }

  SECTION("store requires password") {
    Dictionary settings;
    settings.setString(POSTGRESQL_STORE_USER, "quickfix");
    PostgreSQLStoreFactory factory(settings);
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(factory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(POSTGRESQL_STORE_PASSWORD));
  }

  SECTION("store direct constructors reject empty user") {
    PostgreSQLStoreFactory defaultFactory;
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(defaultFactory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(POSTGRESQL_STORE_USER));
    PostgreSQLStoreFactory explicitFactory("quickfix", "", "", "localhost", 5432);
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(explicitFactory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(POSTGRESQL_STORE_USER));
  }

  SECTION("log requires user") {
    Dictionary settings;
    settings.setString(POSTGRESQL_LOG_PASSWORD, "");
    SessionSettings sessionSettings;
    sessionSettings.set(settings);
    PostgreSQLLogFactory factory(sessionSettings);
    CHECK_THROWS_WITH(std::unique_ptr<Log>(factory.create()), Catch::Matchers::ContainsSubstring(POSTGRESQL_LOG_USER));
  }

  SECTION("log rejects empty user") {
    Dictionary settings;
    settings.setString(POSTGRESQL_LOG_USER, "");
    settings.setString(POSTGRESQL_LOG_PASSWORD, "");
    SessionSettings sessionSettings;
    sessionSettings.set(settings);
    PostgreSQLLogFactory factory(sessionSettings);
    CHECK_THROWS_WITH(std::unique_ptr<Log>(factory.create()), Catch::Matchers::ContainsSubstring(POSTGRESQL_LOG_USER));
  }

  SECTION("log requires password") {
    Dictionary settings;
    settings.setString(POSTGRESQL_LOG_USER, "quickfix");
    SessionSettings sessionSettings;
    sessionSettings.set(settings);
    PostgreSQLLogFactory factory(sessionSettings);
    CHECK_THROWS_WITH(
        std::unique_ptr<Log>(factory.create()),
        Catch::Matchers::ContainsSubstring(POSTGRESQL_LOG_PASSWORD));
  }

  SECTION("log direct constructors reject empty user") {
    PostgreSQLLogFactory defaultFactory;
    CHECK_THROWS_WITH(
        std::unique_ptr<Log>(defaultFactory.create()),
        Catch::Matchers::ContainsSubstring(POSTGRESQL_LOG_USER));
    PostgreSQLLogFactory explicitFactory("quickfix", "", "", "localhost", 5432);
    CHECK_THROWS_WITH(
        std::unique_ptr<Log>(explicitFactory.create()),
        Catch::Matchers::ContainsSubstring(POSTGRESQL_LOG_USER));
  }

  SECTION("settings and direct empty passwords connect") {
    const Dictionary &storeSettings = TestSettings::sessionSettings.get();
    PostgreSQLStoreFactory storeFactory(storeSettings);
    std::unique_ptr<MessageStore> store(storeFactory.create(UtcTimeStamp::now(), sessionID));
    REQUIRE(store != nullptr);

    short port = PostgreSQLStoreFactory::DEFAULT_PORT;
    if (storeSettings.has(POSTGRESQL_STORE_PORT)) {
      port = static_cast<short>(storeSettings.getInt(POSTGRESQL_STORE_PORT));
    }
    PostgreSQLStoreFactory directStoreFactory(
        storeSettings.getString(POSTGRESQL_STORE_DATABASE),
        storeSettings.getString(POSTGRESQL_STORE_USER),
        "",
        storeSettings.getString(POSTGRESQL_STORE_HOST),
        port);
    std::unique_ptr<MessageStore> directStore(directStoreFactory.create(UtcTimeStamp::now(), sessionID));
    REQUIRE(directStore != nullptr);

    Dictionary logSettings;
    logSettings.setString(POSTGRESQL_LOG_DATABASE, storeSettings.getString(POSTGRESQL_STORE_DATABASE));
    logSettings.setString(POSTGRESQL_LOG_USER, storeSettings.getString(POSTGRESQL_STORE_USER));
    logSettings.setString(POSTGRESQL_LOG_PASSWORD, "");
    logSettings.setString(POSTGRESQL_LOG_HOST, storeSettings.getString(POSTGRESQL_STORE_HOST));
    if (storeSettings.has(POSTGRESQL_STORE_PORT)) {
      logSettings.setInt(POSTGRESQL_LOG_PORT, storeSettings.getInt(POSTGRESQL_STORE_PORT));
    }
    SessionSettings sessionSettings;
    sessionSettings.set(logSettings);
    PostgreSQLLogFactory logFactory(sessionSettings);
    std::unique_ptr<Log> log(logFactory.create());
    REQUIRE(log != nullptr);

    PostgreSQLLogFactory directLogFactory(
        storeSettings.getString(POSTGRESQL_STORE_DATABASE),
        storeSettings.getString(POSTGRESQL_STORE_USER),
        "",
        storeSettings.getString(POSTGRESQL_STORE_HOST),
        port);
    std::unique_ptr<Log> directLog(directLogFactory.create());
    REQUIRE(directLog != nullptr);
  }
}

#endif
