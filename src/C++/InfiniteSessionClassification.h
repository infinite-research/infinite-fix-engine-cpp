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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace FIX {
class TimeRange;

struct InfiniteHeartbeatPlan {
  std::string output;
  std::uint64_t nextSenderSequence{0};
  std::uint64_t nextTargetSequence{0};
  std::uint32_t testRequestCount{0};
  bool disconnected{false};
};

struct InfiniteInboundPlan {
  std::vector<std::string> outputs;
  std::vector<std::string> outputMsgTypes;
  std::vector<std::uint64_t> outputSequences;
  std::string msgType;
  std::uint64_t sequence{0};
  std::uint64_t nextSenderSequence{0};
  std::uint64_t nextTargetSequence{0};
  std::uint32_t testRequestCount{0};
  std::uint32_t heartbeatSeconds{0};
  std::uint64_t bodyOffset{0};
  std::uint64_t bodyLength{0};
  bool application{false};
  bool admin{false};
  bool resetLogon{false};
  bool identified{false};
  bool identityMatches{false};
  bool timeMatches{false};
  bool disconnected{false};
};

/// Drives an isolated ordinary QuickFIX Session to classify and render native session work.
class InfiniteSessionPlanner {
public:
  static InfiniteHeartbeatPlan heartbeat(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds,
      const std::string &testRequestId);

  static InfiniteHeartbeatPlan testRequest(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds);

  static InfiniteHeartbeatPlan logout(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds,
      const std::string &reason);

  static InfiniteHeartbeatPlan resendRequest(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds);

  static InfiniteHeartbeatPlan logon(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds,
      bool resetSequenceNumbers);

  static InfiniteHeartbeatPlan reject(
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
      const std::string &refMsgType);

  static InfiniteHeartbeatPlan businessReject(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds,
      std::uint64_t refSequence,
      const std::string &refMsgType);

  static InfiniteInboundPlan inbound(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds,
      std::int64_t lastSentUtcNanoseconds,
      std::int64_t lastReceivedUtcNanoseconds,
      std::uint64_t sessionFlags,
      std::uint32_t testRequestCount,
      const std::string &wire);

  static InfiniteHeartbeatPlan timer(
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
      std::uint32_t logoutTimeoutSeconds);

  static InfiniteHeartbeatPlan timer(
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
      bool nonStop);

  static InfiniteHeartbeatPlan gapFill(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds,
      std::uint64_t beginSequence,
      std::uint64_t endSequenceInclusive);

private:
  enum class Operation {
    Heartbeat,
    TestRequest,
    Logout,
    ResendRequest,
    Logon,
    ResetLogon,
    Reject,
    BusinessReject
  };

  static InfiniteHeartbeatPlan run(
      const std::string &beginString,
      const std::string &senderCompId,
      const std::string &targetCompId,
      std::uint32_t heartbeatSeconds,
      std::uint64_t senderSequence,
      std::uint64_t targetSequence,
      std::int64_t nowUtcNanoseconds,
      Operation operation,
      const std::string &text,
      std::uint64_t refSequence = 0,
      std::uint32_t refTag = 0,
      std::uint32_t rejectReason = 0);
};
} // namespace FIX
