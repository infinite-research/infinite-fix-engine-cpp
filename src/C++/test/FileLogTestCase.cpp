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

#include <FileLog.h>
#include <MessageStore.h>
#include <SessionState.h>
#include <Utility.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <set>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace FIX;

namespace {
class RecordingLog : public Log {
public:
  void clear() override {}
  void backup() override {}
  void onIncoming(const std::string &value) override { incoming = value; }
  void onOutgoing(const std::string &value) override { outgoing = value; }
  void onEvent(const std::string &value) override { event = value; }

  std::string incoming;
  std::string outgoing;
  std::string event;
};

std::string readFile(const std::string &path) {
  std::ifstream stream(path.c_str(), std::ios::in | std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::set<std::string> readPayloads(const std::string &path) {
  std::ifstream stream(path.c_str(), std::ios::in | std::ios::binary);
  std::set<std::string> result;
  std::string line;
  while (std::getline(stream, line)) {
    const std::string::size_type separator = line.find(" : ");
    REQUIRE(separator != std::string::npos);
    result.insert(line.substr(separator + 3));
  }
  return result;
}

inline void deleteLogSession(std::string sender, std::string target) {
  file_unlink(("log/FIX.4.2-" + sender + "-" + target + ".event.current.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.1.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.2.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.3.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.4.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".event.backup.5.log").c_str());

  file_unlink(("log/FIX.4.2-" + sender + "-" + target + ".messages.current.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.1.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.2.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.3.log").c_str());
  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.4.log").c_str());

  file_unlink(("log/backup/FIX.4.2-" + sender + "-" + target + ".messages.backup.5.log").c_str());
}
} // namespace

struct generateFileNameFixture {
  generateFileNameFixture()
      : fileLogFactory("log", "log" + file_separator() + "backup") {
    deleteLogSession("GENERATEFILENAME", "TEST");
    SessionID sessionID(BeginString("FIX.4.2"), SenderCompID("GENERATEFILENAME"), TargetCompID("TEST"));

    object = (FileLog *)fileLogFactory.create(sessionID);
  }

  ~generateFileNameFixture() {
    fileLogFactory.destroy(object);
    deleteLogSession("GENERATEFILENAME", "TEST");
  }

  FileLogFactory fileLogFactory;
  FileLog *object;
};

TEST_CASE_METHOD(generateFileNameFixture, "FileLogTests") {
  SECTION("QuickFIX Mutex remains recursive") {
    Mutex mutex;
    Locker outer(mutex);
    Locker inner(mutex);
    SUCCEED();
  }

  SECTION("generateFileName") {
    object->onEvent("EVENT1");
    object->onIncoming("INCOMING1");
    object->onOutgoing("OUTGOING1");

    CHECK(file_exists("log/FIX.4.2-GENERATEFILENAME-TEST.event.current.log"));
    CHECK(file_exists("log/FIX.4.2-GENERATEFILENAME-TEST.messages.current.log"));

    object->backup();
    object->onEvent("EVENT2");
    object->onIncoming("INCOMING2");
    object->onOutgoing("OUTGOING2");

    CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup.1.log"));
    CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.messages.backup.1.log"));

    object->backup();
    object->onEvent("EVENT3");
    object->onIncoming("INCOMING3");
    object->onOutgoing("OUTGOING3");

    CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup.2.log"));
    CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.messages.backup.2.log"));

    object->backup();
    object->onEvent("EVENT4");
    object->onIncoming("INCOMING4");
    object->onOutgoing("OUTGOING4");

    CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup.3.log"));
    CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.messages.backup.3.log"));

    object->backup();
    object->onEvent("EVENT5");
    object->onIncoming("INCOMING5");
    object->onOutgoing("OUTGOING5");

    CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup.4.log"));
    CHECK(file_exists("log/backup/FIX.4.2-GENERATEFILENAME-TEST.messages.backup.4.log"));
  }

  SECTION("credentials are redacted from log copies without changing stored messages") {
    const std::string user = "visible-user-secret";
    const std::string password = "visible-password-secret";
    std::string message = "8=FIX.4.2\00135=A\001553=" + user + "\001554=" + password + "\00110=000\001";
    const std::string original = message;

    SessionState state(UtcTimeStamp::now());
    MemoryStore store(UtcTimeStamp::now());
    state.store(&store);
    state.log(object);

    REQUIRE(state.set(1, message));
    state.onIncoming(message);

    const std::string contents = readFile("log/FIX.4.2-GENERATEFILENAME-TEST.messages.current.log");
    CHECK(contents.find(user) == std::string::npos);
    CHECK(contents.find(password) == std::string::npos);
    CHECK(contents.find("553=<redacted>\001554=<redacted>\001") != std::string::npos);
    CHECK(message == original);

    std::vector<std::string> stored;
    state.get(1, 1, stored);
    REQUIRE(stored.size() == 1);
    CHECK(stored.front() == original);
  }

  SECTION("redaction honors FIX field boundaries and unterminated fields") {
    const std::string prefixed
        = "Unknown session: 8=FIX.4.2\0011553=keep\00158=keep554=value\001553=user-secret\001554=password-secret\001";
    RecordingLog log;
    SessionState state(UtcTimeStamp::now());
    state.log(&log);

    state.onEvent(prefixed);
    state.onIncoming("553=unterminated-user");
    state.onOutgoing("prefix\001554=unterminated-password");

    CHECK(
        log.event
        == "Unknown session: 8=FIX.4.2\0011553=keep\00158=keep554=value\001553=<redacted>\001554=<redacted>\001");
    CHECK(log.incoming == "553=<redacted>");
    CHECK(log.outgoing == "prefix\001554=<redacted>");
  }

  SECTION("records reversibly escape separators and preserve binary FIX bytes") {
    std::string value = "literal\\n";
    value += "\00196=";
    value.push_back('\0');
    value += "DATA\r\nTAIL";
    object->onIncoming(value);

    const std::string contents = readFile("log/FIX.4.2-GENERATEFILENAME-TEST.messages.current.log");
    const std::string::size_type separator = contents.find(" : ");
    REQUIRE(separator != std::string::npos);

    std::string expected = "literal\\\\n";
    expected += "\00196=";
    expected.push_back('\0');
    expected += "DATA\\r\\nTAIL\n";
    CHECK(contents.substr(separator + 3) == expected);
    CHECK(std::count(contents.begin(), contents.end(), '\n') == 1);
  }

  SECTION("simultaneous message writers keep records whole") {
    constexpr int writerCount = 4;
    constexpr int recordsPerWriter = 50;
    std::atomic<bool> start(false);
    std::vector<std::thread> writers;
    for (int writer = 0; writer < writerCount; ++writer) {
      writers.emplace_back([&, writer] {
        while (!start.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        for (int record = 0; record < recordsPerWriter; ++record) {
          const std::string value = "writer-" + std::to_string(writer) + "-" + std::to_string(record);
          if (record % 2) {
            object->onIncoming(value);
          } else {
            object->onOutgoing(value);
          }
        }
      });
    }
    start.store(true, std::memory_order_release);
    for (std::thread &writer : writers) {
      writer.join();
    }

    const std::string path = "log/FIX.4.2-GENERATEFILENAME-TEST.messages.current.log";
    const std::string contents = readFile(path);
    CHECK(std::count(contents.begin(), contents.end(), '\n') == writerCount * recordsPerWriter);
    const std::set<std::string> payloads = readPayloads(path);
    CHECK(payloads.size() == writerCount * recordsPerWriter);
    for (int writer = 0; writer < writerCount; ++writer) {
      for (int record = 0; record < recordsPerWriter; ++record) {
        CHECK(payloads.count("writer-" + std::to_string(writer) + "-" + std::to_string(record)) == 1);
      }
    }
  }

  SECTION("clear and backup serialize with active writers") {
    std::atomic<bool> stop(false);
    std::atomic<int> written(0);
    std::thread writer([&] {
      while (!stop.load(std::memory_order_acquire)) {
        object->onEvent("race-" + std::to_string(written.fetch_add(1, std::memory_order_relaxed)));
      }
    });

    while (written.load(std::memory_order_acquire) < 10) {
      std::this_thread::yield();
    }
    object->clear();
    object->backup();
    object->clear();
    object->backup();
    stop.store(true, std::memory_order_release);
    writer.join();
    object->onEvent("FINAL");

    const std::set<std::string> current = readPayloads("log/FIX.4.2-GENERATEFILENAME-TEST.event.current.log");
    CHECK(current.count("FINAL") == 1);
    for (const std::string &payload : current) {
      CHECK((payload == "FINAL" || payload.find("race-") == 0));
    }
    for (int backup = 1; backup <= 2; ++backup) {
      const std::string path
          = "log/backup/FIX.4.2-GENERATEFILENAME-TEST.event.backup." + std::to_string(backup) + ".log";
      REQUIRE(file_exists(path.c_str()));
      const std::set<std::string> payloads = readPayloads(path);
      for (const std::string &payload : payloads) {
        CHECK(payload.find("race-") == 0);
      }
    }
  }
}
