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

#ifdef HAVE_MYSQL

#include "MessageStoreTestCase.h"
#include "TestHelper.h"
#include <MySQLLog.h>
#include <MySQLStore.h>
#include <Session.h>

#include <memory>

#include "catch_amalgamated.hpp"

using namespace FIX;

struct mySQLStoreFixture {
  mySQLStoreFixture(bool reset)
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

  MySQLStoreFactory factory;
  std::unique_ptr<MessageStore> object;
  bool resetAfter;
};

struct noResetMySQLStoreFixture : mySQLStoreFixture {
  noResetMySQLStoreFixture()
      : mySQLStoreFixture(false) {}
};

struct resetMySQLStoreFixture : mySQLStoreFixture {
  resetMySQLStoreFixture()
      : mySQLStoreFixture(true) {}
};

TEST_CASE_METHOD(resetMySQLStoreFixture, "resetMySQLStoreTests"){
    SECTION("setGet"){CHECK_MESSAGE_STORE_SET_GET}

    SECTION("setGetWithQuote"){CHECK_MESSAGE_STORE_SET_GET_WITH_QUOTE}

    SECTION("setGetUint64"){CHECK_MESSAGE_STORE_SET_GET_UINT64}

    SECTION("other"){CHECK_MESSAGE_STORE_OTHER}

    SECTION("otherUint64"){CHECK_MESSAGE_STORE_OTHER_UINT64}

    SET_SEQUENCE_NUMBERS}

TEST_CASE_METHOD(noResetMySQLStoreFixture, "noResetMySQLStoreTests") {
  SECTION("reload"){CHECK_MESSAGE_STORE_RELOAD}

  SECTION("refresh") {
    CHECK_MESSAGE_STORE_RELOAD
  }
}

TEST_CASE_METHOD(
    resetMySQLStoreFixture,
    "MySQL duplicate update preserves binary message and unrelated row",
    "[mysql][database-update]") {
  const char replacementBytes[] = {'a', '\'', '"', '\\', '\r', '\n', '\1', '\0', '\x7f', '\xc3', '\xa9'};
  const std::string replacement(replacementBytes, sizeof(replacementBytes));
  REQUIRE(object->set(101, "safe"));
  REQUIRE(object->set(102, "old"));

  const Dictionary &settings = TestSettings::sessionSettings.get();
  const std::vector<std::pair<SessionID, std::string>> protectedRows{
      {SessionID("FIX.4.1", "SETGET", "TEST"), "begin"},
      {SessionID("FIX.4.2", "SETGET-OTHER", "TEST"), "sender"},
      {SessionID("FIX.4.2", "SETGET", "TEST-OTHER"), "target"},
      {SessionID("FIX.4.2", "SETGET", "TEST", "OTHER"), "qualifier"}};
  MySQLStoreFactory otherFactory(settings);
  std::vector<std::unique_ptr<MessageStore>> protectedStores;
  for (const auto &[id, value] : protectedRows) {
    std::unique_ptr<MessageStore> store(otherFactory.create(UtcTimeStamp::now(), id));
    store->reset(UtcTimeStamp::now());
    REQUIRE(store->set(102, value));
    protectedStores.push_back(std::move(store));
  }

  REQUIRE(object->set(102, replacement));

  short port = MySQLStoreFactory::DEFAULT_PORT;
  if (settings.has(MYSQL_STORE_PORT)) {
    port = static_cast<short>(settings.getInt(MYSQL_STORE_PORT));
  }
  MySQLConnection connection(
      settings.getString(MYSQL_STORE_DATABASE),
      settings.getString(MYSQL_STORE_USER),
      settings.getString(MYSQL_STORE_PASSWORD),
      settings.getString(MYSQL_STORE_HOST),
      port);
  MySQLQuery query(
      "SELECT msgseqnum, HEX(message) FROM messages WHERE beginstring=\"FIX.4.2\" "
      "AND sendercompid=\"SETGET\" AND targetcompid=\"TEST\" AND session_qualifier=\"\" "
      "AND msgseqnum IN (101, 102) ORDER BY msgseqnum");
  REQUIRE(connection.execute(query));
  REQUIRE(query.rows() == 2);
  CHECK(std::string(query.getValue(0, 0)) == "101");
  CHECK(std::string(query.getValue(0, 1)) == "73616665");
  CHECK(std::string(query.getValue(1, 0)) == "102");
  CHECK(std::string(query.getValue(1, 1)) == "6127225C0D0A01007FC3A9");

  for (size_t index = 0; index != protectedRows.size(); ++index) {
    std::vector<std::string> messages;
    protectedStores[index]->get(102, 102, messages);
    REQUIRE(messages.size() == 1);
    CHECK(messages[0] == protectedRows[index].second);
  }
}

static int mysqlDatabaseConnectionCount(const std::string &database = "information_schema") {
  const Dictionary &settings = TestSettings::sessionSettings.get();
  short port = MySQLStoreFactory::DEFAULT_PORT;
  if (settings.has(MYSQL_STORE_PORT)) {
    port = static_cast<short>(settings.getInt(MYSQL_STORE_PORT));
  }
  MySQLConnection connection(
      settings.getString(MYSQL_STORE_DATABASE),
      settings.getString(MYSQL_STORE_USER),
      settings.getString(MYSQL_STORE_PASSWORD),
      settings.getString(MYSQL_STORE_HOST),
      port);
  MySQLQuery query("SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE DB='" + database + "'");
  REQUIRE(connection.execute(query));
  REQUIRE(query.rows() == 1);
  return std::stoi(query.getValue(0, 0));
}

TEST_CASE("MySQL log preserves supported special bytes", "[mysql][database-log]") {
  const Dictionary &settings = TestSettings::sessionSettings.get();
  short port = MySQLLogFactory::DEFAULT_PORT;
  if (settings.has(MYSQL_STORE_PORT)) {
    port = static_cast<short>(settings.getInt(MYSQL_STORE_PORT));
  }
  const SessionID sessionID("FIX.4.2", "LOG-BYTES", "TEST");
  MySQLLog log(
      sessionID,
      settings.getString(MYSQL_STORE_DATABASE),
      settings.getString(MYSQL_STORE_USER),
      settings.getString(MYSQL_STORE_PASSWORD),
      settings.getString(MYSQL_STORE_HOST),
      port);
  log.clear();
  const char valueBytes[] = {'a', '\'', '"', '\\', '\r', '\n', '\1', '\0', '\x7f', '\xc3', '\xa9'};
  log.onEvent(std::string(valueBytes, sizeof(valueBytes)));

  MySQLConnection connection(
      settings.getString(MYSQL_STORE_DATABASE),
      settings.getString(MYSQL_STORE_USER),
      settings.getString(MYSQL_STORE_PASSWORD),
      settings.getString(MYSQL_STORE_HOST),
      port);
  MySQLQuery query(
      "SELECT HEX(text) FROM event_log WHERE beginstring=\"FIX.4.2\" AND sendercompid=\"LOG-BYTES\" "
      "AND targetcompid=\"TEST\" ORDER BY id DESC LIMIT 1");
  REQUIRE(connection.execute(query));
  REQUIRE(query.rows() == 1);
  CHECK(std::string(query.getValue(0, 0)) == "6127225C0D0A01007FC3A9");
}

TEST_CASE("MySQL database construction releases ownership on failure", "[mysql][database-ownership]") {
  const Dictionary &settings = TestSettings::sessionSettings.get();
  const std::string user = settings.getString(MYSQL_STORE_USER);
  const std::string password = settings.getString(MYSQL_STORE_PASSWORD);
  const std::string host = settings.getString(MYSQL_STORE_HOST);
  short port = MySQLStoreFactory::DEFAULT_PORT;
  if (settings.has(MYSQL_STORE_PORT)) {
    port = static_cast<short>(settings.getInt(MYSQL_STORE_PORT));
  }
  const SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("OWNERSHIP"), TargetCompID("TEST"));

  SECTION("direct store releases connection when cache population fails") {
    const int before = mysqlDatabaseConnectionCount();
    CHECK_THROWS_AS(
        std::make_unique<MySQLStore>(UtcTimeStamp::now(), sessionID, "information_schema", user, password, host, port),
        ConfigError);
    CHECK(mysqlDatabaseConnectionCount() == before);
  }

  SECTION("pooled store releases connection when cache population fails") {
    const int before = mysqlDatabaseConnectionCount();
    MySQLConnectionPool pool(true);
    const DatabaseConnectionID id("information_schema", user, password, host, port);
    CHECK_THROWS_AS(std::make_unique<MySQLStore>(UtcTimeStamp::now(), sessionID, id, &pool), ConfigError);
    CHECK(mysqlDatabaseConnectionCount() == before);
  }

  SECTION("failed native and log connections remain repeatable") {
    const std::string missingDatabase = "quickfix_missing_raii_database";
    for (int attempt = 0; attempt != 3; ++attempt) {
      CHECK_THROWS_AS(MySQLConnection(missingDatabase, user, password, host, port), ConfigError);
    }
    CHECK_THROWS_AS(std::make_unique<MySQLLog>(sessionID, missingDatabase, user, password, host, port), ConfigError);
    CHECK_THROWS_AS(std::make_unique<MySQLLog>(missingDatabase, user, password, host, port), ConfigError);

    Dictionary logSettings;
    logSettings.setString(MYSQL_LOG_DATABASE, missingDatabase);
    logSettings.setString(MYSQL_LOG_USER, user);
    logSettings.setString(MYSQL_LOG_PASSWORD, password);
    logSettings.setString(MYSQL_LOG_HOST, host);
    logSettings.setInt(MYSQL_LOG_PORT, port);
    logSettings.setString(CONNECTION_TYPE, "initiator");
    SessionSettings sessionSettings;
    sessionSettings.set(logSettings);
    sessionSettings.set(sessionID, logSettings);
    MySQLLogFactory logFactory(sessionSettings);
    CHECK_THROWS_AS(std::unique_ptr<Log>(logFactory.create()), ConfigError);
    CHECK_THROWS_AS(std::unique_ptr<Log>(logFactory.create(sessionID)), ConfigError);
  }

  SECTION("session destroys valid store when log creation fails") {
    const std::string database = settings.getString(MYSQL_STORE_DATABASE);
    const int before = mysqlDatabaseConnectionCount(database);
    MySQLStoreFactory storeFactory(settings);

    Dictionary logSettings;
    logSettings.setString(MYSQL_LOG_DATABASE, "quickfix_missing_raii_database");
    logSettings.setString(MYSQL_LOG_USER, user);
    logSettings.setString(MYSQL_LOG_PASSWORD, password);
    logSettings.setString(MYSQL_LOG_HOST, host);
    logSettings.setInt(MYSQL_LOG_PORT, port);
    logSettings.setString(CONNECTION_TYPE, "initiator");
    SessionSettings sessionSettings;
    sessionSettings.set(sessionID, logSettings);
    MySQLLogFactory logFactory(sessionSettings);
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
    CHECK(mysqlDatabaseConnectionCount(database) == before);
    CHECK(Session::lookupSession(sessionID) == nullptr);
  }
}

TEST_CASE("MySQL database factories require explicit credentials", "[mysql][database-security]") {
  const SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("CREDENTIALS"), TargetCompID("TEST"));

  SECTION("store requires user") {
    Dictionary settings;
    settings.setString(MYSQL_STORE_PASSWORD, "");
    MySQLStoreFactory factory(settings);
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(factory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(MYSQL_STORE_USER));
  }

  SECTION("store rejects empty user") {
    Dictionary settings;
    settings.setString(MYSQL_STORE_USER, "");
    settings.setString(MYSQL_STORE_PASSWORD, "");
    MySQLStoreFactory factory(settings);
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(factory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(MYSQL_STORE_USER));
  }

  SECTION("store requires password") {
    Dictionary settings;
    settings.setString(MYSQL_STORE_USER, "quickfix");
    MySQLStoreFactory factory(settings);
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(factory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(MYSQL_STORE_PASSWORD));
  }

  SECTION("store direct constructors reject empty user") {
    MySQLStoreFactory defaultFactory;
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(defaultFactory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(MYSQL_STORE_USER));
    MySQLStoreFactory explicitFactory("quickfix", "", "", "localhost", 3306);
    CHECK_THROWS_WITH(
        std::unique_ptr<MessageStore>(explicitFactory.create(UtcTimeStamp::now(), sessionID)),
        Catch::Matchers::ContainsSubstring(MYSQL_STORE_USER));
  }

  SECTION("log requires user") {
    Dictionary settings;
    settings.setString(MYSQL_LOG_PASSWORD, "");
    SessionSettings sessionSettings;
    sessionSettings.set(settings);
    MySQLLogFactory factory(sessionSettings);
    CHECK_THROWS_WITH(std::unique_ptr<Log>(factory.create()), Catch::Matchers::ContainsSubstring(MYSQL_LOG_USER));
  }

  SECTION("log rejects empty user") {
    Dictionary settings;
    settings.setString(MYSQL_LOG_USER, "");
    settings.setString(MYSQL_LOG_PASSWORD, "");
    SessionSettings sessionSettings;
    sessionSettings.set(settings);
    MySQLLogFactory factory(sessionSettings);
    CHECK_THROWS_WITH(std::unique_ptr<Log>(factory.create()), Catch::Matchers::ContainsSubstring(MYSQL_LOG_USER));
  }

  SECTION("log requires password") {
    Dictionary settings;
    settings.setString(MYSQL_LOG_USER, "quickfix");
    SessionSettings sessionSettings;
    sessionSettings.set(settings);
    MySQLLogFactory factory(sessionSettings);
    CHECK_THROWS_WITH(std::unique_ptr<Log>(factory.create()), Catch::Matchers::ContainsSubstring(MYSQL_LOG_PASSWORD));
  }

  SECTION("log direct constructors reject empty user") {
    MySQLLogFactory defaultFactory;
    CHECK_THROWS_WITH(
        std::unique_ptr<Log>(defaultFactory.create()),
        Catch::Matchers::ContainsSubstring(MYSQL_LOG_USER));
    MySQLLogFactory explicitFactory("quickfix", "", "", "localhost", 3306);
    CHECK_THROWS_WITH(
        std::unique_ptr<Log>(explicitFactory.create()),
        Catch::Matchers::ContainsSubstring(MYSQL_LOG_USER));
  }

  SECTION("settings and direct empty passwords connect") {
    const Dictionary &storeSettings = TestSettings::sessionSettings.get();
    MySQLStoreFactory storeFactory(storeSettings);
    std::unique_ptr<MessageStore> store(storeFactory.create(UtcTimeStamp::now(), sessionID));
    REQUIRE(store != nullptr);

    short port = MySQLStoreFactory::DEFAULT_PORT;
    if (storeSettings.has(MYSQL_STORE_PORT)) {
      port = static_cast<short>(storeSettings.getInt(MYSQL_STORE_PORT));
    }
    MySQLStoreFactory directStoreFactory(
        storeSettings.getString(MYSQL_STORE_DATABASE),
        storeSettings.getString(MYSQL_STORE_USER),
        "",
        storeSettings.getString(MYSQL_STORE_HOST),
        port);
    std::unique_ptr<MessageStore> directStore(directStoreFactory.create(UtcTimeStamp::now(), sessionID));
    REQUIRE(directStore != nullptr);

    Dictionary logSettings;
    logSettings.setString(MYSQL_LOG_DATABASE, storeSettings.getString(MYSQL_STORE_DATABASE));
    logSettings.setString(MYSQL_LOG_USER, storeSettings.getString(MYSQL_STORE_USER));
    logSettings.setString(MYSQL_LOG_PASSWORD, "");
    logSettings.setString(MYSQL_LOG_HOST, storeSettings.getString(MYSQL_STORE_HOST));
    if (storeSettings.has(MYSQL_STORE_PORT)) {
      logSettings.setInt(MYSQL_LOG_PORT, storeSettings.getInt(MYSQL_STORE_PORT));
    }
    SessionSettings sessionSettings;
    sessionSettings.set(logSettings);
    MySQLLogFactory logFactory(sessionSettings);
    std::unique_ptr<Log> log(logFactory.create());
    REQUIRE(log != nullptr);

    MySQLLogFactory directLogFactory(
        storeSettings.getString(MYSQL_STORE_DATABASE),
        storeSettings.getString(MYSQL_STORE_USER),
        "",
        storeSettings.getString(MYSQL_STORE_HOST),
        port);
    std::unique_ptr<Log> directLog(directLogFactory.create());
    REQUIRE(directLog != nullptr);
  }
}

#endif
