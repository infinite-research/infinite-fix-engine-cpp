/* -*- C++ -*- */

#ifndef FIX_INFINITESESSIONCLASSIFICATION_H
#define FIX_INFINITESESSIONCLASSIFICATION_H

#include "Fields.h"
#include "Message.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
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
  Logon = 3,
  Logout = 4,
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
  UtcTimeStamp lastSentTime;
  UtcTimeStamp lastReceivedTime;
  UtcTimeStamp storeCreationTime;
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

struct InfiniteActionData {
  std::string exactBytes;
  std::string messageType;
  UtcTimeStamp now;
  InfiniteSequenceDisposition sequenceDisposition;
  std::string failure;
  InfiniteExpectedSessionState resultingState;
  std::vector<std::pair<SEQNUM, std::string>> sourceMessages;
  std::vector<InfiniteCallbackKind> callbacks;
  std::vector<InfinitePlannedEffect> effects;

  bool operator==(const InfiniteActionData &rhs) const;
};

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
      InfiniteSessionActionKind action,
      InfiniteActionData actionData)
      : m_binding(std::move(binding)),
        m_expected(std::move(expected)),
        m_action(action),
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
      InfiniteSessionActionKind action,
      InfiniteActionData actionData,
      Message message)
      : m_binding(std::move(binding)),
        m_expected(std::move(expected)),
        m_action(action),
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
