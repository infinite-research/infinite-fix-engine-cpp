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

#include <Parser.h>
#include <SocketConnector.h>
#include <SocketServer.h>
#include <Utility.h>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("ParserTests") {
  Parser object;

  SECTION("extractLength") {
    std::string normalLength = "8=FIX.4.2\0019=12\00135=A\001108=30\00110=31\001";
    std::string badLength = "8=FIX.4.2\0019=A\00135=A\001108=30\00110=31\001";
    std::string negativeLength = "8=FIX.4.2\0019=-1\00135=A\001108=30\00110=31\001";
    std::string incomplete_1 = "8=FIX.4.2";
    std::string incomplete_2 = "8=FIX.4.2\0019=12";

    int length = 0;
    std::string::size_type pos = 0;

    CHECK(object.extractLength(length, pos, normalLength));
    CHECK(12 == length);
    CHECK(15 == (int)pos);

    pos = 0;
    length = 0;
    CHECK_THROWS_AS(object.extractLength(length, pos, badLength), MessageParseError);
    CHECK(length <= 0);

    length = 0;
    CHECK(0U == pos);
    CHECK_THROWS_AS(object.extractLength(length, pos, negativeLength), MessageParseError);
    CHECK(length <= 0);

    CHECK(0U == pos);
    object.extractLength(length, pos, incomplete_1);

    object.extractLength(length, pos, incomplete_2);
    CHECK(0U == pos);

    CHECK(!object.extractLength(length, pos, ""));
  }

  SECTION("readFixMessage") {
    std::string fixMsg1 = "8=FIX.4.2\0019=12\00135=A\001108=30\00110=31\001";
    std::string fixMsg2 = "8=FIX.4.2\0019=17\00135=4\00136=88\001123=Y\00110=34\001";
    std::string fixMsg3 = "8=FIX.4.2\0019=19\00135=A\001108=30\0019710=8\00110=31\001";
    std::string badLength = "8=FIX.4.2\0019=200A\00135=A\001108=30\00110=31\001";

    object.addToStream(fixMsg1 + fixMsg2 + fixMsg3 + badLength);

    std::string readFixMsg;
    CHECK(object.readFixMessage(readFixMsg));
    CHECK(fixMsg1 == readFixMsg);

    CHECK(object.readFixMessage(readFixMsg));
    CHECK(fixMsg2 == readFixMsg);

    CHECK(object.readFixMessage(readFixMsg));
    CHECK(fixMsg3 == readFixMsg);

    CHECK_THROWS_AS(object.readFixMessage(readFixMsg), MessageParseError);
  }

  SECTION("readPartialFixMessage") {
    std::string partFixMsg1 = "8=FIX.4.2\0019=17\00135=4\00136=";
    std::string partFixMsg2 = "88\001123=Y\00110=34\001";

    object.addToStream(partFixMsg1);

    std::string readPartFixMsg;
    CHECK(!object.readFixMessage(readPartFixMsg));
    object.addToStream(partFixMsg2);
    CHECK(object.readFixMessage(readPartFixMsg));
    CHECK(partFixMsg1 + partFixMsg2 == readPartFixMsg);
  }

  SECTION("readMessagesByteByByte") {
    std::string fixMsg;
    std::string fixMsg1 = "8=FIX.4.2\0019=54\00135=i\001117=1\001296=1\001302=A\001"
                          "311=DELL\001364=10\001365=DELL\001COMP\001\00110=152\001";
    std::string fixMsg2 = "8=FIX.4.2\0019=17\00135=4\00136=88\001123=Y\00110=34\001";
    std::string fixMsg3 = "8=FIX.4.2\0019=19\00135=A\001108=30\0019710=8\00110=31\001";

    for (unsigned int i = 0; i < fixMsg1.length(); ++i) {
      object.addToStream(fixMsg1.c_str() + i, 1);
    }
  }

  SECTION("readMessageWithBadLength") {
    std::string fixMsg
        = "8=TEST\0019=TEST\00135=TEST\00149=SS1\00156=RORE\00134=3\00152=20050222-16:45:53\00110=TEST\001";

    object.addToStream(fixMsg);

    std::string readFixMsg;
    CHECK_THROWS_AS(object.readFixMessage(readFixMsg), MessageParseError);
    object.readFixMessage(readFixMsg);
  }

  SECTION("incomplete framing is rejected at the accumulator limit") {
    const std::size_t limit = 16U * 1024U * 1024U;
    for (const std::string prefix :
         {"x",
          "8=FIX.4.2\00135=A\001",
          "8=FIX.4.2\0019=",
          "8=FIX.4.2\0019=5\00135=A\001",
          "8=FIX.4.2\0019=5\00135=A\00110=000"}) {
      CAPTURE(prefix);
      Parser parser;
      std::string message;
      parser.addToStream(prefix);
      CHECK_FALSE(parser.readFixMessage(message));
      parser.addToStream(std::string(limit - prefix.size() - 1, 'x'));
      CHECK_FALSE(parser.readFixMessage(message));
      parser.addToStream("x", 1);
      CHECK_THROWS_AS(parser.readFixMessage(message), MessageParseError);
      CHECK_FALSE(parser.readFixMessage(message));
      const std::string valid = "8=FIX.4.2\0019=5\00135=A\00110=178\001";
      parser.addToStream(valid);
      CHECK(parser.readFixMessage(message));
      CHECK((message == valid));
    }
  }

  SECTION("BodyLength must leave room for the whole frame") {
    for (const std::string length : {"2147483647", "16777217", "16777189", "-1", "bad"}) {
      CAPTURE(length);
      Parser parser;
      std::string message;
      parser.addToStream("8=FIX.4.2\0019=" + length);
      CHECK_FALSE(parser.readFixMessage(message));
      parser.addToStream("\001", 1);
      CHECK_THROWS_AS(parser.readFixMessage(message), MessageParseError);
      CHECK_FALSE(parser.readFixMessage(message));
    }
  }

  SECTION("append rejects overflow before accessing input and clears the accumulator") {
    const std::size_t limit = 16U * 1024U * 1024U;
    std::string message;
    object.addToStream(std::string(limit, 'x'));
    CHECK_THROWS_AS(object.addToStream("x", 1), MessageParseError);
    CHECK_FALSE(object.readFixMessage(message));
    CHECK_THROWS_AS(object.addToStream(std::string(limit + 1, 'x')), MessageParseError);
    CHECK_FALSE(object.readFixMessage(message));
    CHECK_THROWS_AS(object.addToStream("x", std::numeric_limits<std::size_t>::max()), MessageParseError);
    CHECK_FALSE(object.readFixMessage(message));
  }

  SECTION("a complete frame at exactly 16 MiB survives fragmentation") {
    std::string frame = "8=FIX.4.2\0019=16777188\00135=X\00158=";
    frame.append(16777179, 'x');
    frame += '\001';
    const unsigned checksum = std::accumulate(frame.begin(), frame.end(), 0U) % 256;
    frame += "10=" + std::to_string(1000 + checksum).substr(1) + '\001';
    REQUIRE(frame.size() == 16U * 1024U * 1024U);
    std::string message;
    object.addToStream(frame.data(), frame.size() - 1);
    CHECK_FALSE(object.readFixMessage(message));
    object.addToStream(frame.data() + frame.size() - 1, 1);
    REQUIRE(object.readFixMessage(message));
    CHECK((message == frame));
    CHECK_FALSE(object.readFixMessage(message));
    object.addToStream(frame);
    REQUIRE(object.readFixMessage(message));
    CHECK((message == frame));
  }

  SECTION("coalesced frames drain independently across fragment boundaries") {
    const std::string first = "8=FIX.4.2\0019=5\00135=A\00110=178\001";
    const std::string second = "8=FIX.4.2\0019=5\00135=0\00110=161\001";
    std::string message;
    object.addToStream(first + second.substr(0, 1));
    REQUIRE(object.readFixMessage(message));
    CHECK(message == first);
    CHECK_FALSE(object.readFixMessage(message));
    for (std::size_t i = 1; i < second.size(); ++i) {
      object.addToStream(second.data() + i, 1);
      CHECK(object.readFixMessage(message) == (i + 1 == second.size()));
    }
    CHECK(message == second);
    CHECK_FALSE(object.readFixMessage(message));
    object.addToStream(first + second);
    REQUIRE(object.readFixMessage(message));
    CHECK(message == first);
    REQUIRE(object.readFixMessage(message));
    CHECK(message == second);
    CHECK_FALSE(object.readFixMessage(message));
  }
}
