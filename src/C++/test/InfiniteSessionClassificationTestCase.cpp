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
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "InfiniteSessionClassification.h"
#include "Session.h"
#include "TestHelper.h"

#include <DataDictionaryProvider.h>
#include <MessageStore.h>
#include <Responder.h>
#include <TimeRange.h>
#include <fix42/Heartbeat.h>
#include <fix42/Logon.h>
#include <fix42/Logout.h>
#include <fix42/NewOrderSingle.h>
#include <fix42/Reject.h>
#include <fix42/ResendRequest.h>
#include <fix42/SequenceReset.h>
#include <fix42/TestRequest.h>

#include "catch_amalgamated.hpp"

#include <algorithm>

namespace FIX {
class InfiniteSessionClassificationTestAccess {
public:
  static InfiniteAtHeadBinding atHead(std::uint8_t fill) {
    std::array<std::uint8_t, 32> value{};
    value.fill(fill);
    return InfiniteAtHeadBinding(value);
  }

  static InfiniteSessionClassification classify(
      Session &session,
      const InfiniteAtHeadBinding &atHead,
      const std::string &bytes,
      const UtcTimeStamp &now) {
    return session.classifyInfiniteFrame(atHead, bytes, now);
  }

  static InfiniteEffectAuthorization authorization(const InfiniteSessionClassification &classification) {
    return InfiniteEffectAuthorization(
        classification.m_binding,
        classification.m_expected,
        classification.m_actionData);
  }

  static InfiniteEffectAuthorization tamperedAuthorization(const InfiniteSessionClassification &classification) {
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    infiniteActionPlan(authorization.m_actionData).failure = "tampered";
    return authorization;
  }

  static InfiniteEffectAuthorization mismatchedActionAuthorization(
      const InfiniteSessionClassification &classification) {
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    authorization.m_action = InfiniteSessionActionKind::Failure;
    return authorization;
  }

  static InfiniteEffectAuthorization consumedAuthorization(const InfiniteSessionClassification &classification) {
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    authorization.m_consumed = true;
    return authorization;
  }

  static InfiniteExpectedSessionState state(const Session &session) { return session.currentInfiniteExpectedState(); }

  static void setSessionTime(Session &session, const TimeRange &sessionTime) {
    session.m_isNonStopSession = false;
    session.m_sessionTime = sessionTime;
  }

  static void apply(
      Session &session,
      const InfiniteSessionClassification &classification,
      InfiniteEffectAuthorization &&authorization) {
    session.applyInfiniteClassification(classification, std::move(authorization));
  }
};
} // namespace FIX

namespace {
using namespace FIX;

void fillHeader(Header &header, const char *sender, const char *target, SEQNUM sequence) {
  header.setField(SenderCompID(sender));
  header.setField(TargetCompID(target));
  header.setField(SendingTime(UtcTimeStamp(8, 8, 8, 25, 8, 2026)));
  header.setField(MsgSeqNum(sequence));
}

template <typename MessageType> MessageType finish(MessageType message, SEQNUM sequence) {
  fillHeader(message.getHeader(), "PARTICIPANT", "VENUE", sequence);
  message.getHeader().setField(BodyLength(message.bodyLength()));
  message.getTrailer().setField(CheckSum(message.checkSum()));
  return message;
}

FIX42::NewOrderSingle applicationMessage(SEQNUM sequence) {
  return finish(
      FIX42::NewOrderSingle(
          ClOrdID("ID"),
          HandlInst('1'),
          Symbol("SYMBOL"),
          Side(Side_BUY),
          TransactTime(UtcTimeStamp(8, 8, 8, 25, 8, 2026)),
          OrdType(OrdType_MARKET)),
      sequence);
}

struct RecordingEndpoint : NullApplication, Responder {
  bool send(const std::string &bytes) override {
    if (throwSend) {
      throw IOException("send failed");
    }
    if (!sendSucceeds) {
      return false;
    }
    outputs.push_back(bytes);
    return true;
  }

  void disconnect() override { ++disconnects; }
  void onLogon(const SessionID &) override { ++logonCalls; }
  void onLogout(const SessionID &) override { ++logoutCalls; }
  void toAdmin(Message &message, const SessionID &) override {
    ++toAdminCalls;
    if (mutateToAdmin) {
      message.setField(Text("changed"));
    }
  }
  void toApp(Message &message, const SessionID &) override {
    ++toAppCalls;
    if (mutateToApp) {
      message.setField(Text("changed"));
    }
  }
  void fromAdmin(const Message &, const SessionID &) override { ++fromAdminCalls; }
  void fromApp(const Message &, const SessionID &) override {
    ++fromAppCalls;
    if (throwFromApp) {
      throw UnsupportedMessageType();
    }
    if (reenterSession) {
      reenterSession->setNextTargetMsgSeqNum(reenterSession->getExpectedTargetNum() + 1);
    }
  }

  std::vector<std::string> outputs;
  int disconnects{0};
  int logonCalls{0};
  int logoutCalls{0};
  int toAdminCalls{0};
  int toAppCalls{0};
  int fromAdminCalls{0};
  int fromAppCalls{0};
  bool throwFromApp{false};
  bool throwSend{false};
  bool sendSucceeds{true};
  bool mutateToAdmin{false};
  bool mutateToApp{false};
  Session *reenterSession{nullptr};
};

struct RecordingStore : MemoryStore {
  explicit RecordingStore(const UtcTimeStamp &now)
      : MemoryStore(now) {}

  bool set(SEQNUM sequence, const std::string &message) override {
    if (throwSet) {
      throw IOException("set failed");
    }
    if (!setSucceeds) {
      return false;
    }
    ++writes;
    return MemoryStore::set(sequence, message);
  }
  void setNextSenderMsgSeqNum(SEQNUM sequence) override {
    ++writes;
    MemoryStore::setNextSenderMsgSeqNum(sequence);
  }
  void setNextTargetMsgSeqNum(SEQNUM sequence) override {
    if (throwTargetWrite) {
      throw IOException("target sequence write failed");
    }
    ++writes;
    MemoryStore::setNextTargetMsgSeqNum(sequence);
  }
  void incrNextSenderMsgSeqNum() override {
    ++writes;
    MemoryStore::incrNextSenderMsgSeqNum();
  }
  void incrNextTargetMsgSeqNum() override {
    ++writes;
    MemoryStore::incrNextTargetMsgSeqNum();
  }
  void reset(const UtcTimeStamp &now) override {
    ++writes;
    MemoryStore::reset(now);
  }
  void refresh() override {
    ++writes;
    MemoryStore::refresh();
  }

  SEQNUM getNextSenderMsgSeqNum() const override {
    if (throwSnapshot) {
      throw IOException("sender snapshot failed");
    }
    return MemoryStore::getNextSenderMsgSeqNum();
  }
  SEQNUM getNextTargetMsgSeqNum() const override {
    if (throwSnapshot) {
      throw IOException("target snapshot failed");
    }
    return MemoryStore::getNextTargetMsgSeqNum();
  }
  UtcTimeStamp getCreationTime() const override {
    if (throwSnapshot) {
      throw IOException("creation snapshot failed");
    }
    return MemoryStore::getCreationTime();
  }

  int writes{0};
  bool throwSet{false};
  bool setSucceeds{true};
  bool throwTargetWrite{false};
  bool throwSnapshot{false};
};

struct RecordingStoreFactory : MessageStoreFactory {
  MessageStore *create(const UtcTimeStamp &now, const SessionID &) override {
    store = new RecordingStore(now);
    return store;
  }
  void destroy(MessageStore *messageStore) override {
    delete messageStore;
    store = nullptr;
  }

  RecordingStore *store{nullptr};
};

struct RecordingLog : Log {
  void clear() override { kinds.push_back("clear"); }
  void backup() override { kinds.push_back("backup"); }
  void onIncoming(const std::string &bytes) override {
    kinds.push_back("incoming");
    values.push_back(bytes);
  }
  void onOutgoing(const std::string &bytes) override {
    kinds.push_back("outgoing");
    values.push_back(bytes);
  }
  void onEvent(const std::string &event) override {
    kinds.push_back("event");
    values.push_back(event);
  }

  std::vector<std::string> kinds;
  std::vector<std::string> values;
};

struct RecordingLogFactory : LogFactory {
  Log *create() override {
    log = new RecordingLog();
    return log;
  }
  Log *create(const SessionID &) override {
    log = new RecordingLog();
    return log;
  }
  void destroy(Log *value) override {
    delete value;
    log = nullptr;
  }

  RecordingLog *log{nullptr};
};

struct EffectSnapshot {
  InfiniteExpectedSessionState state;
  std::vector<std::string> outputs;
  int disconnects;
  int logonCalls;
  int logoutCalls;
  int toAdminCalls;
  int toAppCalls;
  int fromAdminCalls;
  int fromAppCalls;
  int storeWrites;
  std::vector<std::string> logKinds;
  std::vector<std::string> logValues;

  bool operator==(const EffectSnapshot &rhs) const {
    return state == rhs.state && outputs == rhs.outputs && disconnects == rhs.disconnects
           && logonCalls == rhs.logonCalls && logoutCalls == rhs.logoutCalls && toAdminCalls == rhs.toAdminCalls
           && toAppCalls == rhs.toAppCalls && fromAdminCalls == rhs.fromAdminCalls && fromAppCalls == rhs.fromAppCalls
           && storeWrites == rhs.storeWrites && logKinds == rhs.logKinds && logValues == rhs.logValues;
  }
};

struct Fixture {
  Fixture()
      : now(8, 8, 8, 25, 8, 2026),
        sessionId(BeginString_FIX42, "VENUE", "PARTICIPANT"),
        sessionTime(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0)),
        session(
            [this]() { return now; },
            endpoint,
            storeFactory,
            sessionId,
            dictionaries,
            sessionTime,
            0,
            &logFactory) {
    dictionaries.addTransportDataDictionary(sessionId.getBeginString(), TestSettings::pathForSpec("FIX42"));
    session.setDataDictionaryProvider(dictionaries);
    session.setResponder(&endpoint);

    auto logon = finish(FIX42::Logon(EncryptMethod(0), HeartBtInt(30)), 1);
    session.next(logon, now);
    clearEffects();
  }

  void clearEffects() {
    endpoint.outputs.clear();
    endpoint.disconnects = 0;
    endpoint.logonCalls = 0;
    endpoint.logoutCalls = 0;
    endpoint.toAdminCalls = 0;
    endpoint.toAppCalls = 0;
    endpoint.fromAdminCalls = 0;
    endpoint.fromAppCalls = 0;
    logFactory.log->kinds.clear();
    logFactory.log->values.clear();
    storeFactory.store->writes = 0;
  }

  EffectSnapshot snapshot() {
    return EffectSnapshot{
        InfiniteSessionClassificationTestAccess::state(session),
        endpoint.outputs,
        endpoint.disconnects,
        endpoint.logonCalls,
        endpoint.logoutCalls,
        endpoint.toAdminCalls,
        endpoint.toAppCalls,
        endpoint.fromAdminCalls,
        endpoint.fromAppCalls,
        storeFactory.store->writes,
        logFactory.log->kinds,
        logFactory.log->values};
  }

  UtcTimeStamp now;
  RecordingEndpoint endpoint;
  RecordingStoreFactory storeFactory;
  RecordingLogFactory logFactory;
  SessionID sessionId;
  DataDictionaryProvider dictionaries;
  TimeRange sessionTime;
  Session session;
};

TEST_CASE_METHOD(Fixture, "InfiniteSessionClassificationTests", "[infinite][session]") {
  SECTION("classification is effect free and authorized application is one use") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x11);
    const auto message = applicationMessage(before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    CHECK(classification.kind() == InfiniteSessionActionKind::Application);
    CHECK(std::holds_alternative<InfiniteApplicationData>(classification.actionData()));
    CHECK(snapshot() == before);

    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence + 1);
    CHECK(endpoint.fromAppCalls == before.fromAppCalls + 1);
    REQUIRE(logFactory.log->kinds.size() == before.logKinds.size() + 1);
    CHECK(logFactory.log->kinds.back() == "incoming");
    CHECK(logFactory.log->values.back() == message.toString());

    const auto nextBinding = InfiniteSessionClassificationTestAccess::atHead(0x12);
    const auto nextMessage = applicationMessage(session.getExpectedTargetNum());
    const auto nextClassification
        = InfiniteSessionClassificationTestAccess::classify(session, nextBinding, nextMessage.toString(), now);
    CHECK(nextClassification.expected() == infiniteActionPlan(classification.actionData()).resultingState);

    const auto afterFirstApply = snapshot();
    session.setNextTargetMsgSeqNum(before.state.targetSequence);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(endpoint.fromAppCalls == afterFirstApply.fromAppCalls);
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence);
  }

  SECTION("stale or mismatched authorization has no effect") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x22);
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    session.setNextTargetMsgSeqNum(session.getExpectedTargetNum() + 1);
    const auto stale = snapshot();
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(snapshot() == stale);

    session.setNextTargetMsgSeqNum(classification.expected().targetSequence);
    const auto otherBinding = InfiniteSessionClassificationTestAccess::atHead(0x23);
    const auto other
        = InfiniteSessionClassificationTestAccess::classify(session, otherBinding, message.toString(), now);
    auto mismatchedAuthorization = InfiniteSessionClassificationTestAccess::authorization(other);
    const auto mismatched = snapshot();
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(mismatchedAuthorization));
    CHECK(snapshot() == mismatched);

    auto tamperedAuthorization = InfiniteSessionClassificationTestAccess::tamperedAuthorization(classification);
    const auto tampered = snapshot();
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(tamperedAuthorization));
    CHECK(snapshot() == tampered);

    auto mismatchedAction = InfiniteSessionClassificationTestAccess::mismatchedActionAuthorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(mismatchedAction));
    CHECK(snapshot() == tampered);

    auto consumed = InfiniteSessionClassificationTestAccess::consumedAuthorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(consumed));
    CHECK(snapshot() == tampered);
  }

  SECTION("the supported session families have a closed classification") {
    const auto sequence = session.getExpectedTargetNum();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x33);

    auto heartbeat = finish(FIX42::Heartbeat(), sequence);
    auto testRequest = finish(FIX42::TestRequest(TestReqID("TEST")), sequence);
    auto sequenceReset = finish(FIX42::SequenceReset(NewSeqNo(sequence + 1)), sequence);
    auto resendRequest = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(0)), sequence);
    auto reject = finish(FIX42::Reject(RefSeqNum(1)), sequence);
    const auto before = snapshot();

    CHECK(
        InfiniteSessionClassificationTestAccess::classify(session, binding, heartbeat.toString(), now).kind()
        == InfiniteSessionActionKind::ProtocolControl);
    CHECK(
        std::holds_alternative<InfiniteProtocolControlData>(
            InfiniteSessionClassificationTestAccess::classify(session, binding, heartbeat.toString(), now)
                .actionData()));
    CHECK(
        InfiniteSessionClassificationTestAccess::classify(session, binding, testRequest.toString(), now).kind()
        == InfiniteSessionActionKind::ProtocolControl);
    CHECK(
        InfiniteSessionClassificationTestAccess::classify(session, binding, sequenceReset.toString(), now).kind()
        == InfiniteSessionActionKind::SequenceReset);
    CHECK(
        InfiniteSessionClassificationTestAccess::classify(session, binding, resendRequest.toString(), now).kind()
        == InfiniteSessionActionKind::ResendOrQueuedRelease);
    CHECK(
        InfiniteSessionClassificationTestAccess::classify(session, binding, reject.toString(), now).kind()
        == InfiniteSessionActionKind::ProtocolDisposition);
    CHECK(snapshot() == before);
  }

  SECTION("invalid input is classified without effects") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x44);
    const auto classification = InfiniteSessionClassificationTestAccess::classify(session, binding, "not FIX", now);

    CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
    CHECK(snapshot() == before);
  }

  SECTION("an authorized protocol action applies its captured effects") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x55);
    const auto heartbeat = finish(FIX42::Heartbeat(), before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, heartbeat.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    CHECK(snapshot() == before);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence + 1);
    CHECK(endpoint.fromAdminCalls == before.fromAdminCalls + 1);

    const auto nextBinding = InfiniteSessionClassificationTestAccess::atHead(0x56);
    const auto nextHeartbeat = finish(FIX42::Heartbeat(), session.getExpectedTargetNum());
    const auto nextClassification
        = InfiniteSessionClassificationTestAccess::classify(session, nextBinding, nextHeartbeat.toString(), now);
    CHECK(nextClassification.expected() == infiniteActionPlan(classification.actionData()).resultingState);
  }

  SECTION("authorized TestRequest applies the captured heartbeat only") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x57);
    const auto request = finish(FIX42::TestRequest(TestReqID("TEST")), before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    REQUIRE(endpoint.outputs.size() == before.outputs.size() + 1);
    CHECK(endpoint.outputs.back().find("\00135=0\001") != std::string::npos);
    CHECK(endpoint.outputs.back().find("\001112=TEST\001") != std::string::npos);
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence + 1);
  }

  SECTION("authorized gap fill applies its captured target transition") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x58);
    auto gapFill = finish(FIX42::SequenceReset(NewSeqNo(before.state.targetSequence + 3)), before.state.targetSequence);
    gapFill.set(GapFillFlag(true));
    gapFill.getHeader().setField(BodyLength(gapFill.bodyLength()));
    gapFill.getTrailer().setField(CheckSum(gapFill.checkSum()));
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, gapFill.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedTargetNum() == before.state.targetSequence + 3);
    CHECK(endpoint.fromAdminCalls == before.fromAdminCalls + 1);
  }

  SECTION("authorized Logout applies the captured response and disconnect") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x59);
    const auto logout = finish(FIX42::Logout(), before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, logout.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(endpoint.outputs.size() == before.outputs.size() + 1);
    CHECK(endpoint.disconnects == before.disconnects + 1);
    CHECK(endpoint.fromAdminCalls == before.fromAdminCalls + 1);
  }

  SECTION("authorized ResendRequest applies the bounded captured resend plan") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x5a);
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(0)), before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    REQUIRE(endpoint.outputs.size() == before.outputs.size() + 1);
    CHECK(endpoint.outputs.back().find("\00135=4\001") != std::string::npos);
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence + 1);
  }

  SECTION("authorized Reject applies only its captured consuming transition") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x5b);
    const auto reject = finish(FIX42::Reject(RefSeqNum(1)), before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, reject.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedTargetNum() == before.state.targetSequence + 1);
    CHECK(endpoint.fromAdminCalls == before.fromAdminCalls + 1);
    CHECK(endpoint.outputs == before.outputs);
  }

  SECTION("an authorized frame cannot release an independently queued frame") {
    session.next(applicationMessage(session.getExpectedTargetNum() + 1), now);
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x5c);
    const auto message = applicationMessage(before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    REQUIRE(infiniteActionPlan(classification.actionData()).resultingState.mutableState.queuedMessages.size() == 1);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedTargetNum() == before.state.targetSequence + 1);
    CHECK(endpoint.fromAppCalls == before.fromAppCalls + 1);
    const auto nextBinding = InfiniteSessionClassificationTestAccess::atHead(0x5d);
    const auto next = applicationMessage(session.getExpectedTargetNum());
    const auto nextClassification
        = InfiniteSessionClassificationTestAccess::classify(session, nextBinding, next.toString(), now);
    CHECK(nextClassification.expected().mutableState.queuedMessages.size() == 1);
  }

  SECTION("a too-high frame is discarded and plans request-all-subsequent") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x66);
    const auto message = applicationMessage(before.state.targetSequence + 3);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    REQUIRE(classification.kind() == InfiniteSessionActionKind::ResendOrQueuedRelease);
    CHECK(infiniteActionPlan(classification.actionData()).resultingState.mutableState.queuedMessages.empty());
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    REQUIRE(endpoint.outputs.size() == before.outputs.size() + 1);
    CHECK(
        endpoint.outputs.back().find("\0017=" + std::to_string(before.state.targetSequence) + "\001")
        != std::string::npos);
    CHECK(endpoint.outputs.back().find("\00116=0\001") != std::string::npos);
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence);
  }

  SECTION("too-low application frames are rejected without delivery or sequence advance") {
    session.setNextTargetMsgSeqNum(session.getExpectedTargetNum() + 2);
    clearEffects();
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x67);
    const auto message = applicationMessage(before.state.targetSequence - 1);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    REQUIRE(classification.kind() == InfiniteSessionActionKind::ProtocolDisposition);
    CHECK(snapshot() == before);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedTargetNum() == before.state.targetSequence);
    CHECK(endpoint.fromAppCalls == before.fromAppCalls);
    CHECK(endpoint.disconnects == before.disconnects + 1);
  }

  SECTION("too-low PossDup application frames are ignored without delivery") {
    session.setNextTargetMsgSeqNum(session.getExpectedTargetNum() + 2);
    clearEffects();
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x68);
    auto message = applicationMessage(before.state.targetSequence - 1);
    message.getHeader().setField(PossDupFlag(true));
    message.getHeader().setField(OrigSendingTime(now));
    message.getHeader().setField(BodyLength(message.bodyLength()));
    message.getTrailer().setField(CheckSum(message.checkSum()));
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    REQUIRE(classification.kind() == InfiniteSessionActionKind::ProtocolDisposition);
    CHECK(snapshot() == before);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence);
    CHECK(endpoint.fromAppCalls == before.fromAppCalls);
  }

  SECTION("too-high frames still run CompID validation before resend planning") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x69);
    auto message = applicationMessage(before.state.targetSequence + 3);
    message.getHeader().setField(SenderCompID("WRONG"));
    message.getHeader().setField(BodyLength(message.bodyLength()));
    message.getTrailer().setField(CheckSum(message.checkSum()));
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    REQUIRE(classification.kind() == InfiniteSessionActionKind::ProtocolDisposition);
    for (const auto &effect : infiniteActionPlan(classification.actionData()).effects) {
      CHECK_FALSE((effect.kind == InfiniteEffectKind::Send && effect.bytes.find("\00135=2\001") != std::string::npos));
    }
    CHECK(snapshot() == before);
  }

  SECTION("dictionary-invalid application input plans a protocol disposition") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x6a);
    auto message = applicationMessage(before.state.targetSequence);
    message.removeField(FIELD::Symbol);
    message.getHeader().setField(BodyLength(message.bodyLength()));
    message.getTrailer().setField(CheckSum(message.checkSum()));
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    CHECK(classification.kind() == InfiniteSessionActionKind::ProtocolDisposition);
    CHECK_FALSE(infiniteActionPlan(classification.actionData()).effects.empty());
    CHECK(snapshot() == before);
  }

  SECTION("session-time rejection is classified through the normal gate") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x6b);
    InfiniteSessionClassificationTestAccess::setSessionTime(
        session,
        TimeRange(UtcTimeOnly(7, 0, 0), UtcTimeOnly(9, 0, 0)));
    const UtcTimeStamp outside(10, 0, 0, 25, 8, 2026);
    const auto message = applicationMessage(before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), outside);

    CHECK(classification.kind() == InfiniteSessionActionKind::ProtocolDisposition);
    CHECK(infiniteActionPlan(classification.actionData()).resultingState.targetSequence == 1);
    CHECK(InfiniteSessionClassificationTestAccess::state(session).targetSequence == before.state.targetSequence);
  }

  SECTION("snapshot failures become effect-free failure classifications") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x6c);
    const auto message = applicationMessage(before.state.targetSequence);
    storeFactory.store->throwSnapshot = true;
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);
    storeFactory.store->throwSnapshot = false;

    CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
    CHECK(snapshot() == before);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    storeFactory.store->throwSnapshot = true;
    CHECK_NOTHROW(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    storeFactory.store->throwSnapshot = false;
    CHECK(snapshot() == before);
  }

  SECTION("resend classification captures toApp without invoking it") {
    FIX42::NewOrderSingle outbound(
        ClOrdID("OUT"),
        HandlInst('1'),
        Symbol("SYMBOL"),
        Side(Side_BUY),
        TransactTime(now),
        OrdType(OrdType_MARKET));
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x6d);
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);

    CHECK(endpoint.toAppCalls == 0);
    CHECK(snapshot() == before);
    const auto &plan = infiniteActionPlan(classification.actionData());
    const auto callback = std::find_if(plan.callbacks.begin(), plan.callbacks.end(), [](const auto &value) {
      return value.kind == InfiniteCallbackKind::ToApplication;
    });
    const auto send = std::find_if(plan.effects.begin(), plan.effects.end(), [](const auto &value) {
      return value.kind == InfiniteEffectKind::Send;
    });
    REQUIRE(callback != plan.callbacks.end());
    REQUIRE(send != plan.effects.end());
    CHECK(callback->bytes == send->bytes);

    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(endpoint.toAppCalls == 1);
    REQUIRE_FALSE(endpoint.outputs.empty());
    CHECK(endpoint.outputs.front() == callback->bytes);
  }

  SECTION("callback failure consumes authorization before authoritative effects") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x77);
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();
    endpoint.throwFromApp = true;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    CHECK(session.getExpectedSenderNum() == before.state.senderSequence);
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence);
    CHECK(endpoint.outputs == before.outputs);
    CHECK(endpoint.disconnects == before.disconnects);
    CHECK(storeFactory.store->writes == before.storeWrites);
    CHECK(logFactory.log->kinds == before.logKinds);
    CHECK(logFactory.log->values == before.logValues);
    const auto afterFailure = snapshot();
    endpoint.throwFromApp = false;
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(snapshot() == afterFailure);
  }

  SECTION("callbacks cannot re-enter and mutate the classified session") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x78);
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();
    endpoint.reenterSession = &session;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    endpoint.reenterSession = nullptr;
    CHECK(InfiniteSessionClassificationTestAccess::state(session) == before.state);
    CHECK(storeFactory.store->writes == before.storeWrites);
    CHECK(logFactory.log->kinds == before.logKinds);
    CHECK(endpoint.outputs == before.outputs);
  }

  SECTION("outgoing callback mutation fails before plan effects") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x79);
    const auto request = finish(FIX42::TestRequest(TestReqID("MUTATE")), session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();
    endpoint.mutateToAdmin = true;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    endpoint.mutateToAdmin = false;
    CHECK(InfiniteSessionClassificationTestAccess::state(session) == before.state);
    CHECK(storeFactory.store->writes == before.storeWrites);
    CHECK(logFactory.log->kinds == before.logKinds);
    CHECK(endpoint.outputs == before.outputs);
  }

  SECTION("failed responder send does not advance sequences or revision") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x7a);
    const auto request = finish(FIX42::TestRequest(TestReqID("FAIL-SEND")), session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = InfiniteSessionClassificationTestAccess::state(session);
    endpoint.sendSucceeds = false;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    endpoint.sendSucceeds = true;
    CHECK(InfiniteSessionClassificationTestAccess::state(session) == before);
  }

  SECTION("failed message-store write does not advance sequences or revision") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x7b);
    const auto request = finish(FIX42::TestRequest(TestReqID("FAIL-STORE")), session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = InfiniteSessionClassificationTestAccess::state(session);
    storeFactory.store->setSucceeds = false;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    storeFactory.store->setSucceeds = true;
    CHECK(InfiniteSessionClassificationTestAccess::state(session) == before);
  }
}
} // namespace
