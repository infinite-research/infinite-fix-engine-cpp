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

#include "Application.h"
#include "DataDictionaryProvider.h"
#include "Message.h"
#include "MessageStore.h"
#include "Responder.h"
#include "Session.h"
#include "TimeRange.h"

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace FIX {
namespace {
constexpr std::uint64_t FIX_SEQUENCE_BOUND = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
enum class WireSequenceStatus {
  Missing,
  Valid,
  Invalid
};

WireSequenceStatus wireSequence(const FieldMap &fields, int tag, bool allowZero, std::uint64_t &value) {
  StringField raw(tag);
  if (!fields.getFieldIfSet(raw)) {
    return WireSequenceStatus::Missing;
  }
  const auto &digits = raw.getString();
  if (digits.empty() || digits.size() > 19) {
    return WireSequenceStatus::Invalid;
  }
  value = 0;
  for (const unsigned char digit : digits) {
    if (digit < '0' || digit > '9') {
      return WireSequenceStatus::Invalid;
    }
    value = value * 10 + digit - '0';
  }
  return (allowZero || value != 0) && value < FIX_SEQUENCE_BOUND ? WireSequenceStatus::Valid
                                                                 : WireSequenceStatus::Invalid;
}

class PlanningStore : public MessageStore {
public:
  explicit PlanningStore(const UtcTimeStamp &creationTime)
      : m_creationTime(creationTime) {}

  bool set(SEQNUM sequence, const std::string &message) override {
    requireWireValue(sequence);
    m_messages[sequence] = message;
    return true;
  }

  void get(SEQNUM begin, SEQNUM end, std::vector<std::string> &messages) const override {
    requireWireValue(begin);
    requireWireValue(end);
    messages.clear();
    for (auto current = m_messages.find(begin); current != m_messages.end() && current->first <= end; ++current) {
      messages.push_back(current->second);
    }
  }

  SEQNUM getNextSenderMsgSeqNum() const override {
    if (m_nextSender >= BOUND) {
      throw std::invalid_argument("Planning sender exhausted");
    }
    return m_nextSender;
  }
  SEQNUM getNextTargetMsgSeqNum() const override { return m_nextTarget; }
  void setNextSenderMsgSeqNum(SEQNUM value) override {
    requireWireValue(value);
    m_nextSender = value;
  }
  void setNextTargetMsgSeqNum(SEQNUM value) override {
    requireWireValue(value);
    m_nextTarget = value;
  }
  void incrNextSenderMsgSeqNum() override { increment(m_nextSender); }
  void incrNextTargetMsgSeqNum() override { increment(m_nextTarget); }
  UtcTimeStamp getCreationTime() const override { return m_creationTime; }
  void reset(const UtcTimeStamp &) override { throw std::logic_error("Detached planning reset"); }
  void refresh() override {}

  SEQNUM nextSender() const noexcept { return m_nextSender; }
  SEQNUM nextTarget() const noexcept { return m_nextTarget; }

private:
  static constexpr SEQNUM BOUND = static_cast<SEQNUM>(FIX_SEQUENCE_BOUND);

  static void requireWireValue(SEQNUM value) {
    if (value == 0 || value >= BOUND) {
      throw std::invalid_argument("Planning sequence domain");
    }
  }
  static void increment(SEQNUM &value) {
    if (value == 0 || value >= BOUND) {
      throw std::invalid_argument("Planning sequence exhausted");
    }
    ++value;
  }

  std::map<SEQNUM, std::string> m_messages;
  SEQNUM m_nextSender{1};
  SEQNUM m_nextTarget{1};
  UtcTimeStamp m_creationTime;
};

class PlanningStoreFactory : public MessageStoreFactory {
public:
  MessageStore *create(const UtcTimeStamp &now, const SessionID &) override {
    if (m_store != nullptr) {
      throw std::logic_error("Planning store already created");
    }
    m_store = new PlanningStore(now);
    return m_store;
  }
  void destroy(MessageStore *store) override {
    delete store;
    m_store = nullptr;
  }
  SEQNUM nextSender() const noexcept { return m_store->nextSender(); }
  SEQNUM nextTarget() const noexcept { return m_store->nextTarget(); }

private:
  PlanningStore *m_store{nullptr};
};

class PlanningApplication : public Application {
public:
  explicit PlanningApplication(
      std::string testRequestId,
      const InfiniteSessionStaticProfile *profile = nullptr,
      std::uint64_t lastProcessedSequence = 0,
      bool allowApplicationOutput = false)
      : m_testRequestId(std::move(testRequestId)),
        m_profile(profile),
        m_lastProcessedSequence(lastProcessedSequence),
        m_allowApplicationOutput(allowApplicationOutput) {}

  void onCreate(const SessionID &) override { throw std::logic_error("Detached Session registered"); }
  void onLogon(const SessionID &) override {}
  void onLogout(const SessionID &) override {}
  void toAdmin(Message &message, const SessionID &) override {
    decorate(message);
    if (!m_testRequestId.empty()) {
      message.setField(TestReqID(m_testRequestId));
    }
    MsgType msgType;
    message.getHeader().getField(msgType);
    if (m_profile != nullptr && msgType == MsgType_Logon) {
      message.setField(DefaultApplExtID(299));
      message.setField(DefaultCstmApplVerID(m_profile->defaultCustomApplicationVersion));
    }
  }
  void toApp(Message &message, const SessionID &) override {
    if (!m_allowApplicationOutput) {
      throw std::logic_error("Unexpected application output");
    }
    decorate(message);
  }
  void fromAdmin(const Message &, const SessionID &) override { admin = true; }
  void fromApp(const Message &, const SessionID &) override { application = true; }

  bool application{false};
  bool admin{false};

private:
  void decorate(Message &message) const {
    auto &header = message.getHeader();
    header.setField(LastMsgSeqNumProcessed(m_lastProcessedSequence));
    if (m_profile == nullptr) {
      return;
    }
    if (!m_profile->senderSubId.empty()) {
      header.setField(SenderSubID(m_profile->senderSubId));
    }
    if (!m_profile->senderLocationId.empty()) {
      header.setField(SenderLocationID(m_profile->senderLocationId));
    }
    if (!m_profile->targetSubId.empty()) {
      header.setField(TargetSubID(m_profile->targetSubId));
    }
    if (!m_profile->targetLocationId.empty()) {
      header.setField(TargetLocationID(m_profile->targetLocationId));
    }
  }

  std::string m_testRequestId;
  const InfiniteSessionStaticProfile *m_profile;
  std::uint64_t m_lastProcessedSequence;
  bool m_allowApplicationOutput;
};

class RecordingResponder : public Responder {
public:
  bool send(const std::string &wire) override {
    outputs.push_back(wire);
    output = wire;
    return true;
  }

  void disconnect() override { disconnected = true; }

  std::string output;
  std::vector<std::string> outputs;
  bool disconnected{false};
};

UtcTimeStamp utcTime(std::int64_t nanoseconds) {
  constexpr std::int64_t NANOS_PER_SECOND = INT64_C(1000000000);
  auto seconds = nanoseconds / NANOS_PER_SECOND;
  auto fraction = static_cast<int>(nanoseconds % NANOS_PER_SECOND);
  if (fraction < 0) {
    --seconds;
    fraction += static_cast<int>(NANOS_PER_SECOND);
  }
  if (seconds > std::numeric_limits<std::time_t>::max()
      || (std::numeric_limits<std::time_t>::is_signed && seconds < std::numeric_limits<std::time_t>::min())) {
    throw std::invalid_argument("UTC time");
  }
  return UtcTimeStamp(static_cast<std::time_t>(seconds), fraction, 9);
}

UtcTimeOnly utcTimeOnly(std::uint32_t seconds) {
  return UtcTimeOnly(
      static_cast<int>(seconds / 3600),
      static_cast<int>(seconds / 60 % 60),
      static_cast<int>(seconds % 60));
}

TimeRange governedRange(const InfiniteSessionStaticProfile &profile, std::size_t offset) {
  return TimeRange(
      utcTimeOnly(profile.schedule[offset + 1]),
      utcTimeOnly(profile.schedule[offset + 3]),
      static_cast<int>(profile.schedule[offset] + 1),
      static_cast<int>(profile.schedule[offset + 2] + 1));
}

bool governedRangeContains(
    const InfiniteSessionStaticProfile &profile,
    std::size_t offset,
    const UtcTimeStamp &now) noexcept {
  if (profile.scheduleMode == 1) {
    return true;
  }
  constexpr std::uint64_t SECONDS_PER_DAY = UINT64_C(86400);
  constexpr std::uint64_t NANOSECONDS_PER_SECOND = UINT64_C(1000000000);
  const auto position = [](std::uint64_t day, std::uint64_t second) {
    return (day * SECONDS_PER_DAY + second) * NANOSECONDS_PER_SECOND;
  };
  const auto current = position(
                           static_cast<std::uint64_t>(now.getWeekDay() - 1),
                           static_cast<std::uint64_t>(now.getHour() * 3600 + now.getMinute() * 60 + now.getSecond()))
                       + now.getNanosecond();
  const auto start = position(profile.schedule[offset], profile.schedule[offset + 1]);
  const auto end = position(profile.schedule[offset + 2], profile.schedule[offset + 3]);
  return start == end || (start < end ? current >= start && current <= end : current >= start || current <= end);
}

bool governedSessionContains(
    const InfiniteSessionStaticProfile &profile,
    std::int64_t creationUtcNanoseconds,
    std::int64_t nowUtcNanoseconds,
    const UtcTimeStamp &now) noexcept {
  if (!governedRangeContains(profile, 0, now)) {
    return false;
  }
  if (profile.scheduleMode == 1) {
    return true;
  }
  constexpr std::int64_t NANOSECONDS_PER_SECOND = INT64_C(1000000000);
  constexpr std::int64_t SECONDS_PER_DAY = INT64_C(86400);
  constexpr std::int64_t SECONDS_PER_WEEK = INT64_C(604800);
  const auto creationSecond = creationUtcNanoseconds / NANOSECONDS_PER_SECOND;
  const auto daysSinceEpoch = creationSecond / SECONDS_PER_DAY;
  const auto weekday = (daysSinceEpoch + 4) % 7;
  const auto secondOfDay = creationSecond % SECONDS_PER_DAY;
  const auto currentWeekSecond = weekday * SECONDS_PER_DAY + secondOfDay;
  const auto targetWeekSecond = static_cast<std::int64_t>(profile.schedule[2]) * SECONDS_PER_DAY + profile.schedule[3];
  auto delta = (targetWeekSecond - currentWeekSecond + SECONDS_PER_WEEK) % SECONDS_PER_WEEK;
  if (delta == 0) {
    delta = SECONDS_PER_WEEK;
  }
  constexpr auto LAST_WHOLE_SECOND = INT64_MAX / NANOSECONDS_PER_SECOND;
  return creationSecond <= LAST_WHOLE_SECOND - delta
         && nowUtcNanoseconds <= (creationSecond + delta) * NANOSECONDS_PER_SECOND;
}

void configureStaticProfile(Session &session, const InfiniteSessionStaticProfile *profile) {
  session.setTimestampPrecision(profile == nullptr ? 6 : static_cast<int>(profile->timestampPrecision));
  session.setSenderDefaultApplVerID("10");
  session.setTargetDefaultApplVerID("10");
  if (profile == nullptr) {
    return;
  }
  session.setSendRedundantResendRequests(profile->sendRedundantResendRequests);
  session.setCheckCompId(profile->checkCompId);
  session.setCheckLatency(profile->checkLatency);
  session.setMaxLatency(static_cast<int>(profile->maximumLatency));
  session.setResetOnLogon(profile->resetOnLogon);
  session.setResetOnLogout(profile->resetOnLogout);
  session.setResetOnDisconnect(profile->resetOnDisconnect);
  session.setRefreshOnLogon(profile->refreshOnLogon);
  session.setPersistMessages(profile->persistMessages);
  session.setValidateLengthAndChecksum(profile->validateLengthAndChecksum);
  session.setSendNextExpectedMsgSeqNum(profile->sendNextExpectedMsgSeqNum);
}

std::pair<std::size_t, std::size_t> originalBodyRange(const Message &message, const std::string &wire) {
  const auto headerLength = message.getHeader().calculateLength(0, 0, 0);
  const auto bodyLength = message.calculateLength(0, 0, 0);
  const auto trailerLength = message.getTrailer().calculateLength(0, 0, 0);
  if (headerLength < 0 || bodyLength < 0 || trailerLength < 0) {
    throw std::invalid_argument("FIX body range");
  }
  const auto header = static_cast<std::size_t>(headerLength);
  const auto body = static_cast<std::size_t>(bodyLength);
  const auto trailer = static_cast<std::size_t>(trailerLength);
  if (header > wire.size() || body > wire.size() - header || trailer != wire.size() - header - body) {
    throw std::invalid_argument("FIX body range");
  }
  return {header, body};
}

std::string businessRejectReference(const Message &message, const std::string &msgType) {
  const auto first = [&message](std::initializer_list<int> tags) {
    for (const auto tag : tags) {
      StringField field(tag);
      if (message.getFieldIfSet(field)) {
        return field.getString();
      }
    }
    return std::string{};
  };
  if (msgType == "AH" || msgType == "UAH0") {
    return first({644});
  }
  if (msgType == "R") {
    return first({131});
  }
  if (msgType == "S" || msgType == "Z") {
    return first({1166});
  }
  if (msgType == "AJ") {
    return first({11});
  }
  if (msgType == "EC") {
    return first({2965});
  }
  if (msgType == "j") {
    return first({379});
  }
  if (msgType == "AI") {
    return first({649, 1166, 117, 131, 693});
  }
  if (msgType == "AG") {
    return first({131});
  }
  if (msgType == "8") {
    return first({17, 37, 11, 1166, 693});
  }
  if (msgType == "AE") {
    return first({571, 1003, 17});
  }
  if (msgType == "AK") {
    return first({664});
  }
  if (msgType == "ED") {
    return first({2965});
  }
  if (msgType == "EE") {
    return first({2967, 2965, 664});
  }
  if (msgType == "CW") {
    return first({1166, 117, 131});
  }
  if (msgType == "J") {
    return first({70});
  }
  if (msgType == "AS") {
    return first({755});
  }
  if (msgType == "T") {
    return first({777});
  }
  return {};
}

std::string applicationParseEnvelope(const std::string &beginString, const std::string &body) {
  return "8=" + beginString + "\0019=" + std::to_string(body.size()) + "\001" + body + "10=000\001";
}

} // namespace

InfiniteHeartbeatPlan InfiniteSessionPlanner::application(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    const std::string &msgType,
    const std::string &body,
    InfiniteApplicationRenderMode mode,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider &dictionaries,
    const InfiniteSessionStaticProfile &profile) {
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || heartbeatSeconds == 0
      || senderSequence == 0 || senderSequence >= FIX_SEQUENCE_BOUND || targetSequence == 0
      || targetSequence >= FIX_SEQUENCE_BOUND || nowUtcNanoseconds <= 0 || lastProcessedSequence >= FIX_SEQUENCE_BOUND
      || msgType.empty() || msgType.size() > 8 || body.empty() || body.size() > 65536
      || !std::all_of(msgType.begin(), msgType.end(), [](unsigned char byte) {
           return byte >= 0x21 && byte <= 0x7e;
         })) {
    throw std::invalid_argument("Application planner input");
  }
  const auto &sessionDictionary = dictionaries.getSessionDataDictionary(BeginString(beginString));
  const auto &applicationDictionary = dictionaries.getApplicationDataDictionary(ApplVerID("10"));
  const auto parse = [&] {
    try {
      const auto fakeBody = "35=" + msgType + "\00134=1\00149=" + senderCompId
                            + "\00152=20260828-12:00:00.000000\00156=" + targetCompId + "\001" + body;
      Message message(applicationParseEnvelope(beginString, fakeBody), sessionDictionary, applicationDictionary, false);
      MsgType parsedType;
      message.getHeader().getField(parsedType);
      std::string canonicalBody;
      message.calculateString(canonicalBody);
      DataDictionary::validate(message, &sessionDictionary, &applicationDictionary);
      if (!message.isApp() || parsedType.getValue() != msgType || canonicalBody != body) {
        throw std::invalid_argument("Application body");
      }
      message.getHeader().clear();
      message.getTrailer().clear();
      message.getHeader().setField(parsedType);
      return message;
    } catch (const Exception &) {
      throw std::invalid_argument("Application body");
    }
  };
  const auto render = [&](InfiniteApplicationRenderMode renderMode,
                          std::uint64_t sequence,
                          std::uint64_t frontier,
                          std::int64_t utcNanoseconds) {
    auto message = parse();
    const auto now = utcTime(utcNanoseconds);
    PlanningApplication application("", &profile, frontier, true);
    PlanningStoreFactory stores;
    const SessionID sessionId(beginString, senderCompId, targetCompId, profile.qualifier);
    const TimeRange nonstop(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
    const auto sessionTime = profile.scheduleMode == 1 ? nonstop : governedRange(profile, 0);
    const auto logonTime = profile.scheduleMode == 1 ? nonstop : governedRange(profile, 4);
    Session session([now] { return now; }, application, stores, sessionId, dictionaries, sessionTime, 0, nullptr, true);
    RecordingResponder responder;
    session.setLogonTime(logonTime);
    session.setIsNonStopSession(profile.scheduleMode == 1);
    configureStaticProfile(session, &profile);
    session.setNextSenderMsgSeqNum(sequence);
    session.setNextTargetMsgSeqNum(targetSequence);
    session.m_state.heartBtInt(static_cast<int>(heartbeatSeconds));
    session.m_state.enabled(true);
    session.m_state.receivedLogon(true);
    session.m_state.sentLogon(true);
    session.m_state.lastSentTime(now);
    session.m_state.lastReceivedTime(now);
    session.setResponder(&responder);
    if (renderMode == InfiniteApplicationRenderMode::SemanticReplay) {
      message.getHeader().setField(PossResend(true));
      session.send(message);
    } else if (renderMode == InfiniteApplicationRenderMode::SessionRetransmission) {
      message.getHeader().setField(PossDupFlag(true));
      message.getHeader().setField(OrigSendingTime(now, static_cast<int>(profile.timestampPrecision)));
      session.sendRaw(message, sequence);
    } else {
      session.send(message);
    }
    if (responder.output.empty() || responder.disconnected) {
      throw std::logic_error("Application planner produced no output");
    }
    return std::pair<std::string, std::uint64_t>{std::move(responder.output), stores.nextSender()};
  };
  constexpr auto maximumSequence = FIX_SEQUENCE_BOUND - 1;
  const auto maximumOriginal
      = render(InfiniteApplicationRenderMode::Original, maximumSequence, maximumSequence, INT64_MAX).first.size();
  const auto maximumRetransmission
      = render(InfiniteApplicationRenderMode::SessionRetransmission, maximumSequence, maximumSequence, INT64_MAX)
            .first.size();
  const auto maximumSemantic
      = render(InfiniteApplicationRenderMode::SemanticReplay, maximumSequence, maximumSequence, INT64_MAX).first.size();
  const auto rendered = render(mode, senderSequence, lastProcessedSequence, nowUtcNanoseconds);
  return {
      rendered.first,
      rendered.second,
      targetSequence,
      0,
      false,
      std::max({maximumOriginal, maximumRetransmission, maximumSemantic})};
}

InfiniteStoredFramePlan InfiniteSessionPlanner::storedFrame(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    std::uint64_t lastProcessedSequence,
    const std::string &wire,
    const DataDictionaryProvider &dictionaries,
    const InfiniteSessionStaticProfile &profile) {
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || heartbeatSeconds == 0
      || senderSequence == 0 || senderSequence >= FIX_SEQUENCE_BOUND || targetSequence == 0
      || targetSequence >= FIX_SEQUENCE_BOUND || nowUtcNanoseconds <= 0 || lastProcessedSequence >= FIX_SEQUENCE_BOUND
      || wire.empty() || wire.size() > 65536) {
    throw std::invalid_argument("Stored frame planner input");
  }
  try {
    const auto &sessionDictionary = dictionaries.getSessionDataDictionary(BeginString(beginString));
    const auto &applicationDictionary = dictionaries.getApplicationDataDictionary(ApplVerID("10"));
    Message message(wire, sessionDictionary, applicationDictionary, true);
    MsgType msgType;
    SenderCompID sender;
    TargetCompID target;
    SendingTime originalSendingTime;
    auto &header = message.getHeader();
    header.getField(msgType);
    header.getField(sender);
    header.getField(target);
    header.getField(originalSendingTime);
    if (header.isSetField(FIELD::PossDupFlag) || header.isSetField(FIELD::OrigSendingTime)
        || header.isSetField(FIELD::PossResend)) {
      throw std::invalid_argument("Stored recovery header");
    }
    std::uint64_t sequence = 0;
    const auto matchesOptional = [&header](int tag, const std::string &expected) {
      StringField actual(tag);
      const bool present = header.getFieldIfSet(actual);
      return expected.empty() ? !present : present && actual.getString() == expected;
    };
    if (msgType.getValue().empty() || msgType.getValue().size() > 8
        || !std::all_of(
            msgType.getValue().begin(),
            msgType.getValue().end(),
            [](unsigned char byte) { return byte >= 0x21 && byte <= 0x7e; })
        || wireSequence(header, FIELD::MsgSeqNum, false, sequence) != WireSequenceStatus::Valid
        || sender != senderCompId || target != targetCompId || !matchesOptional(FIELD::SenderSubID, profile.senderSubId)
        || !matchesOptional(FIELD::SenderLocationID, profile.senderLocationId)
        || !matchesOptional(FIELD::TargetSubID, profile.targetSubId)
        || !matchesOptional(FIELD::TargetLocationID, profile.targetLocationId)) {
      throw std::invalid_argument("Stored frame identity");
    }
    if (message.isApp()) {
      DataDictionary::validate(message, &sessionDictionary, &applicationDictionary);
    } else {
      sessionDictionary.validate(message);
    }
    const auto bodyRange = originalBodyRange(message, wire);
    auto body = wire.substr(bodyRange.first, bodyRange.second);
    if (message.isApp() && body.empty()) {
      throw std::invalid_argument("Stored application body");
    }
    const auto now = utcTime(nowUtcNanoseconds);
    PlanningApplication application("", &profile, lastProcessedSequence, true);
    PlanningStoreFactory stores;
    const SessionID sessionId(beginString, senderCompId, targetCompId, profile.qualifier);
    const TimeRange nonstop(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
    const auto sessionTime = profile.scheduleMode == 1 ? nonstop : governedRange(profile, 0);
    const auto logonTime = profile.scheduleMode == 1 ? nonstop : governedRange(profile, 4);
    Session session([now] { return now; }, application, stores, sessionId, dictionaries, sessionTime, 0, nullptr, true);
    RecordingResponder responder;
    session.setLogonTime(logonTime);
    session.setIsNonStopSession(profile.scheduleMode == 1);
    configureStaticProfile(session, &profile);
    session.setNextSenderMsgSeqNum(senderSequence);
    session.setNextTargetMsgSeqNum(targetSequence);
    session.m_state.heartBtInt(static_cast<int>(heartbeatSeconds));
    session.m_state.enabled(true);
    session.m_state.receivedLogon(true);
    session.m_state.sentLogon(true);
    session.m_state.lastSentTime(now);
    session.m_state.lastReceivedTime(now);
    session.setResponder(&responder);
    header.setField(PossDupFlag(true));
    header.setField(OrigSendingTime(originalSendingTime, static_cast<int>(profile.timestampPrecision)));
    if (!session.sendRaw(message, sequence) || responder.output.empty() || responder.disconnected
        || stores.nextSender() != senderSequence) {
      throw std::logic_error("Stored frame planner produced no output");
    }
    return {
        std::move(responder.output),
        msgType.getValue(),
        std::move(body),
        originalSendingTime.getString(),
        sequence,
        message.isApp(),
        message.isAdmin()};
  } catch (const Exception &error) {
    throw std::invalid_argument(error.what());
  }
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::heartbeat(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    const std::string &testRequestId,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::Heartbeat,
      testRequestId,
      lastProcessedSequence,
      0,
      0,
      0,
      dictionaries,
      profile);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::testRequest(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::TestRequest,
      "",
      lastProcessedSequence,
      0,
      0,
      0,
      dictionaries,
      profile);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::logout(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    const std::string &reason,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::Logout,
      reason,
      lastProcessedSequence,
      0,
      0,
      0,
      dictionaries,
      profile);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::resendRequest(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::ResendRequest,
      "",
      lastProcessedSequence,
      0,
      0,
      0,
      dictionaries,
      profile);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::logon(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::Logon,
      "",
      lastProcessedSequence,
      0,
      0,
      0,
      dictionaries,
      profile);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::reject(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    std::uint64_t refSequence,
    std::uint32_t refTag,
    std::uint32_t rejectReason,
    const std::string &refMsgType,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::Reject,
      refMsgType,
      lastProcessedSequence,
      refSequence,
      refTag,
      rejectReason,
      dictionaries,
      profile);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::businessReject(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    std::uint64_t refSequence,
    const std::string &refMsgType,
    const std::string &businessRejectRefId,
    const std::string &gatewayInboundDispositionId,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::BusinessReject,
      refMsgType,
      lastProcessedSequence,
      refSequence,
      0,
      0,
      dictionaries,
      profile,
      businessRejectRefId,
      gatewayInboundDispositionId);
}

InfiniteInboundPlan InfiniteSessionPlanner::inbound(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t creationUtcNanoseconds,
    std::int64_t nowUtcNanoseconds,
    std::int64_t lastSentUtcNanoseconds,
    std::int64_t lastReceivedUtcNanoseconds,
    std::uint64_t sessionFlags,
    std::uint32_t testRequestCount,
    std::uint64_t lastProcessedSequence,
    const std::string &wire,
    const DataDictionaryProvider &dictionaries,
    const InfiniteSessionStaticProfile &profile,
    bool finalizeResetLogon,
    bool queuedReplay) {
  constexpr std::uint64_t FLAGS_MASK = UINT64_C(0x1ff);
  const bool detached = sessionFlags == UINT64_C(1);
  const auto activeHeartbeat = heartbeatSeconds == 0 ? profile.minimumHeartbeat : heartbeatSeconds;
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || activeHeartbeat == 0
      || activeHeartbeat > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || senderSequence == 0
      || senderSequence >= FIX_SEQUENCE_BOUND || targetSequence == 0 || targetSequence >= FIX_SEQUENCE_BOUND
      || lastProcessedSequence >= FIX_SEQUENCE_BOUND || creationUtcNanoseconds <= 0 || nowUtcNanoseconds <= 0
      || creationUtcNanoseconds > nowUtcNanoseconds || lastSentUtcNanoseconds <= 0 || lastReceivedUtcNanoseconds <= 0
      || (sessionFlags & ~FLAGS_MASK) != 0 || wire.empty()
      || (heartbeatSeconds == 0 && (!detached || profile.heartbeatMode != 2)) || profile.timestampPrecision > 9
      || profile.maximumLatency > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || testRequestCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Inbound planner input");
  }
  const auto now = utcTime(nowUtcNanoseconds);
  PlanningApplication application("", &profile, lastProcessedSequence);
  PlanningStoreFactory stores;
  const SessionID sessionId(beginString, senderCompId, targetCompId, profile.qualifier);
  const auto seconds = [](std::uint32_t value) {
    return UtcTimeOnly(static_cast<int>(value / 3600), static_cast<int>(value / 60 % 60), static_cast<int>(value % 60));
  };
  const TimeRange sessionTime(
      seconds(profile.schedule[1]),
      seconds(profile.schedule[3]),
      static_cast<int>(profile.schedule[0] + 1),
      static_cast<int>(profile.schedule[2] + 1));
  const TimeRange logonTime(
      seconds(profile.schedule[5]),
      seconds(profile.schedule[7]),
      static_cast<int>(profile.schedule[4] + 1),
      static_cast<int>(profile.schedule[6] + 1));
  Session session([now] { return now; }, application, stores, sessionId, dictionaries, sessionTime, 0, nullptr, true);
  RecordingResponder responder;
  session.setLogonTime(logonTime);
  session.setIsNonStopSession(queuedReplay || profile.scheduleMode == 1);
  session.setTimestampPrecision(static_cast<int>(profile.timestampPrecision));
  session.setSenderDefaultApplVerID("10");
  session.setTargetDefaultApplVerID("10");
  session.setSendRedundantResendRequests(profile.sendRedundantResendRequests);
  session.setCheckCompId(profile.checkCompId);
  session.setCheckLatency(!queuedReplay && profile.checkLatency);
  session.setMaxLatency(static_cast<int>(profile.maximumLatency));
  session.setResetOnLogon(profile.resetOnLogon);
  session.setResetOnLogout(profile.resetOnLogout);
  session.setResetOnDisconnect(profile.resetOnDisconnect);
  session.setRefreshOnLogon(profile.refreshOnLogon);
  session.setPersistMessages(profile.persistMessages);
  session.setValidateLengthAndChecksum(profile.validateLengthAndChecksum);
  session.setSendNextExpectedMsgSeqNum(profile.sendNextExpectedMsgSeqNum);
  session.setNextSenderMsgSeqNum(senderSequence);
  session.setNextTargetMsgSeqNum(targetSequence);
  session.m_state.heartBtInt(static_cast<int>(activeHeartbeat));
  session.m_state.enabled((sessionFlags & UINT64_C(1)) != 0);
  session.m_state.receivedLogon((sessionFlags & UINT64_C(2)) != 0);
  session.m_state.sentLogon((sessionFlags & UINT64_C(4)) != 0);
  session.m_state.sentLogout((sessionFlags & UINT64_C(16)) != 0);
  session.m_state.receivedReset((sessionFlags & UINT64_C(32)) != 0);
  session.m_state.sentReset((sessionFlags & UINT64_C(64)) != 0);
  session.m_state.initiate(false);
  session.m_state.testRequest(static_cast<int>(testRequestCount));
  session.m_state.lastSentTime(utcTime(lastSentUtcNanoseconds));
  session.m_state.lastReceivedTime(utcTime(lastReceivedUtcNanoseconds));
  session.setResponder(&responder);

  try {
    const auto &sessionDictionary = dictionaries.getSessionDataDictionary(BeginString(beginString));
    const auto &applicationDictionary = dictionaries.getApplicationDataDictionary(ApplVerID("10"));
    Message incoming(wire, sessionDictionary, applicationDictionary, true);
    MsgType msgType;
    SenderCompID incomingSender;
    TargetCompID incomingTarget;
    SendingTime sendingTime;
    const auto &header = incoming.getHeader();
    header.getField(msgType);
    if (msgType.getValue().empty() || msgType.getValue().size() > 8
        || !std::all_of(msgType.getValue().begin(), msgType.getValue().end(), [](unsigned char byte) {
             return byte >= 0x21 && byte <= 0x7e;
           })) {
      throw std::invalid_argument("Inbound MsgType bound");
    }
    std::uint64_t sequence = 0;
    const auto sequenceStatus = wireSequence(header, FIELD::MsgSeqNum, false, sequence);
    const bool identified = sequenceStatus != WireSequenceStatus::Missing;
    const bool sequenceValid = sequenceStatus == WireSequenceStatus::Valid;
    header.getField(incomingSender);
    header.getField(incomingTarget);
    header.getField(sendingTime);
    const auto matchesOptional = [&header](int tag, const std::string &expected) {
      StringField actual(tag);
      const bool present = header.getFieldIfSet(actual);
      return expected.empty() ? !present : present && actual.getString() == expected;
    };
    const bool completeIdentity = incomingSender == targetCompId && incomingTarget == senderCompId
                                  && matchesOptional(FIELD::SenderSubID, profile.targetSubId)
                                  && matchesOptional(FIELD::SenderLocationID, profile.targetLocationId)
                                  && matchesOptional(FIELD::TargetSubID, profile.senderSubId)
                                  && matchesOptional(FIELD::TargetLocationID, profile.senderLocationId);
    bool dictionaryValid = true;
    try {
      if (incoming.isApp()) {
        DataDictionary::validate(incoming, &sessionDictionary, &applicationDictionary);
      } else {
        sessionDictionary.validate(incoming);
      }
    } catch (const Exception &) {
      dictionaryValid = false;
    }
    int invalidLogonProfileTag = 0;
    int heartbeatRejectReason = 0;
    std::uint32_t validatedLogonHeartbeat = heartbeatSeconds;
    if (msgType == MsgType_Logon) {
      const auto matches = [&incoming](int tag, const std::string &expected) {
        StringField actual(tag);
        return incoming.getFieldIfSet(actual) && actual.getString() == expected;
      };
      if (!matches(FIELD::DefaultApplVerID, "10")) {
        invalidLogonProfileTag = FIELD::DefaultApplVerID;
      } else if (!matches(FIELD::DefaultApplExtID, "299")) {
        invalidLogonProfileTag = FIELD::DefaultApplExtID;
      } else if (!matches(FIELD::DefaultCstmApplVerID, profile.defaultCustomApplicationVersion)) {
        invalidLogonProfileTag = FIELD::DefaultCstmApplVerID;
      }
      StringField rawHeartbeat(FIELD::HeartBtInt);
      if (!incoming.getFieldIfSet(rawHeartbeat)) {
        heartbeatRejectReason = SessionRejectReason_REQUIRED_TAG_MISSING;
      } else {
        std::uint64_t requestedHeartbeat = 0;
        const auto &digits = rawHeartbeat.getString();
        const bool numeric = !digits.empty() && digits.front() != '0' && digits.size() <= 10
                             && std::all_of(digits.begin(), digits.end(), [&](const unsigned char digit) {
                                  if (digit < '0' || digit > '9') {
                                    return false;
                                  }
                                  requestedHeartbeat = requestedHeartbeat * 10 + digit - '0';
                                  return true;
                                });
        if (!numeric || requestedHeartbeat == 0 || requestedHeartbeat > std::numeric_limits<int>::max()
            || (profile.heartbeatMode == 1 && requestedHeartbeat != profile.configuredHeartbeat)
            || (profile.heartbeatMode == 2
                && (requestedHeartbeat < profile.minimumHeartbeat || requestedHeartbeat > profile.maximumHeartbeat))) {
          heartbeatRejectReason = SessionRejectReason_VALUE_IS_INCORRECT;
        } else {
          validatedLogonHeartbeat = static_cast<std::uint32_t>(requestedHeartbeat);
        }
      }
    }
    std::pair<std::size_t, std::size_t> bodyRange{};
    if (dictionaryValid) {
      bodyRange = originalBodyRange(incoming, wire);
      if (incoming.isApp() && bodyRange.second == 0) {
        throw std::invalid_argument("Inbound body range");
      }
    }
    const auto businessRejectRefId
        = dictionaryValid && incoming.isApp() ? businessRejectReference(incoming, msgType.getValue()) : std::string{};
    ResetSeqNumFlag reset(false);
    incoming.getFieldIfSet(reset);
    std::uint64_t lastProcessedSequence = 0;
    const auto lastProcessedStatus = wireSequence(header, FIELD::LastMsgSeqNumProcessed, true, lastProcessedSequence);
    const bool requiresLastProcessed
        = (sessionFlags & UINT64_C(6)) != 0 || (msgType == MsgType_Logon && static_cast<bool>(reset));
    std::uint64_t referencedSequence = 0;
    const bool invalidReferencedSequence
        = msgType == MsgType_Reject
          && wireSequence(incoming, FIELD::RefSeqNum, false, referencedSequence) != WireSequenceStatus::Valid;
    std::uint64_t nextExpectedSequence = 0;
    const auto nextExpectedStatus = wireSequence(incoming, FIELD::NextExpectedMsgSeqNum, false, nextExpectedSequence);
    const bool invalidNextExpectedSequence
        = msgType == MsgType_Logon && nextExpectedStatus == WireSequenceStatus::Invalid;
    const bool sequenceFieldsValid = lastProcessedStatus != WireSequenceStatus::Invalid
                                     && (!requiresLastProcessed || lastProcessedStatus == WireSequenceStatus::Valid)
                                     && !invalidReferencedSequence && !invalidNextExpectedSequence;
    std::uint64_t resendBegin = 0;
    std::uint64_t resendEndInclusive = 0;
    const auto resendBeginStatus = wireSequence(incoming, FIELD::BeginSeqNo, false, resendBegin);
    const auto resendEndStatus = wireSequence(incoming, FIELD::EndSeqNo, true, resendEndInclusive);
    const bool resendRangeValid
        = msgType != MsgType_ResendRequest
          || (resendBeginStatus == WireSequenceStatus::Valid && resendEndStatus == WireSequenceStatus::Valid
              && (resendEndInclusive == 0 || resendEndInclusive >= resendBegin));
    const bool timeMatches = session.isGoodTime(sendingTime);
    const bool sessionTimeMatches
        = queuedReplay || governedSessionContains(profile, creationUtcNanoseconds, nowUtcNanoseconds, now);
    const bool logonTimeMatches = queuedReplay || governedRangeContains(profile, 4, now);
    const bool headerSafe = completeIdentity && timeMatches && sessionTimeMatches;
    const bool safeToIntercept = dictionaryValid && headerSafe;
    bool intercepted = false;
    if (!identified) {
      session.next(incoming, now, queuedReplay);
      session.disconnect();
      intercepted = true;
    } else if (!sequenceValid) {
      session.generateLogout("Invalid MsgSeqNum");
      session.disconnect();
      intercepted = true;
    } else if (
        lastProcessedStatus == WireSequenceStatus::Invalid
        || (requiresLastProcessed && lastProcessedStatus != WireSequenceStatus::Valid) || invalidReferencedSequence
        || invalidNextExpectedSequence) {
      session.generateLogout("Invalid inbound sequence field");
      session.disconnect();
      intercepted = true;
    } else if (msgType == MsgType_Logon && (invalidLogonProfileTag != 0 || heartbeatRejectReason != 0)) {
      const auto invalidTag = invalidLogonProfileTag == 0 ? FIELD::HeartBtInt : invalidLogonProfileTag;
      session.generateReject(incoming, invalidLogonProfileTag == 0 ? heartbeatRejectReason : 18, invalidTag);
      session.generateLogout("Invalid Logon profile");
      session.disconnect();
      intercepted = true;
    } else if (
        msgType == MsgType_Logon && !static_cast<bool>(reset) && dictionaryValid && completeIdentity && timeMatches
        && (!sessionTimeMatches || !logonTimeMatches)) {
      intercepted = true;
    } else if (
        safeToIntercept && msgType == MsgType_Logon && !static_cast<bool>(reset)
        && targetSequence == FIX_SEQUENCE_BOUND - 1) {
      session.generateLogout("NextExpectedMsgSeqNum exhausted");
      session.disconnect();
      intercepted = true;
    } else if (!finalizeResetLogon && safeToIntercept && msgType == MsgType_Logon && static_cast<bool>(reset)) {
      application.admin = true;
      intercepted = true;
    } else if (safeToIntercept && msgType == MsgType_Logon && !static_cast<bool>(reset)) {
      intercepted = true;
    } else if (headerSafe && msgType == MsgType_ResendRequest) {
      if (!resendRangeValid) {
        session.generateLogout("Invalid ResendRequest range");
        session.disconnect();
        intercepted = true;
      }
    } else if (!dictionaryValid && msgType == MsgType_Logon) {
      session.next(incoming, now, queuedReplay);
      session.generateLogout("Invalid Logon dictionary");
      session.disconnect();
      intercepted = true;
    } else if (!dictionaryValid && msgType == MsgType_SequenceReset) {
      session.next(incoming, now, queuedReplay);
      session.generateLogout("Invalid GapFill NewSeqNo");
      session.disconnect();
      intercepted = true;
    } else if (safeToIntercept && msgType == MsgType_SequenceReset) {
      GapFillFlag gapFill(false);
      const auto hasGapFill = incoming.getFieldIfSet(gapFill);
      if (!hasGapFill || !static_cast<bool>(gapFill)) {
        session.generateReject(
            incoming,
            hasGapFill ? SessionRejectReason_VALUE_IS_INCORRECT : SessionRejectReason_REQUIRED_TAG_MISSING,
            FIELD::GapFillFlag);
        session.generateLogout("Reset mode is not supported");
        session.disconnect();
        intercepted = true;
      } else {
        std::uint64_t newSequence = 0;
        if (wireSequence(incoming, FIELD::NewSeqNo, false, newSequence) != WireSequenceStatus::Valid) {
          session.generateLogout("Invalid GapFill NewSeqNo");
          session.disconnect();
          intercepted = true;
        } else if (sequence < targetSequence) {
          PossDupFlag possDup(false);
          OrigSendingTime original;
          SendingTime sending;
          const auto validDuplicate = incoming.getHeader().getFieldIfSet(possDup) && static_cast<bool>(possDup)
                                      && incoming.getHeader().getFieldIfSet(original)
                                      && incoming.getHeader().getFieldIfSet(sending) && sending >= original;
          if (!validDuplicate) {
            session.generateLogout("Invalid stale GapFill");
            session.disconnect();
          }
          intercepted = true;
        } else if (sequence == targetSequence && newSequence <= targetSequence) {
          session.generateReject(incoming, SessionRejectReason_VALUE_IS_INCORRECT, FIELD::NewSeqNo);
          intercepted = true;
        }
      }
    }
    if (!intercepted) {
      session.next(incoming, now, queuedReplay);
    }
    if (sequenceValid && sequence < targetSequence && !responder.outputs.empty() && !responder.disconnected) {
      const auto sentLogout = std::any_of(responder.outputs.begin(), responder.outputs.end(), [](const auto &output) {
        Message parsed(output, true);
        MsgType outputMsgType;
        parsed.getHeader().getField(outputMsgType);
        return outputMsgType == MsgType_Logout;
      });
      if (!sentLogout) {
        session.generateLogout("Invalid stale PossDup");
      }
      session.disconnect();
    }
    std::vector<std::string> outputMsgTypes;
    std::vector<std::uint64_t> outputSequences;
    for (const auto &output : responder.outputs) {
      Message parsed(output, true);
      MsgType outputMsgType;
      MsgSeqNum outputSequence;
      parsed.getHeader().getField(outputMsgType);
      parsed.getHeader().getField(outputSequence);
      outputMsgTypes.push_back(outputMsgType);
      outputSequences.push_back(outputSequence);
    }
    const bool requiresRejectLogout
        = !completeIdentity || !timeMatches
          || (msgType == MsgType_Logon
              && (!dictionaryValid || invalidLogonProfileTag != 0 || heartbeatRejectReason != 0));
    if (responder.disconnected && requiresRejectLogout
        && (std::find(outputMsgTypes.begin(), outputMsgTypes.end(), MsgType_Reject) == outputMsgTypes.end()
            || std::find(outputMsgTypes.begin(), outputMsgTypes.end(), MsgType_Logout) == outputMsgTypes.end())) {
      throw std::invalid_argument("Incomplete Reject and Logout terminal output");
    }
    return {
        std::move(responder.outputs),
        std::move(outputMsgTypes),
        std::move(outputSequences),
        msgType,
        businessRejectRefId,
        sequenceValid ? sequence : 0,
        stores.nextSender(),
        stores.nextTarget(),
        static_cast<std::uint32_t>(session.m_state.testRequest()),
        msgType == MsgType_Logon && heartbeatRejectReason == 0
            ? validatedLogonHeartbeat
            : static_cast<std::uint32_t>(session.m_state.heartBtInt()),
        static_cast<std::uint64_t>(bodyRange.first),
        static_cast<std::uint64_t>(bodyRange.second),
        application.application,
        incoming.isAdmin(),
        msgType == MsgType_Logon && reset && application.admin,
        identified,
        sequenceValid,
        sequenceFieldsValid,
        completeIdentity,
        timeMatches,
        sessionTimeMatches,
        logonTimeMatches,
        dictionaryValid,
        responder.disconnected,
        resendBegin,
        resendEndInclusive,
        resendRangeValid,
        nextExpectedSequence,
        nextExpectedStatus == WireSequenceStatus::Valid,
        nextExpectedStatus == WireSequenceStatus::Invalid,
        msgType == MsgType_Logon && invalidLogonProfileTag == 0 && heartbeatRejectReason == 0};
  } catch (const Exception &error) {
    throw std::invalid_argument(error.what());
  }
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::timer(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowTaiNanoseconds,
    std::int64_t nowUtcNanoseconds,
    std::int64_t lastSentTaiNanoseconds,
    std::int64_t lastReceivedTaiNanoseconds,
    std::uint64_t sessionFlags,
    std::uint32_t testRequestCount,
    std::uint32_t logonTimeoutSeconds,
    std::uint32_t logoutTimeoutSeconds,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  const TimeRange nonstop(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
  return timer(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      nowTaiNanoseconds,
      nowUtcNanoseconds,
      lastSentTaiNanoseconds,
      lastReceivedTaiNanoseconds,
      sessionFlags,
      testRequestCount,
      logonTimeoutSeconds,
      logoutTimeoutSeconds,
      nonstop,
      nonstop,
      true,
      lastProcessedSequence,
      dictionaries,
      profile);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::timer(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t creationUtcNanoseconds,
    std::int64_t nowTaiNanoseconds,
    std::int64_t nowUtcNanoseconds,
    std::int64_t lastSentTaiNanoseconds,
    std::int64_t lastReceivedTaiNanoseconds,
    std::uint64_t sessionFlags,
    std::uint32_t testRequestCount,
    std::uint32_t logonTimeoutSeconds,
    std::uint32_t logoutTimeoutSeconds,
    const TimeRange &sessionTime,
    const TimeRange &logonTime,
    bool nonStop,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile) {
  constexpr std::uint64_t FLAGS_MASK = UINT64_C(0x1ff);
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || heartbeatSeconds == 0
      || heartbeatSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || senderSequence == 0
      || senderSequence >= FIX_SEQUENCE_BOUND || targetSequence == 0 || targetSequence >= FIX_SEQUENCE_BOUND
      || lastProcessedSequence >= FIX_SEQUENCE_BOUND || creationUtcNanoseconds <= 0 || nowTaiNanoseconds <= 0
      || nowUtcNanoseconds <= 0 || creationUtcNanoseconds > nowUtcNanoseconds || lastSentTaiNanoseconds <= 0
      || lastReceivedTaiNanoseconds <= 0 || lastSentTaiNanoseconds > nowTaiNanoseconds
      || lastReceivedTaiNanoseconds > nowTaiNanoseconds || (sessionFlags & ~FLAGS_MASK) != 0 || logonTimeoutSeconds == 0
      || logoutTimeoutSeconds == 0 || logonTimeoutSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || logoutTimeoutSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || testRequestCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Timer planner input");
  }
  auto scratchUtc = utcTime(creationUtcNanoseconds);
  const auto nowTai = utcTime(nowTaiNanoseconds);
  const auto nowUtc = utcTime(nowUtcNanoseconds);
  const bool weekly = profile != nullptr && profile->scheduleMode == 2;
  if (weekly && !governedSessionContains(*profile, creationUtcNanoseconds, nowUtcNanoseconds, nowUtc)) {
    return {{}, senderSequence, targetSequence, testRequestCount, false};
  }
  const bool logonTimeMatches = !weekly || governedRangeContains(*profile, 4, nowUtc);
  PlanningApplication application("", profile, lastProcessedSequence);
  PlanningStoreFactory stores;
  DataDictionaryProvider emptyDictionaries;
  const auto &selectedDictionaries = dictionaries == nullptr ? emptyDictionaries : *dictionaries;
  const SessionID sessionId(beginString, senderCompId, targetCompId, profile == nullptr ? "" : profile->qualifier);
  const TimeRange governedNonstop(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
  Session session(
      [&scratchUtc] { return scratchUtc; },
      application,
      stores,
      sessionId,
      selectedDictionaries,
      weekly ? governedNonstop : sessionTime,
      0,
      nullptr,
      true);
  scratchUtc = nowUtc;
  RecordingResponder responder;
  session.setLogonTime(weekly ? governedNonstop : logonTime);
  session.setIsNonStopSession(weekly || nonStop);
  configureStaticProfile(session, profile);
  session.setNextSenderMsgSeqNum(senderSequence);
  session.setNextTargetMsgSeqNum(targetSequence);
  session.setLogonTimeout(static_cast<int>(logonTimeoutSeconds));
  session.setLogoutTimeout(static_cast<int>(logoutTimeoutSeconds));
  session.m_state.heartBtInt(static_cast<int>(heartbeatSeconds));
  session.m_state.enabled((sessionFlags & UINT64_C(1)) != 0 && logonTimeMatches);
  session.m_state.receivedLogon((sessionFlags & UINT64_C(2)) != 0);
  session.m_state.sentLogon((sessionFlags & UINT64_C(4)) != 0);
  session.m_state.sentLogout((sessionFlags & UINT64_C(16)) != 0);
  session.m_state.receivedReset((sessionFlags & UINT64_C(32)) != 0);
  session.m_state.sentReset((sessionFlags & UINT64_C(64)) != 0);
  session.m_state.initiate((sessionFlags & UINT64_C(4)) != 0 && (sessionFlags & UINT64_C(2)) == 0);
  session.m_state.testRequest(static_cast<int>(testRequestCount));
  session.m_state.lastSentTime(utcTime(lastSentTaiNanoseconds));
  session.m_state.lastReceivedTime(utcTime(lastReceivedTaiNanoseconds));
  session.setResponder(&responder);

  session.next(nowTai, nowUtc);
  return {
      std::move(responder.output),
      stores.nextSender(),
      stores.nextTarget(),
      static_cast<std::uint32_t>(session.m_state.testRequest()),
      responder.disconnected};
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::gapFill(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    std::uint64_t beginSequence,
    std::uint64_t endSequenceInclusive,
    std::uint64_t lastProcessedSequence,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile,
    const std::string &originalSendingTime) {
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || heartbeatSeconds == 0
      || heartbeatSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || senderSequence <= 1
      || senderSequence >= FIX_SEQUENCE_BOUND || targetSequence == 0 || targetSequence >= FIX_SEQUENCE_BOUND
      || lastProcessedSequence >= FIX_SEQUENCE_BOUND || nowUtcNanoseconds <= 0 || beginSequence == 0
      || beginSequence >= senderSequence
      || (endSequenceInclusive != 0
          && (endSequenceInclusive < beginSequence || endSequenceInclusive >= senderSequence))) {
    throw std::invalid_argument("GapFill planner input");
  }
  const auto now = utcTime(nowUtcNanoseconds);
  PlanningApplication application("", profile, lastProcessedSequence);
  PlanningStoreFactory stores;
  DataDictionaryProvider emptyDictionaries;
  const auto &selectedDictionaries = dictionaries == nullptr ? emptyDictionaries : *dictionaries;
  const SessionID sessionId(beginString, senderCompId, targetCompId, profile == nullptr ? "" : profile->qualifier);
  const TimeRange nonstop(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
  const auto sessionTime = profile == nullptr || profile->scheduleMode == 1 ? nonstop : governedRange(*profile, 0);
  const auto logonTime = profile == nullptr || profile->scheduleMode == 1 ? nonstop : governedRange(*profile, 4);
  Session session(
      [now] { return now; },
      application,
      stores,
      sessionId,
      selectedDictionaries,
      sessionTime,
      0,
      nullptr,
      true);
  RecordingResponder responder;
  session.setLogonTime(logonTime);
  session.setIsNonStopSession(profile == nullptr || profile->scheduleMode == 1);
  configureStaticProfile(session, profile);
  session.setNextSenderMsgSeqNum(senderSequence);
  session.setNextTargetMsgSeqNum(targetSequence);
  session.m_state.heartBtInt(static_cast<int>(heartbeatSeconds));
  session.m_state.initiate(false);
  session.m_state.receivedLogon(true);
  session.m_state.sentLogon(true);
  session.m_state.lastSentTime(now);
  session.m_state.lastReceivedTime(now);
  session.setResponder(&responder);

  const auto lastStored = endSequenceInclusive == 0 ? senderSequence - 1 : endSequenceInclusive;
  const auto retainedTime = originalSendingTime.empty() ? now : UtcTimeStampConvertor::convert(originalSendingTime);
  for (auto sequence = beginSequence; sequence <= lastStored; ++sequence) {
    Message stored;
    stored.getHeader().setField(BeginString(beginString));
    stored.getHeader().setField(MsgType(MsgType_Heartbeat));
    stored.getHeader().setField(SenderCompID(senderCompId));
    stored.getHeader().setField(TargetCompID(targetCompId));
    stored.getHeader().setField(MsgSeqNum(sequence));
    stored.getHeader().setField(SendingTime(retainedTime, 6));
    session.m_state.set(sequence, stored.toString());
  }
  Message request;
  request.getHeader().setField(BeginString(beginString));
  request.getHeader().setField(MsgType(MsgType_ResendRequest));
  request.getHeader().setField(SenderCompID(targetCompId));
  request.getHeader().setField(TargetCompID(senderCompId));
  request.getHeader().setField(MsgSeqNum(targetSequence));
  request.getHeader().setField(SendingTime(now, 6));
  request.setField(BeginSeqNo(beginSequence));
  request.setField(EndSeqNo(endSequenceInclusive));
  session.next(request.toString(), now);
  if (responder.output.empty() || responder.disconnected) {
    throw std::logic_error("GapFill planner produced no output");
  }
  if (!originalSendingTime.empty()) {
    const auto &sessionDictionary = selectedDictionaries.getSessionDataDictionary(BeginString(beginString));
    const auto &applicationDictionary = selectedDictionaries.getApplicationDataDictionary(ApplVerID("10"));
    Message rendered(responder.output, sessionDictionary, applicationDictionary, true);
    rendered.getHeader().setField(OrigSendingTime(
        UtcTimeStampConvertor::convert(originalSendingTime),
        static_cast<int>(profile == nullptr ? 6 : profile->timestampPrecision)));
    responder.output = rendered.toString();
  }
  return {
      std::move(responder.output),
      stores.nextSender(),
      stores.nextTarget(),
      static_cast<std::uint32_t>(session.m_state.testRequest()),
      responder.disconnected};
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::run(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    Operation operation,
    const std::string &text,
    std::uint64_t lastProcessedSequence,
    std::uint64_t refSequence,
    std::uint32_t refTag,
    std::uint32_t rejectReason,
    const DataDictionaryProvider *dictionaries,
    const InfiniteSessionStaticProfile *profile,
    const std::string &businessRejectRefId,
    const std::string &gatewayInboundDispositionId) {
  const auto printable = [](const std::string &value, std::size_t minimum, std::size_t maximum) {
    return value.size() >= minimum && value.size() <= maximum
           && std::all_of(value.begin(), value.end(), [](unsigned char byte) { return byte >= 0x21 && byte <= 0x7e; });
  };
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || heartbeatSeconds == 0
      || heartbeatSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || senderSequence == 0
      || senderSequence >= FIX_SEQUENCE_BOUND || targetSequence == 0 || targetSequence >= FIX_SEQUENCE_BOUND
      || lastProcessedSequence >= FIX_SEQUENCE_BOUND || nowUtcNanoseconds <= 0 || text.size() > 64
      || (operation == Operation::ResendRequest && targetSequence == FIX_SEQUENCE_BOUND - 1)
      || ((operation == Operation::Reject || operation == Operation::BusinessReject)
          && (text.size() > 8 || refSequence == 0 || refSequence >= INT64_MAX
              || (operation == Operation::Reject
                  && (refTag > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                      || (rejectReason > 18 && rejectReason != 99)))))
      || (operation == Operation::BusinessReject && !printable(gatewayInboundDispositionId, 1, 64))) {
    throw std::invalid_argument("Heartbeat planner input");
  }
  const auto now = utcTime(nowUtcNanoseconds);
  const auto heartbeatAgo
      = utcTime(nowUtcNanoseconds - static_cast<std::int64_t>(heartbeatSeconds) * INT64_C(1000000000));
  const auto twoHeartbeatsAgo
      = utcTime(nowUtcNanoseconds - static_cast<std::int64_t>(heartbeatSeconds) * INT64_C(2000000000));
  PlanningApplication application(
      operation == Operation::Heartbeat ? text : "",
      profile,
      lastProcessedSequence,
      operation == Operation::BusinessReject);
  PlanningStoreFactory stores;
  DataDictionaryProvider emptyDictionaries;
  const auto &selectedDictionaries = dictionaries == nullptr ? emptyDictionaries : *dictionaries;
  const SessionID sessionId(beginString, senderCompId, targetCompId, profile == nullptr ? "" : profile->qualifier);
  const TimeRange nonstop(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
  const auto sessionTime = profile == nullptr || profile->scheduleMode == 1 ? nonstop : governedRange(*profile, 0);
  const auto logonTime = profile == nullptr || profile->scheduleMode == 1 ? nonstop : governedRange(*profile, 4);
  Session session(
      [now] { return now; },
      application,
      stores,
      sessionId,
      selectedDictionaries,
      sessionTime,
      0,
      nullptr,
      true);
  RecordingResponder responder;
  session.setLogonTime(logonTime);
  session.setIsNonStopSession(profile == nullptr || profile->scheduleMode == 1);
  configureStaticProfile(session, profile);
  session.setNextSenderMsgSeqNum(senderSequence);
  session.setNextTargetMsgSeqNum(targetSequence);
  session.m_state.heartBtInt(static_cast<int>(heartbeatSeconds));
  session.m_state.initiate(false);
  const bool inboundLogon = operation == Operation::Logon;
  session.m_state.receivedLogon(!inboundLogon);
  session.m_state.sentLogon(!inboundLogon);
  session.m_state.lastSentTime(operation == Operation::Heartbeat ? heartbeatAgo : now);
  session.m_state.lastReceivedTime(operation == Operation::TestRequest ? twoHeartbeatsAgo : now);
  session.setResponder(&responder);

  if (operation == Operation::Logout) {
    session.logout(text);
    session.next(now);
  } else if (operation == Operation::ResendRequest) {
    session.generateResendRequest(BeginString(beginString), MsgSeqNum(targetSequence + 1));
  } else if (inboundLogon) {
    Message incoming;
    incoming.getHeader().setField(BeginString(beginString));
    incoming.getHeader().setField(MsgType(MsgType_Logon));
    incoming.getHeader().setField(SenderCompID(targetCompId));
    incoming.getHeader().setField(TargetCompID(senderCompId));
    incoming.getHeader().setField(MsgSeqNum(targetSequence));
    incoming.getHeader().setField(SendingTime(now, 6));
    incoming.setField(EncryptMethod(0));
    incoming.setField(HeartBtInt(static_cast<int>(heartbeatSeconds)));
    incoming.setField(DefaultApplVerID("10"));
    if (profile != nullptr) {
      incoming.setField(DefaultApplExtID(299));
      incoming.setField(DefaultCstmApplVerID(profile->defaultCustomApplicationVersion));
    }
    session.next(incoming.toString(), now);
  } else if (operation == Operation::Reject) {
    Message incoming;
    incoming.getHeader().setField(BeginString(beginString));
    incoming.getHeader().setField(MsgType(text));
    incoming.getHeader().setField(SenderCompID(targetCompId));
    incoming.getHeader().setField(TargetCompID(senderCompId));
    incoming.getHeader().setField(MsgSeqNum(refSequence));
    incoming.getHeader().setField(SendingTime(now, 6));
    session.generateReject(incoming, static_cast<int>(rejectReason), static_cast<int>(refTag));
  } else if (operation == Operation::BusinessReject) {
    Message reject = session.newMessage(MsgType(MsgType_BusinessMessageReject));
    reject.setField(RefMsgType(text));
    if (!businessRejectRefId.empty()) {
      reject.setField(BusinessRejectRefID(businessRejectRefId));
    }
    reject.setField(BusinessRejectReason(BusinessRejectReason_UNSUPPORTED_MESSAGE_TYPE));
    reject.setField(Text("Application message is unsupported."));
    reject.setField(StringField(20003, gatewayInboundDispositionId));
    reject.setField(StringField(20004, "INF-1002"));
    session.m_state.incrNextTargetMsgSeqNum();
    session.send(reject);
  } else {
    session.next(now);
  }
  if (responder.output.empty() || responder.disconnected) {
    throw std::logic_error(
        responder.disconnected ? "Heartbeat planner disconnected" : "Heartbeat planner produced no output");
  }
  return {
      std::move(responder.output),
      stores.nextSender(),
      stores.nextTarget(),
      static_cast<std::uint32_t>(session.m_state.testRequest()),
      responder.disconnected};
}
} // namespace FIX
