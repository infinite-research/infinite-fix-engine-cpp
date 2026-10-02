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

#include <SessionSettings.h>
#include <sstream>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("SessionSettingsTests") {
  SECTION("longAllowedRemoteAddressesPreservesTrailingSession") {
    const std::string newline = GENERATE(std::string("\n"), std::string("\r\n"));
    const bool finalNewline = GENERATE(false, true);
    std::string addresses = "127.0.0.1";
    for (int i = 0; i < 512; ++i) {
      addresses += ",192.0.2.1";
    }
    std::istringstream input(
        "[DEFAULT]" + newline + "ConnectionType=acceptor" + newline + "BeginString=FIX.4.2" + newline
        + "SenderCompID=LOCAL" + newline + "[SESSION]" + newline + "TargetCompID=FIRST" + newline
        + "AllowedRemoteAddresses=" + addresses + newline + "[SESSION]" + newline + "TargetCompID=LAST"
        + (finalNewline ? newline : ""));
    SessionSettings object;
    CHECK_NOTHROW(input >> object);
    CHECK(object.size() == 2);
    CHECK(object.has(SessionID("FIX.4.2", "LOCAL", "LAST")));
    REQUIRE(object.has(SessionID("FIX.4.2", "LOCAL", "FIRST")));
    const auto &first = object.get(SessionID("FIX.4.2", "LOCAL", "FIRST"));
    CHECK(first.has(ALLOWED_REMOTE_ADDRESSES));
    if (first.has(ALLOWED_REMOTE_ADDRESSES)) {
      CHECK(first.getString(ALLOWED_REMOTE_ADDRESSES) == addresses);
    }
  }

  SECTION("failedReadPreservesExistingSessionSettings") {
    struct FailingBuffer : std::stringbuf {
      FailingBuffer(const std::string &configuration)
          : std::stringbuf(configuration) {
        setg(eback(), eback(), eback() + configuration.find("unfinished") + 3);
      }
      int_type underflow() override { throw std::ios_base::failure("read failed"); }
    } buffer(
        "[DEFAULT]\nConnectionType=acceptor\nBeginString=FIX.4.2\nValue=new\n"
        "[SESSION]\nSenderCompID=LOCAL\nTargetCompID=NEW\nValue=unfinished\n"
        "[SESSION]\nSenderCompID=LOCAL\nTargetCompID=UNREAD\n");
    std::istream input(&buffer);
    std::istringstream initial(
        "[DEFAULT]\nConnectionType=initiator\nBeginString=FIX.4.2\nValue=old\n"
        "[SESSION]\nSenderCompID=LOCAL\nTargetCompID=OLD\n");
    SessionSettings object(initial);
    std::ostringstream before;
    before << object;

    CHECK_THROWS_AS(input >> object, ConfigError);
    CHECK(input.bad());
    std::ostringstream after;
    after << object;
    CHECK(after.str() == before.str());
  }

  SECTION("rejectFailedStreamState") {
    const auto state = GENERATE(std::ios::failbit, std::ios::badbit | std::ios::eofbit);
    std::istringstream input("[DEFAULT]\nValue=new\n");
    input.setstate(state);
    SessionSettings object;
    Dictionary defaults;
    defaults.setString("Value", "old");
    object.set(defaults);

    CHECK_THROWS_AS(input >> object, ConfigError);
    CHECK(object.get().getString("Value") == "old");
    CHECK(object.size() == 0);
  }

  SECTION("readFromIstream") {
    SessionSettings object;
    std::string configuration = "[DEFAULT]\n"
                                "ConnectionType=initiator\n"
                                "BeginString=FIX.4.0\n"
                                "Value=4\n"
                                "Empty=\n"
                                "[SESSION]\n"
                                "BeginString=FIX.4.2\n"
                                "SenderCompID=ISLD\n"
                                "TargetCompID=TW\n"
                                "Value=1\n"
                                "# this is a comment\n"
                                "[SESSION]\n"
                                "BeginString=FIX.4.1\n"
                                "SenderCompID=ISLD\n"
                                "TargetCompID=WT\n"
                                "Value=2\n"
                                "[SESSION]\n"
                                "SenderCompID=ARCA\n"
                                "TargetCompID=TW\n"
                                "Value=3\n"
                                "[SESSION]\n"
                                "SenderCompID=ARCA\n"
                                "TargetCompID=WT\n"
                                "[SESSION]\n"
                                "SenderCompID=NYSE\n"
                                "TargetCompID=TW\n"
                                "SessionQualifier=QUAL1\n"
                                "Value=5\n"
                                "[SESSION]\n"
                                "SenderCompID=NYSE\n"
                                "TargetCompID=TW\n"
                                "SessionQualifier=QUAL2\n"
                                "Value=6\n"
                                "[SESSION]\n"
                                "BeginString=FIXT.1.1\n"
                                "SenderCompID=NYSE\n"
                                "TargetCompID=TW\n";

    std::istringstream input(configuration);

    input >> object;

    SessionID session1(BeginString("FIX.4.2"), SenderCompID("ISLD"), TargetCompID("TW"));
    SessionID session2(BeginString("FIX.4.1"), SenderCompID("ISLD"), TargetCompID("WT"));
    SessionID session3(BeginString("FIX.4.0"), SenderCompID("ARCA"), TargetCompID("TW"));
    SessionID session4(BeginString("FIX.4.0"), SenderCompID("ARCA"), TargetCompID("WT"));
    SessionID session5(BeginString("FIX.4.0"), SenderCompID("NYSE"), TargetCompID("TW"), "QUAL1");
    SessionID session6(BeginString("FIX.4.0"), SenderCompID("NYSE"), TargetCompID("TW"), "QUAL2");

    CHECK("" == object.get().getString("Empty"));

    CHECK(4 == object.get().getInt("Value"));
    CHECK(1 == object.get(session1).getInt("Value"));
    CHECK(2 == object.get(session2).getInt("Value"));
    CHECK(3 == object.get(session3).getInt("Value"));
    CHECK(4 == object.get(session4).getInt("Value"));
    CHECK(5 == object.get(session5).getInt("Value"));
    CHECK(6 == object.get(session6).getInt("Value"));

    // case insensitivity
    CHECK(4 == object.get().getInt("value"));
    CHECK(1 == object.get(session1).getInt("value"));
    CHECK(2 == object.get(session2).getInt("value"));
    CHECK(3 == object.get(session3).getInt("value"));
    CHECK(4 == object.get(session4).getInt("value"));
    CHECK(5 == object.get(session5).getInt("value"));
    CHECK(6 == object.get(session6).getInt("value"));

    CHECK(4 == object.get().getInt("VALUE"));
    CHECK(1 == object.get(session1).getInt("VALUE"));
    CHECK(2 == object.get(session2).getInt("VALUE"));
    CHECK(3 == object.get(session3).getInt("VALUE"));
    CHECK(4 == object.get(session4).getInt("VALUE"));
    CHECK(5 == object.get(session5).getInt("VALUE"));
    CHECK(6 == object.get(session6).getInt("VALUE"));
  }

  SECTION("readFromIstreamDefaultLast") {
    SessionSettings object;
    std::string configuration = "[SESSION]\n"
                                "BeginString=FIX.4.2\n"
                                "SenderCompID=ISLD\n"
                                "TargetCompID=TW\n"
                                "Value=1\n"
                                "# this is a comment\n"
                                "[SESSION]\n"
                                "BeginString=FIX.4.1\n"
                                "SenderCompID=ISLD\n"
                                "TargetCompID=WT\n"
                                "Value=2\n"
                                "[SESSION]\n"
                                "SenderCompID=ARCA\n"
                                "TargetCompID=TW\n"
                                "Value=3\n"
                                "[SESSION]\n"
                                "SenderCompID=ARCA\n"
                                "TargetCompID=WT\n"
                                "[SESSION]\n"
                                "SenderCompID=NYSE\n"
                                "TargetCompID=TW\n"
                                "SessionQualifier=QUAL1\n"
                                "Value=5\n"
                                "[SESSION]\n"
                                "SenderCompID=NYSE\n"
                                "TargetCompID=TW\n"
                                "SessionQualifier=QUAL2\n"
                                "Value=6\n"
                                "[DEFAULT]\n"
                                "ConnectionType=initiator\n"
                                "BeginString=FIX.4.0\n"
                                "Value=4\n"
                                "Empty=\n";

    std::istringstream input(configuration);

    input >> object;

    SessionID session1(BeginString("FIX.4.2"), SenderCompID("ISLD"), TargetCompID("TW"));
    SessionID session2(BeginString("FIX.4.1"), SenderCompID("ISLD"), TargetCompID("WT"));
    SessionID session3(BeginString("FIX.4.0"), SenderCompID("ARCA"), TargetCompID("TW"));
    SessionID session4(BeginString("FIX.4.0"), SenderCompID("ARCA"), TargetCompID("WT"));
    SessionID session5(BeginString("FIX.4.0"), SenderCompID("NYSE"), TargetCompID("TW"), "QUAL1");
    SessionID session6(BeginString("FIX.4.0"), SenderCompID("NYSE"), TargetCompID("TW"), "QUAL2");

    CHECK("" == object.get().getString("Empty"));

    CHECK(4 == object.get().getInt("Value"));
    CHECK(1 == object.get(session1).getInt("Value"));
    CHECK(2 == object.get(session2).getInt("Value"));
    CHECK(3 == object.get(session3).getInt("Value"));
    CHECK(4 == object.get(session4).getInt("Value"));
    CHECK(5 == object.get(session5).getInt("Value"));
    CHECK(6 == object.get(session6).getInt("Value"));

    // case insensitivity
    CHECK(4 == object.get().getInt("value"));
    CHECK(1 == object.get(session1).getInt("value"));
    CHECK(2 == object.get(session2).getInt("value"));
    CHECK(3 == object.get(session3).getInt("value"));
    CHECK(4 == object.get(session4).getInt("value"));
    CHECK(5 == object.get(session5).getInt("value"));
    CHECK(6 == object.get(session6).getInt("value"));

    CHECK(4 == object.get().getInt("VALUE"));
    CHECK(1 == object.get(session1).getInt("VALUE"));
    CHECK(2 == object.get(session2).getInt("VALUE"));
    CHECK(3 == object.get(session3).getInt("VALUE"));
    CHECK(4 == object.get(session4).getInt("VALUE"));
    CHECK(5 == object.get(session5).getInt("VALUE"));
    CHECK(6 == object.get(session6).getInt("VALUE"));
  }

  SECTION("readFromIstreamDuplicateSession") {
    SessionSettings object;
    std::string configuration = "[DEFAULT]\n"
                                "ConnectionType=initiator\n"
                                "BeginString=FIX.4.0\n"
                                "[SESSION]\n"
                                "BeginString=FIX.4.2\n"
                                "SenderCompID=ISLD\n"
                                "TargetCompID=TW\n"
                                "[SESSION]\n"
                                "BeginString=FIX.4.2\n"
                                "SenderCompID=ISLD\n"
                                "TargetCompID=TW\n";

    std::istringstream input(configuration);

    CHECK_THROWS_AS(input >> object, ConfigError);
  }

  SECTION("stripSpaces") {
    SessionSettings object;
    std::string configuration = " [ DEFAULT ] \n"
                                "StringValue1=StringValue1\n"
                                "StringValue2=StringValue2 \n"
                                "StringValue3= StringValue3\n"
                                "StringValue4 =StringValue4\n"
                                " StringValue5=StringValue5\n"
                                "  StringValue6  =  StringValue6  \n"
                                "IntegerValue1=1\n"
                                "IntegerValue2=2 \n"
                                "IntegerValue3= 3\n"
                                "IntegerValue4 =4\n"
                                " IntegerValue5=5\n"
                                "  IntegerValue6  =  6  \n"
                                "  [  SESSION  ]  \n"
                                "ConnectionType=initiator\n"
                                "BeginString=FIX.4.0\n"
                                "SenderCompID=ISLD\n"
                                "TargetCompID=TW\n"
                                "DoubleValue1=1.1\n"
                                "DoubleValue2=2.2 \n"
                                "DoubleValue3= 3.3\n"
                                "DoubleValue4 =4.4\n"
                                " DoubleValue5=5.5\n"
                                "  DoubleValue6  =  6.6  \n";

    std::istringstream input(configuration);

    input >> object;

    SessionID session1(BeginString("FIX.4.0"), SenderCompID("ISLD"), TargetCompID("TW"));

    CHECK("StringValue1" == object.get().getString("StringValue1"));
    CHECK("StringValue2" == object.get().getString("StringValue2"));
    CHECK("StringValue3" == object.get().getString("StringValue3"));
    CHECK("StringValue4" == object.get().getString("StringValue4"));
    CHECK("StringValue5" == object.get().getString("StringValue5"));
    CHECK("StringValue6" == object.get().getString("StringValue6"));

    CHECK(1 == object.get().getInt("IntegerValue1"));
    CHECK(2 == object.get().getInt("IntegerValue2"));
    CHECK(3 == object.get().getInt("IntegerValue3"));
    CHECK(4 == object.get().getInt("IntegerValue4"));
    CHECK(5 == object.get().getInt("IntegerValue5"));
    CHECK(6 == object.get().getInt("IntegerValue6"));

    CHECK(1.1 == object.get(session1).getDouble("DoubleValue1"));
    CHECK(2.2 == object.get(session1).getDouble("DoubleValue2"));
    CHECK(3.3 == object.get(session1).getDouble("DoubleValue3"));
    CHECK(4.4 == object.get(session1).getDouble("DoubleValue4"));
    CHECK(5.5 == object.get(session1).getDouble("DoubleValue5"));
    CHECK(6.6 == object.get(session1).getDouble("DoubleValue6"));
  }

  SECTION("writeToOstream") {
    SessionSettings object;
    std::stringstream istream;
    istream << "[DEFAULT]" << std::endl
            << "BEGINSTRING=FIX.4.0" << std::endl
            << "CONNECTIONTYPE=initiator" << std::endl
            << "VALUE=4" << std::endl
            << std::endl
            << "[SESSION]" << std::endl
            << "BEGINSTRING=FIX.4.2" << std::endl
            << "SENDERCOMPID=SENDER1" << std::endl
            << "TARGETCOMPID=TARGET1" << std::endl
            << "VALUE=1" << std::endl
            << std::endl
            << "[SESSION]" << std::endl
            << "BEGINSTRING=FIX.4.2" << std::endl
            << "SENDERCOMPID=SENDER2" << std::endl
            << "TARGETCOMPID=TARGET2" << std::endl
            << "VALUE=2" << std::endl
            << std::endl;

    istream >> object;

    std::stringstream ostream;
    ostream << object;
    CHECK(istream.str() == ostream.str());
  }

  SECTION("validate") {
    SessionSettings object;
    SessionID sessionID = SessionID("FIX.4.2", "SenderCompID", "TargetCompID");

    // ConnectionType not set
    Dictionary dictionary;
    CHECK_THROWS_AS(object.set(sessionID, dictionary), ConfigError);

    // ConnectionType set to invalid value
    dictionary.setString(CONNECTION_TYPE, "badvalue");
    CHECK_THROWS_AS(object.set(sessionID, dictionary), ConfigError);

    // ConnectionType set to valid value
    dictionary.setString(CONNECTION_TYPE, "initiator");
    object.set(sessionID, dictionary);

    // Invalid BeginString
    sessionID = SessionID("FIX4.2", "SenderCompID", "TargetCompID");
    CHECK_THROWS_AS(object.set(sessionID, dictionary), ConfigError);
  }
}
