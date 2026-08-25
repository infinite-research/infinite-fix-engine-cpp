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
        classification.m_action,
        classification.m_actionData);
  }

  static InfiniteEffectAuthorization tamperedAuthorization(const InfiniteSessionClassification &classification) {
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    authorization.m_actionData.failure = "tampered";
    return authorization;
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
    outputs.push_back(bytes);
    return true;
  }

  void disconnect() override { ++disconnects; }
  void fromAdmin(const Message &, const SessionID &) override { ++fromAdminCalls; }
  void fromApp(const Message &, const SessionID &) override {
    ++fromAppCalls;
    if (throwFromApp) {
      throw UnsupportedMessageType();
    }
  }

  std::vector<std::string> outputs;
  int disconnects{0};
  int fromAdminCalls{0};
  int fromAppCalls{0};
  bool throwFromApp{false};
};

struct RecordingStore : MemoryStore {
  explicit RecordingStore(const UtcTimeStamp &now)
      : MemoryStore(now) {}

  bool set(SEQNUM sequence, const std::string &message) override {
    ++writes;
    return MemoryStore::set(sequence, message);
  }
  void setNextSenderMsgSeqNum(SEQNUM sequence) override {
    ++writes;
    MemoryStore::setNextSenderMsgSeqNum(sequence);
  }
  void setNextTargetMsgSeqNum(SEQNUM sequence) override {
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

  int writes{0};
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
  void clear() override { ++entries; }
  void backup() override { ++entries; }
  void onIncoming(const std::string &) override { ++entries; }
  void onOutgoing(const std::string &) override { ++entries; }
  void onEvent(const std::string &) override { ++entries; }

  int entries{0};
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
  SEQNUM senderSequence;
  SEQNUM targetSequence;
  std::size_t outputs;
  int disconnects;
  int fromAdminCalls;
  int fromAppCalls;
  int storeWrites;
  int logEntries;

  bool operator==(const EffectSnapshot &rhs) const {
    return senderSequence == rhs.senderSequence && targetSequence == rhs.targetSequence && outputs == rhs.outputs
           && disconnects == rhs.disconnects && fromAdminCalls == rhs.fromAdminCalls && fromAppCalls == rhs.fromAppCalls
           && storeWrites == rhs.storeWrites && logEntries == rhs.logEntries;
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
    endpoint.outputs.clear();
    endpoint.fromAdminCalls = 0;
  }

  EffectSnapshot snapshot() {
    return EffectSnapshot{
        session.getExpectedSenderNum(),
        session.getExpectedTargetNum(),
        endpoint.outputs.size(),
        endpoint.disconnects,
        endpoint.fromAdminCalls,
        endpoint.fromAppCalls,
        storeFactory.store->writes,
        logFactory.log->entries};
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
    const auto message = applicationMessage(before.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    CHECK(classification.kind() == InfiniteSessionActionKind::Application);
    CHECK(snapshot() == before);

    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(session.getExpectedTargetNum() == before.targetSequence + 1);
    CHECK(endpoint.fromAppCalls == before.fromAppCalls + 1);

    const auto nextBinding = InfiniteSessionClassificationTestAccess::atHead(0x12);
    const auto nextMessage = applicationMessage(session.getExpectedTargetNum());
    const auto nextClassification
        = InfiniteSessionClassificationTestAccess::classify(session, nextBinding, nextMessage.toString(), now);
    CHECK(nextClassification.expected() == classification.actionData().resultingState);

    const auto afterFirstApply = snapshot();
    session.setNextTargetMsgSeqNum(before.targetSequence);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(endpoint.fromAppCalls == afterFirstApply.fromAppCalls);
    CHECK(session.getExpectedTargetNum() == before.targetSequence);
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
    const auto heartbeat = finish(FIX42::Heartbeat(), before.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, heartbeat.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    CHECK(snapshot() == before);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(session.getExpectedTargetNum() == before.targetSequence + 1);
    CHECK(endpoint.fromAdminCalls == before.fromAdminCalls + 1);

    const auto nextBinding = InfiniteSessionClassificationTestAccess::atHead(0x56);
    const auto nextHeartbeat = finish(FIX42::Heartbeat(), session.getExpectedTargetNum());
    const auto nextClassification
        = InfiniteSessionClassificationTestAccess::classify(session, nextBinding, nextHeartbeat.toString(), now);
    CHECK(nextClassification.expected() == classification.actionData().resultingState);
  }

  SECTION("authorized TestRequest applies the captured heartbeat only") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x57);
    const auto request = finish(FIX42::TestRequest(TestReqID("TEST")), before.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    REQUIRE(endpoint.outputs.size() == before.outputs + 1);
    CHECK(endpoint.outputs.back().find("\00135=0\001") != std::string::npos);
    CHECK(endpoint.outputs.back().find("\001112=TEST\001") != std::string::npos);
    CHECK(session.getExpectedTargetNum() == before.targetSequence + 1);
  }

  SECTION("authorized gap fill applies its captured target transition") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x58);
    auto gapFill = finish(FIX42::SequenceReset(NewSeqNo(before.targetSequence + 3)), before.targetSequence);
    gapFill.set(GapFillFlag(true));
    gapFill.getHeader().setField(BodyLength(gapFill.bodyLength()));
    gapFill.getTrailer().setField(CheckSum(gapFill.checkSum()));
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, gapFill.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedTargetNum() == before.targetSequence + 3);
    CHECK(endpoint.fromAdminCalls == before.fromAdminCalls + 1);
  }

  SECTION("authorized Logout applies the captured response and disconnect") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x59);
    const auto logout = finish(FIX42::Logout(), before.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, logout.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(endpoint.outputs.size() == before.outputs + 1);
    CHECK(endpoint.disconnects == before.disconnects + 1);
    CHECK(endpoint.fromAdminCalls == before.fromAdminCalls + 1);
  }

  SECTION("authorized ResendRequest applies the bounded captured resend plan") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x5a);
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(0)), before.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    REQUIRE(endpoint.outputs.size() == before.outputs + 1);
    CHECK(endpoint.outputs.back().find("\00135=4\001") != std::string::npos);
    CHECK(session.getExpectedTargetNum() == before.targetSequence + 1);
  }

  SECTION("authorized Reject applies only its captured consuming transition") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x5b);
    const auto reject = finish(FIX42::Reject(RefSeqNum(1)), before.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, reject.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedTargetNum() == before.targetSequence + 1);
    CHECK(endpoint.fromAdminCalls == before.fromAdminCalls + 1);
    CHECK(endpoint.outputs.size() == before.outputs);
  }

  SECTION("an authorized frame cannot release an independently queued frame") {
    session.next(applicationMessage(session.getExpectedTargetNum() + 1), now);
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x5c);
    const auto message = applicationMessage(before.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    REQUIRE(classification.actionData().resultingState.mutableState.queuedMessages.size() == 1);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedTargetNum() == before.targetSequence + 1);
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
    const auto message = applicationMessage(before.targetSequence + 3);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    REQUIRE(classification.kind() == InfiniteSessionActionKind::ResendOrQueuedRelease);
    CHECK(classification.actionData().resultingState.mutableState.queuedMessages.empty());
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    REQUIRE(endpoint.outputs.size() == before.outputs + 1);
    CHECK(endpoint.outputs.back().find("\0017=" + std::to_string(before.targetSequence) + "\001") != std::string::npos);
    CHECK(endpoint.outputs.back().find("\00116=0\001") != std::string::npos);
    CHECK(session.getExpectedTargetNum() == before.targetSequence);
  }

  SECTION("callback failure consumes authorization before authoritative effects") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x77);
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();
    endpoint.throwFromApp = true;

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedSenderNum() == before.senderSequence);
    CHECK(session.getExpectedTargetNum() == before.targetSequence);
    CHECK(endpoint.outputs.size() == before.outputs);
    CHECK(endpoint.disconnects == before.disconnects);
    CHECK(storeFactory.store->writes == before.storeWrites);
    CHECK(logFactory.log->entries == before.logEntries);
  }
}
} // namespace
