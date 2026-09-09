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

#include "Application.h"

#include "catch_amalgamated.hpp"
#include "quickfix/DataDictionaryProvider.h"
#include "quickfix/MessageStore.h"
#include "quickfix/Session.h"
#include "quickfix/TimeRange.h"
#include "quickfix/fix42/OrderCancelReject.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {
const std::string symbol = "TEST";

FIX::SessionID sessionID(const std::string &owner) { return FIX::SessionID("FIX.4.2", "VENUE", owner); }

FIX42::NewOrderSingle newOrder(
    const std::string &id,
    double quantity,
    double price,
    const std::string &headerOwner = "SPOOFED") {
  FIX42::NewOrderSingle message(
      FIX::ClOrdID(id),
      FIX::HandlInst(FIX::HandlInst_AUTOMATED_EXECUTION_NO_INTERVENTION),
      FIX::Symbol(symbol),
      FIX::Side(FIX::Side_BUY),
      FIX::TransactTime::now(),
      FIX::OrdType(FIX::OrdType_LIMIT));
  message.set(FIX::OrderQty(quantity));
  message.set(FIX::Price(price));
  message.getHeader().set(FIX::SenderCompID(headerOwner));
  message.getHeader().set(FIX::TargetCompID("VENUE"));
  return message;
}

FIX42::NewOrderSingle newOrder(const std::string &id, const std::string &quantity, double price) {
  FIX42::NewOrderSingle message = newOrder(id, 1.0, price);
  message.setField(FIX::FIELD::OrderQty, quantity);
  return message;
}

FIX42::OrderCancelRequest cancelRequest(const std::string &id, const std::string &requestID) {
  FIX42::OrderCancelRequest message(
      FIX::OrigClOrdID(id),
      FIX::ClOrdID(requestID),
      FIX::Symbol(symbol),
      FIX::Side(FIX::Side_BUY),
      FIX::TransactTime::now());
  return message;
}

void dispatch(::Application &application, const FIX::Message &message, const std::string &owner) {
  static_cast<FIX::Application &>(application).fromApp(message, sessionID(owner));
}

const Order *findOrder(const ::Application &application, const std::string &owner, const std::string &id) {
  try {
    return &application.orderMatcher().find(symbol, Order::buy, owner, id);
  } catch (const std::exception &) {
    return nullptr;
  }
}

std::vector<FIX::Message> storedMessages(FIX::Session &session) {
  std::vector<std::string> wire;
  const FIX::SEQNUM end = session.getStore()->getNextSenderMsgSeqNum();
  if (end > 1) {
    session.getStore()->get(1, end - 1, wire);
  }

  std::vector<FIX::Message> messages;
  for (const std::string &value : wire) {
    messages.emplace_back(value);
  }
  return messages;
}

struct Sessions {
  explicit Sessions(::Application &application)
      : ownerA("FIX.4.2", "VENUE", "OWNER-A"),
        ownerB("FIX.4.2", "VENUE", "OWNER-B"),
        now(FIX::UtcTimeStamp::now()),
        a([this] { return now; }, application, stores, ownerA, dictionaries, range, 0, nullptr),
        b([this] { return now; }, application, stores, ownerB, dictionaries, range, 0, nullptr) {}

  FIX::MemoryStoreFactory stores;
  FIX::DataDictionaryProvider dictionaries;
  FIX::SessionID ownerA;
  FIX::SessionID ownerB;
  FIX::UtcTimeStamp now;
  FIX::TimeRange range{FIX::UtcTimeOnly(), FIX::UtcTimeOnly()};
  FIX::Session a;
  FIX::Session b;
};
} // namespace

TEST_CASE("ordermatch rejects invalid wire economics", "[ordermatch][security]") {
  const std::vector<double> invalidQuantities{
      0.0,
      -1.0,
      0.5,
      1.25,
      std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN()};

  for (std::size_t i = 0; i < invalidQuantities.size(); ++i) {
    ::Application application;
    const std::string id = "Q" + std::to_string(i);
    dispatch(application, newOrder(id, invalidQuantities[i], 10.0), "OWNER");
    CHECK(findOrder(application, "OWNER", id) == nullptr);
  }

  ::Application boundaryApplication;
  const std::string exclusiveLongUpperBound
      = std::numeric_limits<long>::digits == 63 ? "9223372036854775808" : "2147483648";
  dispatch(boundaryApplication, newOrder("boundary", exclusiveLongUpperBound, 10.0), "OWNER");
  CHECK(findOrder(boundaryApplication, "OWNER", "boundary") == nullptr);

  const std::vector<double> invalidPrices{
      0.0,
      -1.0,
      std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN()};
  for (std::size_t i = 0; i < invalidPrices.size(); ++i) {
    ::Application application;
    const std::string id = "P" + std::to_string(i);
    dispatch(application, newOrder(id, 1.0, invalidPrices[i]), "OWNER");
    CHECK(findOrder(application, "OWNER", id) == nullptr);
  }

  ::Application application;
  const std::string largestRepresentable
      = std::numeric_limits<long>::digits == 63 ? "9223372036854774784" : "2147483647";
  dispatch(application, newOrder("largest", largestRepresentable, 10.0), "OWNER");
  REQUIRE(findOrder(application, "OWNER", "largest") != nullptr);
  CHECK(findOrder(application, "OWNER", "largest")->getQuantity() == std::stol(largestRepresentable));
}

TEST_CASE("orders reject invalid construction economics", "[ordermatch][security]") {
  for (double quantity :
       {0.0,
        -1.0,
        0.5,
        1.25,
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
        std::ldexp(1.0, std::numeric_limits<long>::digits)}) {
    CHECK_THROWS_AS(Order("id", symbol, "owner", "target", Order::buy, Order::limit, 10.0, quantity), std::logic_error);
  }

  for (double price : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_THROWS_AS(Order("id", symbol, "owner", "target", Order::buy, Order::limit, price, 1), std::logic_error);
  }
}

TEST_CASE("ordermatch validates fills before mutation", "[ordermatch][security]") {
  for (long invalid : {0L, -1L, 11L}) {
    Order order("id", symbol, "owner", "target", Order::buy, Order::limit, 10.0, 10);
    CHECK_THROWS_AS(order.execute(11.0, invalid), std::logic_error);
    CHECK(order.getOpenQuantity() == 10);
    CHECK(order.getExecutedQuantity() == 0);
    CHECK(order.getAvgExecutedPrice() == 0.0);
    CHECK(order.getLastExecutedPrice() == 0.0);
    CHECK(order.getLastExecutedQuantity() == 0);
  }

  Order order("id", symbol, "owner", "target", Order::buy, Order::limit, 10.0, 10);
  order.execute(10.0, 4);
  CHECK(order.getOpenQuantity() + order.getExecutedQuantity() == order.getQuantity());
  CHECK(order.getOpenQuantity() == 6);
  CHECK(order.getAvgExecutedPrice() == Catch::Approx(10.0));
  order.execute(14.0, 6);
  CHECK(order.getOpenQuantity() + order.getExecutedQuantity() == order.getQuantity());
  CHECK(order.getOpenQuantity() == 0);
  CHECK(order.getExecutedQuantity() == 10);
  CHECK(order.getAvgExecutedPrice() == Catch::Approx(12.4));
  CHECK(order.getLastExecutedPrice() == Catch::Approx(14.0));
  CHECK(order.getLastExecutedQuantity() == 6);
  CHECK(std::isfinite(order.getAvgExecutedPrice()));

  Order maximumPrice("max", symbol, "owner", "target", Order::buy, Order::limit, std::numeric_limits<double>::max(), 2);
  maximumPrice.execute(std::numeric_limits<double>::max(), 2);
  CHECK(std::isfinite(maximumPrice.getAvgExecutedPrice()));
  CHECK(maximumPrice.getAvgExecutedPrice() == std::numeric_limits<double>::max());
}

TEST_CASE("ordermatch scopes active IDs by owner and side", "[ordermatch][security]") {
  OrderMatcher matcher;
  const Order a("same", symbol, "OWNER-A", "VENUE", Order::buy, Order::limit, 11.0, 10);
  const Order b("same", symbol, "OWNER-B", "VENUE", Order::buy, Order::limit, 10.0, 10);
  const Order duplicate("same", symbol, "OWNER-A", "VENUE", Order::buy, Order::limit, 9.0, 10);
  const Order otherSide("same", symbol, "OWNER-A", "VENUE", Order::sell, Order::limit, 12.0, 10);

  REQUIRE(matcher.insert(a));
  CHECK(matcher.insert(b));
  CHECK_FALSE(matcher.insert(duplicate));
  CHECK(matcher.insert(otherSide));

  CHECK(matcher.find(symbol, Order::buy, "OWNER-B", "same").getOwner() == "OWNER-B");
  matcher.erase(b);
  CHECK(matcher.find(symbol, Order::buy, "OWNER-A", "same").getOwner() == "OWNER-A");
  CHECK_THROWS_AS(matcher.find(symbol, Order::buy, "OWNER-B", "same"), std::logic_error);
}

TEST_CASE("ordermatch preserves valid matching", "[ordermatch][security]") {
  OrderMatcher matcher;
  REQUIRE(matcher.insert(Order("buy", symbol, "BUYER", "VENUE", Order::buy, Order::limit, 11.0, 10)));
  REQUIRE(matcher.insert(Order("sell", symbol, "SELLER", "VENUE", Order::sell, Order::limit, 10.0, 4)));

  std::queue<Order> updates;
  REQUIRE(matcher.match(symbol, updates));
  REQUIRE(updates.size() == 2);
  const Order buy = updates.front();
  updates.pop();
  const Order sell = updates.front();
  CHECK(buy.getOpenQuantity() == 6);
  CHECK(buy.getExecutedQuantity() == 4);
  CHECK(buy.getAvgExecutedPrice() == Catch::Approx(10.0));
  CHECK(sell.isFilled());
}

TEST_CASE("ordermatch trusts the session owner and scopes cancellation", "[ordermatch][security]") {
  ::Application application;
  Sessions sessions(application);

  dispatch(application, newOrder("same", 10.0, 11.0, "SPOOFED"), "OWNER-A");
  const Order *placed = findOrder(application, "OWNER-A", "same");
  REQUIRE(placed != nullptr);
  CHECK(placed->getOwner() == "OWNER-A");
  CHECK(placed->getTarget() == "VENUE");
  CHECK(findOrder(application, "SPOOFED", "same") == nullptr);

  dispatch(application, cancelRequest("same", "foreign-cancel"), "OWNER-B");
  CHECK(findOrder(application, "OWNER-A", "same") != nullptr);

  dispatch(application, cancelRequest("missing", "missing-cancel"), "OWNER-B");
  const std::vector<FIX::Message> rejects = storedMessages(sessions.b);
  REQUIRE(rejects.size() == 2);
  for (std::size_t i : {0U, 1U}) {
    FIX::MsgType type;
    FIX::CxlRejReason reason;
    FIX::CxlRejResponseTo responseTo;
    FIX::Text text;
    FIX::ClOrdID requestID;
    FIX::OrigClOrdID originalID;
    FIX::SenderCompID sender;
    FIX::TargetCompID target;
    rejects[i].getHeader().getField(type);
    rejects[i].getHeader().getField(sender);
    rejects[i].getHeader().getField(target);
    rejects[i].getField(reason);
    rejects[i].getField(responseTo);
    rejects[i].getField(text);
    rejects[i].getField(requestID);
    rejects[i].getField(originalID);
    CHECK(type == FIX::MsgType_OrderCancelReject);
    CHECK(reason == FIX::CxlRejReason_UNKNOWN_ORDER);
    CHECK(responseTo == FIX::CxlRejResponseTo_ORDER_CANCEL_REQUEST);
    CHECK(text == "Unknown order");
    CHECK(requestID == (i == 0 ? "foreign-cancel" : "missing-cancel"));
    CHECK(originalID == (i == 0 ? "same" : "missing"));
    CHECK(sender == "VENUE");
    CHECK(target == "OWNER-B");
  }

  dispatch(application, cancelRequest("same", "owner-cancel"), "OWNER-A");
  CHECK(findOrder(application, "OWNER-A", "same") == nullptr);
}
