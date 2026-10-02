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

#include <DataDictionaryProvider.h>
#include <Fields.h>
#include <InfiniteSessionClassification.h>
#include <Message.h>
#include <Values.h>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("DataDictionaryProviderTests") {
  SECTION("configured application dictionaries reject unknown versions") {
    DataDictionaryProvider object;
    auto dictionary = std::make_shared<DataDictionary>();
    dictionary->setVersion(BeginString_FIX50);
    object.addApplicationDataDictionary(ApplVerID(ApplVerID_FIX50), dictionary);
    CHECK_THROWS_AS(object.getApplicationDataDictionary(ApplVerID(ApplVerID_FIX42)), DataDictionaryNotFound);
    CHECK(&object.getApplicationDataDictionary(ApplVerID(ApplVerID_FIX50)) == dictionary.get());
  }

  SECTION("getApplicationDataDictionary_DataDictionaryNotSet") {
    DataDictionaryProvider object;
    DataDictionary expected;

    ApplVerID id;
    DataDictionary actual = object.getApplicationDataDictionary(id);

    CHECK(expected.getVersion() == actual.getVersion());
    CHECK(expected.getOrderedFields() == actual.getOrderedFields());
  }

  SECTION("copies preserve shared dictionary identity") {
    const BeginString beginString(BeginString_FIX42);
    const ApplVerID applVerID(ApplVerID_FIX50);
    auto transportDictionary = std::make_shared<DataDictionary>();
    transportDictionary->setVersion(BeginString_FIX42);
    auto applicationDictionary = std::make_shared<DataDictionary>();
    applicationDictionary->setVersion(BeginString_FIX50);
    DataDictionaryProvider original;
    original.addTransportDataDictionary(beginString, transportDictionary);
    original.addApplicationDataDictionary(applVerID, applicationDictionary);

    DataDictionaryProvider copied(original);
    DataDictionaryProvider assigned;
    assigned = original;

    CHECK(&copied.getSessionDataDictionary(beginString) == transportDictionary.get());
    CHECK(&assigned.getSessionDataDictionary(beginString) == transportDictionary.get());
    CHECK(&copied.getApplicationDataDictionary(applVerID) == applicationDictionary.get());
    CHECK(&assigned.getApplicationDataDictionary(applVerID) == applicationDictionary.get());
    CHECK(copied.getSessionDataDictionary(beginString).getVersion() == BeginString_FIX42);
    CHECK(copied.getApplicationDataDictionary(applVerID).getVersion() == BeginString_FIX50);
  }
}

TEST_CASE("Infinite callers reject missing configured application dictionary", "[infinite][dictionary]") {
  DataDictionaryProvider dictionaries;
  dictionaries.addApplicationDataDictionary(ApplVerID(ApplVerID_FIX50), std::make_shared<DataDictionary>());
  InfiniteSessionStaticProfile profile;
  profile.scheduleMode = 1;
  profile.timestampPrecision = 6;
  const auto now = INT64_C(1700000000123456000);
  Message message;
  message.getHeader().setField(BeginString("FIXT.1.1"));
  message.getHeader().setField(MsgType("U1"));
  message.getHeader().setField(SenderCompID("LOCAL"));
  message.getHeader().setField(TargetCompID("PEER"));
  message.getHeader().setField(MsgSeqNum(1));
  message.getHeader().setField(SendingTime(UtcTimeStamp(22, 13, 20, 14, 11, 2023)));
  message.setField(Text("hello"));
  SECTION("application rendering") {
    CHECK_THROWS_AS(
        InfiniteSessionPlanner::application(
            "FIXT.1.1",
            "LOCAL",
            "PEER",
            30,
            2,
            2,
            now,
            "U1",
            "58=hello\001",
            InfiniteApplicationRenderMode::Original,
            0,
            dictionaries,
            profile),
        DataDictionaryNotFound);
  }
  SECTION("stored frame validation translates the dictionary exception") {
    CHECK_THROWS_AS(
        InfiniteSessionPlanner::
            storedFrame("FIXT.1.1", "LOCAL", "PEER", 30, 2, 2, now, 0, message.toString(), dictionaries, profile),
        std::invalid_argument);
  }
  SECTION("inbound classification translates the dictionary exception") {
    message.getHeader().setField(SenderCompID("PEER"));
    message.getHeader().setField(TargetCompID("LOCAL"));
    CHECK_THROWS_AS(
        InfiniteSessionPlanner::inbound(
            "FIXT.1.1",
            "LOCAL",
            "PEER",
            30,
            2,
            1,
            now,
            now,
            now,
            now,
            7,
            0,
            0,
            message.toString(),
            dictionaries,
            profile),
        std::invalid_argument);
  }
  SECTION("gap fill fails closed with unavailable configured dictionaries") {
    CHECK_THROWS(
        InfiniteSessionPlanner::gapFill(
            "FIXT.1.1",
            "LOCAL",
            "PEER",
            30,
            4,
            2,
            now,
            1,
            2,
            0,
            &dictionaries,
            &profile,
            "20231114-22:13:20.000000"));
  }
}
