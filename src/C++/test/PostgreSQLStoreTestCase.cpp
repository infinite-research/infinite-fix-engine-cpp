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

#include "catch_amalgamated.hpp"

using namespace FIX;

struct postgreSQLStoreFixture {
  postgreSQLStoreFixture(bool reset)
      : factory(TestSettings::sessionSettings.get()) {
    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));

    try {
      object = factory.create(UtcTimeStamp::now(), sessionID);
    } catch (std::exception &e) {
      std::cerr << e.what() << std::endl;
      throw;
    }

    if (reset) {
      object->reset(UtcTimeStamp::now());
    }

    this->resetAfter = reset;
  }

  ~postgreSQLStoreFixture() { factory.destroy(object); }

  PostgreSQLStoreFactory factory;
  MessageStore *object;
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
  REQUIRE(object->set(102, replacement));

  std::vector<std::string> messages;
  object->get(101, 102, messages);
  REQUIRE(messages.size() == 2);
  CHECK(messages[0] == "safe");
  CHECK(messages[1] == replacement);
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
    MessageStore *store = storeFactory.create(UtcTimeStamp::now(), sessionID);
    REQUIRE(store != nullptr);
    storeFactory.destroy(store);

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
    MessageStore *directStore = directStoreFactory.create(UtcTimeStamp::now(), sessionID);
    REQUIRE(directStore != nullptr);
    directStoreFactory.destroy(directStore);

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
    Log *log = logFactory.create();
    REQUIRE(log != nullptr);
    logFactory.destroy(log);

    PostgreSQLLogFactory directLogFactory(
        storeSettings.getString(POSTGRESQL_STORE_DATABASE),
        storeSettings.getString(POSTGRESQL_STORE_USER),
        "",
        storeSettings.getString(POSTGRESQL_STORE_HOST),
        port);
    Log *directLog = directLogFactory.create();
    REQUIRE(directLog != nullptr);
    directLogFactory.destroy(directLog);
  }
}

#endif
