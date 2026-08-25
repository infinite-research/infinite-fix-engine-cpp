/* -*- C++ -*- */

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

#ifndef FIX_INFINITESESSIONCLASSIFICATION_H
#define FIX_INFINITESESSIONCLASSIFICATION_H

#include "Fields.h"
#include "Message.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace FIX {
class InfiniteSessionClassificationTestAccess;
class Session;

enum class InfiniteSessionActionKind : std::uint8_t {
  ProtocolControl = 1,
  SequenceReset = 2,
  Logout = 3,
  ResendOrQueuedRelease = 4,
  ProtocolDisposition = 5,
  Application = 6,
  Failure = 7,
};

enum class InfiniteSequenceDisposition : std::uint8_t {
  AtHead = 1,
  TooHigh = 2,
  TooLow = 3,
  Unavailable = 4,
};

enum class InfiniteCallbackKind : std::uint8_t {
  FromAdmin = 1,
  FromApplication = 2,
  ToAdmin = 3,
  ToApplication = 4,
  Logon = 5,
  Logout = 6,
};

struct InfinitePlannedCallback {
  InfiniteCallbackKind kind;
  std::string bytes;

  bool operator==(const InfinitePlannedCallback &rhs) const;
};

enum class InfiniteEffectKind : std::uint8_t {
  StoreMessage = 1,
  SetSenderSequence = 2,
  SetTargetSequence = 3,
  ResetStore = 4,
  LogIncoming = 5,
  LogOutgoing = 6,
  LogEvent = 7,
  Send = 8,
  Disconnect = 9,
};

struct InfinitePlannedEffect {
  InfiniteEffectKind kind;
  SEQNUM sequence;
  std::string bytes;
  UtcTimeStamp timestamp;

  bool operator==(const InfinitePlannedEffect &rhs) const;
};

struct InfiniteSessionStateFingerprint {
  bool enabled;
  bool receivedLogon;
  bool sentLogout;
  bool sentLogon;
  bool sentReset;
  bool receivedReset;
  bool initiate;
  int logonTimeout;
  int logoutTimeout;
  int testRequest;
  SEQNUM resendBegin;
  SEQNUM resendEnd;
  int heartBtInt;
  UtcTimeStamp lastSentTime{UtcTimeStamp::now()};
  UtcTimeStamp lastReceivedTime{UtcTimeStamp::now()};
  UtcTimeStamp storeCreationTime{UtcTimeStamp::now()};
  std::string logoutReason;
  std::vector<std::pair<SEQNUM, std::string>> queuedMessages;
  std::string senderDefaultApplVerID;
  std::string targetDefaultApplVerID;
  bool sendRedundantResendRequests;
  bool checkCompId;
  bool checkLatency;
  int maxLatency;
  bool resetOnLogon;
  bool resetOnLogout;
  bool resetOnDisconnect;
  bool refreshOnLogon;
  int timestampPrecision;
  bool persistMessages;
  bool validateLengthAndChecksum;
  bool sendNextExpectedMsgSeqNum;
  bool nonStopSession;
  std::uintptr_t responderIdentity;

  bool operator==(const InfiniteSessionStateFingerprint &rhs) const;
};

struct InfiniteExpectedSessionState {
  std::uint64_t revision;
  SEQNUM senderSequence;
  SEQNUM targetSequence;
  bool loggedOn;
  InfiniteSessionStateFingerprint mutableState;

  bool operator==(const InfiniteExpectedSessionState &rhs) const;
};

struct InfiniteActionPlan {
  std::string exactBytes;
  std::string messageType;
  UtcTimeStamp now;
  InfiniteSequenceDisposition sequenceDisposition;
  std::string failure;
  InfiniteExpectedSessionState resultingState;
  std::vector<std::pair<SEQNUM, std::string>> sourceMessages;
  std::vector<InfinitePlannedCallback> callbacks;
  std::vector<InfinitePlannedEffect> effects;

  bool operator==(const InfiniteActionPlan &rhs) const;
};

struct InfiniteProtocolControlData {
  InfiniteActionPlan plan;
  bool operator==(const InfiniteProtocolControlData &rhs) const { return plan == rhs.plan; }
};
struct InfiniteSequenceResetData {
  InfiniteActionPlan plan;
  bool operator==(const InfiniteSequenceResetData &rhs) const { return plan == rhs.plan; }
};
struct InfiniteLogoutData {
  InfiniteActionPlan plan;
  bool operator==(const InfiniteLogoutData &rhs) const { return plan == rhs.plan; }
};
struct InfiniteResendOrQueuedReleaseData {
  InfiniteActionPlan plan;
  bool operator==(const InfiniteResendOrQueuedReleaseData &rhs) const { return plan == rhs.plan; }
};
struct InfiniteProtocolDispositionData {
  InfiniteActionPlan plan;
  bool operator==(const InfiniteProtocolDispositionData &rhs) const { return plan == rhs.plan; }
};
struct InfiniteApplicationData {
  InfiniteActionPlan plan;
  bool operator==(const InfiniteApplicationData &rhs) const { return plan == rhs.plan; }
};
struct InfiniteFailureData {
  InfiniteActionPlan plan;
  bool operator==(const InfiniteFailureData &rhs) const { return plan == rhs.plan; }
};

using InfiniteActionData = std::variant<
    InfiniteProtocolControlData,
    InfiniteSequenceResetData,
    InfiniteLogoutData,
    InfiniteResendOrQueuedReleaseData,
    InfiniteProtocolDispositionData,
    InfiniteApplicationData,
    InfiniteFailureData>;

InfiniteSessionActionKind infiniteActionKind(const InfiniteActionData &actionData);
InfiniteActionPlan &infiniteActionPlan(InfiniteActionData &actionData);
const InfiniteActionPlan &infiniteActionPlan(const InfiniteActionData &actionData);

class InfiniteAtHeadBinding {
public:
  const std::array<std::uint8_t, 32> &value() const { return m_value; }

private:
  friend class InfiniteSessionClassificationTestAccess;
  explicit InfiniteAtHeadBinding(std::array<std::uint8_t, 32> value)
      : m_value(std::move(value)) {}

  std::array<std::uint8_t, 32> m_value;
};

class InfiniteEffectAuthorization {
public:
  InfiniteEffectAuthorization(const InfiniteEffectAuthorization &) = delete;
  InfiniteEffectAuthorization &operator=(const InfiniteEffectAuthorization &) = delete;
  InfiniteEffectAuthorization(InfiniteEffectAuthorization &&) = default;
  InfiniteEffectAuthorization &operator=(InfiniteEffectAuthorization &&) = default;

private:
  friend class Session;
  friend class InfiniteSessionClassificationTestAccess;
  InfiniteEffectAuthorization(
      std::array<std::uint8_t, 32> binding,
      InfiniteExpectedSessionState expected,
      InfiniteActionData actionData)
      : m_binding(std::move(binding)),
        m_expected(std::move(expected)),
        m_action(infiniteActionKind(actionData)),
        m_actionData(std::move(actionData)) {}

  std::array<std::uint8_t, 32> m_binding;
  InfiniteExpectedSessionState m_expected;
  InfiniteSessionActionKind m_action;
  InfiniteActionData m_actionData;
  bool m_consumed{false};
};

class InfiniteSessionClassification {
public:
  InfiniteSessionActionKind kind() const { return m_action; }
  const InfiniteExpectedSessionState &expected() const { return m_expected; }
  const InfiniteActionData &actionData() const { return m_actionData; }
  const Message &message() const { return m_message; }

private:
  friend class Session;
  friend class InfiniteSessionClassificationTestAccess;
  InfiniteSessionClassification(
      std::array<std::uint8_t, 32> binding,
      InfiniteExpectedSessionState expected,
      InfiniteActionData actionData,
      Message message)
      : m_binding(std::move(binding)),
        m_expected(std::move(expected)),
        m_action(infiniteActionKind(actionData)),
        m_actionData(std::move(actionData)),
        m_message(std::move(message)) {}

  std::array<std::uint8_t, 32> m_binding;
  InfiniteExpectedSessionState m_expected;
  InfiniteSessionActionKind m_action;
  InfiniteActionData m_actionData;
  Message m_message;
};
} // namespace FIX

#endif // FIX_INFINITESESSIONCLASSIFICATION_H
