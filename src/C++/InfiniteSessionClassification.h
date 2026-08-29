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

namespace FIX {
class TimeRange;

struct InfiniteHeartbeatPlan {
  std::string output;
  std::uint64_t nextSenderSequence{0};
  std::uint64_t nextTargetSequence{0};
  std::uint32_t testRequestCount{0};
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
    ResetLogon
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
      const std::string &text);
};
} // namespace FIX
