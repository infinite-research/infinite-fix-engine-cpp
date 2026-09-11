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

#include "MessageStoreTestCase.h"
#include "TestHelper.h"
#include <FileStore.h>
#include <array>
#include <fstream>
#include <iterator>

#include "catch_amalgamated.hpp"

using namespace FIX;

struct fileStoreFixture {
  fileStoreFixture(bool resetBefore, bool reset)
      : factory("store") {
    if (resetBefore) {
      deleteSession("SETGET", "TEST");
      file_unlink("store/FIX.4.2-SETGET-TEST.body");
    }

    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));

    object = factory.create(UtcTimeStamp::now(), sessionID);

    this->resetAfter = reset;
  }

  ~fileStoreFixture() {
    factory.destroy(object);

    if (resetAfter) {
      deleteSession("SETGET", "TEST");
      file_unlink("store/FIX.4.2-SETGET-TEST.body");
    }
  }

  FileStoreFactory factory;
  MessageStore *object;
  bool resetAfter;
};

struct resetBeforeFileStoreFixture : fileStoreFixture {
  resetBeforeFileStoreFixture()
      : fileStoreFixture(true, false) {}
};

struct resetAfterFileStoreFixture : fileStoreFixture {
  resetAfterFileStoreFixture()
      : fileStoreFixture(false, true) {}
};

struct resetBeforeAndAfterFileStoreFixture : fileStoreFixture {
  resetBeforeAndAfterFileStoreFixture()
      : fileStoreFixture(true, true) {}
};

struct noResetFileStoreFixture : fileStoreFixture {
  noResetFileStoreFixture()
      : fileStoreFixture(false, false) {}
};

struct resetBeforeAndAfterWithTestFileManager : resetBeforeAndAfterFileStoreFixture {
  resetBeforeAndAfterWithTestFileManager()
      : resetBeforeAndAfterFileStoreFixture() {
    factory.destroy(object);

    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"), "Test");

    object = new FileStore(UtcTimeStamp::now(), "store", sessionID);
  }
};

TEST_CASE_METHOD(resetBeforeFileStoreFixture, "resetFileStoreTests"){
    SECTION("setGet"){CHECK_MESSAGE_STORE_SET_GET}

    SECTION("setGetUint64"){CHECK_MESSAGE_STORE_SET_GET_UINT64}

    SECTION("setGetWithQuote"){CHECK_MESSAGE_STORE_SET_GET_WITH_QUOTE}

    SECTION("other"){CHECK_MESSAGE_STORE_OTHER}

    SECTION("otherUint64"){CHECK_MESSAGE_STORE_OTHER_UINT64}

    SET_SEQUENCE_NUMBERS}

TEST_CASE_METHOD(noResetFileStoreFixture, "noResetFileStoreTests") {
  SECTION("reload"){CHECK_MESSAGE_STORE_RELOAD}

  SECTION("refresh") {
    CHECK_MESSAGE_STORE_RELOAD
  }
}

TEST_CASE_METHOD(noResetFileStoreFixture, "FileStoreTests_3") {
  SECTION("refresh") { CHECK_MESSAGE_STORE_REFRESH }
}

TEST_CASE_METHOD(resetAfterFileStoreFixture, "FileStoreTests_4") {
  SECTION("reload") { CHECK_MESSAGE_STORE_RELOAD }
}

TEST_CASE_METHOD(resetBeforeAndAfterFileStoreFixture, "FileStoreTests_5") {
  SECTION("FileStore_refresh_reset") {
    // Init store with 3 messages
    CHECK_MESSAGE_STORE_SET_GET
    object->get(1, 10, messages);

    // Still 3 messages after refresh
    object->refresh();
    object->get(1, 10, messages);

    // Should be 0 messages after reset
    object->reset(UtcTimeStamp::now());
    object->get(1, 10, messages);
  }
}

TEST_CASE_METHOD(resetBeforeAndAfterWithTestFileManager, "FileStoreTests_6") {
  SECTION("Refresh_DeleteFileStartup_NoException") {
    try {
      object->refresh();
    } catch (Exception &e) {
      CHECK(false);
      throw e;
    }
  }

  SECTION("Reset_DeleteFileStartup_NoException") {
    try {
      object->reset(UtcTimeStamp::now());
    } catch (Exception &e) {
      CHECK(false);
      throw e;
    }
  }

  SECTION("FileStoreCreationTime") {
    UtcTimeStamp timeStamp = object->getCreationTime();
    UtcTimeStamp currentTimeStamp = UtcTimeStamp::now();
    CHECK(currentTimeStamp.getYear() == timeStamp.getYear());
  }

  SECTION("FileStoreFactory_FileStoreFromDictionary") {
    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("SETGET"), TargetCompID("TEST"));
    Dictionary dictionary;
    dictionary.setString("ConnectionType", "acceptor");
    dictionary.setString("FileStorePath", "store");

    SessionSettings settings;
    settings.set(sessionID, dictionary);
    FileStoreFactory fileStoreFactory(settings);

    MessageStore *fileStore = fileStoreFactory.create(UtcTimeStamp::now(), sessionID);
    CHECK(fileStore != nullptr);
    fileStoreFactory.destroy(fileStore);
  }
}

struct restoredFileStoreFixture {
  SessionID id{BeginString("FIX.4.2"), SenderCompID("RESTORE"), TargetCompID("TASK8")};
  const std::array<std::string, 4> extensions{{"body", "header", "seqnums", "session"}};

  restoredFileStoreFixture() {
    file_mkdir("store");
    write("body", "oldnew");
    write("header", "1,0,3 ");
    write("seqnums", "00000000000000000007 : 00000000000000000009");
    write("session", "20240102-03:04:05");
  }

  ~restoredFileStoreFixture() {
    for (const auto &extension : extensions) {
      file_unlink(path(extension).c_str());
    }
  }

  std::string path(const std::string &extension) const { return "store/FIX.4.2-RESTORE-TASK8." + extension; }

  void write(const std::string &extension, const std::string &value) const {
    std::ofstream file(path(extension), std::ios::binary | std::ios::trunc);
    file.write(value.data(), value.size());
    REQUIRE(file.good());
  }

  std::array<std::string, 4> contents() const {
    std::array<std::string, 4> result;
    for (std::size_t i = 0; i < extensions.size(); ++i) {
      std::ifstream file(path(extensions[i]), std::ios::binary);
      result[i].assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    return result;
  }
};

TEST_CASE("FileStoreTests_reject_corrupt_restore_atomically") {
  struct Corruption {
    const char *name;
    const char *extension;
    std::string value;
  };
  const Corruption cases[] = {
      {"22 byte timestamp", "session", "20240102-03:04:05.1234X"},
      {"invalid timestamp", "session", "20241302-03:04:05"},
      {"timestamp trailing garbage", "session", "20240102-03:04:05 junk"},
      {"negative offset", "header", "2,-1,3 "},
      {"offset overflow", "header", "2,9223372036854775808,3 "},
      {"numeric overflow", "header", "18446744073709551616,0,3 "},
      {"size overflow", "header", "2,0,18446744073709551616 "},
      {"SIZE_MAX and size plus one wrap", "header", "2,0," + std::to_string(SIZE_MAX) + " "},
      {"offset beyond EOF", "header", "2,7,0 "},
      {"range beyond EOF", "header", "2,4,3 "},
      {"incomplete header row", "header", "2,3,3 3,0"},
      {"header trailing garbage", "header", "2,3,3 garbage"},
      {"zero header sequence", "header", "0,0,3 "},
      {"truncated body", "body", "ol"},
      {"negative sequence", "seqnums", "-1 : 9"},
      {"zero sequence", "seqnums", "7 : 0"},
      {"sequence overflow", "seqnums", "18446744073709551616 : 9"},
      {"incomplete sequence", "seqnums", "7 :"},
      {"sequence trailing garbage", "seqnums", "7 : 9 junk"},
      {"empty sequence", "seqnums", ""},
      {"empty timestamp", "session", ""},
      {"header control Z suffix",
       "header",
       "1,0,3 \x1a"
       "garbage"},
      {"sequence control Z suffix",
       "seqnums",
       "7 : 9\x1a"
       "garbage"},
      {"timestamp control Z suffix",
       "session",
       "20240102-03:04:05\x1a"
       "garbage"},
  };
  for (const auto &corruption : cases) {
    DYNAMIC_SECTION(corruption.name) {
      restoredFileStoreFixture fixture;
      SECTION("constructor") {
        fixture.write(corruption.extension, corruption.value);
        const auto before = fixture.contents();
        CHECK_THROWS_AS(FileStore(UtcTimeStamp::now(), "store", fixture.id), ConfigError);
        CHECK(fixture.contents() == before);
      }
#ifndef _WIN32
      SECTION("refresh after external mutation") {
        FileStore store(UtcTimeStamp::now(), "store", fixture.id);
        const auto creationTime = store.getCreationTime();
        fixture.write("header", "2,3,3 ");
        fixture.write("seqnums", "00000000000000000011 : 00000000000000000013");
        // Keep the original indexed range for the truncated-body fixture.
        if (std::string(corruption.extension) == "body") {
          fixture.write("header", "1,0,3 ");
        }
        fixture.write(corruption.extension, corruption.value);
        const auto before = fixture.contents();
        CHECK_THROWS_AS(store.refresh(), IOException);
        CHECK(store.getNextSenderMsgSeqNum() == 7);
        CHECK(store.getNextTargetMsgSeqNum() == 9);
        CHECK(store.getCreationTime() == creationTime);
        CHECK(fixture.contents() == before);
        std::vector<std::string> messages;
        if (std::string(corruption.extension) != "body" && store.getNextSenderMsgSeqNum() == 7
            && store.getNextTargetMsgSeqNum() == 9) {
          store.get(1, 2, messages);
          CHECK(messages == std::vector<std::string>{"old"});
        }
      }
#endif
    }
  }
}

TEST_CASE_METHOD(restoredFileStoreFixture, "FileStoreTests_restore_compatibility") {
  SECTION("legacy 32 bit sequence format") { write("seqnums", "0000000007 : 2147483647"); }
  SECTION("current sequence format") { write("seqnums", "00000000000000000007 : 00000000002147483647"); }
  const auto before = contents();
  FileStore store(UtcTimeStamp::now(), "store", id);
  CHECK(store.getNextSenderMsgSeqNum() == 7);
  CHECK(store.getNextTargetMsgSeqNum() == 2147483647);
  store.refresh();
  CHECK(store.getNextSenderMsgSeqNum() == 7);
  CHECK(store.getNextTargetMsgSeqNum() == 2147483647);
  CHECK(contents() == before);
  std::vector<std::string> messages;
  store.get(1, 1, messages);
  CHECK(messages == std::vector<std::string>{"old"});
}

TEST_CASE_METHOD(restoredFileStoreFixture, "FileStoreTests_binary_exact_reads") {
  const std::string message("a\0b\r\n", 5);
  {
    FileStore store(UtcTimeStamp::now(), "store", id);
    store.set(2, message);
    std::vector<std::string> messages;
    store.get(2, 2, messages);
    CHECK(messages == std::vector<std::string>{message});
    store.refresh();
    store.get(2, 2, messages);
    CHECK(messages == std::vector<std::string>{message});
  }
  FileStore store(UtcTimeStamp::now(), "store", id);
  std::vector<std::string> messages;
  store.get(2, 2, messages);
  CHECK(messages == std::vector<std::string>{message});
}

#ifdef _WIN32
TEST_CASE("FileStoreTests_windows_control_z_metadata") {
  const std::pair<const char *, const char *> metadata[]
      = {{"header", "1,0,3 "}, {"seqnums", "7 : 9"}, {"session", "20240102-03:04:05"}};
  for (const auto &artifact : metadata) {
    for (const auto &suffix : {"garbage", ""}) {
      DYNAMIC_SECTION(artifact.first << " control Z " << suffix) {
        restoredFileStoreFixture fixture;
        fixture.write(artifact.first, std::string(artifact.second) + '\x1a' + suffix);
        const auto before = fixture.contents();
        CHECK_THROWS_AS(FileStore(UtcTimeStamp::now(), "store", fixture.id), ConfigError);
        CHECK(fixture.contents() == before);
      }
    }
  }
}

TEST_CASE_METHOD(restoredFileStoreFixture, "FileStoreTests_windows_trailing_control_z_body") {
  write("body", "old\x1a");
  SECTION("valid store remains unchanged after open and refresh") {
    const auto before = contents();
    {
      FileStore store(UtcTimeStamp::now(), "store", id);
      std::vector<std::string> messages;
      store.get(1, 1, messages);
      CHECK(messages == std::vector<std::string>{"old"});
      CHECK(contents() == before);
      store.refresh();
      store.get(1, 1, messages);
      CHECK(messages == std::vector<std::string>{"old"});
      CHECK(contents() == before);
    }
    CHECK(contents() == before);
  }
  SECTION("corrupt metadata does not truncate the body during open") {
    write("seqnums", "invalid");
    const auto before = contents();
    CHECK_THROWS_AS(FileStore(UtcTimeStamp::now(), "store", id), ConfigError);
    CHECK(contents() == before);
  }
}

TEST_CASE_METHOD(restoredFileStoreFixture, "FileStoreTests_historical_windows_newlines") {
  SECTION("historical body survives open and refresh") {
    write("body", "a\r\nb");
    const auto before = contents();
    FileStore store(UtcTimeStamp::now(), "store", id);
    std::vector<std::string> messages;
    store.get(1, 1, messages);
    CHECK(messages == std::vector<std::string>{"a\nb"});
    store.refresh();
    store.get(1, 1, messages);
    CHECK(messages == std::vector<std::string>{"a\nb"});
    CHECK(contents() == before);
  }
  SECTION("new stores retain historical text encoding") {
    for (const auto &extension : extensions) {
      file_unlink(path(extension).c_str());
    }
    {
      FileStore store(UtcTimeStamp::now(), "store", id);
      store.set(1, "a\nb");
      store.refresh();
      std::vector<std::string> messages;
      store.get(1, 1, messages);
      CHECK(messages == std::vector<std::string>{"a\nb"});
    }
    CHECK(contents()[0] == "a\r\nb");
    CHECK(contents()[1] == "1,0,3 ");
  }
}
#endif

#ifndef _WIN32
TEST_CASE_METHOD(restoredFileStoreFixture, "FileStoreTests_body_truncated_after_open") {
  FileStore store(UtcTimeStamp::now(), "store", id);
  write("body", "o");
  std::vector<std::string> messages;
  CHECK_THROWS_AS(store.get(1, 1, messages), IOException);
  CHECK(messages.empty());
}
#endif

TEST_CASE("FileStoreTests_missing_artifacts") {
  for (const auto &extension : {"body", "header", "seqnums", "session"}) {
    DYNAMIC_SECTION(extension) {
      restoredFileStoreFixture fixture;
      file_unlink(fixture.path(extension).c_str());
      REQUIRE_FALSE(std::ifstream(fixture.path(extension)).good());
      const auto before = fixture.contents();
      CHECK_THROWS_AS(FileStore(UtcTimeStamp::now(), "store", fixture.id), ConfigError);
      CHECK(fixture.contents() == before);
      CHECK_FALSE(std::ifstream(fixture.path(extension)).good());
    }
  }
}

#ifndef _WIN32
TEST_CASE("FileStoreTests_missing_artifacts_after_open") {
  for (const auto &extension : {"body", "header", "seqnums", "session", "all"}) {
    DYNAMIC_SECTION(extension) {
      restoredFileStoreFixture fixture;
      FileStore store(UtcTimeStamp::now(), "store", fixture.id);
      for (const auto &candidate : fixture.extensions) {
        if (candidate == extension || std::string(extension) == "all") {
          file_unlink(fixture.path(candidate).c_str());
          REQUIRE_FALSE(std::ifstream(fixture.path(candidate)).good());
        }
      }
      const auto before = fixture.contents();
      CHECK_THROWS_AS(store.refresh(), IOException);
      CHECK(store.getNextSenderMsgSeqNum() == 7);
      CHECK(store.getNextTargetMsgSeqNum() == 9);
      CHECK(fixture.contents() == before);
    }
  }
}
#endif

TEST_CASE_METHOD(restoredFileStoreFixture, "FileStoreTests_orphan_body") {
  for (const auto &extension : {"header", "seqnums", "session"}) {
    file_unlink(path(extension).c_str());
    REQUIRE_FALSE(std::ifstream(path(extension)).good());
  }
  SECTION("nonempty orphan fails without creating metadata") {
    const auto before = contents();
    CHECK_THROWS_AS(FileStore(UtcTimeStamp::now(), "store", id), ConfigError);
    CHECK(contents() == before);
    CHECK_FALSE(std::ifstream(path("seqnums")).good());
  }
  SECTION("empty orphan initializes") {
    write("body", "");
    FileStore store(UtcTimeStamp::now(), "store", id);
    CHECK(store.getNextSenderMsgSeqNum() == 1);
    CHECK(store.getNextTargetMsgSeqNum() == 1);
    store.refresh();
    store.set(1, "first");
    std::vector<std::string> messages;
    store.get(1, 1, messages);
    CHECK(messages == std::vector<std::string>{"first"});
  }
  SECTION("fully absent store initializes") {
    file_unlink(path("body").c_str());
    REQUIRE_FALSE(std::ifstream(path("body")).good());
    FileStore store(UtcTimeStamp::now(), "store", id);
    CHECK(store.getNextSenderMsgSeqNum() == 1);
    CHECK(store.getNextTargetMsgSeqNum() == 1);
    store.refresh();
  }
}
