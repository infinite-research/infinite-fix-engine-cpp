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

#ifndef FIX_SESSION_H
#define FIX_SESSION_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "Application.h"
#include "DataDictionaryProvider.h"
#include "Fields.h"
#include "Log.h"
#include "Mutex.h"
#include "Responder.h"
#include "SessionID.h"
#include "SessionState.h"
#include "TimeRange.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <stdexcept>
#include <utility>

namespace FIX {
struct InfiniteActionPlan;
class InfiniteAtHeadBinding;
class InfiniteEffectAuthorization;
struct InfiniteExpectedSessionState;
class InfiniteFrameAdapterAccess;
class InfiniteSessionClassification;
class InfiniteSessionClassificationTestAccess;
enum class InfiniteSessionActionKind : std::uint8_t;
enum class InfiniteCallbackKind : std::uint8_t;

/// Maintains the state and implements the logic of a %FIX %session.
class Session {
public:
  Session(
      std::function<UtcTimeStamp()> timestamper,
      Application &,
      MessageStoreFactory &,
      const SessionID &,
      const DataDictionaryProvider &,
      const TimeRange &,
      int heartBtInt,
      LogFactory *pLogFactory);
  virtual ~Session();

  void logon() {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    m_state.enabled(true);
    m_state.logoutReason("");
  }
  void logout(const std::string &reason = "") {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    m_state.enabled(false);
    m_state.logoutReason(reason);
  }
  bool isEnabled() { return m_state.enabled(); }

  bool sentLogon() { return m_state.sentLogon(); }
  bool sentLogout() { return m_state.sentLogout(); }
  bool receivedLogon() { return m_state.receivedLogon(); }
  bool isLoggedOn() { return m_state.receivedLogon() && m_state.sentLogon(); }
  void reset() EXCEPT(IOException) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    generateLogout();
    disconnect();
    m_state.reset(m_timestamper());
  }
  void refresh() EXCEPT(IOException) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    m_state.refresh();
  }
  void setNextSenderMsgSeqNum(SEQNUM num) EXCEPT(IOException) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    m_state.setNextSenderMsgSeqNum(num);
  }
  void setNextTargetMsgSeqNum(SEQNUM num) EXCEPT(IOException) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    m_state.setNextTargetMsgSeqNum(num);
  }

  const SessionID &getSessionID() const { return m_sessionID; }
  void setDataDictionaryProvider(const DataDictionaryProvider &dataDictionaryProvider) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_dataDictionaryProvider = dataDictionaryProvider.deepCopy();
  }
  const DataDictionaryProvider &getDataDictionaryProvider() const { return m_dataDictionaryProvider; }

  static bool sendToTarget(Message &message, const std::string &qualifier = "") EXCEPT(SessionNotFound);
  static bool sendToTarget(Message &message, const SessionID &sessionID) EXCEPT(SessionNotFound);
  static bool sendToTarget(
      Message &,
      const SenderCompID &senderCompID,
      const TargetCompID &targetCompID,
      const std::string &qualifier = "") EXCEPT(SessionNotFound);
  static bool sendToTarget(
      Message &message,
      const std::string &senderCompID,
      const std::string &targetCompID,
      const std::string &qualifier = "") EXCEPT(SessionNotFound);

  static std::set<SessionID> getSessions();
  static bool doesSessionExist(const SessionID &);
  static Session *lookupSession(const SessionID &);
  static Session *lookupSession(const std::string &, bool reverse = false);
  static bool isSessionRegistered(const SessionID &);
  static Session *registerSession(const SessionID &);
  static void unregisterSession(const SessionID &);

  static size_t numSessions();

  bool isSessionTime(const UtcTimeStamp &now) { return m_sessionTime.isInRange(now); }
  bool isLogonTime(const UtcTimeStamp &now) { return m_logonTime.isInRange(now); }
  bool isInitiator() { return m_state.initiate(); }
  bool isAcceptor() { return !m_state.initiate(); }

  const TimeRange &getLogonTime() { return m_logonTime; }
  void setLogonTime(const TimeRange &value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_logonTime = value;
  }

  const std::string &getSenderDefaultApplVerID() { return m_senderDefaultApplVerID; }
  void setSenderDefaultApplVerID(const std::string &senderDefaultApplVerID) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_senderDefaultApplVerID = senderDefaultApplVerID;
  }

  const std::string &getTargetDefaultApplVerID() { return m_targetDefaultApplVerID; }
  void setTargetDefaultApplVerID(const std::string &targetDefaultApplVerID) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_targetDefaultApplVerID = targetDefaultApplVerID;
  }

  bool getSendRedundantResendRequests() { return m_sendRedundantResendRequests; }
  void setSendRedundantResendRequests(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_sendRedundantResendRequests = value;
  }

  bool getCheckCompId() { return m_checkCompId; }
  void setCheckCompId(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_checkCompId = value;
  }

  bool getCheckLatency() { return m_checkLatency; }
  void setCheckLatency(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_checkLatency = value;
  }

  int getMaxLatency() { return m_maxLatency; }
  void setMaxLatency(int value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_maxLatency = value;
  }

  int getLogonTimeout() { return m_state.logonTimeout(); }
  void setLogonTimeout(int value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_state.logonTimeout(value);
  }

  int getLogoutTimeout() { return m_state.logoutTimeout(); }
  void setLogoutTimeout(int value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_state.logoutTimeout(value);
  }

  bool getResetOnLogon() { return m_resetOnLogon; }
  void setResetOnLogon(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_resetOnLogon = value;
  }

  bool getResetOnLogout() { return m_resetOnLogout; }
  void setResetOnLogout(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_resetOnLogout = value;
  }

  bool getResetOnDisconnect() { return m_resetOnDisconnect; }
  void setResetOnDisconnect(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_resetOnDisconnect = value;
  }

  bool getRefreshOnLogon() { return m_refreshOnLogon; }
  void setRefreshOnLogon(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_refreshOnLogon = value;
  }

  bool getMillisecondsInTimeStamp() { return (m_timestampPrecision == 3); }
  void setMillisecondsInTimeStamp(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    if (value) {
      m_timestampPrecision = 3;
    } else {
      m_timestampPrecision = 0;
    }
  }
  int getTimestampPrecision() { return m_timestampPrecision; }
  void setTimestampPrecision(int precision) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    if (precision < 0 || precision > 9) {
      return;
    }

    advanceInfiniteConfigurationRevision();
    m_timestampPrecision = precision;
  }
  int getSupportedTimestampPrecision() {
    return supportsSubSecondTimestamps(m_sessionID.getBeginString()) ? m_timestampPrecision : 0;
  }
  static bool supportsSubSecondTimestamps(const std::string &beginString) {
    if (beginString == BeginString_FIXT11) {
      return true;
    } else {
      return beginString >= BeginString_FIX42;
    }
  }

  bool getPersistMessages() { return m_persistMessages; }
  void setPersistMessages(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_persistMessages = value;
  }

  bool getValidateLengthAndChecksum() { return m_validateLengthAndChecksum; }
  void setValidateLengthAndChecksum(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_validateLengthAndChecksum = value;
  }

  bool getSendNextExpectedMsgSeqNum() { return m_sendNextExpectedMsgSeqNum; }
  void setSendNextExpectedMsgSeqNum(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_sendNextExpectedMsgSeqNum = value;
  }

  bool getIsNonStopSession() const { return m_isNonStopSession; }
  void setIsNonStopSession(bool value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    advanceInfiniteConfigurationRevision();
    m_isNonStopSession = value;
  }

  const std::set<std::string> &getAllowedRemoteAddresses() const { return m_allowedRemoteAddresses; }
  void setAllowedRemoteAddresses(const std::set<std::string> &value) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    m_allowedRemoteAddresses = value;
  }
  bool inAllowedRemoteAddresses(const std::string &value) const {
    return (m_allowedRemoteAddresses.cend() != m_allowedRemoteAddresses.find(value));
  }

  void setResponder(Responder *pR) {
    Locker l(m_mutex);
    ensureInfiniteCallbackNotReentrant();
    if (m_refreshOnLogon) {
      refresh();
    }
    if (!checkSessionTime(m_timestamper())) {
      reset();
    }
    const auto generation = pR ? nextInfiniteGeneration() : 0;
    m_pResponder = pR;
    m_infiniteResponderGeneration = generation;
  }

  bool send(Message &);
  void next(const UtcTimeStamp &now);
  void next(const std::string &, const UtcTimeStamp &now, bool queued = false);
  void next(const Message &, const UtcTimeStamp &now, bool queued = false);
  void disconnect();

  SEQNUM getExpectedSenderNum() { return m_state.getNextSenderMsgSeqNum(); }
  SEQNUM getExpectedTargetNum() { return m_state.getNextTargetMsgSeqNum(); }

  Log *getLog() { return &m_state; }
  const MessageStore *getStore() { return &m_state; }

private:
  friend class InfiniteFrameAdapterAccess;
  friend class InfiniteEffectAuthorization;
  friend class InfiniteSessionClassificationTestAccess;
  friend class InfiniteSessionClassification;
  friend struct InfiniteActionPlan;
  friend struct InfinitePlannedCallback;
  friend struct InfinitePlannedMessage;
  typedef std::map<SessionID, Session *> Sessions;
  typedef std::set<SessionID> SessionIDs;

  static bool addSession(Session &);
  static void removeSession(Session &);

  bool send(const std::string &);
  bool sendRaw(Message &, SEQNUM msgSeqNum = 0);
  bool resend(Message &message);
  void persist(const Message &, const std::string &) EXCEPT(IOException);

  void insertSendingTime(Header &);
  void insertOrigSendingTime(Header &, const UtcTimeStamp &now);
  void fill(Header &);

  bool isGoodTime(const SendingTime &sendingTime) {
    if (!m_checkLatency) {
      return true;
    }
    return labs(m_timestamper() - sendingTime) <= m_maxLatency;
  }
  bool checkSessionTime(const UtcTimeStamp &now) {
    if (getIsNonStopSession()) {
      return true;
    }
    UtcTimeStamp creationTime = m_state.getCreationTime();
    return m_sessionTime.isInSameRange(now, creationTime);
  }
  bool isTargetTooHigh(const MsgSeqNum &msgSeqNum) { return msgSeqNum > (m_state.getNextTargetMsgSeqNum()); }
  bool isTargetTooLow(const MsgSeqNum &msgSeqNum) { return msgSeqNum < (m_state.getNextTargetMsgSeqNum()); }
  bool isCorrectCompID(const SenderCompID &senderCompID, const TargetCompID &targetCompID) {
    if (!m_checkCompId) {
      return true;
    }

    return m_sessionID.getSenderCompID().getValue() == targetCompID.getValue()
           && m_sessionID.getTargetCompID().getValue() == senderCompID.getValue();
  }
  bool shouldSendReset();

  bool validLogonState(const MsgType &msgType);
  void fromCallback(const MsgType &msgType, const Message &msg, const SessionID &sessionID);

  void doBadTime(const Message &msg);
  void doBadCompID(const Message &msg);
  bool doPossDup(const Message &msg);
  bool doTargetTooLow(const Message &msg);
  void doTargetTooHigh(const Message &msg);
  void nextQueued(const UtcTimeStamp &now);
  bool nextQueued(SEQNUM num, const UtcTimeStamp &now);

  void nextLogon(const Message &, const UtcTimeStamp &now, bool releaseQueued = true);
  void nextHeartbeat(const Message &, const UtcTimeStamp &now, bool releaseQueued = true);
  void nextTestRequest(const Message &, const UtcTimeStamp &now, bool releaseQueued = true);
  void nextLogout(const Message &, const UtcTimeStamp &now);
  void nextReject(const Message &, const UtcTimeStamp &now, bool releaseQueued = true);
  void nextSequenceReset(const Message &, const UtcTimeStamp &now, bool releaseQueued = true);
  void nextResendRequest(const Message &, const UtcTimeStamp &now);

  void generateLogon();
  void generateLogon(const Message &);
  void generateResendRequest(const BeginString &, const MsgSeqNum &);
  void generateSequenceReset(SEQNUM, SEQNUM);
  void generateRetransmits(SEQNUM beginSeqNo, SEQNUM endSeqNo);
  void generateHeartbeat();
  void generateHeartbeat(const Message &);
  void generateTestRequest(const std::string &);
  void generateReject(const Message &, int err, int field = 0);
  void generateReject(const Message &, const std::string &text);
  void generateBusinessReject(const Message &, int err, int field = 0);
  void generateLogout(const std::string &text = "");

  void populateRejectReason(Message &, int field, const std::string &text);
  void populateRejectReason(Message &, const std::string &text);

  bool verify(const Message &msg, bool checkTooHigh = true, bool checkTooLow = true);

  Message newMessage(const MsgType &msgType) const;

  void ensureInfiniteCallbackNotReentrant() const {
    if (m_state.m_infiniteCallbackActive.load(std::memory_order_acquire)) {
      throw IOException("Infinite callback cannot re-enter its Session");
    }
  }

  static std::uint64_t nextInfiniteGeneration();
  void advanceInfiniteConfigurationRevision();
  void recordInfiniteCallback(InfiniteCallbackKind, const std::string &, const Message & = Message());
  static void cleanseInfiniteMessageCredentials(Message &) noexcept;
  static void cleanseInfiniteBytes(std::string &) noexcept;
  static void cleanseInfiniteFieldMapCredentials(FieldMap &) noexcept;
  static void cleanseInfiniteMessage(Message &) noexcept;
  static void cleanseInfiniteFieldMap(FieldMap &) noexcept;
  static void cleanseInfiniteExpectedState(InfiniteExpectedSessionState &) noexcept;
  static void cleanseInfiniteActionPlan(InfiniteActionPlan &) noexcept;
  void installInfiniteExpectedState(const InfiniteExpectedSessionState &, bool queueAlreadyInstalled = false);

  InfiniteExpectedSessionState currentInfiniteExpectedState(
      const UtcTimeStamp &,
      const InfiniteExpectedSessionState *reusableQueueState = nullptr) const;
  InfiniteSessionClassification classifyInfiniteFrame(
      const InfiniteAtHeadBinding &,
      std::string &&,
      const UtcTimeStamp &) const;
  void applyInfiniteClassification(const InfiniteSessionClassification &, InfiniteEffectAuthorization &&);

  std::function<UtcTimeStamp()> m_timestamper;
  Application &m_application;
  SessionID m_sessionID;
  TimeRange m_sessionTime;
  TimeRange m_logonTime;

  std::string m_senderDefaultApplVerID;
  std::string m_targetDefaultApplVerID;
  bool m_sendRedundantResendRequests;
  bool m_checkCompId;
  bool m_checkLatency;
  int m_maxLatency;
  bool m_resetOnLogon;
  bool m_resetOnLogout;
  bool m_resetOnDisconnect;
  bool m_refreshOnLogon;
  int m_timestampPrecision;
  bool m_persistMessages;
  bool m_validateLengthAndChecksum;
  bool m_sendNextExpectedMsgSeqNum;
  bool m_isNonStopSession;
  std::set<std::string> m_allowedRemoteAddresses;

  SessionState m_state;
  DataDictionaryProvider m_dataDictionaryProvider;
  MessageStoreFactory &m_messageStoreFactory;
  LogFactory *m_pLogFactory;
  Responder *m_pResponder;
  mutable Mutex m_mutex;
  const std::uint64_t m_infiniteSessionIdentity;
  std::uint64_t m_infiniteSessionRevision{0};
  std::uint64_t m_infiniteConfigurationRevision{0};
  std::uint64_t m_infiniteResponderGeneration{0};
  InfiniteActionPlan *m_infinitePlan{nullptr};
  InfiniteSessionActionKind *m_infiniteAction{nullptr};
  bool m_infiniteSessionFenced{false};

  static Sessions s_sessions;
  static SessionIDs s_sessionIDs;
  static Sessions s_registered;
  static Mutex s_mutex;
};
} // namespace FIX

#endif // FIX_SESSION_H
