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

#include "catch_amalgamated.hpp"
#include <MessageCracker.h>
#include <MessageStore.h>
#include <fix40/NewOrderSingle.h>
#include <fix41/NewOrderSingle.h>
#include <fix42/NewOrderSingle.h>
#include <fix43/NewOrderSingle.h>
#include <fix44/NewOrderSingle.h>
#include <fix50/NewOrderSingle.h>
#include <fix50sp1/NewOrderSingle.h>
#include <fix50sp2/NewOrderSingle.h>
#include <fixt11/Logon.h>
#include <type_traits>
#include <utility>

namespace {
template <typename VersionMessage> void checkHeaderTrailer() {
  VersionMessage message(FIX::MsgType("D"));
  CHECK((std::is_same_v<decltype(message.getHeader()), FIX::Header &>));
  CHECK((std::is_same_v<decltype(std::as_const(message).getHeader()), const FIX::Header &>));
  CHECK((std::is_same_v<decltype(message.getTrailer()), FIX::Trailer &>));
  CHECK((std::is_same_v<decltype(std::as_const(message).getTrailer()), const FIX::Trailer &>));
  if constexpr (std::is_same_v<decltype(message.getHeader()), FIX::Header &>) {
    message.getHeader().set(FIX::SenderCompID("sender"));
    message.getTrailer().set(FIX::CheckSum(123));
    FIX::SenderCompID sender;
    FIX::CheckSum checksum;
    CHECK(message.getHeader().get(sender).getValue() == "sender");
    CHECK(message.getHeader().isSet(sender));
    CHECK(message.getHeader().getIfSet(sender));
    CHECK(message.getTrailer().get(checksum).getValue() == 123);
    CHECK(message.getTrailer().isSet(checksum));
    CHECK(message.getTrailer().getIfSet(checksum));
    FIX::StringField custom(9001, "custom");
    message.getHeader().set(custom);
    custom.setString("changed");
    CHECK(message.getHeader().get(custom).getString() == "custom");
    CHECK(message.getHeader().isSet(custom));
    FIX::StringField missing(9002, "unchanged");
    CHECK_FALSE(message.getTrailer().getIfSet(missing));
    CHECK(missing.getString() == "unchanged");
    CHECK_THROWS_AS(message.getTrailer().get(missing), FIX::FieldNotFound);
  }
}

template <typename Cracker, typename Input, typename Typed> void checkCracker() {
  struct Callback : Cracker {
    bool called = false;
    bool shouldThrow = false;
    void onMessage(const Typed &message, const FIX::SessionID &sessionID) override {
      called = true;
      CHECK(typeid(message) == typeid(Typed));
      CHECK(sessionID.getSenderCompID() == "sender");
      CHECK(message.getField(FIX::FIELD::Symbol) == "MSFT");
    }
    void onMessage(Typed &message, const FIX::SessionID &sessionID) override {
      onMessage(std::as_const(message), sessionID);
      message.setField(FIX::Symbol("IBM"));
      message.getHeader().setField(FIX::SenderCompID("changed"));
      message.getTrailer().setField(FIX::CheckSum(123));
      FIX::Group group(453, 448);
      group.setField(FIX::PartyID("party"));
      message.addGroup(group);
      if (shouldThrow) {
        throw std::runtime_error("callback failure");
      }
    }
  } cracker;
  Input message = Typed();
  message.setField(FIX::Symbol("MSFT"));
  const FIX::SessionID sessionID("FIX.4.2", "sender", "target");
  SECTION("const callback receives a genuine typed message") {
    cracker.crack(std::as_const(message), sessionID);
    CHECK(cracker.called);
    CHECK(message.getField(FIX::FIELD::Symbol) == "MSFT");
  }
  SECTION("mutable callback copies all changes back") {
    cracker.shouldThrow = GENERATE(false, true);
    if (cracker.shouldThrow) {
      CHECK_THROWS_WITH(cracker.crack(message, sessionID), "callback failure");
    } else {
      CHECK_NOTHROW(cracker.crack(message, sessionID));
    }
    CHECK(cracker.called);
    CHECK(message.getField(FIX::FIELD::Symbol) == "IBM");
    CHECK(message.getHeader().getField(FIX::FIELD::SenderCompID) == "changed");
    CHECK(message.getTrailer().getField(FIX::FIELD::CheckSum) == "123");
    FIX::Group group(453, 448);
    REQUIRE(message.groupCount(453) == 1);
    message.getGroup(1, group);
    CHECK(group.getField(FIX::FIELD::PartyID) == "party");
  }
}
} // namespace

TEST_CASE("MessageHeaderTrailerTests") {
  checkHeaderTrailer<FIX40::Message>();
  checkHeaderTrailer<FIX41::Message>();
  checkHeaderTrailer<FIX42::Message>();
  checkHeaderTrailer<FIX43::Message>();
  checkHeaderTrailer<FIX44::Message>();
  checkHeaderTrailer<FIX50::Message>();
  checkHeaderTrailer<FIX50SP1::Message>();
  checkHeaderTrailer<FIX50SP2::Message>();
  checkHeaderTrailer<FIXT11::Message>();
}

TEST_CASE("MessageCrackerTests") {
  SECTION("common FIX 4.0") { checkCracker<FIX::MessageCracker, FIX::Message, FIX40::NewOrderSingle>(); }
  SECTION("common FIX 4.1") { checkCracker<FIX::MessageCracker, FIX::Message, FIX41::NewOrderSingle>(); }
  SECTION("common FIX 4.2") { checkCracker<FIX::MessageCracker, FIX::Message, FIX42::NewOrderSingle>(); }
  SECTION("common FIX 4.3") { checkCracker<FIX::MessageCracker, FIX::Message, FIX43::NewOrderSingle>(); }
  SECTION("common FIX 4.4") { checkCracker<FIX::MessageCracker, FIX::Message, FIX44::NewOrderSingle>(); }
  SECTION("common FIX 5.0 application version") {
    checkCracker<FIX::MessageCracker, FIX::Message, FIX50::NewOrderSingle>();
  }
  SECTION("common FIX 5.0 SP1 application version") {
    checkCracker<FIX::MessageCracker, FIX::Message, FIX50SP1::NewOrderSingle>();
  }
  SECTION("common FIX 5.0 SP2 application version") {
    checkCracker<FIX::MessageCracker, FIX::Message, FIX50SP2::NewOrderSingle>();
  }
  SECTION("common FIXT admin") { checkCracker<FIX::MessageCracker, FIX::Message, FIXT11::Logon>(); }
  SECTION("generated FIX 4.0") { checkCracker<FIX40::MessageCracker, FIX40::Message, FIX40::NewOrderSingle>(); }
  SECTION("generated FIX 4.1") { checkCracker<FIX41::MessageCracker, FIX41::Message, FIX41::NewOrderSingle>(); }
  SECTION("generated FIX 4.2") { checkCracker<FIX42::MessageCracker, FIX42::Message, FIX42::NewOrderSingle>(); }
  SECTION("generated FIX 4.3") { checkCracker<FIX43::MessageCracker, FIX43::Message, FIX43::NewOrderSingle>(); }
  SECTION("generated FIX 4.4") { checkCracker<FIX44::MessageCracker, FIX44::Message, FIX44::NewOrderSingle>(); }
  SECTION("generated FIX 5.0") { checkCracker<FIX50::MessageCracker, FIX50::Message, FIX50::NewOrderSingle>(); }
  SECTION("generated FIX 5.0 SP1") {
    checkCracker<FIX50SP1::MessageCracker, FIX50SP1::Message, FIX50SP1::NewOrderSingle>();
  }
  SECTION("generated FIX 5.0 SP2") {
    checkCracker<FIX50SP2::MessageCracker, FIX50SP2::Message, FIX50SP2::NewOrderSingle>();
  }
  SECTION("generated FIXT 1.1") { checkCracker<FIXT11::MessageCracker, FIXT11::Message, FIXT11::Logon>(); }
  SECTION("generated FIX 4.2 generic input") {
    checkCracker<FIX42::MessageCracker, FIX::Message, FIX42::NewOrderSingle>();
  }
  SECTION("generated FIX 5.0 SP2 generic input") {
    checkCracker<FIX50SP2::MessageCracker, FIX::Message, FIX50SP2::NewOrderSingle>();
  }
  SECTION("generated FIXT generic input") { checkCracker<FIXT11::MessageCracker, FIX::Message, FIXT11::Logon>(); }
  SECTION("FIXT falls back to the session application version") {
    struct Callback : FIX::MessageCracker {
      int called = 0;
      void onMessage(const FIX44::NewOrderSingle &message, const FIX::SessionID &) override {
        CHECK(typeid(message) == typeid(FIX44::NewOrderSingle));
        ++called;
      }
      void onMessage(FIX44::NewOrderSingle &message, const FIX::SessionID &sessionID) override {
        onMessage(std::as_const(message), sessionID);
        message.setField(FIX::Symbol("IBM"));
      }
    } cracker;
    const FIX::SessionID sessionID("FIXT.1.1", "sender", "target");
    FIX::NullApplication application;
    FIX::MemoryStoreFactory factory;
    FIX::Session session(
        [] { return FIX::UtcTimeStamp::now(); },
        application,
        factory,
        sessionID,
        FIX::DataDictionaryProvider(),
        FIX::TimeRange(FIX::UtcTimeOnly(0, 0, 0), FIX::UtcTimeOnly(0, 0, 0)),
        30,
        nullptr);
    session.setSenderDefaultApplVerID(FIX::ApplVerID_FIX44);
    FIX::Message message = FIX50SP2::NewOrderSingle();
    message.getHeader().removeField(FIX::FIELD::ApplVerID);
    cracker.crack(std::as_const(message), sessionID);
    cracker.crack(message, sessionID);
    CHECK(cracker.called == 2);
    CHECK(message.getField(FIX::FIELD::Symbol) == "IBM");
  }
  SECTION("unknown and unsupported messages") {
    FIX::Message message;
    message.getHeader().setField(FIX::BeginString("FIX.4.2"));
    message.getHeader().setField(FIX::MsgType("unknown"));
    FIX::MessageCracker cracker;
    FIX::SessionID sessionID("FIX.4.2", "sender", "target");
    CHECK_THROWS_AS(cracker.crack(std::as_const(message), sessionID), FIX::UnsupportedMessageType);
    CHECK_THROWS_AS(cracker.crack(message, sessionID), FIX::UnsupportedMessageType);
    message.getHeader().setField(FIX::MsgType("D"));
    CHECK_THROWS_AS(cracker.crack(std::as_const(message), sessionID), FIX::UnsupportedMessageType);
    CHECK_NOTHROW(cracker.crack(message, sessionID));
  }
}
