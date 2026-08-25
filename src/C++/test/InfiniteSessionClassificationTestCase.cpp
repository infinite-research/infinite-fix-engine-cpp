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
#include <fix42/QuoteRequest.h>
#include <fix42/Reject.h>
#include <fix42/ResendRequest.h>
#include <fix42/SequenceReset.h>
#include <fix42/TestRequest.h>

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <limits>
#include <mutex>
#include <thread>

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
    auto owned = bytes;
    return session.classifyInfiniteFrame(atHead, std::move(owned), now);
  }

  static InfiniteSessionClassification classifyOwned(
      Session &session,
      const InfiniteAtHeadBinding &atHead,
      std::string &bytes,
      const UtcTimeStamp &now) {
    return session.classifyInfiniteFrame(atHead, std::move(bytes), now);
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

  static InfiniteEffectAuthorization tamperedCallbackBytesAuthorization(
      const InfiniteSessionClassification &classification) {
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    auto &callbacks = infiniteActionPlan(authorization.m_actionData).callbacks;
    if (callbacks.empty()) {
      throw std::logic_error("Expected planned callback");
    }
    callbacks.front().bytes += "tampered";
    return authorization;
  }

  static InfiniteExpectedSessionState state(const Session &session) {
    return session.currentInfiniteExpectedState(session.m_timestamper());
  }

  static void cleanseCredentials(Message &message) { Session::cleanseInfiniteMessageCredentials(message); }

  static void setSessionTime(Session &session, const TimeRange &sessionTime) {
    session.m_isNonStopSession = false;
    session.m_sessionTime = sessionTime;
  }

  static void setTimestamper(Session &session, std::function<UtcTimeStamp()> timestamper) {
    session.m_timestamper = std::move(timestamper);
  }

  static Message queuedMessage(const Session &session, SEQNUM sequence) {
    auto &mutableSession = const_cast<Session &>(session);
    Message message;
    if (!mutableSession.m_state.retrieve(sequence, message)) {
      throw std::logic_error("Expected queued message");
    }
    mutableSession.m_state.queue(sequence, message);
    return message;
  }

  static void queueMessage(Session &session, SEQNUM sequence, const Message &message) {
    session.m_state.queue(sequence, message);
  }

  static std::size_t queuedMessageCount(const Session &session) {
    const auto &queued = state(session).mutableState.queuedMessages;
    return queued ? queued->size() : 0;
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

std::string rewriteWireTag(std::string bytes, const std::string &tag, const std::string &replacement) {
  const auto field = bytes.find("\001" + tag + "=");
  if (field == std::string::npos) {
    throw std::runtime_error("FIX field not found");
  }
  bytes.replace(field + 1, tag.size(), replacement);

  const auto bodyLengthField = bytes.find("\0019=");
  auto checksumField = bytes.rfind("\00110=");
  if (bodyLengthField == std::string::npos || checksumField == std::string::npos) {
    throw std::runtime_error("FIX envelope fields not found");
  }
  const auto bodyLengthStart = bodyLengthField + 3;
  const auto bodyDelimiter = bytes.find('\001', bodyLengthStart);
  if (bodyDelimiter == std::string::npos) {
    throw std::runtime_error("FIX BodyLength delimiter not found");
  }
  const auto bodyStart = bodyDelimiter + 1;
  bytes.replace(bodyLengthStart, bodyStart - bodyLengthStart - 1, std::to_string(checksumField + 1 - bodyStart));

  checksumField = bytes.rfind("\00110=");
  unsigned checksum = 0;
  for (std::size_t index = 0; index <= checksumField; ++index) {
    checksum += static_cast<unsigned char>(bytes[index]);
  }
  auto encodedChecksum = std::to_string(checksum % 256);
  encodedChecksum.insert(encodedChecksum.begin(), 3 - encodedChecksum.size(), '0');
  bytes.replace(checksumField + 4, 3, encodedChecksum);
  return bytes;
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

std::string applicationMessageOfSize(SEQNUM sequence, std::size_t totalSize) {
  auto message = applicationMessage(sequence);
  std::string text(totalSize - 256, 'x');
  for (int attempt = 0; attempt < 4; ++attempt) {
    message.setField(Text(text));
    message.getHeader().setField(BodyLength(message.bodyLength()));
    message.getTrailer().setField(CheckSum(message.checkSum()));
    const auto bytes = message.toString();
    if (bytes.size() == totalSize) {
      return bytes;
    }
    if (bytes.size() < totalSize) {
      text.append(totalSize - bytes.size(), 'x');
    } else {
      text.resize(text.size() - (bytes.size() - totalSize));
    }
  }
  throw std::runtime_error("Unable to construct requested FIX message size");
}

FIX42::QuoteRequest groupedApplicationMessage(SEQNUM sequence) {
  FIX42::QuoteRequest message(QuoteReqID("GROUPED"));
  FIX42::QuoteRequest::NoRelatedSym group;
  group.set(Symbol("SYMBOL"));
  message.addGroup(group);
  return finish(std::move(message), sequence);
}

void recordOperation(std::vector<std::string> *ledger, std::string operation) {
  if (ledger) {
    ledger->push_back(std::move(operation));
  }
}

struct RecordingEndpoint : NullApplication, Responder {
  explicit RecordingEndpoint(std::vector<std::string> *ledger = nullptr)
      : ledger(ledger) {}

  bool send(const std::string &bytes) override {
    if (!sendSucceeds) {
      return false;
    }
    recordOperation(ledger, "send:" + bytes);
    outputs.push_back(bytes);
    return true;
  }

  void disconnect() override {
    recordOperation(ledger, "disconnect");
    ++disconnects;
  }
  void onLogon(const SessionID &) override {
    recordOperation(ledger, "callback:onLogon");
    ++logonCalls;
    if (onLogonHook) {
      onLogonHook();
    }
  }
  void onLogout(const SessionID &) override {
    recordOperation(ledger, "callback:onLogout");
    ++logoutCalls;
  }
  void toAdmin(Message &message, const SessionID &) override {
    recordOperation(ledger, "callback:toAdmin:" + message.toString());
    ++toAdminCalls;
    if (toAdminHook) {
      toAdminHook();
    }
    if (mutateToAdmin) {
      message.setField(Text("changed"));
    }
    if (injectToAdminCredentials) {
      message.setField(Username("sensitive-user"));
      message.setField(Password("sensitive-password"));
    }
  }
  void toApp(Message &message, const SessionID &) override {
    recordOperation(ledger, "callback:toApp:" + message.toString());
    ++toAppCalls;
    if (toAppHook) {
      toAppHook();
    }
    Group group(FIELD::NoRelatedSym, FIELD::Symbol);
    sawToAppGroup = message.hasGroup(1, group);
    if (doNotSendToApp) {
      throw DoNotSend();
    }
    if (mutateToApp) {
      message.setField(Text("changed"));
    }
    if (injectToAppCredentials) {
      message.getHeader().setField(Username("sensitive-user"));
      message.getTrailer().setField(Password("sensitive-password"));
    }
  }
  void fromAdmin(const Message &message, const SessionID &) override {
    recordOperation(ledger, "callback:fromAdmin:" + message.toString());
    ++fromAdminCalls;
  }
  void fromApp(const Message &message, const SessionID &) override {
    recordOperation(ledger, "callback:fromApp:" + message.toString());
    ++fromAppCalls;
    if (fromAppHook) {
      fromAppHook();
    }
    if (blockFromApp) {
      std::unique_lock<std::mutex> lock(callbackMutex);
      callbackEntered = true;
      callbackCondition.notify_all();
      callbackCondition.wait(lock, [this]() { return releaseCallback; });
    }
    if (throwFromApp) {
      throw UnsupportedMessageType();
    }
    if (reenterSession) {
      reenterSession->setNextTargetMsgSeqNum(reenterSession->getExpectedTargetNum() + 1);
    }
  }

  std::vector<std::string> outputs;
  std::vector<std::string> *ledger;
  int disconnects{0};
  int logonCalls{0};
  int logoutCalls{0};
  int toAdminCalls{0};
  int toAppCalls{0};
  int fromAdminCalls{0};
  int fromAppCalls{0};
  bool throwFromApp{false};
  bool sendSucceeds{true};
  bool mutateToAdmin{false};
  bool injectToAdminCredentials{false};
  bool mutateToApp{false};
  bool injectToAppCredentials{false};
  bool doNotSendToApp{false};
  bool sawToAppGroup{false};
  bool blockFromApp{false};
  bool callbackEntered{false};
  bool releaseCallback{false};
  std::mutex callbackMutex;
  std::condition_variable callbackCondition;
  Session *reenterSession{nullptr};
  std::function<void()> onLogonHook;
  std::function<void()> toAdminHook;
  std::function<void()> toAppHook;
  std::function<void()> fromAppHook;
};

struct RecordingStore : MemoryStore {
  explicit RecordingStore(const UtcTimeStamp &now, std::vector<std::string> *ledger = nullptr)
      : MemoryStore(now),
        ledger(ledger) {}

  bool set(SEQNUM sequence, const std::string &message) override {
    if (throwSet) {
      throw IOException("set failed");
    }
    if (!setSucceeds) {
      return false;
    }
    const auto stored = MemoryStore::set(sequence, message);
    if (stored) {
      recordOperation(ledger, "store:set:" + std::to_string(sequence) + ":" + message);
      ++writes;
    }
    return stored;
  }
  void get(SEQNUM begin, SEQNUM end, std::vector<std::string> &messages) const override {
    ++getCalls;
    if (blockGet) {
      std::unique_lock<std::mutex> lock(getMutex);
      getEntered = true;
      getCondition.notify_all();
      getCondition.wait(lock, [this]() { return releaseGet; });
    }
    MemoryStore::get(begin, end, messages);
    if (reverseReads) {
      std::reverse(messages.begin(), messages.end());
    }
  }
  void setNextSenderMsgSeqNum(SEQNUM sequence) override {
    recordOperation(ledger, "store:sender:" + std::to_string(sequence));
    ++writes;
    MemoryStore::setNextSenderMsgSeqNum(sequence);
  }
  void setNextTargetMsgSeqNum(SEQNUM sequence) override {
    if (throwTargetWrite) {
      throw IOException("target sequence write failed");
    }
    recordOperation(ledger, "store:target:" + std::to_string(sequence));
    ++writes;
    MemoryStore::setNextTargetMsgSeqNum(sequence);
  }
  void incrNextSenderMsgSeqNum() override {
    recordOperation(ledger, "store:sender:" + std::to_string(MemoryStore::getNextSenderMsgSeqNum() + 1));
    ++writes;
    MemoryStore::incrNextSenderMsgSeqNum();
  }
  void incrNextTargetMsgSeqNum() override {
    recordOperation(ledger, "store:target:" + std::to_string(MemoryStore::getNextTargetMsgSeqNum() + 1));
    ++writes;
    MemoryStore::incrNextTargetMsgSeqNum();
  }
  void reset(const UtcTimeStamp &now) override {
    recordOperation(ledger, "store:reset");
    ++writes;
    MemoryStore::reset(now);
  }
  void refresh() override {
    recordOperation(ledger, "store:refresh");
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
  std::vector<std::string> *ledger;
  bool throwSet{false};
  bool setSucceeds{true};
  bool throwTargetWrite{false};
  bool throwSnapshot{false};
  mutable int getCalls{0};
  mutable bool blockGet{false};
  mutable bool getEntered{false};
  mutable bool releaseGet{false};
  mutable bool reverseReads{false};
  mutable std::mutex getMutex;
  mutable std::condition_variable getCondition;
};

struct RecordingStoreFactory : MessageStoreFactory {
  explicit RecordingStoreFactory(std::vector<std::string> *ledger = nullptr)
      : ledger(ledger) {}

  MessageStore *create(const UtcTimeStamp &now, const SessionID &) override {
    store = new RecordingStore(now, ledger);
    return store;
  }
  void destroy(MessageStore *messageStore) override {
    delete messageStore;
    store = nullptr;
  }

  RecordingStore *store{nullptr};
  std::vector<std::string> *ledger;
};

struct RecordingLog : Log {
  explicit RecordingLog(std::vector<std::string> *ledger = nullptr)
      : ledger(ledger) {}

  void clear() override {
    recordOperation(ledger, "log:clear");
    kinds.push_back("clear");
  }
  void backup() override {
    recordOperation(ledger, "log:backup");
    kinds.push_back("backup");
  }
  void onIncoming(const std::string &bytes) override {
    recordOperation(ledger, "log:incoming:" + bytes);
    kinds.push_back("incoming");
    values.push_back(bytes);
  }
  void onOutgoing(const std::string &bytes) override {
    recordOperation(ledger, "log:outgoing:" + bytes);
    kinds.push_back("outgoing");
    values.push_back(bytes);
  }
  void onEvent(const std::string &event) override {
    recordOperation(ledger, "log:event:" + event);
    kinds.push_back("event");
    values.push_back(event);
  }

  std::vector<std::string> kinds;
  std::vector<std::string> values;
  std::vector<std::string> *ledger;
};

struct RecordingLogFactory : LogFactory {
  explicit RecordingLogFactory(std::vector<std::string> *ledger = nullptr)
      : ledger(ledger) {}

  Log *create() override {
    log = new RecordingLog(ledger);
    return log;
  }
  Log *create(const SessionID &) override {
    log = new RecordingLog(ledger);
    return log;
  }
  void destroy(Log *value) override {
    delete value;
    log = nullptr;
  }

  RecordingLog *log{nullptr};
  std::vector<std::string> *ledger;
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
  std::vector<std::string> ledger;

  bool operator==(const EffectSnapshot &rhs) const {
    return state == rhs.state && outputs == rhs.outputs && disconnects == rhs.disconnects
           && logonCalls == rhs.logonCalls && logoutCalls == rhs.logoutCalls && toAdminCalls == rhs.toAdminCalls
           && toAppCalls == rhs.toAppCalls && fromAdminCalls == rhs.fromAdminCalls && fromAppCalls == rhs.fromAppCalls
           && storeWrites == rhs.storeWrites && logKinds == rhs.logKinds && logValues == rhs.logValues
           && ledger == rhs.ledger;
  }
};

EffectSnapshot normalizeInfiniteBookkeeping(EffectSnapshot snapshot) {
  snapshot.state.sessionIdentity = 0;
  snapshot.state.revision = 0;
  snapshot.state.mutableState.responderGeneration = 0;
  return snapshot;
}

void checkEquivalentEffects(EffectSnapshot actual, EffectSnapshot expected) {
  actual = normalizeInfiniteBookkeeping(std::move(actual));
  expected = normalizeInfiniteBookkeeping(std::move(expected));
  CHECK(actual.state == expected.state);
  CHECK(actual.outputs == expected.outputs);
  CHECK(actual.disconnects == expected.disconnects);
  CHECK(actual.logonCalls == expected.logonCalls);
  CHECK(actual.logoutCalls == expected.logoutCalls);
  CHECK(actual.toAdminCalls == expected.toAdminCalls);
  CHECK(actual.toAppCalls == expected.toAppCalls);
  CHECK(actual.fromAdminCalls == expected.fromAdminCalls);
  CHECK(actual.fromAppCalls == expected.fromAppCalls);
  CHECK(actual.storeWrites == expected.storeWrites);
  CHECK(actual.logKinds == expected.logKinds);
  CHECK(actual.logValues == expected.logValues);
  CHECK(actual.ledger == expected.ledger);
}

void checkFencedWithoutAuthorizedEffects(EffectSnapshot after, const EffectSnapshot &before) {
  REQUIRE(after.state.mutableState.infiniteFenced);
  after.state.mutableState.infiniteFenced = before.state.mutableState.infiniteFenced;
  CHECK(after == before);
}

struct Fixture {
  explicit Fixture(bool establishLogon = true)
      : now(8, 8, 8, 25, 8, 2026),
        endpoint(&ledger),
        storeFactory(&ledger),
        logFactory(&ledger),
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

    if (establishLogon) {
      auto logon = finish(FIX42::Logon(EncryptMethod(0), HeartBtInt(30)), 1);
      session.next(logon, now);
    }
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
    endpoint.sawToAppGroup = false;
    endpoint.onLogonHook = {};
    endpoint.toAdminHook = {};
    endpoint.toAppHook = {};
    endpoint.fromAppHook = {};
    logFactory.log->kinds.clear();
    logFactory.log->values.clear();
    ledger.clear();
    storeFactory.store->writes = 0;
    storeFactory.store->getCalls = 0;
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
        logFactory.log->values,
        ledger};
  }

  UtcTimeStamp now;
  std::vector<std::string> ledger;
  RecordingEndpoint endpoint;
  RecordingStoreFactory storeFactory;
  RecordingLogFactory logFactory;
  SessionID sessionId;
  DataDictionaryProvider dictionaries;
  TimeRange sessionTime;
  Session session;
};

TEST_CASE_METHOD(Fixture, "InfiniteSessionClassificationTests", "[infinite][session]") {
  SECTION("credential cleanup clears Message field and encoding buffers") {
    auto logon = finish(FIX42::Logon(EncryptMethod(0), HeartBtInt(30)), 1);
    logon.setField(Username("sensitive-user"));
    logon.setField(Password("sensitive-password"));
    logon.getHeader().setField(Username("sensitive-header"));
    logon.getTrailer().setField(Password("sensitive-trailer"));
    Group group(FIELD::NoRelatedSym, FIELD::Symbol);
    group.setField(Username("sensitive-group"));
    logon.addGroup(group);
    logon.getFieldRef(FIELD::Username).getFixString();
    logon.getFieldRef(FIELD::Password).getFixString();
    logon.getHeader().getFieldRef(FIELD::Username).getFixString();
    logon.getTrailer().getFieldRef(FIELD::Password).getFixString();
    logon.getGroupRef(1, FIELD::NoRelatedSym).getFieldRef(FIELD::Username).getFixString();

    InfiniteSessionClassificationTestAccess::cleanseCredentials(logon);

    CHECK(logon.getField(FIELD::Username).empty());
    CHECK(logon.getField(FIELD::Password).empty());
    CHECK(logon.getHeader().getField(FIELD::Username).empty());
    CHECK(logon.getTrailer().getField(FIELD::Password).empty());
    CHECK(logon.getFieldRef(FIELD::Username).getFixString() == "553=\001");
    CHECK(logon.getFieldRef(FIELD::Password).getFixString() == "554=\001");
    CHECK(logon.getHeader().getFieldRef(FIELD::Username).getFixString() == "553=\001");
    CHECK(logon.getTrailer().getFieldRef(FIELD::Password).getFixString() == "554=\001");
    Group cleansedGroup(FIELD::NoRelatedSym, FIELD::Symbol);
    logon.getGroup(1, cleansedGroup);
    CHECK(cleansedGroup.getField(FIELD::Username).empty());
    CHECK(cleansedGroup.getFieldRef(FIELD::Username).getFixString() == "553=\001");
  }

  SECTION("Session privately owns transport and application dictionaries") {
    const BeginString beginString(BeginString_FIX42);
    const ApplVerID applVerID(ApplVerID_FIX50);
    auto transportDictionary = std::make_shared<DataDictionary>();
    transportDictionary->setVersion(BeginString_FIX42);
    transportDictionary->addValueName(FIELD::Side, "1", "BUY");
    auto applicationDictionary = std::make_shared<DataDictionary>();
    applicationDictionary->setVersion(BeginString_FIX50);
    DataDictionaryProvider replacement;
    replacement.addTransportDataDictionary(beginString, transportDictionary);
    replacement.addApplicationDataDictionary(applVerID, applicationDictionary);

    session.setDataDictionaryProvider(replacement);

    const auto &installed = session.getDataDictionaryProvider();
    CHECK(&installed.getSessionDataDictionary(beginString) != transportDictionary.get());
    CHECK(&installed.getApplicationDataDictionary(applVerID) != applicationDictionary.get());
    CHECK(installed.getSessionDataDictionary(beginString).getVersion() == BeginString_FIX42);
    CHECK(installed.getApplicationDataDictionary(applVerID).getVersion() == BeginString_FIX50);
    std::string valueName;
    std::string nameValue;
    CHECK(installed.getSessionDataDictionary(beginString).getValueName(FIELD::Side, "1", valueName));
    CHECK(valueName == "BUY");
    CHECK(installed.getSessionDataDictionary(beginString).getNameValue(FIELD::Side, "BUY", nameValue));
    CHECK(nameValue == "1");
  }

  SECTION("classification is effect free and authorized application is one use") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x11);
    const auto message = applicationMessage(before.state.targetSequence);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    CHECK(classification.kind() == InfiniteSessionActionKind::Application);
    CHECK(std::holds_alternative<InfiniteApplicationData>(classification.actionData()));
    CHECK(snapshot() == before);

    endpoint.fromAppHook = [&]() {
      REQUIRE_FALSE(logFactory.log->kinds.empty());
      CHECK(logFactory.log->kinds.back() == "incoming");
      CHECK(session.getExpectedTargetNum() == before.state.targetSequence);
    };
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

    session.setNextTargetMsgSeqNum(before.state.targetSequence);
    const auto beforeReuse = snapshot();
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    checkFencedWithoutAuthorizedEffects(snapshot(), beforeReuse);
    CHECK(session.getExpectedTargetNum() == before.state.targetSequence);
  }

  SECTION("credential-bearing Logon is rejected before parsing or retention") {
    const std::string secret = "not-retained-secret";
    FIX42::Logon logon(EncryptMethod(0), HeartBtInt(30));
    logon.setField(Username("participant"));
    logon.setField(Password(secret));
    auto bytes = finish(std::move(logon), session.getExpectedTargetNum()).toString();
    REQUIRE(bytes.find("\001553=participant\001") != std::string::npos);
    REQUIRE(bytes.find("\001554=" + secret + "\001") != std::string::npos);
    const auto before = snapshot();

    const auto classification = InfiniteSessionClassificationTestAccess::classifyOwned(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x21),
        bytes,
        now);
    const auto &plan = infiniteActionPlan(classification.actionData());

    CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
    CHECK(std::all_of(bytes.begin(), bytes.end(), [](char value) { return value == '\0'; }));
    CHECK(snapshot() == before);
    CHECK(classification.message().toString().find(secret) == std::string::npos);
    CHECK(plan.failure.find(secret) == std::string::npos);
    CHECK(plan.sourceMessages.empty());
    CHECK(plan.callbacks.empty());
    CHECK(plan.effects.empty());
  }

  SECTION("noncanonical Logon and credential tags are rejected before retention") {
    struct Case {
      bool username;
      const char *tag;
      const char *replacement;
    };
    const Case cases[] = {{true, "35", "035"}, {true, "553", "0553"}, {false, "554", "0554"}};

    for (const auto &test : cases) {
      CAPTURE(test.replacement);
      const std::string secret = "not-retained-leading-zero-secret";
      FIX42::Logon logon(EncryptMethod(0), HeartBtInt(30));
      if (test.username) {
        logon.setField(Username(secret));
      } else {
        logon.setField(Password(secret));
      }
      auto bytes = rewriteWireTag(
          finish(std::move(logon), session.getExpectedTargetNum()).toString(),
          test.tag,
          test.replacement);
      REQUIRE(bytes.find("\001" + std::string(test.replacement) + "=") != std::string::npos);
      const auto before = snapshot();

      const auto classification = InfiniteSessionClassificationTestAccess::classifyOwned(
          session,
          InfiniteSessionClassificationTestAccess::atHead(0x21),
          bytes,
          now);
      const auto &plan = infiniteActionPlan(classification.actionData());

      CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
      CHECK(std::all_of(bytes.begin(), bytes.end(), [](char value) { return value == '\0'; }));
      CHECK(snapshot() == before);
      CHECK(classification.message().toString().find(secret) == std::string::npos);
      CHECK(plan.failure.find(secret) == std::string::npos);
      CHECK(plan.sourceMessages.empty());
      CHECK(plan.callbacks.empty());
      CHECK(plan.effects.empty());
    }
  }

  SECTION("credential-bearing queued Logon is never copied into an Infinite state snapshot") {
    const std::string secret = "queued-secret";
    FIX42::Logon logon(EncryptMethod(0), HeartBtInt(30));
    logon.setField(Username("participant"));
    logon.setField(Password(secret));
    const auto queuedSequence = session.getExpectedTargetNum() + 1;
    auto queued = finish(std::move(logon), queuedSequence);
    InfiniteSessionClassificationTestAccess::queueMessage(session, queuedSequence, queued);
    auto heartbeat = finish(FIX42::Heartbeat(), session.getExpectedTargetNum()).toString();

    const auto classification = InfiniteSessionClassificationTestAccess::classifyOwned(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x22),
        heartbeat,
        now);
    const auto &plan = infiniteActionPlan(classification.actionData());

    CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
    CHECK(std::all_of(heartbeat.begin(), heartbeat.end(), [](char value) { return value == '\0'; }));
    CHECK(classification.message().toString().find(secret) == std::string::npos);
    CHECK(plan.failure.find(secret) == std::string::npos);
    CHECK_FALSE(plan.resultingState.mutableState.queuedMessages);
  }

  SECTION("credential-bearing stored Logon is never copied into a resend plan") {
    const std::string secret = "stored-secret";
    FIX42::Logon logon(EncryptMethod(0), HeartBtInt(30));
    logon.setField(Username("participant"));
    logon.setField(Password(secret));
    const auto storedSequence = session.getExpectedSenderNum();
    const auto stored = finish(std::move(logon), storedSequence).toString();
    REQUIRE(storeFactory.store->set(storedSequence, stored));
    session.setNextSenderMsgSeqNum(storedSequence + 1);
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(storedSequence), EndSeqNo(storedSequence)),
        session.getExpectedTargetNum());

    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x23),
        request.toString(),
        now);
    const auto &plan = infiniteActionPlan(classification.actionData());

    CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
    CHECK(plan.failure.find(secret) == std::string::npos);
    CHECK(plan.sourceMessages.empty());
    CHECK(plan.callbacks.empty());
    CHECK(plan.effects.empty());
  }

  SECTION("owned frame storage is cleansed on success, parse failure, and fence refusal") {
    auto valid = applicationMessage(session.getExpectedTargetNum()).toString();
    const auto accepted = InfiniteSessionClassificationTestAccess::classifyOwned(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x24),
        valid,
        now);
    CHECK(accepted.kind() == InfiniteSessionActionKind::Application);
    CHECK(std::all_of(valid.begin(), valid.end(), [](char value) { return value == '\0'; }));

    std::string invalid = "not FIX";
    const auto refused = InfiniteSessionClassificationTestAccess::classifyOwned(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x25),
        invalid,
        now);
    CHECK(refused.kind() == InfiniteSessionActionKind::Failure);
    CHECK(std::all_of(invalid.begin(), invalid.end(), [](char value) { return value == '\0'; }));
    auto refusalAuthorization = InfiniteSessionClassificationTestAccess::authorization(refused);
    InfiniteSessionClassificationTestAccess::apply(session, refused, std::move(refusalAuthorization));

    auto fenced = applicationMessage(session.getExpectedTargetNum()).toString();
    const auto fencedRefusal = InfiniteSessionClassificationTestAccess::classifyOwned(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x26),
        fenced,
        now);
    CHECK(fencedRefusal.kind() == InfiniteSessionActionKind::Failure);
    CHECK(std::all_of(fenced.begin(), fenced.end(), [](char value) { return value == '\0'; }));
  }

  SECTION("invalid authorizations fence before any authorized effect") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x22);
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    SECTION("stale state") {
      auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
      session.setNextTargetMsgSeqNum(session.getExpectedTargetNum() + 1);
      const auto before = snapshot();
      InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
      checkFencedWithoutAuthorizedEffects(snapshot(), before);
    }

    SECTION("mismatched binding") {
      const auto other = InfiniteSessionClassificationTestAccess::classify(
          session,
          InfiniteSessionClassificationTestAccess::atHead(0x23),
          message.toString(),
          now);
      auto authorization = InfiniteSessionClassificationTestAccess::authorization(other);
      const auto before = snapshot();
      InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
      checkFencedWithoutAuthorizedEffects(snapshot(), before);
    }

    SECTION("tampered plan") {
      auto authorization = InfiniteSessionClassificationTestAccess::tamperedAuthorization(classification);
      const auto before = snapshot();
      InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
      checkFencedWithoutAuthorizedEffects(snapshot(), before);
    }

    SECTION("mismatched action") {
      auto authorization = InfiniteSessionClassificationTestAccess::mismatchedActionAuthorization(classification);
      const auto before = snapshot();
      InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
      checkFencedWithoutAuthorizedEffects(snapshot(), before);
    }

    SECTION("already consumed") {
      auto authorization = InfiniteSessionClassificationTestAccess::consumedAuthorization(classification);
      const auto before = snapshot();
      InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
      checkFencedWithoutAuthorizedEffects(snapshot(), before);
    }

    SECTION("tampered callback message") {
      auto authorization = InfiniteSessionClassificationTestAccess::tamperedCallbackBytesAuthorization(classification);
      const auto before = snapshot();
      InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
      checkFencedWithoutAuthorizedEffects(snapshot(), before);
    }
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

  SECTION("legacy and authorized Infinite paths have a paired differential corpus") {
    struct Scenario {
      const char *name;
      bool establishLogon;
      InfiniteSessionActionKind expectedAction;
      std::function<Message(Fixture &)> input;
    };
    const std::vector<Scenario> scenarios{
        {"Logon",
         false,
         InfiniteSessionActionKind::ProtocolControl,
         [](Fixture &fixture) {
           return finish(FIX42::Logon(EncryptMethod(0), HeartBtInt(30)), fixture.session.getExpectedTargetNum());
         }},
        {"Heartbeat",
         true,
         InfiniteSessionActionKind::ProtocolControl,
         [](Fixture &fixture) { return finish(FIX42::Heartbeat(), fixture.session.getExpectedTargetNum()); }},
        {"TestRequest",
         true,
         InfiniteSessionActionKind::ProtocolControl,
         [](Fixture &fixture) {
           return finish(FIX42::TestRequest(TestReqID("DIFFERENTIAL")), fixture.session.getExpectedTargetNum());
         }},
        {"SequenceReset",
         true,
         InfiniteSessionActionKind::SequenceReset,
         [](Fixture &fixture) {
           const auto sequence = fixture.session.getExpectedTargetNum();
           return finish(FIX42::SequenceReset(NewSeqNo(sequence + 1)), sequence);
         }},
        {"Logout",
         true,
         InfiniteSessionActionKind::Logout,
         [](Fixture &fixture) { return finish(FIX42::Logout(), fixture.session.getExpectedTargetNum()); }},
        {"ResendRequest",
         true,
         InfiniteSessionActionKind::ResendOrQueuedRelease,
         [](Fixture &fixture) {
           auto outbound = applicationMessage(fixture.session.getExpectedSenderNum());
           if (!fixture.session.send(outbound)) {
             throw std::runtime_error("Unable to seed resend message");
           }
           const auto sequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
           return finish(
               FIX42::ResendRequest(BeginSeqNo(sequence), EndSeqNo(sequence)),
               fixture.session.getExpectedTargetNum());
         }},
        {"Reject",
         true,
         InfiniteSessionActionKind::ProtocolDisposition,
         [](Fixture &fixture) { return finish(FIX42::Reject(RefSeqNum(1)), fixture.session.getExpectedTargetNum()); }},
        {"dictionary-invalid application",
         true,
         InfiniteSessionActionKind::ProtocolDisposition,
         [](Fixture &fixture) {
           auto message = applicationMessage(fixture.session.getExpectedTargetNum());
           message.removeField(FIELD::Symbol);
           message.getHeader().setField(BodyLength(message.bodyLength()));
           message.getTrailer().setField(CheckSum(message.checkSum()));
           return message;
         }},
        {"application",
         true,
         InfiniteSessionActionKind::Application,
         [](Fixture &fixture) { return applicationMessage(fixture.session.getExpectedTargetNum()); }},
        {"session timeout",
         true,
         InfiniteSessionActionKind::ProtocolDisposition,
         [](Fixture &fixture) {
           fixture.now = UtcTimeStamp(10, 0, 0, 25, 8, 2026);
           InfiniteSessionClassificationTestAccess::setSessionTime(
               fixture.session,
               TimeRange(UtcTimeOnly(7, 0, 0), UtcTimeOnly(9, 0, 0)));
           return applicationMessage(fixture.session.getExpectedTargetNum());
         }},
    };

    for (const auto &scenario : scenarios) {
      INFO(scenario.name);
      Fixture legacy(scenario.establishLogon);
      Fixture infinite(scenario.establishLogon);
      const auto legacyInput = scenario.input(legacy);
      const auto infiniteInput = scenario.input(infinite);
      legacy.clearEffects();
      infinite.clearEffects();

      legacy.session.getLog()->onIncoming(legacyInput.toString());
      legacy.session.next(legacyInput, legacy.now);
      const auto classification = InfiniteSessionClassificationTestAccess::classify(
          infinite.session,
          InfiniteSessionClassificationTestAccess::atHead(0x35),
          infiniteInput.toString(),
          infinite.now);
      CHECK(classification.kind() == scenario.expectedAction);
      auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
      InfiniteSessionClassificationTestAccess::apply(infinite.session, classification, std::move(authorization));

      checkEquivalentEffects(infinite.snapshot(), legacy.snapshot());
    }
  }

  SECTION("legacy callbacks can enter another Session without cross-session lock inversion") {
    Fixture left;
    Fixture right;
    const auto leftMessage = applicationMessage(left.session.getExpectedTargetNum());
    const auto rightMessage = applicationMessage(right.session.getExpectedTargetNum());
    std::mutex mutex;
    std::condition_variable condition;
    int entered = 0;
    const auto crossEnter = [&](Session &other) {
      {
        std::unique_lock<std::mutex> lock(mutex);
        ++entered;
        condition.notify_all();
        condition.wait(lock, [&]() { return entered == 2; });
      }
      other.getExpectedTargetNum();
    };
    left.endpoint.fromAppHook = [&]() { crossEnter(right.session); };
    right.endpoint.fromAppHook = [&]() { crossEnter(left.session); };

    auto leftNext = std::async(std::launch::async, [&]() { left.session.next(leftMessage, left.now); });
    auto rightNext = std::async(std::launch::async, [&]() { right.session.next(rightMessage, right.now); });

    REQUIRE(leftNext.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    REQUIRE(rightNext.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK_NOTHROW(leftNext.get());
    CHECK_NOTHROW(rightNext.get());
  }

  SECTION("out-of-sequence admin frames retain their closed action family") {
    const auto sequence = session.getExpectedTargetNum() + 3;
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x34);
    const auto sequenceReset = finish(FIX42::SequenceReset(NewSeqNo(sequence + 1)), sequence);
    const auto logout = finish(FIX42::Logout(), sequence);
    const auto reject = finish(FIX42::Reject(RefSeqNum(1)), sequence);

    CHECK(
        InfiniteSessionClassificationTestAccess::classify(session, binding, sequenceReset.toString(), now).kind()
        == InfiniteSessionActionKind::SequenceReset);
    CHECK(
        InfiniteSessionClassificationTestAccess::classify(session, binding, logout.toString(), now).kind()
        == InfiniteSessionActionKind::Logout);
    CHECK(
        InfiniteSessionClassificationTestAccess::classify(session, binding, reject.toString(), now).kind()
        == InfiniteSessionActionKind::ProtocolDisposition);
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

    REQUIRE(infiniteActionPlan(classification.actionData()).resultingState.mutableState.queuedMessages->size() == 1);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(session.getExpectedTargetNum() == before.state.targetSequence + 1);
    CHECK(endpoint.fromAppCalls == before.fromAppCalls + 1);
    const auto nextBinding = InfiniteSessionClassificationTestAccess::atHead(0x5d);
    const auto next = applicationMessage(session.getExpectedTargetNum());
    const auto nextClassification
        = InfiniteSessionClassificationTestAccess::classify(session, nextBinding, next.toString(), now);
    CHECK(nextClassification.expected().mutableState.queuedMessages->size() == 1);
  }

  SECTION("callbacks share one immutable queued-state snapshot") {
    const auto queuedSequence = session.getExpectedTargetNum() + 1;
    session.next(applicationMessage(queuedSequence), now);
    const auto request = finish(FIX42::TestRequest(TestReqID("QUEUE-SNAPSHOT")), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x5e),
        request.toString(),
        now);
    const auto &callbacks = infiniteActionPlan(classification.actionData()).callbacks;

    REQUIRE(callbacks.size() >= 2);
    const auto *const queue = callbacks.front().observedState.mutableState.queuedMessages.get();
    REQUIRE(queue != nullptr);
    REQUIRE(queue->size() == 1);
    for (const auto &callback : callbacks) {
      CHECK(callback.observedState.mutableState.queuedMessages.get() == queue);
    }
  }

  SECTION("a too-high frame is discarded and plans request-all-subsequent") {
    const auto before = snapshot();
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x66);
    const auto message = applicationMessage(before.state.targetSequence + 3);
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, message.toString(), now);

    REQUIRE(classification.kind() == InfiniteSessionActionKind::ResendOrQueuedRelease);
    CHECK(infiniteActionPlan(classification.actionData()).resultingState.mutableState.queuedMessages->empty());
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
    const auto beforeApply = snapshot();
    storeFactory.store->throwSnapshot = true;
    CHECK_NOTHROW(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    storeFactory.store->throwSnapshot = false;
    checkFencedWithoutAuthorizedEffects(snapshot(), beforeApply);
  }

  SECTION("freshness snapshot failures fence before the first authorized effect") {
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6c),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();
    storeFactory.store->throwSnapshot = true;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    storeFactory.store->throwSnapshot = false;
    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("application-time clock failures fence before the first authorized effect") {
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6c),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();
    InfiniteSessionClassificationTestAccess::setTimestamper(session, []() -> UtcTimeStamp {
      throw std::runtime_error("clock failed");
    });

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    InfiniteSessionClassificationTestAccess::setTimestamper(session, [this]() { return now; });
    checkFencedWithoutAuthorizedEffects(snapshot(), before);
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

  SECTION("resend source-store drift fences before the first planned operation") {
    auto outbound = applicationMessage(session.getExpectedSenderNum());
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6d),
        request.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    REQUIRE(storeFactory.store->set(outboundSequence, "changed"));
    const auto before = snapshot();

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("an empty resend source range binds the absence of messages") {
    session.setNextSenderMsgSeqNum(3);
    clearEffects();
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(2), EndSeqNo(2)), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6d),
        request.toString(),
        now);
    const auto &plan = infiniteActionPlan(classification.actionData());
    REQUIRE(classification.kind() == InfiniteSessionActionKind::ResendOrQueuedRelease);
    CHECK(plan.sourceRangeRead);
    CHECK(plan.sourceRangeBegin == 2);
    CHECK(plan.sourceRangeEnd == 2);
    CHECK(plan.sourceMessages.empty());
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    REQUIRE(storeFactory.store->set(2, applicationMessage(2).toString()));
    clearEffects();
    const auto before = snapshot();

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("a sparse resend source range binds later additions") {
    REQUIRE(storeFactory.store->set(1, applicationMessage(1).toString()));
    session.setNextSenderMsgSeqNum(3);
    clearEffects();
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(2)), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6d),
        request.toString(),
        now);
    const auto &plan = infiniteActionPlan(classification.actionData());
    REQUIRE(classification.kind() == InfiniteSessionActionKind::ResendOrQueuedRelease);
    REQUIRE(plan.sourceMessages.size() == 1);
    CHECK(plan.sourceMessages.front().first == 1);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    REQUIRE(storeFactory.store->set(2, applicationMessage(2).toString()));
    clearEffects();
    const auto before = snapshot();

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("a resend source range binds the store response order") {
    REQUIRE(storeFactory.store->set(1, applicationMessage(1).toString()));
    REQUIRE(storeFactory.store->set(2, applicationMessage(2).toString()));
    session.setNextSenderMsgSeqNum(3);
    clearEffects();
    storeFactory.store->reverseReads = true;
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(2)), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6d),
        request.toString(),
        now);
    const auto &sources = infiniteActionPlan(classification.actionData()).sourceMessages;
    REQUIRE(sources.size() == 2);
    CHECK(sources[0].first == 2);
    CHECK(sources[1].first == 1);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    storeFactory.store->reverseReads = false;
    clearEffects();
    const auto before = snapshot();

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("resend effects preserve a custom store response order") {
    Fixture legacy;
    Fixture infinite;
    REQUIRE(legacy.storeFactory.store->set(1, applicationMessage(1).toString()));
    REQUIRE(legacy.storeFactory.store->set(2, applicationMessage(2).toString()));
    REQUIRE(infinite.storeFactory.store->set(1, applicationMessage(1).toString()));
    REQUIRE(infinite.storeFactory.store->set(2, applicationMessage(2).toString()));
    legacy.session.setNextSenderMsgSeqNum(3);
    infinite.session.setNextSenderMsgSeqNum(3);
    legacy.clearEffects();
    infinite.clearEffects();
    legacy.storeFactory.store->reverseReads = true;
    infinite.storeFactory.store->reverseReads = true;
    const auto legacyRequest
        = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(2)), legacy.session.getExpectedTargetNum());
    const auto infiniteRequest
        = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(2)), infinite.session.getExpectedTargetNum());

    legacy.session.getLog()->onIncoming(legacyRequest.toString());
    legacy.session.next(legacyRequest, legacy.now);
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        infinite.session,
        InfiniteSessionClassificationTestAccess::atHead(0x6e),
        infiniteRequest.toString(),
        infinite.now);
    const auto &sources = infiniteActionPlan(classification.actionData()).sourceMessages;
    REQUIRE(sources.size() == 2);
    CHECK(sources[0].first == 2);
    CHECK(sources[1].first == 1);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    InfiniteSessionClassificationTestAccess::apply(infinite.session, classification, std::move(authorization));

    checkEquivalentEffects(infinite.snapshot(), legacy.snapshot());
  }

  SECTION("an inverted resend store range remains an empty legacy read") {
    clearEffects();
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(10), EndSeqNo(0)), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6d),
        request.toString(),
        now);

    CHECK(classification.kind() != InfiniteSessionActionKind::Failure);
    CHECK_FALSE(infiniteActionPlan(classification.actionData()).sourceRangeRead);
    CHECK(storeFactory.store->getCalls == 0);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    CHECK_NOTHROW(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    CHECK_FALSE(InfiniteSessionClassificationTestAccess::state(session).mutableState.infiniteFenced);
  }

  SECTION("the captured effect order preserves sequence progress before output") {
    const auto before = snapshot();
    const auto request = finish(FIX42::TestRequest(TestReqID("ORDER")), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6e),
        request.toString(),
        now);
    const auto &effects = infiniteActionPlan(classification.actionData()).effects;
    const std::vector<InfiniteEffectKind> expected{
        InfiniteEffectKind::LogIncoming,
        InfiniteEffectKind::StoreMessage,
        InfiniteEffectKind::SetSenderSequence,
        InfiniteEffectKind::LogOutgoing,
        InfiniteEffectKind::Send,
        InfiniteEffectKind::SetTargetSequence};
    std::vector<InfiniteEffectKind> actual;
    for (const auto &effect : effects) {
      actual.push_back(effect.kind);
    }
    CHECK(actual == expected);
    const auto &callbacks = infiniteActionPlan(classification.actionData()).callbacks;
    const auto toAdmin = std::find_if(callbacks.begin(), callbacks.end(), [](const auto &callback) {
      return callback.kind == InfiniteCallbackKind::ToAdmin;
    });
    const auto fromAdmin = std::find_if(callbacks.begin(), callbacks.end(), [](const auto &callback) {
      return callback.kind == InfiniteCallbackKind::FromAdmin;
    });
    REQUIRE(fromAdmin != callbacks.end());
    REQUIRE(toAdmin != callbacks.end());
    CHECK(effects.front().order == 0);
    CHECK(fromAdmin->order == 1);
    CHECK(toAdmin->order == 2);
    CHECK(effects[1].order == 3);

    endpoint.toAdminHook = [&]() {
      REQUIRE_FALSE(logFactory.log->kinds.empty());
      CHECK(logFactory.log->kinds.back() == "incoming");
      CHECK(endpoint.outputs.empty());
      CHECK(session.getExpectedSenderNum() == before.state.senderSequence);
    };
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    CHECK(session.getExpectedSenderNum() == before.state.senderSequence + 1);
    REQUIRE_FALSE(endpoint.outputs.empty());
  }

  SECTION("the exact 256-frame and 16-MiB resend cap is accepted") {
    constexpr std::size_t FRAME_BYTES = 65'536;
    for (SEQNUM sequence = 1; sequence <= 256; ++sequence) {
      REQUIRE(storeFactory.store->set(sequence, applicationMessageOfSize(sequence, FRAME_BYTES)));
    }
    session.setNextSenderMsgSeqNum(257);
    clearEffects();
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(256)), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6f),
        request.toString(),
        now);

    CHECK(classification.kind() == InfiniteSessionActionKind::ResendOrQueuedRelease);
    CHECK(infiniteActionPlan(classification.actionData()).sourceMessages.size() == 256);
    CHECK(storeFactory.store->getCalls == 1);
  }

  SECTION("the 16-MiB resend cap rejects one byte over without retaining the prefix") {
    constexpr std::size_t FRAME_BYTES = 65'536;
    for (SEQNUM sequence = 1; sequence < 256; ++sequence) {
      REQUIRE(storeFactory.store->set(sequence, applicationMessageOfSize(sequence, FRAME_BYTES)));
    }
    REQUIRE(storeFactory.store->set(256, applicationMessageOfSize(256, FRAME_BYTES + 1)));
    session.setNextSenderMsgSeqNum(257);
    clearEffects();
    const auto before = snapshot();
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(256)), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6f),
        request.toString(),
        now);
    const auto &plan = infiniteActionPlan(classification.actionData());

    CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
    CHECK(plan.sourceMessages.empty());
    CHECK(plan.callbacks.empty());
    CHECK(plan.effects.empty());
    CHECK(snapshot() == before);
  }

  SECTION("the 257-frame resend cap is refused before the source store is read") {
    session.setNextSenderMsgSeqNum(258);
    clearEffects();
    const auto request = finish(FIX42::ResendRequest(BeginSeqNo(1), EndSeqNo(257)), session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x6f),
        request.toString(),
        now);

    CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
    CHECK(storeFactory.store->getCalls == 0);
  }

  SECTION("private classifications are serialized on their exclusively owned Session") {
    auto outbound = applicationMessage(session.getExpectedSenderNum());
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        session.getExpectedTargetNum());
    const auto heartbeat = finish(FIX42::Heartbeat(), session.getExpectedTargetNum());
    storeFactory.store->blockGet = true;
    auto classification = std::async(std::launch::async, [&]() {
      return InfiniteSessionClassificationTestAccess::classify(
          session,
          InfiniteSessionClassificationTestAccess::atHead(0x70),
          request.toString(),
          now);
    });
    {
      std::unique_lock<std::mutex> lock(storeFactory.store->getMutex);
      storeFactory.store->getCondition.wait(lock, [&]() { return storeFactory.store->getEntered; });
    }

    auto secondClassification = std::async(std::launch::async, [&]() {
      return InfiniteSessionClassificationTestAccess::classify(
          session,
          InfiniteSessionClassificationTestAccess::atHead(0x71),
          heartbeat.toString(),
          now);
    });
    CHECK(secondClassification.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    {
      std::lock_guard<std::mutex> lock(storeFactory.store->getMutex);
      storeFactory.store->releaseGet = true;
    }
    storeFactory.store->getCondition.notify_all();
    CHECK(classification.get().kind() == InfiniteSessionActionKind::ResendOrQueuedRelease);
    REQUIRE(secondClassification.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK(secondClassification.get().kind() == InfiniteSessionActionKind::ProtocolControl);
  }

  SECTION("repeating groups survive retransmit callback capture") {
    auto outbound = groupedApplicationMessage(session.getExpectedSenderNum());
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x70),
        request.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    CHECK(endpoint.sawToAppGroup);
  }

  SECTION("repeating groups survive queued-state application") {
    const auto queuedSequence = session.getExpectedTargetNum() + 1;
    session.next(groupedApplicationMessage(queuedSequence), now);
    clearEffects();
    const auto atHead = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x71),
        atHead.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    auto queued = InfiniteSessionClassificationTestAccess::queuedMessage(session, queuedSequence);
    Group group(FIELD::NoRelatedSym, FIELD::Symbol);
    CHECK(queued.hasGroup(1, group));
  }

  SECTION("authorization cannot cross equivalent Session objects") {
    RecordingStoreFactory otherStoreFactory;
    RecordingLogFactory otherLogFactory;
    Session other(
        [this]() { return now; },
        endpoint,
        otherStoreFactory,
        sessionId,
        dictionaries,
        sessionTime,
        0,
        &otherLogFactory);
    other.setResponder(&endpoint);
    other.next(finish(FIX42::Logon(EncryptMethod(0), HeartBtInt(30)), 1), now);
    const auto before = InfiniteSessionClassificationTestAccess::state(other);
    const auto beforeFromApp = endpoint.fromAppCalls;
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x72),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    InfiniteSessionClassificationTestAccess::apply(other, classification, std::move(authorization));

    auto after = InfiniteSessionClassificationTestAccess::state(other);
    REQUIRE(after.mutableState.infiniteFenced);
    after.mutableState.infiniteFenced = before.mutableState.infiniteFenced;
    CHECK(after == before);
    CHECK(endpoint.fromAppCalls == beforeFromApp);
  }

  SECTION("logon-time policy changes invalidate a captured plan") {
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x73),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    session.setLogonTime(TimeRange(UtcTimeOnly(9, 0, 0), UtcTimeOnly(10, 0, 0)));
    const auto before = snapshot();

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("dictionary replacement invalidates a captured plan") {
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x74),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    session.setDataDictionaryProvider(dictionaries);
    const auto before = snapshot();

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("timestamp-precision changes invalidate a captured plan") {
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x74),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    session.setTimestampPrecision(6);
    const auto before = snapshot();

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("current time invalidates a plan after its logon window closes") {
    session.setLogonTime(TimeRange(UtcTimeOnly(8, 0, 0), UtcTimeOnly(9, 0, 0)));
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x75),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    now = UtcTimeStamp(10, 0, 0, 25, 8, 2026);
    const auto before = snapshot();

    InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));

    checkFencedWithoutAuthorizedEffects(snapshot(), before);
  }

  SECTION("too-high Logon never enters the private C++ queue") {
    RecordingEndpoint freshEndpoint;
    RecordingStoreFactory freshStoreFactory;
    RecordingLogFactory freshLogFactory;
    Session freshSession(
        [this]() { return now; },
        freshEndpoint,
        freshStoreFactory,
        sessionId,
        dictionaries,
        sessionTime,
        0,
        &freshLogFactory);
    freshSession.setResponder(&freshEndpoint);
    freshSession.setSendNextExpectedMsgSeqNum(true);
    const auto before = InfiniteSessionClassificationTestAccess::state(freshSession);
    const auto logon = finish(FIX42::Logon(EncryptMethod(0), HeartBtInt(30)), before.targetSequence + 1);

    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        freshSession,
        InfiniteSessionClassificationTestAccess::atHead(0x76),
        logon.toString(),
        now);

    CHECK(classification.kind() == InfiniteSessionActionKind::ProtocolControl);
    CHECK(infiniteActionPlan(classification.actionData()).resultingState.mutableState.queuedMessages->empty());
    CHECK(InfiniteSessionClassificationTestAccess::queuedMessageCount(freshSession) == 0);
  }

  SECTION("sequence exhaustion never wraps planned state") {
    SECTION("target sequence") {
      session.setNextTargetMsgSeqNum(std::numeric_limits<SEQNUM>::max());
      clearEffects();
      const auto before = snapshot();
      const auto message = applicationMessage(before.state.targetSequence);
      const auto classification = InfiniteSessionClassificationTestAccess::classify(
          session,
          InfiniteSessionClassificationTestAccess::atHead(0x77),
          message.toString(),
          now);

      CHECK(classification.kind() == InfiniteSessionActionKind::ProtocolDisposition);
      CHECK(
          infiniteActionPlan(classification.actionData()).resultingState.targetSequence
          == std::numeric_limits<SEQNUM>::max());
      CHECK(snapshot() == before);
    }

    SECTION("sender sequence") {
      session.setNextSenderMsgSeqNum(std::numeric_limits<SEQNUM>::max());
      clearEffects();
      const auto before = snapshot();
      const auto request = finish(FIX42::TestRequest(TestReqID("MAX")), before.state.targetSequence);
      const auto classification = InfiniteSessionClassificationTestAccess::classify(
          session,
          InfiniteSessionClassificationTestAccess::atHead(0x78),
          request.toString(),
          now);

      CHECK(classification.kind() == InfiniteSessionActionKind::Failure);
      CHECK(snapshot() == before);
    }
  }

  SECTION("DoNotSend fences the private path before any authorized output") {
    auto outbound = applicationMessage(session.getExpectedSenderNum());
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x74),
        request.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    endpoint.doNotSendToApp = true;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    endpoint.doNotSendToApp = false;
    CHECK(endpoint.outputs.empty());
    CHECK(InfiniteSessionClassificationTestAccess::state(session).mutableState.infiniteFenced);
  }

  SECTION("toApp mutation fences the private path before any authorized output") {
    auto outbound = applicationMessage(session.getExpectedSenderNum());
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x76),
        request.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    endpoint.mutateToApp = true;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    endpoint.mutateToApp = false;
    CHECK(endpoint.outputs.empty());
    CHECK(InfiniteSessionClassificationTestAccess::state(session).mutableState.infiniteFenced);
  }

  SECTION("toApp credentials fence the private path before any authorized output") {
    auto outbound = applicationMessage(session.getExpectedSenderNum());
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x76),
        request.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    endpoint.injectToAppCredentials = true;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    endpoint.injectToAppCredentials = false;
    CHECK(endpoint.outputs.empty());
    CHECK(InfiniteSessionClassificationTestAccess::state(session).mutableState.infiniteFenced);
  }

  SECTION("source-store changes during a callback fence before the next planned effect") {
    auto outbound = applicationMessage(session.getExpectedSenderNum());
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x74),
        request.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    endpoint.toAppHook = [&]() { REQUIRE(storeFactory.store->set(outboundSequence, "changed")); };

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    endpoint.toAppHook = {};
    CHECK(endpoint.outputs.empty());
    CHECK(InfiniteSessionClassificationTestAccess::state(session).mutableState.infiniteFenced);
  }

  SECTION("callbacks release the Session lock while same-session mutation stays blocked") {
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x75),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    endpoint.blockFromApp = true;
    std::thread applying(
        [&]() { InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)); });
    {
      std::unique_lock<std::mutex> lock(endpoint.callbackMutex);
      endpoint.callbackCondition.wait(lock, [&]() { return endpoint.callbackEntered; });
    }

    auto mutation = std::async(std::launch::async, [&]() { session.setNextTargetMsgSeqNum(99); });
    REQUIRE(mutation.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK_THROWS_AS(mutation.get(), IOException);
    {
      std::lock_guard<std::mutex> lock(endpoint.callbackMutex);
      endpoint.releaseCallback = true;
    }
    endpoint.callbackCondition.notify_all();
    applying.join();
    CHECK(session.getExpectedTargetNum() == classification.expected().targetSequence + 1);
  }

  SECTION("a state mutator admitted before callback activation is rechecked under lock") {
    auto outbound = applicationMessage(session.getExpectedSenderNum());
    REQUIRE(session.send(outbound));
    const auto outboundSequence = outbound.getHeader().getField<MsgSeqNum>().getValue();
    clearEffects();
    const auto request = finish(
        FIX42::ResendRequest(BeginSeqNo(outboundSequence), EndSeqNo(outboundSequence)),
        session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x75),
        request.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    endpoint.toAppHook = [&]() {
      std::unique_lock<std::mutex> lock(endpoint.callbackMutex);
      endpoint.callbackEntered = true;
      endpoint.callbackCondition.notify_all();
      endpoint.callbackCondition.wait(lock, [&]() { return endpoint.releaseCallback; });
    };
    storeFactory.store->blockGet = true;
    auto applying = std::async(std::launch::async, [&]() {
      InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    });
    {
      std::unique_lock<std::mutex> lock(storeFactory.store->getMutex);
      storeFactory.store->getCondition.wait(lock, [&]() { return storeFactory.store->getEntered; });
    }

    auto *const cachedLog = session.getLog();
    auto mutation = std::async(std::launch::async, [cachedLog]() { cachedLog->onEvent("pre-admitted mutation"); });
    {
      std::lock_guard<std::mutex> lock(storeFactory.store->getMutex);
      storeFactory.store->releaseGet = true;
    }
    storeFactory.store->getCondition.notify_all();
    {
      std::unique_lock<std::mutex> lock(endpoint.callbackMutex);
      endpoint.callbackCondition.wait(lock, [&]() { return endpoint.callbackEntered; });
    }

    REQUIRE(mutation.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK_THROWS_AS(mutation.get(), IOException);
    {
      std::lock_guard<std::mutex> lock(endpoint.callbackMutex);
      endpoint.releaseCallback = true;
    }
    endpoint.callbackCondition.notify_all();
    REQUIRE(applying.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK_NOTHROW(applying.get());
    endpoint.toAppHook = {};
    storeFactory.store->blockGet = false;
  }

  SECTION("a concurrent authorization cannot clear a fence raised during callback application") {
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x75),
        message.toString(),
        now);
    const auto competing = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x76),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    auto competingAuthorization = InfiniteSessionClassificationTestAccess::authorization(competing);
    endpoint.blockFromApp = true;
    auto applying = std::async(std::launch::async, [&]() {
      InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization));
    });
    {
      std::unique_lock<std::mutex> lock(endpoint.callbackMutex);
      endpoint.callbackCondition.wait(lock, [&]() { return endpoint.callbackEntered; });
    }

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, competing, std::move(competingAuthorization)));
    {
      std::lock_guard<std::mutex> lock(endpoint.callbackMutex);
      endpoint.releaseCallback = true;
    }
    endpoint.callbackCondition.notify_all();
    REQUIRE(applying.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    CHECK_THROWS(applying.get());
    CHECK(InfiniteSessionClassificationTestAccess::state(session).mutableState.infiniteFenced);
  }

  SECTION("a callback can enter a different Session without lock inversion") {
    RecordingStoreFactory otherStoreFactory;
    RecordingLogFactory otherLogFactory;
    Session other(
        [this]() { return now; },
        endpoint,
        otherStoreFactory,
        SessionID(BeginString_FIX42, "VENUE", "OTHER"),
        dictionaries,
        sessionTime,
        0,
        &otherLogFactory);
    const auto otherTarget = other.getExpectedTargetNum();
    endpoint.reenterSession = &other;
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x79),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);

    CHECK_NOTHROW(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    endpoint.reenterSession = nullptr;
    CHECK(other.getExpectedTargetNum() == otherTarget + 1);
  }

  SECTION("a cached Log handle cannot mutate the Session from a callback") {
    auto *const cachedLog = session.getLog();
    endpoint.fromAppHook = [cachedLog]() { cachedLog->onEvent("callback mutation"); };
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x7a),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    endpoint.fromAppHook = {};
    const auto fenced = InfiniteSessionClassificationTestAccess::state(session);
    CHECK(fenced.mutableState.infiniteFenced);
    CHECK(fenced.targetSequence == before.state.targetSequence);
    REQUIRE(logFactory.log->kinds.size() == before.logKinds.size() + 1);
    CHECK(logFactory.log->kinds.back() == "incoming");
  }

  SECTION("callback failure preserves the exact effects ordered before it") {
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
    REQUIRE(logFactory.log->kinds.size() == before.logKinds.size() + 1);
    CHECK(logFactory.log->kinds.back() == "incoming");
    CHECK(logFactory.log->values.back() == message.toString());
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
    const auto fenced = InfiniteSessionClassificationTestAccess::state(session);
    CHECK(fenced.revision == before.state.revision);
    CHECK(fenced.senderSequence == before.state.senderSequence);
    CHECK(fenced.targetSequence == before.state.targetSequence);
    CHECK(fenced.mutableState.infiniteFenced);
    CHECK(storeFactory.store->writes == before.storeWrites);
    REQUIRE(logFactory.log->kinds.size() == before.logKinds.size() + 1);
    CHECK(logFactory.log->kinds.back() == "incoming");
    CHECK(endpoint.outputs == before.outputs);
  }

  SECTION("outgoing callback mutation retains only earlier ordered effects") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x79);
    const auto request = finish(FIX42::TestRequest(TestReqID("MUTATE")), session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();
    endpoint.mutateToAdmin = true;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    endpoint.mutateToAdmin = false;
    const auto fenced = InfiniteSessionClassificationTestAccess::state(session);
    CHECK(fenced.revision == before.state.revision);
    CHECK(fenced.senderSequence == before.state.senderSequence);
    CHECK(fenced.targetSequence == before.state.targetSequence);
    CHECK(fenced.mutableState.infiniteFenced);
    CHECK(storeFactory.store->writes == before.storeWrites);
    REQUIRE(logFactory.log->kinds.size() == before.logKinds.size() + 1);
    CHECK(logFactory.log->kinds.back() == "incoming");
    CHECK(endpoint.outputs == before.outputs);
  }

  SECTION("outgoing callback credentials are rejected and cleansed") {
    Fixture unlogged(false);
    const auto logon = finish(FIX42::Logon(EncryptMethod(0), HeartBtInt(30)), unlogged.session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        unlogged.session,
        InfiniteSessionClassificationTestAccess::atHead(0x7a),
        logon.toString(),
        unlogged.now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    unlogged.endpoint.injectToAdminCredentials = true;

    CHECK_THROWS(
        InfiniteSessionClassificationTestAccess::apply(unlogged.session, classification, std::move(authorization)));

    unlogged.endpoint.injectToAdminCredentials = false;
    CHECK(InfiniteSessionClassificationTestAccess::state(unlogged.session).mutableState.infiniteFenced);
    CHECK(unlogged.endpoint.outputs.empty());
  }

  SECTION("failed responder send preserves exact prior effects and fences further classification") {
    const auto binding = InfiniteSessionClassificationTestAccess::atHead(0x7a);
    const auto request = finish(FIX42::TestRequest(TestReqID("FAIL-SEND")), session.getExpectedTargetNum());
    const auto classification
        = InfiniteSessionClassificationTestAccess::classify(session, binding, request.toString(), now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = InfiniteSessionClassificationTestAccess::state(session);
    endpoint.sendSucceeds = false;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));
    endpoint.sendSucceeds = true;
    const auto fenced = InfiniteSessionClassificationTestAccess::state(session);
    CHECK(fenced.revision == before.revision);
    CHECK(fenced.senderSequence == infiniteActionPlan(classification.actionData()).resultingState.senderSequence);
    CHECK(fenced.targetSequence == before.targetSequence);
    CHECK(fenced.mutableState.infiniteFenced);
    const auto refused = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x7c),
        applicationMessage(fenced.targetSequence).toString(),
        now);
    CHECK(refused.kind() == InfiniteSessionActionKind::Failure);
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
    const auto fenced = InfiniteSessionClassificationTestAccess::state(session);
    CHECK(fenced.revision == before.revision);
    CHECK(fenced.senderSequence == before.senderSequence);
    CHECK(fenced.targetSequence == before.targetSequence);
    CHECK(fenced.mutableState.infiniteFenced);
  }

  SECTION("failed terminal sequence write retains prior evidence and fences") {
    const auto message = applicationMessage(session.getExpectedTargetNum());
    const auto classification = InfiniteSessionClassificationTestAccess::classify(
        session,
        InfiniteSessionClassificationTestAccess::atHead(0x7d),
        message.toString(),
        now);
    auto authorization = InfiniteSessionClassificationTestAccess::authorization(classification);
    const auto before = snapshot();
    storeFactory.store->throwTargetWrite = true;

    CHECK_THROWS(InfiniteSessionClassificationTestAccess::apply(session, classification, std::move(authorization)));

    storeFactory.store->throwTargetWrite = false;
    const auto fenced = InfiniteSessionClassificationTestAccess::state(session);
    CHECK(fenced.revision == before.state.revision);
    CHECK(fenced.targetSequence == before.state.targetSequence);
    CHECK(fenced.mutableState.infiniteFenced);
    REQUIRE(logFactory.log->kinds.size() == before.logKinds.size() + 1);
    CHECK(logFactory.log->kinds.back() == "incoming");
  }
}
} // namespace
