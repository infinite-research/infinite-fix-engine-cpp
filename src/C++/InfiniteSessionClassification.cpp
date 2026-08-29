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
#include "MessageStore.h"
#include "Responder.h"
#include "Session.h"
#include "TimeRange.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace FIX {
namespace {
class PlanningApplication : public Application {
public:
  explicit PlanningApplication(std::string testRequestId)
      : m_testRequestId(std::move(testRequestId)) {}

  void onCreate(const SessionID &) override { throw std::logic_error("Detached Session registered"); }
  void onLogon(const SessionID &) override {}
  void onLogout(const SessionID &) override {}
  void toAdmin(Message &message, const SessionID &) override {
    if (!m_testRequestId.empty()) {
      message.setField(TestReqID(m_testRequestId));
    }
  }
  void toApp(Message &, const SessionID &) override { throw std::logic_error("Unexpected application output"); }
  void fromAdmin(const Message &, const SessionID &) override {}
  void fromApp(const Message &, const SessionID &) override { throw std::logic_error("Unexpected application input"); }

private:
  std::string m_testRequestId;
};

class RecordingResponder : public Responder {
public:
  bool send(const std::string &wire) override {
    if (!output.empty()) {
      throw std::logic_error("Multiple planner outputs");
    }
    output = wire;
    return true;
  }

  void disconnect() override { disconnected = true; }

  std::string output;
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
} // namespace

InfiniteHeartbeatPlan InfiniteSessionPlanner::heartbeat(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    const std::string &testRequestId) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::Heartbeat,
      testRequestId);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::testRequest(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::TestRequest,
      "");
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::logout(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    const std::string &reason) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::Logout,
      reason);
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::resendRequest(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      Operation::ResendRequest,
      "");
}

InfiniteHeartbeatPlan InfiniteSessionPlanner::logon(
    const std::string &beginString,
    const std::string &senderCompId,
    const std::string &targetCompId,
    std::uint32_t heartbeatSeconds,
    std::uint64_t senderSequence,
    std::uint64_t targetSequence,
    std::int64_t nowUtcNanoseconds,
    bool resetSequenceNumbers) {
  return run(
      beginString,
      senderCompId,
      targetCompId,
      heartbeatSeconds,
      senderSequence,
      targetSequence,
      nowUtcNanoseconds,
      resetSequenceNumbers ? Operation::ResetLogon : Operation::Logon,
      "");
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
    std::uint32_t logoutTimeoutSeconds) {
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
      true);
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
    bool nonStop) {
  constexpr std::uint64_t FLAGS_MASK = UINT64_C(0x1ff);
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || heartbeatSeconds == 0
      || heartbeatSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || senderSequence == 0
      || targetSequence == 0 || creationUtcNanoseconds <= 0 || nowTaiNanoseconds <= 0 || nowUtcNanoseconds <= 0
      || creationUtcNanoseconds > nowUtcNanoseconds || lastSentTaiNanoseconds <= 0 || lastReceivedTaiNanoseconds <= 0
      || lastSentTaiNanoseconds > nowTaiNanoseconds || lastReceivedTaiNanoseconds > nowTaiNanoseconds
      || (sessionFlags & ~FLAGS_MASK) != 0 || logonTimeoutSeconds == 0 || logoutTimeoutSeconds == 0
      || logonTimeoutSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || logoutTimeoutSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Timer planner input");
  }
  auto scratchUtc = utcTime(creationUtcNanoseconds);
  const auto nowTai = utcTime(nowTaiNanoseconds);
  const auto nowUtc = utcTime(nowUtcNanoseconds);
  PlanningApplication application("");
  MemoryStoreFactory stores;
  DataDictionaryProvider dictionaries;
  const SessionID sessionId(beginString, senderCompId, targetCompId);
  Session session(
      [&scratchUtc] { return scratchUtc; },
      application,
      stores,
      sessionId,
      dictionaries,
      sessionTime,
      0,
      nullptr,
      true);
  scratchUtc = nowUtc;
  RecordingResponder responder;
  session.setLogonTime(logonTime);
  session.setIsNonStopSession(nonStop);
  session.setTimestampPrecision(6);
  session.setNextSenderMsgSeqNum(senderSequence);
  session.setNextTargetMsgSeqNum(targetSequence);
  session.setLogonTimeout(static_cast<int>(logonTimeoutSeconds));
  session.setLogoutTimeout(static_cast<int>(logoutTimeoutSeconds));
  session.m_state.heartBtInt(static_cast<int>(heartbeatSeconds));
  session.m_state.enabled((sessionFlags & UINT64_C(1)) != 0);
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
      session.getExpectedSenderNum(),
      session.getExpectedTargetNum(),
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
    std::uint64_t endSequenceInclusive) {
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || heartbeatSeconds == 0
      || heartbeatSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || senderSequence <= 1
      || targetSequence == 0 || nowUtcNanoseconds <= 0 || beginSequence == 0 || beginSequence >= senderSequence
      || (endSequenceInclusive != 0
          && (endSequenceInclusive < beginSequence || endSequenceInclusive >= senderSequence))) {
    throw std::invalid_argument("GapFill planner input");
  }
  const auto now = utcTime(nowUtcNanoseconds);
  PlanningApplication application("");
  MemoryStoreFactory stores;
  DataDictionaryProvider dictionaries;
  const SessionID sessionId(beginString, senderCompId, targetCompId);
  const TimeRange nonstop(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
  Session session([now] { return now; }, application, stores, sessionId, dictionaries, nonstop, 0, nullptr, true);
  RecordingResponder responder;
  session.setIsNonStopSession(true);
  session.setTimestampPrecision(6);
  session.setSenderDefaultApplVerID("10");
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
  for (auto sequence = beginSequence; sequence <= lastStored; ++sequence) {
    Message stored;
    stored.getHeader().setField(BeginString(beginString));
    stored.getHeader().setField(MsgType(MsgType_Heartbeat));
    stored.getHeader().setField(SenderCompID(senderCompId));
    stored.getHeader().setField(TargetCompID(targetCompId));
    stored.getHeader().setField(MsgSeqNum(sequence));
    stored.getHeader().setField(SendingTime(now, 6));
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
  session.next(request, now);
  if (responder.output.empty() || responder.disconnected) {
    throw std::logic_error("GapFill planner produced no output");
  }
  return {
      std::move(responder.output),
      session.getExpectedSenderNum(),
      session.getExpectedTargetNum(),
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
    const std::string &text) {
  if (beginString.empty() || senderCompId.empty() || targetCompId.empty() || heartbeatSeconds == 0
      || heartbeatSeconds > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || senderSequence == 0
      || targetSequence == 0 || nowUtcNanoseconds <= 0 || text.size() > 64) {
    throw std::invalid_argument("Heartbeat planner input");
  }
  const auto now = utcTime(nowUtcNanoseconds);
  const auto heartbeatAgo
      = utcTime(nowUtcNanoseconds - static_cast<std::int64_t>(heartbeatSeconds) * INT64_C(1000000000));
  const auto twoHeartbeatsAgo
      = utcTime(nowUtcNanoseconds - static_cast<std::int64_t>(heartbeatSeconds) * INT64_C(2000000000));
  PlanningApplication application(operation == Operation::Heartbeat ? text : "");
  MemoryStoreFactory stores;
  DataDictionaryProvider dictionaries;
  const SessionID sessionId(beginString, senderCompId, targetCompId);
  const TimeRange nonstop(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0));
  Session session([now] { return now; }, application, stores, sessionId, dictionaries, nonstop, 0, nullptr, true);
  RecordingResponder responder;
  session.setIsNonStopSession(true);
  session.setTimestampPrecision(6);
  session.setSenderDefaultApplVerID("10");
  session.setNextSenderMsgSeqNum(senderSequence);
  session.setNextTargetMsgSeqNum(targetSequence);
  session.m_state.heartBtInt(static_cast<int>(heartbeatSeconds));
  session.m_state.initiate(false);
  const bool inboundLogon = operation == Operation::Logon || operation == Operation::ResetLogon;
  session.m_state.receivedLogon(!inboundLogon);
  session.m_state.sentLogon(!inboundLogon);
  session.m_state.lastSentTime(operation == Operation::Heartbeat ? heartbeatAgo : now);
  session.m_state.lastReceivedTime(operation == Operation::TestRequest ? twoHeartbeatsAgo : now);
  session.setResponder(&responder);

  if (operation == Operation::Logout) {
    session.logout(text);
    session.next(now);
  } else if (operation == Operation::ResendRequest) {
    Message incoming;
    incoming.getHeader().setField(BeginString(beginString));
    incoming.getHeader().setField(MsgType(MsgType_Heartbeat));
    incoming.getHeader().setField(SenderCompID(targetCompId));
    incoming.getHeader().setField(TargetCompID(senderCompId));
    incoming.getHeader().setField(MsgSeqNum(targetSequence + 2));
    incoming.getHeader().setField(SendingTime(now, 6));
    session.next(incoming, now);
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
    if (operation == Operation::ResetLogon) {
      incoming.setField(ResetSeqNumFlag(true));
    }
    session.next(incoming, now);
  } else {
    session.next(now);
  }
  if (responder.output.empty() || responder.disconnected) {
    throw std::logic_error("Heartbeat planner produced no output");
  }
  return {
      std::move(responder.output),
      session.getExpectedSenderNum(),
      session.getExpectedTargetNum(),
      static_cast<std::uint32_t>(session.m_state.testRequest()),
      responder.disconnected};
}
} // namespace FIX
