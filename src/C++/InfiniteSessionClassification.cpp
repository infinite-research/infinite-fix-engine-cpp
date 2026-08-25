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

#ifdef _MSC_VER
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "InfiniteSessionClassification.h"
#include "Session.h"
#include "Values.h"
#include "scope_guard.hpp"

#include <limits>
#include <map>
#include <stdexcept>

namespace FIX {
namespace {
constexpr std::size_t MAX_INFINITE_PLANNED_MESSAGES = 256;
constexpr std::size_t MAX_INFINITE_PLANNED_BYTES = 16 * 1024 * 1024;

InfiniteActionData makeActionData(InfiniteSessionActionKind action, InfiniteActionPlan plan) {
  switch (action) {
  case InfiniteSessionActionKind::ProtocolControl:
    return InfiniteProtocolControlData{std::move(plan)};
  case InfiniteSessionActionKind::SequenceReset:
    return InfiniteSequenceResetData{std::move(plan)};
  case InfiniteSessionActionKind::Logout:
    return InfiniteLogoutData{std::move(plan)};
  case InfiniteSessionActionKind::ResendOrQueuedRelease:
    return InfiniteResendOrQueuedReleaseData{std::move(plan)};
  case InfiniteSessionActionKind::ProtocolDisposition:
    return InfiniteProtocolDispositionData{std::move(plan)};
  case InfiniteSessionActionKind::Application:
    return InfiniteApplicationData{std::move(plan)};
  case InfiniteSessionActionKind::Failure:
    return InfiniteFailureData{std::move(plan)};
  }
  throw std::logic_error("Unknown Infinite session action");
}

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
      const MessageStore &sourceStore,
      std::vector<std::pair<SEQNUM, std::string>> &sourceMessages,
      std::vector<InfinitePlannedEffect> &effects,
      const UtcTimeStamp &now)
      : m_senderSequence(initial.senderSequence),
        m_targetSequence(initial.targetSequence),
        m_creationTime(initial.mutableState.storeCreationTime),
        m_sourceStore(sourceStore),
        m_sourceMessages(sourceMessages),
        m_effects(effects),
        m_now(now) {}

  bool set(SEQNUM sequence, const std::string &message) override {
    m_messages[sequence] = message;
    appendEffect(m_effects, InfiniteEffectKind::StoreMessage, sequence, message, m_now);
    return true;
  }

  void get(SEQNUM begin, SEQNUM end, std::vector<std::string> &messages) const override {
    if (begin <= 0 || end < begin
        || static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin) >= MAX_INFINITE_PLANNED_MESSAGES) {
      throw IOException("Infinite effect plan message range exceeds bound");
    }
    for (SEQNUM sequence = begin; sequence <= end; ++sequence) {
      const auto cached = m_messages.find(sequence);
      if (cached != m_messages.end()) {
        messages.push_back(cached->second);
        continue;
      }
      if (m_reset) {
        continue;
      }

      std::vector<std::string> loaded;
      m_sourceStore.get(sequence, sequence, loaded);
      for (const auto &bytes : loaded) {
        if (m_sourceMessages.size() >= MAX_INFINITE_PLANNED_MESSAGES
            || bytes.size() > MAX_INFINITE_PLANNED_BYTES - m_sourceBytes) {
          throw IOException("Infinite effect plan store input exceeds bound");
        }
        Message message(bytes, false);
        const auto loadedSequence = message.getHeader().getField<MsgSeqNum>().getValue();
        if (loadedSequence != sequence) {
          throw IOException("Infinite effect plan store sequence mismatch");
        }
        m_sourceBytes += bytes.size();
        m_sourceMessages.emplace_back(sequence, bytes);
        m_messages.emplace(sequence, bytes);
        messages.push_back(bytes);
      }
      if (sequence == std::numeric_limits<SEQNUM>::max()) {
        break;
      }
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
    m_reset = true;
    m_senderSequence = 1;
    m_targetSequence = 1;
    m_creationTime = now;
    appendEffect(m_effects, InfiniteEffectKind::ResetStore, 0, "", now);
  }

  void refresh() override {}

private:
  mutable std::map<SEQNUM, std::string> m_messages;
  SEQNUM m_senderSequence;
  SEQNUM m_targetSequence;
  UtcTimeStamp m_creationTime;
  const MessageStore &m_sourceStore;
  std::vector<std::pair<SEQNUM, std::string>> &m_sourceMessages;
  mutable std::size_t m_sourceBytes{0};
  bool m_reset{false};
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

bool InfinitePlannedCallback::operator==(const InfinitePlannedCallback &rhs) const {
  return kind == rhs.kind && bytes == rhs.bytes;
}

bool InfinitePlannedMessage::operator==(const InfinitePlannedMessage &rhs) const {
  return sequence == rhs.sequence && bytes == rhs.bytes;
}

bool InfinitePlannedEffect::operator==(const InfinitePlannedEffect &rhs) const {
  return kind == rhs.kind && sequence == rhs.sequence && bytes == rhs.bytes && timestamp == rhs.timestamp;
}

bool InfiniteSessionStateFingerprint::operator==(const InfiniteSessionStateFingerprint &rhs) const {
  return enabled == rhs.enabled && receivedLogon == rhs.receivedLogon && sentLogout == rhs.sentLogout
         && sentLogon == rhs.sentLogon && sentReset == rhs.sentReset && receivedReset == rhs.receivedReset
         && initiate == rhs.initiate && logonTimeout == rhs.logonTimeout && logoutTimeout == rhs.logoutTimeout
         && testRequest == rhs.testRequest && resendBegin == rhs.resendBegin && resendEnd == rhs.resendEnd
         && heartBtInt == rhs.heartBtInt && lastSentTime == rhs.lastSentTime && lastReceivedTime == rhs.lastReceivedTime
         && storeCreationTime == rhs.storeCreationTime && sessionTimeActive == rhs.sessionTimeActive
         && logonTimeActive == rhs.logonTimeActive && infiniteFenced == rhs.infiniteFenced
         && logoutReason == rhs.logoutReason && queuedMessages == rhs.queuedMessages
         && senderDefaultApplVerID == rhs.senderDefaultApplVerID && targetDefaultApplVerID == rhs.targetDefaultApplVerID
         && sendRedundantResendRequests == rhs.sendRedundantResendRequests && checkCompId == rhs.checkCompId
         && checkLatency == rhs.checkLatency && maxLatency == rhs.maxLatency && resetOnLogon == rhs.resetOnLogon
         && resetOnLogout == rhs.resetOnLogout && resetOnDisconnect == rhs.resetOnDisconnect
         && refreshOnLogon == rhs.refreshOnLogon && timestampPrecision == rhs.timestampPrecision
         && persistMessages == rhs.persistMessages && validateLengthAndChecksum == rhs.validateLengthAndChecksum
         && sendNextExpectedMsgSeqNum == rhs.sendNextExpectedMsgSeqNum && nonStopSession == rhs.nonStopSession
         && responderIdentity == rhs.responderIdentity;
}

bool InfiniteExpectedSessionState::operator==(const InfiniteExpectedSessionState &rhs) const {
  return sessionIdentity == rhs.sessionIdentity && revision == rhs.revision && senderSequence == rhs.senderSequence
         && targetSequence == rhs.targetSequence && loggedOn == rhs.loggedOn && mutableState == rhs.mutableState;
}

bool InfiniteActionPlan::operator==(const InfiniteActionPlan &rhs) const {
  return exactBytes == rhs.exactBytes && messageType == rhs.messageType && now == rhs.now
         && sequenceDisposition == rhs.sequenceDisposition && failure == rhs.failure
         && resultingState == rhs.resultingState && sourceMessages == rhs.sourceMessages && callbacks == rhs.callbacks
         && effects == rhs.effects;
}

InfiniteSessionActionKind infiniteActionKind(const InfiniteActionData &actionData) {
  if (std::holds_alternative<InfiniteProtocolControlData>(actionData)) {
    return InfiniteSessionActionKind::ProtocolControl;
  }
  if (std::holds_alternative<InfiniteSequenceResetData>(actionData)) {
    return InfiniteSessionActionKind::SequenceReset;
  }
  if (std::holds_alternative<InfiniteLogoutData>(actionData)) {
    return InfiniteSessionActionKind::Logout;
  }
  if (std::holds_alternative<InfiniteResendOrQueuedReleaseData>(actionData)) {
    return InfiniteSessionActionKind::ResendOrQueuedRelease;
  }
  if (std::holds_alternative<InfiniteProtocolDispositionData>(actionData)) {
    return InfiniteSessionActionKind::ProtocolDisposition;
  }
  if (std::holds_alternative<InfiniteApplicationData>(actionData)) {
    return InfiniteSessionActionKind::Application;
  }
  if (std::holds_alternative<InfiniteFailureData>(actionData)) {
    return InfiniteSessionActionKind::Failure;
  }
  throw std::logic_error("Unknown Infinite action-data alternative");
}

InfiniteActionPlan &infiniteActionPlan(InfiniteActionData &actionData) {
  return std::visit([](auto &data) -> InfiniteActionPlan & { return data.plan; }, actionData);
}

const InfiniteActionPlan &infiniteActionPlan(const InfiniteActionData &actionData) {
  return std::visit([](const auto &data) -> const InfiniteActionPlan & { return data.plan; }, actionData);
}

InfiniteExpectedSessionState Session::currentInfiniteExpectedState(const UtcTimeStamp &now) const {
  Locker sessionLock(m_mutex);
  Locker lock(m_state.m_mutex);
  const auto senderSequence = m_state.m_pStore->getNextSenderMsgSeqNum();
  const auto targetSequence = m_state.m_pStore->getNextTargetMsgSeqNum();
  const auto storeCreationTime = m_state.m_pStore->getCreationTime();

  std::vector<InfinitePlannedMessage> queue;
  if (m_state.m_queue.size() > MAX_INFINITE_PLANNED_MESSAGES) {
    throw std::length_error("Infinite queued message count exceeds bound");
  }
  queue.reserve(m_state.m_queue.size());
  std::size_t queuedBytes = 0;
  for (const auto &entry : m_state.m_queue) {
    auto bytes = entry.second.toString();
    if (bytes.size() > MAX_INFINITE_PLANNED_BYTES - queuedBytes) {
      throw std::length_error("Infinite queued message bytes exceed bound");
    }
    queuedBytes += bytes.size();
    queue.push_back(InfinitePlannedMessage{entry.first, std::move(bytes), entry.second});
  }

  const auto resend = m_state.m_resendRange;
  auto sessionTime = m_sessionTime;
  auto logonTime = m_logonTime;
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
      storeCreationTime,
      m_isNonStopSession || sessionTime.isInSameRange(now, storeCreationTime),
      logonTime.isInRange(now),
      m_infiniteSessionFenced,
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
      reinterpret_cast<std::uintptr_t>(this),
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
  Locker sessionLock(m_mutex);
  InfiniteExpectedSessionState expected{};
  InfiniteActionPlan plan{bytes, "", now, InfiniteSequenceDisposition::Unavailable, "", expected, {}, {}, {}};
  Message message;
  InfiniteSessionActionKind action = InfiniteSessionActionKind::Failure;

  try {
    if (bytes.size() > 65536) {
      throw std::length_error("Infinite FIX frame exceeds bound");
    }
    expected = currentInfiniteExpectedState(now);
    plan.resultingState = expected;
    if (expected.mutableState.infiniteFenced) {
      throw std::logic_error("Infinite session is fenced");
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
    MsgType messageType;
    MsgSeqNum messageSequence;
    const bool hasMessageType = header.getFieldIfSet(messageType);
    const bool hasSequence = header.getFieldIfSet(messageSequence);
    plan.messageType = hasMessageType ? messageType.getValue() : "";
    if (!hasMessageType || !hasSequence) {
      action = InfiniteSessionActionKind::ProtocolDisposition;
    } else {
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
      if (messageSequence.getValue() > expected.targetSequence) {
        plan.sequenceDisposition = InfiniteSequenceDisposition::TooHigh;
        if (action == InfiniteSessionActionKind::Application) {
          action = InfiniteSessionActionKind::ResendOrQueuedRelease;
        }
      } else if (messageSequence.getValue() < expected.targetSequence) {
        plan.sequenceDisposition = InfiniteSequenceDisposition::TooLow;
        if (action == InfiniteSessionActionKind::Application) {
          action = InfiniteSessionActionKind::ProtocolDisposition;
        }
      } else {
        plan.sequenceDisposition = InfiniteSequenceDisposition::AtHead;
      }
    }

  } catch (const std::exception &error) {
    plan.callbacks.clear();
    plan.effects.clear();
    plan.failure = error.what();
    action = InfiniteSessionActionKind::Failure;
    return InfiniteSessionClassification(
        atHead.value(),
        expected,
        makeActionData(action, std::move(plan)),
        std::move(message));
  }

  Session &session = const_cast<Session &>(*this);
  Locker stateLock(session.m_state.m_mutex);
  MessageStore *const originalStore = session.m_state.m_pStore;
  RecordingMessageStore recordingStore(expected, *originalStore, plan.sourceMessages, plan.effects, now);
  RecordingLog recordingLog(plan.effects, now);
  RecordingResponder recordingResponder(plan.effects, now);
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
    session.m_infiniteAction = nullptr;
  };
  auto restoreGuard = sg::make_scope_guard(restore);

  session.m_state.m_pStore = &recordingStore;
  session.m_state.m_pLog = &recordingLog;
  session.m_pResponder = originalResponder ? &recordingResponder : nullptr;
  session.m_timestamper = [now]() { return now; };
  session.m_infinitePlan = &plan;
  session.m_infiniteAction = &action;

  try {
    recordingLog.onIncoming(bytes);
    session.next(message, now, false);

    plan.resultingState = session.currentInfiniteExpectedState(now);
    plan.resultingState.revision = expected.revision + 1;
    plan.resultingState.mutableState.responderIdentity
        = session.m_pResponder ? reinterpret_cast<std::uintptr_t>(originalResponder) : 0;
    restore();
    restoreGuard.dismiss();
  } catch (const std::exception &error) {
    restore();
    restoreGuard.dismiss();
    plan.callbacks.clear();
    plan.effects.clear();
    plan.failure = error.what();
    action = InfiniteSessionActionKind::Failure;
  } catch (...) {
    restore();
    restoreGuard.dismiss();
    plan.callbacks.clear();
    plan.effects.clear();
    plan.failure = "Non-standard exception during Infinite classification";
    action = InfiniteSessionActionKind::Failure;
  }

  return InfiniteSessionClassification(
      atHead.value(),
      expected,
      makeActionData(action, std::move(plan)),
      std::move(message));
}

void Session::applyInfiniteClassification(
    const InfiniteSessionClassification &classification,
    InfiniteEffectAuthorization &&authorization) {
  Locker sessionLock(m_mutex);
  Locker stateLock(m_state.m_mutex);
  const auto &plan = infiniteActionPlan(classification.m_actionData);
  if (authorization.m_consumed || authorization.m_binding != classification.m_binding
      || !(authorization.m_expected == classification.m_expected) || authorization.m_action != classification.m_action
      || !(authorization.m_actionData == classification.m_actionData)) {
    return;
  }
  authorization.m_consumed = true;
  if (classification.m_action == InfiniteSessionActionKind::Failure) {
    return;
  }
  if (!(currentInfiniteExpectedState(plan.now) == classification.m_expected)
      || m_infiniteSessionRevision == std::numeric_limits<std::uint64_t>::max()) {
    return;
  }

  try {
    m_infiniteCallbackActive = true;
    auto callbackGuard = sg::make_scope_guard([this]() { m_infiniteCallbackActive = false; });
    for (const auto &callback : plan.callbacks) {
      if (callback.kind == InfiniteCallbackKind::FromAdmin) {
        m_application.fromAdmin(callback.message, m_sessionID);
      } else if (callback.kind == InfiniteCallbackKind::FromApplication) {
        m_application.fromApp(callback.message, m_sessionID);
      } else if (callback.kind == InfiniteCallbackKind::ToAdmin) {
        Message outgoing = callback.message;
        m_application.toAdmin(outgoing, m_sessionID);
        if (outgoing.toString() != callback.bytes) {
          throw IOException("Infinite toAdmin callback changed the authorized bytes");
        }
      } else if (callback.kind == InfiniteCallbackKind::ToApplication) {
        Message outgoing = callback.message;
        try {
          m_application.toApp(outgoing, m_sessionID);
        } catch (DoNotSend &) {
          throw IOException("Infinite toApp callback refused the authorized bytes");
        }
        if (outgoing.toString() != callback.bytes) {
          throw IOException("Infinite toApp callback changed the authorized bytes");
        }
      } else if (callback.kind == InfiniteCallbackKind::Logon) {
        m_application.onLogon(m_sessionID);
      } else if (callback.kind == InfiniteCallbackKind::Logout) {
        m_application.onLogout(m_sessionID);
      }
    }
    callbackGuard.dismiss();
    m_infiniteCallbackActive = false;

    if (!(currentInfiniteExpectedState(plan.now) == classification.m_expected)) {
      throw IOException("Infinite callback changed the expected session state");
    }

    for (const auto &effect : plan.effects) {
      switch (effect.kind) {
      case InfiniteEffectKind::StoreMessage:
        if (!m_state.set(effect.sequence, effect.bytes)) {
          throw IOException("Infinite message-store write failed");
        }
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
        if (!m_pResponder || !m_pResponder->send(effect.bytes)) {
          throw IOException("Infinite responder send failed");
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

    const auto &result = plan.resultingState;
    const auto &state = result.mutableState;
    if (m_state.getNextSenderMsgSeqNum() != result.senderSequence
        || m_state.getNextTargetMsgSeqNum() != result.targetSequence) {
      throw IOException("Infinite sequence effects did not reach the authorized state");
    }
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
        m_state.m_queue.emplace(entry.sequence, entry.message);
      }
    }
    m_targetDefaultApplVerID = state.targetDefaultApplVerID;
    m_infiniteSessionRevision = result.revision;
  } catch (...) {
    m_infiniteCallbackActive = false;
    m_infiniteSessionFenced = true;
    throw;
  }
}
} // namespace FIX
