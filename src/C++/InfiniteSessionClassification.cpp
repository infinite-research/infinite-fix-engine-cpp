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

#ifdef HAVE_SSL
#include <openssl/crypto.h>
#endif

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace FIX {
namespace {
void cleanse(std::string &bytes) noexcept {
#ifdef HAVE_SSL
  OPENSSL_cleanse(bytes.data(), bytes.size());
#else
  volatile char *cursor = bytes.empty() ? nullptr : &bytes[0];
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    cursor[index] = 0;
  }
#endif
}

void cleanse(InfiniteSensitiveString &bytes) noexcept { bytes.clear(); }

bool containsLogonCredentialField(const std::string &bytes) {
  bool isLogon = false;
  bool hasCredential = false;
  for (std::size_t fieldStart = 0; fieldStart < bytes.size();) {
    const auto fieldEnd = bytes.find('\001', fieldStart);
    const auto valueEnd = fieldEnd == std::string::npos ? bytes.size() : fieldEnd;
    const auto equals = bytes.find('=', fieldStart);
    if (equals < valueEnd) {
      auto tagStart = fieldStart;
      while (tagStart < equals && bytes[tagStart] == '0') {
        ++tagStart;
      }
      const auto isTag
          = [&](const char *tag) { return tagStart < equals && bytes.compare(tagStart, equals - tagStart, tag) == 0; };
      isLogon = isLogon || (isTag("35") && bytes.compare(equals + 1, valueEnd - equals - 1, "A") == 0);
      hasCredential = hasCredential || isTag("553") || isTag("554");
    }
    if (fieldEnd == std::string::npos) {
      break;
    }
    fieldStart = fieldEnd + 1;
  }
  return isLogon && hasCredential;
}

bool cleanseCredentialMessages(std::vector<std::string> &messages) noexcept {
  bool found = false;
  for (auto &bytes : messages) {
    if (containsLogonCredentialField(bytes)) {
      cleanse(bytes);
      found = true;
    }
  }
  return found;
}

void cleanseMessages(std::vector<std::string> &messages) noexcept {
  for (auto &bytes : messages) {
    cleanse(bytes);
  }
}

void cleanseMessage(Message &message) noexcept { InfinitePlannedMessage planned{0, {}, std::move(message)}; }

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
    InfiniteActionPlan &plan,
    InfiniteEffectKind kind,
    SEQNUM sequence,
    const std::string &bytes,
    const UtcTimeStamp &now) {
  plan.effects.push_back(InfinitePlannedEffect{plan.operationCount++, kind, sequence, bytes, now});
}

class RecordingMessageStore : public MessageStore {
public:
  RecordingMessageStore(
      const InfiniteExpectedSessionState &initial,
      const MessageStore &sourceStore,
      std::vector<std::pair<SEQNUM, std::string>> &sourceMessages,
      InfiniteActionPlan &plan,
      const UtcTimeStamp &now)
      : m_senderSequence(initial.senderSequence),
        m_targetSequence(initial.targetSequence),
        m_creationTime(initial.mutableState.storeCreationTime),
        m_sourceStore(sourceStore),
        m_sourceMessages(sourceMessages),
        m_plan(plan),
        m_now(now) {}

  ~RecordingMessageStore() override {
    for (auto &entry : m_messages) {
      cleanse(entry.second);
    }
  }

  bool set(SEQNUM sequence, const std::string &message) override {
    auto replacement = message;
    auto replacementGuard = sg::make_scope_guard([&replacement]() { cleanse(replacement); });
    auto existing = m_messages.find(sequence);
    if (existing == m_messages.end()) {
      m_messages.emplace(sequence, std::move(replacement));
    } else {
      cleanse(existing->second);
      existing->second.swap(replacement);
    }
    appendEffect(m_plan, InfiniteEffectKind::StoreMessage, sequence, message, m_now);
    return true;
  }

  void get(SEQNUM begin, SEQNUM end, std::vector<std::string> &messages) const override {
    if (begin <= 0) {
      throw IOException("Infinite effect plan message range exceeds bound");
    }
    if (end < begin) {
      messages.clear();
      return;
    }
    if (static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin) >= INFINITE_MAX_PLANNED_MESSAGES) {
      throw IOException("Infinite effect plan message range exceeds bound");
    }

    std::vector<std::string> loaded;
    auto loadedGuard = sg::make_scope_guard([&loaded]() { cleanseMessages(loaded); });
    std::set<SEQNUM> loadedSequences;
    if (!m_reset) {
      if (m_plan.sourceRangeRead) {
        throw IOException("Infinite effect plan read more than one source range");
      }
      if (!m_messages.empty()) {
        throw IOException("Infinite effect plan read after a planned message-store write");
      }
      try {
        m_sourceStore.get(begin, end, loaded);
      } catch (...) {
        cleanseCredentialMessages(loaded);
        throw;
      }
      if (cleanseCredentialMessages(loaded)) {
        throw IOException("Infinite effect plan store contains credential-bearing Logon");
      }
      if (loaded.size() > INFINITE_MAX_PLANNED_MESSAGES) {
        throw IOException("Infinite effect plan store returned too many messages");
      }
      std::size_t loadedBytes = 0;
      for (std::size_t index = 0; index < loaded.size(); ++index) {
        const auto &bytes = loaded[index];
        if (bytes.size() > INFINITE_MAX_PLANNED_BYTES - loadedBytes) {
          throw IOException("Infinite effect plan store returned too many bytes");
        }
        loadedBytes += bytes.size();
        Message message;
        auto messageGuard = sg::make_scope_guard([&message]() { cleanseMessage(message); });
        message.setString(bytes, false);
        const auto sequence = message.getHeader().getField<MsgSeqNum>().getValue();
        if (sequence < begin || sequence > end || !loadedSequences.insert(sequence).second) {
          throw IOException("Infinite effect plan store returned invalid sequence range");
        }
      }
    }

    std::vector<std::string> stagedMessages;
    auto stagedGuard = sg::make_scope_guard([&stagedMessages]() { cleanseMessages(stagedMessages); });
    if (m_reset) {
      stagedMessages.reserve(static_cast<std::size_t>(end - begin) + 1);
      for (SEQNUM sequence = begin; sequence <= end; ++sequence) {
        const auto cached = m_messages.find(sequence);
        if (cached != m_messages.end()) {
          stagedMessages.push_back(cached->second);
        }
        if (sequence == std::numeric_limits<SEQNUM>::max()) {
          break;
        }
      }
    } else {
      stagedMessages = loaded;
    }

    if (!m_reset) {
      std::vector<std::pair<SEQNUM, std::string>> stagedSources;
      auto sourcesGuard = sg::make_scope_guard([&stagedSources]() {
        for (auto &source : stagedSources) {
          cleanse(source.second);
        }
      });
      stagedSources.reserve(loaded.size());
      for (auto &bytes : loaded) {
        Message message;
        auto messageGuard = sg::make_scope_guard([&message]() { cleanseMessage(message); });
        message.setString(bytes, false);
        const auto sequence = message.getHeader().getField<MsgSeqNum>().getValue();
        stagedSources.emplace_back(sequence, std::move(bytes));
      }
      for (const auto &source : stagedSources) {
        m_messages.emplace(source.first, source.second);
      }
      m_sourceMessages = std::move(stagedSources);
      m_plan.sourceRangeRead = true;
      m_plan.sourceRangeBegin = begin;
      m_plan.sourceRangeEnd = end;
    }
    messages.insert(messages.end(), stagedMessages.begin(), stagedMessages.end());
  }

  SEQNUM getNextSenderMsgSeqNum() const override { return m_senderSequence; }
  SEQNUM getNextTargetMsgSeqNum() const override { return m_targetSequence; }

  void setNextSenderMsgSeqNum(SEQNUM sequence) override {
    m_senderSequence = sequence;
    appendEffect(m_plan, InfiniteEffectKind::SetSenderSequence, sequence, "", m_now);
  }

  void setNextTargetMsgSeqNum(SEQNUM sequence) override {
    m_targetSequence = sequence;
    appendEffect(m_plan, InfiniteEffectKind::SetTargetSequence, sequence, "", m_now);
  }

  void incrNextSenderMsgSeqNum() override {
    if (m_senderSequence == std::numeric_limits<SEQNUM>::max()) {
      throw IOException("Infinite sender sequence exhausted");
    }
    setNextSenderMsgSeqNum(m_senderSequence + 1);
  }
  void incrNextTargetMsgSeqNum() override {
    if (m_targetSequence == std::numeric_limits<SEQNUM>::max()) {
      throw IOException("Infinite target sequence exhausted");
    }
    setNextTargetMsgSeqNum(m_targetSequence + 1);
  }
  UtcTimeStamp getCreationTime() const override { return m_creationTime; }

  void reset(const UtcTimeStamp &now) override {
    for (auto &entry : m_messages) {
      cleanse(entry.second);
    }
    m_messages.clear();
    m_reset = true;
    m_senderSequence = 1;
    m_targetSequence = 1;
    m_creationTime = now;
    appendEffect(m_plan, InfiniteEffectKind::ResetStore, 0, "", now);
  }

  void refresh() override {}

private:
  mutable std::map<SEQNUM, std::string> m_messages;
  SEQNUM m_senderSequence;
  SEQNUM m_targetSequence;
  UtcTimeStamp m_creationTime;
  const MessageStore &m_sourceStore;
  std::vector<std::pair<SEQNUM, std::string>> &m_sourceMessages;
  bool m_reset{false};
  InfiniteActionPlan &m_plan;
  UtcTimeStamp m_now;
};

bool sourceMessagesMatch(const MessageStore &store, const InfiniteActionPlan &plan) {
  const auto &expected = plan.sourceMessages;
  if (!plan.sourceRangeRead) {
    return expected.empty() && plan.sourceRangeBegin == 0 && plan.sourceRangeEnd == 0;
  }
  if (plan.sourceRangeBegin <= 0 || plan.sourceRangeEnd < plan.sourceRangeBegin
      || static_cast<std::uint64_t>(plan.sourceRangeEnd) - static_cast<std::uint64_t>(plan.sourceRangeBegin)
             >= INFINITE_MAX_PLANNED_MESSAGES
      || expected.size() > INFINITE_MAX_PLANNED_MESSAGES) {
    return false;
  }

  std::vector<std::string> loaded;
  auto loadedGuard = sg::make_scope_guard([&loaded]() { cleanseMessages(loaded); });
  try {
    store.get(plan.sourceRangeBegin, plan.sourceRangeEnd, loaded);
  } catch (...) {
    cleanseCredentialMessages(loaded);
    throw;
  }
  if (cleanseCredentialMessages(loaded)) {
    throw IOException("Infinite source store contains credential-bearing Logon");
  }
  if (loaded.size() != expected.size()) {
    return false;
  }

  std::set<SEQNUM> sequences;
  std::size_t loadedBytes = 0;
  for (std::size_t index = 0; index < loaded.size(); ++index) {
    const auto &bytes = loaded[index];
    if (bytes.size() > INFINITE_MAX_PLANNED_BYTES - loadedBytes) {
      return false;
    }
    loadedBytes += bytes.size();
    Message message;
    auto messageGuard = sg::make_scope_guard([&message]() { cleanseMessage(message); });
    message.setString(bytes, false);
    const auto sequence = message.getHeader().getField<MsgSeqNum>().getValue();
    if (sequence < plan.sourceRangeBegin || sequence > plan.sourceRangeEnd || !sequences.insert(sequence).second
        || expected[index].first != sequence || expected[index].second != bytes) {
      return false;
    }
  }
  return true;
}

class RecordingLog : public Log {
public:
  RecordingLog(InfiniteActionPlan &plan, const UtcTimeStamp &now)
      : m_plan(plan),
        m_now(now) {}

  void clear() override {}
  void backup() override {}
  void onIncoming(const std::string &bytes) override {
    appendEffect(m_plan, InfiniteEffectKind::LogIncoming, 0, bytes, m_now);
  }
  void onOutgoing(const std::string &bytes) override {
    appendEffect(m_plan, InfiniteEffectKind::LogOutgoing, 0, bytes, m_now);
  }
  void onEvent(const std::string &event) override {
    appendEffect(m_plan, InfiniteEffectKind::LogEvent, 0, event, m_now);
  }

private:
  InfiniteActionPlan &m_plan;
  UtcTimeStamp m_now;
};

class RecordingResponder : public Responder {
public:
  RecordingResponder(InfiniteActionPlan &plan, const UtcTimeStamp &now)
      : m_plan(plan),
        m_now(now) {}

  bool send(const std::string &bytes) override {
    appendEffect(m_plan, InfiniteEffectKind::Send, 0, bytes, m_now);
    return true;
  }
  void disconnect() override { appendEffect(m_plan, InfiniteEffectKind::Disconnect, 0, "", m_now); }

private:
  InfiniteActionPlan &m_plan;
  UtcTimeStamp m_now;
};
} // namespace

InfinitePlannedMessage::InfinitePlannedMessage(SEQNUM sequence, InfiniteSensitiveString value, Message message)
    : sequence(sequence),
      bytes(std::move(value)),
      message(std::move(message)) {}

InfinitePlannedMessage::InfinitePlannedMessage(InfinitePlannedMessage &&other) noexcept
    : sequence(other.sequence),
      bytes(std::move(other.bytes)),
      message(std::move(other.message)) {}

InfinitePlannedMessage &InfinitePlannedMessage::operator=(const InfinitePlannedMessage &other) {
  if (this != &other) {
    auto replacement = other;
    *this = std::move(replacement);
  }
  return *this;
}

InfinitePlannedMessage &InfinitePlannedMessage::operator=(InfinitePlannedMessage &&other) noexcept {
  if (this != &other) {
    cleanse(bytes);
    Session::cleanseInfiniteMessage(message);
    sequence = other.sequence;
    bytes = std::move(other.bytes);
    message = std::move(other.message);
  }
  return *this;
}

InfinitePlannedMessage::~InfinitePlannedMessage() {
  cleanse(bytes);
  Session::cleanseInfiniteMessage(message);
}

InfinitePlannedCallback::InfinitePlannedCallback(
    std::size_t order,
    InfiniteCallbackKind kind,
    InfiniteSensitiveString value,
    Message message,
    InfiniteExpectedSessionState observedState)
    : order(order),
      kind(kind),
      bytes(std::move(value)),
      message(std::move(message)),
      observedState(std::move(observedState)) {}

InfinitePlannedCallback::InfinitePlannedCallback(InfinitePlannedCallback &&other) noexcept
    : order(other.order),
      kind(other.kind),
      bytes(std::move(other.bytes)),
      message(std::move(other.message)),
      observedState(std::move(other.observedState)) {}

InfinitePlannedCallback &InfinitePlannedCallback::operator=(const InfinitePlannedCallback &other) {
  if (this != &other) {
    auto replacement = other;
    *this = std::move(replacement);
  }
  return *this;
}

InfinitePlannedCallback &InfinitePlannedCallback::operator=(InfinitePlannedCallback &&other) noexcept {
  if (this != &other) {
    cleanse(bytes);
    Session::cleanseInfiniteMessage(message);
    Session::cleanseInfiniteExpectedState(observedState);
    order = other.order;
    kind = other.kind;
    bytes = std::move(other.bytes);
    message = std::move(other.message);
    observedState = std::move(other.observedState);
  }
  return *this;
}

InfinitePlannedCallback::~InfinitePlannedCallback() {
  cleanse(bytes);
  Session::cleanseInfiniteMessage(message);
  Session::cleanseInfiniteExpectedState(observedState);
}

InfinitePlannedEffect::InfinitePlannedEffect(
    std::size_t order,
    InfiniteEffectKind kind,
    SEQNUM sequence,
    InfiniteSensitiveString value,
    UtcTimeStamp timestamp)
    : order(order),
      kind(kind),
      sequence(sequence),
      bytes(std::move(value)),
      timestamp(timestamp) {}

InfinitePlannedEffect::InfinitePlannedEffect(InfinitePlannedEffect &&other) noexcept
    : order(other.order),
      kind(other.kind),
      sequence(other.sequence),
      bytes(std::move(other.bytes)),
      timestamp(other.timestamp) {}

InfinitePlannedEffect &InfinitePlannedEffect::operator=(const InfinitePlannedEffect &other) {
  if (this != &other) {
    auto replacement = other;
    *this = std::move(replacement);
  }
  return *this;
}

InfinitePlannedEffect &InfinitePlannedEffect::operator=(InfinitePlannedEffect &&other) noexcept {
  if (this != &other) {
    cleanse(bytes);
    order = other.order;
    kind = other.kind;
    sequence = other.sequence;
    bytes = std::move(other.bytes);
    timestamp = other.timestamp;
  }
  return *this;
}

InfinitePlannedEffect::~InfinitePlannedEffect() { cleanse(bytes); }

InfiniteActionPlan::InfiniteActionPlan(
    InfiniteSensitiveString type,
    UtcTimeStamp now,
    InfiniteSequenceDisposition sequenceDisposition,
    InfiniteSensitiveString failureReason,
    InfiniteExpectedSessionState resultingState,
    std::vector<std::pair<SEQNUM, std::string>> sourceMessages,
    std::vector<InfinitePlannedCallback> callbacks,
    std::vector<InfinitePlannedEffect> effects,
    std::size_t operationCount)
    : messageType(std::move(type)),
      now(now),
      sequenceDisposition(sequenceDisposition),
      failure(std::move(failureReason)),
      resultingState(std::move(resultingState)),
      sourceMessages(std::move(sourceMessages)),
      callbacks(std::move(callbacks)),
      effects(std::move(effects)),
      operationCount(operationCount) {}

InfiniteActionPlan::InfiniteActionPlan(InfiniteActionPlan &&other) noexcept
    : messageType(std::move(other.messageType)),
      now(other.now),
      sequenceDisposition(other.sequenceDisposition),
      failure(std::move(other.failure)),
      resultingState(std::move(other.resultingState)),
      sourceMessages(std::move(other.sourceMessages)),
      callbacks(std::move(other.callbacks)),
      effects(std::move(other.effects)),
      operationCount(other.operationCount),
      sourceRangeRead(other.sourceRangeRead),
      sourceRangeBegin(other.sourceRangeBegin),
      sourceRangeEnd(other.sourceRangeEnd) {}

InfiniteActionPlan &InfiniteActionPlan::operator=(const InfiniteActionPlan &other) {
  if (this != &other) {
    auto replacement = other;
    *this = std::move(replacement);
  }
  return *this;
}

InfiniteActionPlan &InfiniteActionPlan::operator=(InfiniteActionPlan &&other) noexcept {
  if (this != &other) {
    Session::cleanseInfiniteActionPlan(*this);
    messageType = std::move(other.messageType);
    now = other.now;
    sequenceDisposition = other.sequenceDisposition;
    failure = std::move(other.failure);
    resultingState = std::move(other.resultingState);
    sourceMessages = std::move(other.sourceMessages);
    callbacks = std::move(other.callbacks);
    effects = std::move(other.effects);
    operationCount = other.operationCount;
    sourceRangeRead = other.sourceRangeRead;
    sourceRangeBegin = other.sourceRangeBegin;
    sourceRangeEnd = other.sourceRangeEnd;
  }
  return *this;
}

InfiniteActionPlan::~InfiniteActionPlan() { Session::cleanseInfiniteActionPlan(*this); }

InfiniteEffectAuthorization::InfiniteEffectAuthorization(
    std::array<std::uint8_t, 32> binding,
    InfiniteExpectedSessionState expected,
    InfiniteActionData actionData)
    : m_binding(std::move(binding)),
      m_expected(std::move(expected)),
      m_action(infiniteActionKind(actionData)),
      m_actionData(std::move(actionData)) {}

InfiniteEffectAuthorization::InfiniteEffectAuthorization(InfiniteEffectAuthorization &&other) noexcept
    : m_binding(std::move(other.m_binding)),
      m_expected(std::move(other.m_expected)),
      m_action(other.m_action),
      m_actionData(std::move(other.m_actionData)),
      m_consumed(other.m_consumed) {}

InfiniteEffectAuthorization &InfiniteEffectAuthorization::operator=(InfiniteEffectAuthorization &&other) noexcept {
  if (this != &other) {
    Session::cleanseInfiniteExpectedState(m_expected);
    m_binding = std::move(other.m_binding);
    m_expected = std::move(other.m_expected);
    m_action = other.m_action;
    m_actionData = std::move(other.m_actionData);
    m_consumed = other.m_consumed;
  }
  return *this;
}

InfiniteEffectAuthorization::~InfiniteEffectAuthorization() { Session::cleanseInfiniteExpectedState(m_expected); }

InfiniteSessionClassification::InfiniteSessionClassification(
    std::array<std::uint8_t, 32> binding,
    InfiniteExpectedSessionState expected,
    InfiniteActionData actionData,
    Message message)
    : m_binding(std::move(binding)),
      m_expected(std::move(expected)),
      m_action(infiniteActionKind(actionData)),
      m_actionData(std::move(actionData)),
      m_message(std::move(message)) {}

InfiniteSessionClassification::InfiniteSessionClassification(InfiniteSessionClassification &&other) noexcept
    : m_binding(std::move(other.m_binding)),
      m_expected(std::move(other.m_expected)),
      m_action(other.m_action),
      m_actionData(std::move(other.m_actionData)),
      m_message(std::move(other.m_message)) {}

InfiniteSessionClassification &InfiniteSessionClassification::operator=(
    InfiniteSessionClassification &&other) noexcept {
  if (this != &other) {
    Session::cleanseInfiniteExpectedState(m_expected);
    Session::cleanseInfiniteMessage(m_message);
    m_binding = std::move(other.m_binding);
    m_expected = std::move(other.m_expected);
    m_action = other.m_action;
    m_actionData = std::move(other.m_actionData);
    m_message = std::move(other.m_message);
  }
  return *this;
}

InfiniteSessionClassification::~InfiniteSessionClassification() {
  Session::cleanseInfiniteExpectedState(m_expected);
  Session::cleanseInfiniteMessage(m_message);
}

bool InfinitePlannedCallback::operator==(const InfinitePlannedCallback &rhs) const {
  return order == rhs.order && kind == rhs.kind && bytes == rhs.bytes && observedState == rhs.observedState;
}

bool InfinitePlannedMessage::operator==(const InfinitePlannedMessage &rhs) const {
  return sequence == rhs.sequence && bytes == rhs.bytes;
}

bool InfinitePlannedEffect::operator==(const InfinitePlannedEffect &rhs) const {
  return order == rhs.order && kind == rhs.kind && sequence == rhs.sequence && bytes == rhs.bytes
         && timestamp == rhs.timestamp;
}

bool InfiniteSessionStateFingerprint::operator==(const InfiniteSessionStateFingerprint &rhs) const {
  return enabled == rhs.enabled && receivedLogon == rhs.receivedLogon && sentLogout == rhs.sentLogout
         && sentLogon == rhs.sentLogon && sentReset == rhs.sentReset && receivedReset == rhs.receivedReset
         && initiate == rhs.initiate && logonTimeout == rhs.logonTimeout && logoutTimeout == rhs.logoutTimeout
         && testRequest == rhs.testRequest && resendBegin == rhs.resendBegin && resendEnd == rhs.resendEnd
         && heartBtInt == rhs.heartBtInt && lastSentTime == rhs.lastSentTime && lastReceivedTime == rhs.lastReceivedTime
         && storeCreationTime == rhs.storeCreationTime && sessionTimeActive == rhs.sessionTimeActive
         && logonTimeActive == rhs.logonTimeActive && infiniteFenced == rhs.infiniteFenced
         && logoutReason == rhs.logoutReason
         && ((!queuedMessages && !rhs.queuedMessages)
             || (queuedMessages && rhs.queuedMessages && *queuedMessages == *rhs.queuedMessages))
         && senderDefaultApplVerID == rhs.senderDefaultApplVerID && targetDefaultApplVerID == rhs.targetDefaultApplVerID
         && sendRedundantResendRequests == rhs.sendRedundantResendRequests && checkCompId == rhs.checkCompId
         && checkLatency == rhs.checkLatency && maxLatency == rhs.maxLatency && resetOnLogon == rhs.resetOnLogon
         && resetOnLogout == rhs.resetOnLogout && resetOnDisconnect == rhs.resetOnDisconnect
         && refreshOnLogon == rhs.refreshOnLogon && timestampPrecision == rhs.timestampPrecision
         && persistMessages == rhs.persistMessages && validateLengthAndChecksum == rhs.validateLengthAndChecksum
         && sendNextExpectedMsgSeqNum == rhs.sendNextExpectedMsgSeqNum && nonStopSession == rhs.nonStopSession
         && responderGeneration == rhs.responderGeneration;
}

bool InfiniteExpectedSessionState::operator==(const InfiniteExpectedSessionState &rhs) const {
  return sessionIdentity == rhs.sessionIdentity && revision == rhs.revision
         && configurationRevision == rhs.configurationRevision && senderSequence == rhs.senderSequence
         && targetSequence == rhs.targetSequence && loggedOn == rhs.loggedOn && mutableState == rhs.mutableState;
}

bool InfiniteActionPlan::operator==(const InfiniteActionPlan &rhs) const {
  return messageType == rhs.messageType && now == rhs.now && sequenceDisposition == rhs.sequenceDisposition
         && failure == rhs.failure && resultingState == rhs.resultingState && sourceMessages == rhs.sourceMessages
         && callbacks == rhs.callbacks && effects == rhs.effects && operationCount == rhs.operationCount
         && sourceRangeRead == rhs.sourceRangeRead && sourceRangeBegin == rhs.sourceRangeBegin
         && sourceRangeEnd == rhs.sourceRangeEnd;
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

InfiniteExpectedSessionState Session::currentInfiniteExpectedState(
    const UtcTimeStamp &now,
    const InfiniteExpectedSessionState *reusableQueueState) const {
  Locker sessionLock(m_mutex);
  Locker lock(m_state.m_mutex);
  const auto senderSequence = m_state.m_pStore->getNextSenderMsgSeqNum();
  const auto targetSequence = m_state.m_pStore->getNextTargetMsgSeqNum();
  const auto storeCreationTime = m_state.m_pStore->getCreationTime();

  if (m_state.m_queue.size() > INFINITE_MAX_PLANNED_MESSAGES) {
    throw std::length_error("Infinite queued message count exceeds bound");
  }

  std::shared_ptr<const InfinitePlannedMessages> queue;
  const auto &reusableQueue = reusableQueueState ? reusableQueueState->mutableState.queuedMessages : nullptr;
  // Private planning suppresses queue release and insertion; only reset can change it, which changes its size.
  if (m_infinitePlan && reusableQueue && reusableQueue->size() == m_state.m_queue.size()) {
    queue = reusableQueue;
  } else {
    auto captured = std::make_shared<InfinitePlannedMessages>();
    captured->reserve(m_state.m_queue.size());
    std::size_t queuedBytes = 0;
    for (const auto &entry : m_state.m_queue) {
      auto bytes = entry.second.toString();
      auto bytesGuard = sg::make_scope_guard([&bytes]() { cleanse(bytes); });
      if (containsLogonCredentialField(bytes)) {
        throw std::invalid_argument("Infinite queued state contains credential-bearing Logon");
      }
      if (bytes.size() > INFINITE_MAX_PLANNED_BYTES - queuedBytes) {
        throw std::length_error("Infinite queued message bytes exceed bound");
      }
      queuedBytes += bytes.size();
      captured->push_back(InfinitePlannedMessage{entry.first, bytes, entry.second});
    }
    queue = std::move(captured);
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
      m_infiniteResponderGeneration};

  return InfiniteExpectedSessionState{
      m_infiniteSessionIdentity,
      m_infiniteSessionRevision,
      m_infiniteConfigurationRevision,
      senderSequence,
      targetSequence,
      m_state.m_receivedLogon && m_state.m_sentLogon,
      std::move(fingerprint)};
}

InfiniteSessionClassification Session::classifyInfiniteFrame(
    const InfiniteAtHeadBinding &atHead,
    InfiniteSensitiveString &&bytes,
    const UtcTimeStamp &now) const {
  auto cleanseGuard = sg::make_scope_guard([&bytes]() { cleanse(bytes); });
  Locker sessionLock(m_mutex);
  ensureInfiniteCallbackNotReentrant();
  InfiniteExpectedSessionState expected{};
  InfiniteActionPlan plan{"", now, InfiniteSequenceDisposition::Unavailable, "", expected, {}, {}, {}, 0};
  Message message;
  InfiniteSessionActionKind action = InfiniteSessionActionKind::Failure;

  try {
    if (bytes.size() > 65536) {
      throw std::length_error("Infinite FIX frame exceeds bound");
    }
    if (containsLogonCredentialField(bytes)) {
      throw std::invalid_argument("Infinite FIX classification does not accept credential fields");
    }
    cleanseInfiniteExpectedState(expected);
    expected = currentInfiniteExpectedState(now);
    cleanseInfiniteExpectedState(plan.resultingState);
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
      auto headerGuard = sg::make_scope_guard([&headerOnly]() { cleanseInfiniteMessage(headerOnly); });
      headerOnly.setStringHeader(bytes);
      ApplVerID applVerID = m_targetDefaultApplVerID;
      headerOnly.getHeader().getFieldIfSet(applVerID);
      const auto &applicationVersion = applVerID.getValue();
      if (applicationVersion != ApplVerID_FIX40 && applicationVersion != ApplVerID_FIX41
          && applicationVersion != ApplVerID_FIX42 && applicationVersion != ApplVerID_FIX43
          && applicationVersion != ApplVerID_FIX44 && applicationVersion != ApplVerID_FIX50
          && applicationVersion != ApplVerID_FIX50_SP1 && applicationVersion != ApplVerID_FIX50_SP2) {
        throw std::invalid_argument("Infinite FIXT frame uses an unsupported application dictionary");
      }
      const DataDictionary &applicationDictionary = m_dataDictionaryProvider.getApplicationDataDictionary(applVerID);
      message.setString(bytes, m_validateLengthAndChecksum, &sessionDictionary, &applicationDictionary);
    } else {
      message.setString(bytes, m_validateLengthAndChecksum, &sessionDictionary);
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
    plan.operationCount = 0;
    cleanse(plan.failure);
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
  RecordingMessageStore recordingStore(expected, *originalStore, plan.sourceMessages, plan, now);
  RecordingLog recordingLog(plan, now);
  RecordingResponder recordingResponder(plan, now);
  Log *const originalLog = session.m_state.m_pLog;
  Responder *const originalResponder = session.m_pResponder;
  auto originalHeartBtInt = session.m_state.m_heartBtInt;
  auto originalLogoutReason = session.m_state.m_logoutReason;
  decltype(session.m_state.m_queue) originalQueue;
  auto originalQueueGuard = sg::make_scope_guard([&originalQueue]() noexcept {
    for (auto &entry : originalQueue) {
      Session::cleanseInfiniteMessage(entry.second);
    }
  });
  for (const auto &entry : session.m_state.m_queue) {
    originalQueue[entry.first] = entry.second;
  }
  auto originalTimestamper = session.m_timestamper;
  auto originalTargetDefaultApplVerID = session.m_targetDefaultApplVerID;
  auto sensitiveStateGuard = sg::make_scope_guard([&]() noexcept {
    cleanse(originalLogoutReason);
    cleanse(originalTargetDefaultApplVerID);
  });
  const auto originalConfigurationRevision = session.m_infiniteConfigurationRevision;
  const auto originalResponderGeneration = session.m_infiniteResponderGeneration;

  bool restored = false;
  const auto restore = [&]() noexcept {
    if (std::exchange(restored, true)) {
      return;
    }
    const auto &state = expected.mutableState;
    session.m_infinitePlan = nullptr;
    session.m_infiniteAction = nullptr;
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
    session.m_state.m_heartBtInt.swap(originalHeartBtInt);
    session.m_state.m_lastSentTime = state.lastSentTime;
    session.m_state.m_lastReceivedTime = state.lastReceivedTime;
    cleanse(session.m_state.m_logoutReason);
    session.m_state.m_logoutReason.swap(originalLogoutReason);
    for (auto &entry : session.m_state.m_queue) {
      cleanseInfiniteMessage(entry.second);
    }
    session.m_state.m_queue.swap(originalQueue);
    session.m_state.m_pStore = originalStore;
    session.m_state.m_pLog = originalLog;
    session.m_pResponder = originalResponder;
    session.m_timestamper.swap(originalTimestamper);
    cleanse(session.m_targetDefaultApplVerID);
    session.m_targetDefaultApplVerID.swap(originalTargetDefaultApplVerID);
    session.m_infiniteConfigurationRevision = originalConfigurationRevision;
    session.m_infiniteResponderGeneration = originalResponderGeneration;
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

    cleanseInfiniteExpectedState(plan.resultingState);
    plan.resultingState = session.currentInfiniteExpectedState(
        now,
        plan.callbacks.empty() ? &expected : &plan.callbacks.back().observedState);
    plan.resultingState.revision = expected.revision + 1;
    restore();
    restoreGuard.dismiss();
  } catch (const std::exception &error) {
    restore();
    restoreGuard.dismiss();
    for (auto &source : plan.sourceMessages) {
      cleanse(source.second);
    }
    plan.sourceMessages.clear();
    plan.sourceRangeRead = false;
    plan.sourceRangeBegin = 0;
    plan.sourceRangeEnd = 0;
    plan.callbacks.clear();
    plan.effects.clear();
    plan.operationCount = 0;
    cleanse(plan.failure);
    plan.failure = error.what();
    action = InfiniteSessionActionKind::Failure;
  } catch (...) {
    restore();
    restoreGuard.dismiss();
    for (auto &source : plan.sourceMessages) {
      cleanse(source.second);
    }
    plan.sourceMessages.clear();
    plan.sourceRangeRead = false;
    plan.sourceRangeBegin = 0;
    plan.sourceRangeEnd = 0;
    plan.callbacks.clear();
    plan.effects.clear();
    plan.operationCount = 0;
    cleanse(plan.failure);
    plan.failure = "Non-standard exception during Infinite classification";
    action = InfiniteSessionActionKind::Failure;
  }

  return InfiniteSessionClassification(
      atHead.value(),
      expected,
      makeActionData(action, std::move(plan)),
      std::move(message));
}

void Session::installInfiniteExpectedState(const InfiniteExpectedSessionState &expected, bool queueAlreadyInstalled) {
  const auto &state = expected.mutableState;
  if (m_infiniteSessionFenced && !state.infiniteFenced) {
    throw IOException("Infinite session fence is monotonic");
  }
  if (expected.sessionIdentity != m_infiniteSessionIdentity
      || m_state.m_pStore->getNextSenderMsgSeqNum() != expected.senderSequence
      || m_state.m_pStore->getNextTargetMsgSeqNum() != expected.targetSequence
      || m_state.m_pStore->getCreationTime() != state.storeCreationTime
      || m_infiniteResponderGeneration != state.responderGeneration
      || static_cast<bool>(m_pResponder) != (state.responderGeneration != 0)) {
    throw IOException("Infinite ordered state transition mismatch");
  }

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
  cleanse(m_state.m_logoutReason);
  m_state.m_logoutReason = state.logoutReason;
  if (!queueAlreadyInstalled) {
    for (auto &entry : m_state.m_queue) {
      cleanseInfiniteMessage(entry.second);
    }
    m_state.m_queue.clear();
    if (state.queuedMessages) {
      for (const auto &entry : *state.queuedMessages) {
        m_state.m_queue.emplace(entry.sequence, entry.message);
      }
    }
  }

  cleanse(m_senderDefaultApplVerID);
  m_senderDefaultApplVerID = state.senderDefaultApplVerID;
  cleanse(m_targetDefaultApplVerID);
  m_targetDefaultApplVerID = state.targetDefaultApplVerID;
  m_sendRedundantResendRequests = state.sendRedundantResendRequests;
  m_checkCompId = state.checkCompId;
  m_checkLatency = state.checkLatency;
  m_maxLatency = state.maxLatency;
  m_resetOnLogon = state.resetOnLogon;
  m_resetOnLogout = state.resetOnLogout;
  m_resetOnDisconnect = state.resetOnDisconnect;
  m_refreshOnLogon = state.refreshOnLogon;
  m_timestampPrecision = state.timestampPrecision;
  m_persistMessages = state.persistMessages;
  m_validateLengthAndChecksum = state.validateLengthAndChecksum;
  m_sendNextExpectedMsgSeqNum = state.sendNextExpectedMsgSeqNum;
  m_isNonStopSession = state.nonStopSession;
  m_infiniteSessionFenced = m_infiniteSessionFenced || state.infiniteFenced;
  m_infiniteSessionRevision = expected.revision;
  m_infiniteConfigurationRevision = expected.configurationRevision;
}

void Session::applyInfiniteClassification(
    const InfiniteSessionClassification &classification,
    InfiniteEffectAuthorization &&authorization) {
  const auto &plan = infiniteActionPlan(classification.m_actionData);
  Locker sessionLock(m_mutex);
  auto initialNow = plan.now;
  try {
    initialNow = m_timestamper();
  } catch (...) {
    m_infiniteSessionFenced = true;
    throw;
  }
  Locker stateLock(m_state.m_mutex);
  if (m_state.m_infiniteCallbackActive.load(std::memory_order_acquire)) {
    m_infiniteSessionFenced = true;
    throw IOException("Infinite classification application is already in a callback");
  }
  if (authorization.m_consumed || authorization.m_binding != classification.m_binding
      || !(authorization.m_expected == classification.m_expected) || authorization.m_action != classification.m_action
      || !(authorization.m_actionData == classification.m_actionData)) {
    m_infiniteSessionFenced = true;
    return;
  }
  authorization.m_consumed = true;
  if (classification.m_action == InfiniteSessionActionKind::Failure) {
    m_infiniteSessionFenced = true;
    return;
  }

  try {
    if (!(currentInfiniteExpectedState(initialNow) == classification.m_expected)
        || m_infiniteSessionRevision == std::numeric_limits<std::uint64_t>::max()) {
      m_infiniteSessionFenced = true;
      return;
    }

    const auto *const revisionStore = dynamic_cast<const InfiniteMessageStoreRevision *>(m_state.m_pStore);
    const auto sourceRevision = revisionStore ? revisionStore->infiniteContentRevision() : 0;
    if (!sourceMessagesMatch(*m_state.m_pStore, plan)
        || (revisionStore && revisionStore->infiniteContentRevision() != sourceRevision)) {
      throw IOException("Infinite source message changed before application");
    }

    const InfinitePlannedMessages *installedQueue = classification.m_expected.mutableState.queuedMessages.get();
    const auto installExpectedState = [&](const InfiniteExpectedSessionState &expected) {
      const auto *const expectedQueue = expected.mutableState.queuedMessages.get();
      installInfiniteExpectedState(expected, installedQueue == expectedQueue);
      installedQueue = expectedQueue;
    };

    const auto invokeCallback = [&](const InfinitePlannedCallback &callback) {
      installExpectedState(callback.observedState);
      const auto callbackSourceRevision = revisionStore ? revisionStore->infiniteContentRevision() : 0;
      m_state.m_infiniteCallbackActive.store(true, std::memory_order_release);
      auto callbackGuard = sg::make_scope_guard(
          [this]() { m_state.m_infiniteCallbackActive.store(false, std::memory_order_release); });
      auto callbackNow = initialNow;
      {
        ReverseLocker stateUnlock(m_state.m_mutex);
        ReverseLocker sessionUnlock(m_mutex);
        if (callback.kind == InfiniteCallbackKind::FromAdmin) {
          m_application.fromAdmin(callback.message, m_sessionID);
        } else if (callback.kind == InfiniteCallbackKind::FromApplication) {
          m_application.fromApp(callback.message, m_sessionID);
        } else if (callback.kind == InfiniteCallbackKind::ToAdmin) {
          Message outgoing = callback.message;
          auto outgoingGuard = sg::make_scope_guard([&outgoing]() { cleanseInfiniteMessage(outgoing); });
          m_application.toAdmin(outgoing, m_sessionID);
          auto actual = outgoing.toString();
          auto actualGuard = sg::make_scope_guard([&actual]() { cleanse(actual); });
          if (actual != callback.bytes) {
            throw IOException("Infinite toAdmin callback changed the authorized bytes");
          }
        } else if (callback.kind == InfiniteCallbackKind::ToApplication) {
          Message outgoing = callback.message;
          auto outgoingGuard = sg::make_scope_guard([&outgoing]() { cleanseInfiniteMessage(outgoing); });
          try {
            m_application.toApp(outgoing, m_sessionID);
          } catch (DoNotSend &) {
            throw IOException("Infinite toApp callback refused the authorized bytes");
          }
          auto actual = outgoing.toString();
          auto actualGuard = sg::make_scope_guard([&actual]() { cleanse(actual); });
          if (actual != callback.bytes) {
            throw IOException("Infinite toApp callback changed the authorized bytes");
          }
        } else if (callback.kind == InfiniteCallbackKind::Logon) {
          m_application.onLogon(m_sessionID);
        } else if (callback.kind == InfiniteCallbackKind::Logout) {
          m_application.onLogout(m_sessionID);
        }
        callbackNow = m_timestamper();
      }
      const auto sourceMessagesUnchanged = revisionStore
                                               ? revisionStore->infiniteContentRevision() == callbackSourceRevision
                                               : sourceMessagesMatch(*m_state.m_pStore, plan);
      if (!(currentInfiniteExpectedState(callbackNow) == callback.observedState) || !sourceMessagesUnchanged) {
        throw IOException("Infinite session changed during callback");
      }
    };

    const auto applyEffect = [&](const InfinitePlannedEffect &effect) {
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
          m_infiniteResponderGeneration = 0;
        }
        break;
      }
    };

    std::size_t callbackIndex = 0;
    std::size_t effectIndex = 0;
    for (std::size_t order = 0; order < plan.operationCount; ++order) {
      const bool callbackAtOrder
          = callbackIndex < plan.callbacks.size() && plan.callbacks[callbackIndex].order == order;
      const bool effectAtOrder = effectIndex < plan.effects.size() && plan.effects[effectIndex].order == order;
      if (callbackAtOrder == effectAtOrder) {
        throw IOException("Infinite effect plan operation order is invalid");
      }
      if (callbackAtOrder) {
        invokeCallback(plan.callbacks[callbackIndex++]);
      } else {
        applyEffect(plan.effects[effectIndex++]);
      }
    }
    if (callbackIndex != plan.callbacks.size() || effectIndex != plan.effects.size()) {
      throw IOException("Infinite effect plan operation count is invalid");
    }
    installExpectedState(plan.resultingState);
  } catch (...) {
    m_state.m_infiniteCallbackActive.store(false, std::memory_order_release);
    m_infiniteSessionFenced = true;
    throw;
  }
}

void Session::cleanseInfiniteMessageCredentials(Message &message) noexcept {
  cleanseInfiniteFieldMapCredentials(message);
  cleanseInfiniteFieldMapCredentials(message.getHeader());
  cleanseInfiniteFieldMapCredentials(message.getTrailer());
}

void Session::cleanseInfiniteBytes(std::string &bytes) noexcept { cleanse(bytes); }

void Session::cleanseInfiniteMessage(Message &message) noexcept {
  cleanseInfiniteFieldMap(message);
  cleanseInfiniteFieldMap(message.getHeader());
  cleanseInfiniteFieldMap(message.getTrailer());
}

void Session::cleanseInfiniteFieldMap(FieldMap &fields) noexcept {
  for (auto &field : fields) {
    cleanse(field.m_string);
    cleanse(field.m_data);
    field.m_string.clear();
    field.m_data.clear();
    field.m_metrics = FieldBase::no_metrics();
  }
  for (const auto &groupSet : fields.groups()) {
    for (auto *group : groupSet.second) {
      if (group) {
        cleanseInfiniteFieldMap(*group);
      }
    }
  }
}

void Session::cleanseInfiniteExpectedState(InfiniteExpectedSessionState &expected) noexcept {
  auto &state = expected.mutableState;
  cleanse(state.logoutReason);
  cleanse(state.senderDefaultApplVerID);
  cleanse(state.targetDefaultApplVerID);
}

void Session::cleanseInfiniteActionPlan(InfiniteActionPlan &plan) noexcept {
  cleanse(plan.messageType);
  cleanse(plan.failure);
  cleanseInfiniteExpectedState(plan.resultingState);
  for (auto &source : plan.sourceMessages) {
    cleanse(source.second);
  }
  for (auto &callback : plan.callbacks) {
    cleanse(callback.bytes);
    cleanseInfiniteMessage(callback.message);
    cleanseInfiniteExpectedState(callback.observedState);
  }
  for (auto &effect : plan.effects) {
    cleanse(effect.bytes);
  }
}

void Session::cleanseInfiniteFieldMapCredentials(FieldMap &fields) noexcept {
  for (auto &field : fields) {
    if (field.getTag() != FIELD::Username && field.getTag() != FIELD::Password) {
      continue;
    }
    cleanse(field.m_string);
    cleanse(field.m_data);
    field.m_string.clear();
    field.m_data.clear();
    field.m_metrics = FieldBase::no_metrics();
  }
  for (const auto &groupSet : fields.groups()) {
    for (auto *group : groupSet.second) {
      if (group) {
        cleanseInfiniteFieldMapCredentials(*group);
      }
    }
  }
}
} // namespace FIX
