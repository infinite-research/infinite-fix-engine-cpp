/* -*- C++ -*- */

#ifdef _MSC_VER
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "InfiniteSessionClassification.h"
#include "Session.h"
#include "Values.h"

#include <limits>
#include <map>

namespace FIX {
namespace {
constexpr std::size_t kMaxInfinitePlannedMessages = 256;
constexpr std::size_t kMaxInfinitePlannedBytes = 16 * 1024 * 1024;

void appendEffect(
    std::vector<InfinitePlannedEffect> &effects,
    InfiniteEffectKind kind,
    SEQNUM sequence,
    const std::string &bytes,
    const UtcTimeStamp &now) {
  effects.push_back(InfinitePlannedEffect{kind, sequence, bytes, now});
}

class RecordingMessageStore : public MessageStore {
public:
  RecordingMessageStore(
      const InfiniteExpectedSessionState &initial,
      const std::vector<std::pair<SEQNUM, std::string>> &sourceMessages,
      std::vector<InfinitePlannedEffect> &effects,
      const UtcTimeStamp &now)
      : m_senderSequence(initial.senderSequence),
        m_targetSequence(initial.targetSequence),
        m_creationTime(initial.mutableState.storeCreationTime),
        m_effects(effects),
        m_now(now) {
    for (const auto &message : sourceMessages) {
      m_messages.emplace(message.first, message.second);
    }
  }

  bool set(SEQNUM sequence, const std::string &message) override {
    m_messages[sequence] = message;
    appendEffect(m_effects, InfiniteEffectKind::StoreMessage, sequence, message, m_now);
    return true;
  }

  void get(SEQNUM begin, SEQNUM end, std::vector<std::string> &messages) const override {
    for (auto entry = m_messages.lower_bound(begin); entry != m_messages.end() && entry->first <= end; ++entry) {
      messages.push_back(entry->second);
    }
  }

  SEQNUM getNextSenderMsgSeqNum() const override { return m_senderSequence; }
  SEQNUM getNextTargetMsgSeqNum() const override { return m_targetSequence; }

  void setNextSenderMsgSeqNum(SEQNUM sequence) override {
    m_senderSequence = sequence;
    appendEffect(m_effects, InfiniteEffectKind::SetSenderSequence, sequence, "", m_now);
  }

  void setNextTargetMsgSeqNum(SEQNUM sequence) override {
    m_targetSequence = sequence;
    appendEffect(m_effects, InfiniteEffectKind::SetTargetSequence, sequence, "", m_now);
  }

  void incrNextSenderMsgSeqNum() override { setNextSenderMsgSeqNum(m_senderSequence + 1); }
  void incrNextTargetMsgSeqNum() override { setNextTargetMsgSeqNum(m_targetSequence + 1); }
  UtcTimeStamp getCreationTime() const override { return m_creationTime; }

  void reset(const UtcTimeStamp &now) override {
    m_messages.clear();
    m_senderSequence = 1;
    m_targetSequence = 1;
    m_creationTime = now;
    appendEffect(m_effects, InfiniteEffectKind::ResetStore, 0, "", now);
  }

  void refresh() override {}

private:
  std::map<SEQNUM, std::string> m_messages;
  SEQNUM m_senderSequence;
  SEQNUM m_targetSequence;
  UtcTimeStamp m_creationTime;
  std::vector<InfinitePlannedEffect> &m_effects;
  UtcTimeStamp m_now;
};

class RecordingLog : public Log {
public:
  RecordingLog(std::vector<InfinitePlannedEffect> &effects, const UtcTimeStamp &now)
      : m_effects(effects),
        m_now(now) {}

  void clear() override {}
  void backup() override {}
  void onIncoming(const std::string &bytes) override {
    appendEffect(m_effects, InfiniteEffectKind::LogIncoming, 0, bytes, m_now);
  }
  void onOutgoing(const std::string &bytes) override {
    appendEffect(m_effects, InfiniteEffectKind::LogOutgoing, 0, bytes, m_now);
  }
  void onEvent(const std::string &event) override {
    appendEffect(m_effects, InfiniteEffectKind::LogEvent, 0, event, m_now);
  }

private:
  std::vector<InfinitePlannedEffect> &m_effects;
  UtcTimeStamp m_now;
};

class RecordingResponder : public Responder {
public:
  RecordingResponder(std::vector<InfinitePlannedEffect> &effects, const UtcTimeStamp &now)
      : m_effects(effects),
        m_now(now) {}

  bool send(const std::string &bytes) override {
    appendEffect(m_effects, InfiniteEffectKind::Send, 0, bytes, m_now);
    return true;
  }
  void disconnect() override { appendEffect(m_effects, InfiniteEffectKind::Disconnect, 0, "", m_now); }

private:
  std::vector<InfinitePlannedEffect> &m_effects;
  UtcTimeStamp m_now;
};
} // namespace

bool InfinitePlannedEffect::operator==(const InfinitePlannedEffect &rhs) const {
  return kind == rhs.kind && sequence == rhs.sequence && bytes == rhs.bytes && timestamp == rhs.timestamp;
}

bool InfiniteSessionStateFingerprint::operator==(const InfiniteSessionStateFingerprint &rhs) const {
  return enabled == rhs.enabled && receivedLogon == rhs.receivedLogon && sentLogout == rhs.sentLogout
         && sentLogon == rhs.sentLogon && sentReset == rhs.sentReset && receivedReset == rhs.receivedReset
         && initiate == rhs.initiate && logonTimeout == rhs.logonTimeout && logoutTimeout == rhs.logoutTimeout
         && testRequest == rhs.testRequest && resendBegin == rhs.resendBegin && resendEnd == rhs.resendEnd
         && heartBtInt == rhs.heartBtInt && lastSentTime == rhs.lastSentTime && lastReceivedTime == rhs.lastReceivedTime
         && storeCreationTime == rhs.storeCreationTime && logoutReason == rhs.logoutReason
         && queuedMessages == rhs.queuedMessages && senderDefaultApplVerID == rhs.senderDefaultApplVerID
         && targetDefaultApplVerID == rhs.targetDefaultApplVerID
         && sendRedundantResendRequests == rhs.sendRedundantResendRequests && checkCompId == rhs.checkCompId
         && checkLatency == rhs.checkLatency && maxLatency == rhs.maxLatency && resetOnLogon == rhs.resetOnLogon
         && resetOnLogout == rhs.resetOnLogout && resetOnDisconnect == rhs.resetOnDisconnect
         && refreshOnLogon == rhs.refreshOnLogon && timestampPrecision == rhs.timestampPrecision
         && persistMessages == rhs.persistMessages && validateLengthAndChecksum == rhs.validateLengthAndChecksum
         && sendNextExpectedMsgSeqNum == rhs.sendNextExpectedMsgSeqNum && nonStopSession == rhs.nonStopSession
         && responderIdentity == rhs.responderIdentity;
}

bool InfiniteExpectedSessionState::operator==(const InfiniteExpectedSessionState &rhs) const {
  return revision == rhs.revision && senderSequence == rhs.senderSequence && targetSequence == rhs.targetSequence
         && loggedOn == rhs.loggedOn && mutableState == rhs.mutableState;
}

bool InfiniteActionData::operator==(const InfiniteActionData &rhs) const {
  return exactBytes == rhs.exactBytes && messageType == rhs.messageType && now == rhs.now
         && sequenceDisposition == rhs.sequenceDisposition && failure == rhs.failure
         && resultingState == rhs.resultingState && sourceMessages == rhs.sourceMessages && callbacks == rhs.callbacks
         && effects == rhs.effects;
}

InfiniteExpectedSessionState Session::currentInfiniteExpectedState() const {
  Locker lock(m_state.m_mutex);
  const auto senderSequence = m_state.m_pStore->getNextSenderMsgSeqNum();
  const auto targetSequence = m_state.m_pStore->getNextTargetMsgSeqNum();

  std::vector<std::pair<SEQNUM, std::string>> queue;
  if (m_state.m_queue.size() > kMaxInfinitePlannedMessages) {
    throw std::length_error("Infinite queued message count exceeds bound");
  }
  queue.reserve(m_state.m_queue.size());
  std::size_t queuedBytes = 0;
  for (const auto &entry : m_state.m_queue) {
    auto bytes = entry.second.toString();
    if (bytes.size() > kMaxInfinitePlannedBytes - queuedBytes) {
      throw std::length_error("Infinite queued message bytes exceed bound");
    }
    queuedBytes += bytes.size();
    queue.emplace_back(entry.first, std::move(bytes));
  }

  const auto resend = m_state.m_resendRange;
  InfiniteSessionStateFingerprint fingerprint{
      m_state.m_enabled,
      m_state.m_receivedLogon,
      m_state.m_sentLogout,
      m_state.m_sentLogon,
      m_state.m_sentReset,
      m_state.m_receivedReset,
      m_state.m_initiate,
      m_state.m_logonTimeout,
      m_state.m_logoutTimeout,
      m_state.m_testRequest,
      resend.first,
      resend.second,
      m_state.m_heartBtInt,
      m_state.m_lastSentTime,
      m_state.m_lastReceivedTime,
      m_state.m_pStore->getCreationTime(),
      m_state.m_logoutReason,
      std::move(queue),
      m_senderDefaultApplVerID,
      m_targetDefaultApplVerID,
      m_sendRedundantResendRequests,
      m_checkCompId,
      m_checkLatency,
      m_maxLatency,
      m_resetOnLogon,
      m_resetOnLogout,
      m_resetOnDisconnect,
      m_refreshOnLogon,
      m_timestampPrecision,
      m_persistMessages,
      m_validateLengthAndChecksum,
      m_sendNextExpectedMsgSeqNum,
      m_isNonStopSession,
      reinterpret_cast<std::uintptr_t>(m_pResponder)};

  return InfiniteExpectedSessionState{
      m_infiniteSessionRevision,
      senderSequence,
      targetSequence,
      m_state.m_receivedLogon && m_state.m_sentLogon,
      std::move(fingerprint)};
}

InfiniteSessionClassification Session::classifyInfiniteFrame(
    const InfiniteAtHeadBinding &atHead,
    const std::string &bytes,
    const UtcTimeStamp &now) const {
  const auto expected = currentInfiniteExpectedState();
  InfiniteActionData actionData{bytes, "", now, InfiniteSequenceDisposition::Unavailable, "", expected, {}, {}, {}};
  Message message;
  InfiniteSessionActionKind action = InfiniteSessionActionKind::Failure;

  try {
    if (bytes.size() > 65536) {
      throw std::length_error("Infinite FIX frame exceeds bound");
    }
    if (expected.revision == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error("Infinite session-state revision exhausted");
    }
    if (m_refreshOnLogon) {
      throw std::logic_error("Infinite classification does not permit refresh-on-logon");
    }

    const DataDictionary &sessionDictionary
        = m_dataDictionaryProvider.getSessionDataDictionary(m_sessionID.getBeginString());
    if (m_sessionID.isFIXT()) {
      Message headerOnly;
      headerOnly.setStringHeader(bytes);
      ApplVerID applVerID = m_targetDefaultApplVerID;
      headerOnly.getHeader().getFieldIfSet(applVerID);
      const DataDictionary &applicationDictionary = m_dataDictionaryProvider.getApplicationDataDictionary(applVerID);
      message = Message(bytes, sessionDictionary, applicationDictionary, m_validateLengthAndChecksum);
    } else {
      message = Message(bytes, sessionDictionary, m_validateLengthAndChecksum);
    }

    const Header &header = message.getHeader();
    const auto &messageType = FIELD_GET_REF(header, MsgType);
    const auto &beginString = FIELD_GET_REF(header, BeginString);
    FIELD_THROW_IF_NOT_FOUND(header, SenderCompID);
    FIELD_THROW_IF_NOT_FOUND(header, TargetCompID);
    if (beginString != m_sessionID.getBeginString()) {
      throw UnsupportedVersion();
    }

    if (m_sessionID.isFIXT() && message.isApp()) {
      ApplVerID applVerID = m_targetDefaultApplVerID;
      header.getFieldIfSet(applVerID);
      const DataDictionary &applicationDictionary = m_dataDictionaryProvider.getApplicationDataDictionary(applVerID);
      DataDictionary::validate(message, &sessionDictionary, &applicationDictionary);
    } else {
      sessionDictionary.validate(message);
    }

    actionData.messageType = messageType;
    const auto sequence = header.getField<MsgSeqNum>().getValue();
    if (sequence > expected.targetSequence) {
      actionData.sequenceDisposition = InfiniteSequenceDisposition::TooHigh;
      action = InfiniteSessionActionKind::ResendOrQueuedRelease;
    } else if (sequence < expected.targetSequence) {
      actionData.sequenceDisposition = InfiniteSequenceDisposition::TooLow;
      action = InfiniteSessionActionKind::ProtocolDisposition;
    } else {
      actionData.sequenceDisposition = InfiniteSequenceDisposition::AtHead;
      action = InfiniteSessionActionKind::Application;
      if (messageType == MsgType_Logon || messageType == MsgType_Heartbeat || messageType == MsgType_TestRequest) {
        action = InfiniteSessionActionKind::ProtocolControl;
      } else if (messageType == MsgType_SequenceReset) {
        action = InfiniteSessionActionKind::SequenceReset;
      } else if (messageType == MsgType_Logout) {
        action = InfiniteSessionActionKind::Logout;
      } else if (messageType == MsgType_ResendRequest) {
        action = InfiniteSessionActionKind::ResendOrQueuedRelease;
      } else if (messageType == MsgType_Reject) {
        action = InfiniteSessionActionKind::ProtocolDisposition;
      }
    }

    SEQNUM storedBegin = 0;
    SEQNUM storedEnd = 0;
    if (messageType == MsgType_ResendRequest) {
      storedBegin = message.getField<BeginSeqNo>().getValue();
      storedEnd = message.getField<EndSeqNo>().getValue();
      if (storedEnd == 0 || storedEnd >= expected.senderSequence) {
        storedEnd = expected.senderSequence - 1;
      }
    } else if (messageType == MsgType_Logon) {
      NextExpectedMsgSeqNum nextExpected;
      if (message.getFieldIfSet(nextExpected) && nextExpected.getValue() < expected.senderSequence) {
        storedBegin = nextExpected.getValue();
        storedEnd = expected.senderSequence - 1;
      }
    }

    if (storedBegin && storedBegin <= storedEnd) {
      std::vector<std::string> messages;
      m_state.get(storedBegin, storedEnd, messages);
      if (messages.size() > kMaxInfinitePlannedMessages) {
        throw std::length_error("Infinite effect plan message count exceeds bound");
      }
      std::size_t totalBytes = 0;
      actionData.sourceMessages.reserve(messages.size());
      for (const auto &storedBytes : messages) {
        if (storedBytes.size() > kMaxInfinitePlannedBytes - totalBytes) {
          throw std::length_error("Infinite effect plan bytes exceed bound");
        }
        totalBytes += storedBytes.size();
        Message storedMessage(storedBytes, false);
        actionData.sourceMessages.emplace_back(storedMessage.getHeader().getField<MsgSeqNum>().getValue(), storedBytes);
      }
    }
  } catch (const std::exception &error) {
    actionData.failure = error.what();
    return InfiniteSessionClassification(
        atHead.value(),
        expected,
        InfiniteSessionActionKind::Failure,
        actionData,
        std::move(message));
  }

  Session &session = const_cast<Session &>(*this);
  RecordingMessageStore recordingStore(expected, actionData.sourceMessages, actionData.effects, now);
  RecordingLog recordingLog(actionData.effects, now);
  RecordingResponder recordingResponder(actionData.effects, now);
  MessageStore *const originalStore = session.m_state.m_pStore;
  Log *const originalLog = session.m_state.m_pLog;
  Responder *const originalResponder = session.m_pResponder;
  auto originalQueue = session.m_state.m_queue;
  const auto originalTimestamper = session.m_timestamper;
  const auto originalTargetDefaultApplVerID = session.m_targetDefaultApplVerID;

  const auto restore = [&]() {
    const auto &state = expected.mutableState;
    session.m_state.m_enabled = state.enabled;
    session.m_state.m_receivedLogon = state.receivedLogon;
    session.m_state.m_sentLogout = state.sentLogout;
    session.m_state.m_sentLogon = state.sentLogon;
    session.m_state.m_sentReset = state.sentReset;
    session.m_state.m_receivedReset = state.receivedReset;
    session.m_state.m_initiate = state.initiate;
    session.m_state.m_logonTimeout = state.logonTimeout;
    session.m_state.m_logoutTimeout = state.logoutTimeout;
    session.m_state.m_testRequest = state.testRequest;
    session.m_state.m_resendRange = std::make_pair(state.resendBegin, state.resendEnd);
    session.m_state.m_heartBtInt = HeartBtInt(state.heartBtInt);
    session.m_state.m_lastSentTime = state.lastSentTime;
    session.m_state.m_lastReceivedTime = state.lastReceivedTime;
    session.m_state.m_logoutReason = state.logoutReason;
    session.m_state.m_queue.swap(originalQueue);
    session.m_state.m_pStore = originalStore;
    session.m_state.m_pLog = originalLog;
    session.m_pResponder = originalResponder;
    session.m_timestamper = originalTimestamper;
    session.m_targetDefaultApplVerID = originalTargetDefaultApplVerID;
    session.m_infinitePlan = nullptr;
  };

  session.m_state.m_pStore = &recordingStore;
  session.m_state.m_pLog = &recordingLog;
  session.m_pResponder = originalResponder ? &recordingResponder : nullptr;
  session.m_timestamper = [now]() { return now; };
  session.m_infinitePlan = &actionData;

  try {
    if (actionData.sequenceDisposition == InfiniteSequenceDisposition::TooHigh) {
      const Header &header = message.getHeader();
      const auto &beginString = header.getField<BeginString>();
      const auto &messageSequence = header.getField<MsgSeqNum>();
      session.m_state.onEvent(
          "MsgSeqNum too high, expecting " + SEQNUM_CONVERTOR::convert(session.getExpectedTargetNum())
          + " but received " + SEQNUM_CONVERTOR::convert(messageSequence));
      const auto resend = session.m_state.resendRange();
      if (!session.m_state.resendRequested() || session.m_sendRedundantResendRequests
          || messageSequence < resend.first) {
        session.generateResendRequest(beginString, messageSequence);
      }
    } else if (actionData.messageType == MsgType_Logon) {
      if (session.m_sessionID.isFIXT()) {
        session.m_targetDefaultApplVerID = message.getField<DefaultApplVerID>();
      } else {
        session.m_targetDefaultApplVerID = Message::toApplVerID(message.getHeader().getField<BeginString>());
      }
      session.nextLogon(message, now, false);
    } else if (actionData.messageType == MsgType_Heartbeat) {
      session.nextHeartbeat(message, now, false);
    } else if (actionData.messageType == MsgType_TestRequest) {
      session.nextTestRequest(message, now, false);
    } else if (actionData.messageType == MsgType_SequenceReset) {
      session.nextSequenceReset(message, now, false);
    } else if (actionData.messageType == MsgType_Logout) {
      session.nextLogout(message, now);
    } else if (actionData.messageType == MsgType_ResendRequest) {
      session.nextResendRequest(message, now);
    } else if (actionData.messageType == MsgType_Reject) {
      session.nextReject(message, now, false);
    } else if (session.verify(message, false, false)) {
      session.m_state.incrNextTargetMsgSeqNum();
    }

    actionData.resultingState = session.currentInfiniteExpectedState();
    actionData.resultingState.revision = expected.revision + 1;
    actionData.resultingState.mutableState.responderIdentity
        = session.m_pResponder ? reinterpret_cast<std::uintptr_t>(originalResponder) : 0;
    restore();
  } catch (const std::exception &error) {
    restore();
    actionData.callbacks.clear();
    actionData.effects.clear();
    actionData.failure = error.what();
    action = InfiniteSessionActionKind::Failure;
  }

  return InfiniteSessionClassification(atHead.value(), expected, action, actionData, std::move(message));
}

void Session::applyInfiniteClassification(
    const InfiniteSessionClassification &classification,
    InfiniteEffectAuthorization &&authorization) {
  if (authorization.m_consumed || authorization.m_binding != classification.m_binding
      || !(authorization.m_expected == classification.m_expected) || authorization.m_action != classification.m_action
      || !(authorization.m_actionData == classification.m_actionData)
      || !(currentInfiniteExpectedState() == classification.m_expected)
      || m_infiniteSessionRevision == std::numeric_limits<std::uint64_t>::max()) {
    return;
  }
  authorization.m_consumed = true;
  if (classification.m_action == InfiniteSessionActionKind::Failure) {
    return;
  }

  try {
    for (const auto callback : classification.m_actionData.callbacks) {
      if (callback == InfiniteCallbackKind::FromAdmin) {
        m_application.fromAdmin(classification.m_message, m_sessionID);
      } else if (callback == InfiniteCallbackKind::FromApplication) {
        m_application.fromApp(classification.m_message, m_sessionID);
      } else if (callback == InfiniteCallbackKind::Logon) {
        m_application.onLogon(m_sessionID);
      } else if (callback == InfiniteCallbackKind::Logout) {
        m_application.onLogout(m_sessionID);
      }
    }
  } catch (const std::exception &) {
    return;
  }

  for (const auto &effect : classification.m_actionData.effects) {
    switch (effect.kind) {
    case InfiniteEffectKind::StoreMessage:
      m_state.set(effect.sequence, effect.bytes);
      break;
    case InfiniteEffectKind::SetSenderSequence:
      m_state.setNextSenderMsgSeqNum(effect.sequence);
      break;
    case InfiniteEffectKind::SetTargetSequence:
      m_state.setNextTargetMsgSeqNum(effect.sequence);
      break;
    case InfiniteEffectKind::ResetStore:
      m_state.reset(effect.timestamp);
      break;
    case InfiniteEffectKind::LogIncoming:
      m_state.onIncoming(effect.bytes);
      break;
    case InfiniteEffectKind::LogOutgoing:
      m_state.onOutgoing(effect.bytes);
      break;
    case InfiniteEffectKind::LogEvent:
      m_state.onEvent(effect.bytes);
      break;
    case InfiniteEffectKind::Send:
      if (m_pResponder) {
        m_pResponder->send(effect.bytes);
      }
      break;
    case InfiniteEffectKind::Disconnect:
      if (m_pResponder) {
        m_pResponder->disconnect();
        m_pResponder = nullptr;
      }
      break;
    }
  }

  const auto &result = classification.m_actionData.resultingState;
  const auto &state = result.mutableState;
  {
    Locker lock(m_state.m_mutex);
    m_state.m_enabled = state.enabled;
    m_state.m_receivedLogon = state.receivedLogon;
    m_state.m_sentLogout = state.sentLogout;
    m_state.m_sentLogon = state.sentLogon;
    m_state.m_sentReset = state.sentReset;
    m_state.m_receivedReset = state.receivedReset;
    m_state.m_initiate = state.initiate;
    m_state.m_logonTimeout = state.logonTimeout;
    m_state.m_logoutTimeout = state.logoutTimeout;
    m_state.m_testRequest = state.testRequest;
    m_state.m_resendRange = std::make_pair(state.resendBegin, state.resendEnd);
    m_state.m_heartBtInt = HeartBtInt(state.heartBtInt);
    m_state.m_lastSentTime = state.lastSentTime;
    m_state.m_lastReceivedTime = state.lastReceivedTime;
    m_state.m_logoutReason = state.logoutReason;
    m_state.m_queue.clear();
    for (const auto &entry : state.queuedMessages) {
      m_state.m_queue.emplace(entry.first, Message(entry.second, false));
    }
  }
  m_targetDefaultApplVerID = state.targetDefaultApplVerID;
  m_infiniteSessionRevision = result.revision;
}
} // namespace FIX
