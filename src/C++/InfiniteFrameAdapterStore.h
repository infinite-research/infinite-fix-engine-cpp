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

#pragma once

#include "InfiniteSessionClassification.h"
#include "MessageStore.h"

#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace FIX {
namespace infinite_frame_adapter_detail {
namespace {
class BoundedMemoryStore : public MessageStore, public InfiniteMessageStoreRevision {
public:
  explicit BoundedMemoryStore(const UtcTimeStamp &now)
      : m_creationTime(now) {}

  ~BoundedMemoryStore() override { eraseMessages(); }

  bool set(SEQNUM sequence, const std::string &message) EXCEPT(IOException) override {
    const auto found = m_messages.find(sequence);
    const auto previousSize = found == m_messages.end() ? std::size_t{0} : found->second.size();
    const auto retainedBytes = m_storedBytes - previousSize;
    if ((found == m_messages.end() && m_messages.size() >= INFINITE_MAX_PLANNED_MESSAGES)
        || message.size() > INFINITE_MAX_PLANNED_BYTES - retainedBytes) {
      throw IOException("Infinite adapter message store bound exceeded");
    }
    if (m_contentRevision == std::numeric_limits<std::uint64_t>::max()) {
      throw IOException("Infinite adapter message store revision exhausted");
    }
    if (found == m_messages.end()) {
      m_messages.emplace(sequence, message);
    } else {
      auto replacement = message;
      erase(found->second);
      found->second.swap(replacement);
    }
    m_storedBytes = retainedBytes + message.size();
    ++m_contentRevision;
    return true;
  }

  void get(SEQNUM begin, SEQNUM end, std::vector<std::string> &messages) const EXCEPT(IOException) override {
    for (auto &message : messages) {
      erase(message);
    }
    messages.clear();
    messages.reserve(m_messages.size());
    auto found = m_messages.find(begin);
    for (; found != m_messages.end() && found->first <= end; ++found) {
      messages.push_back(found->second);
    }
  }

  SEQNUM getNextSenderMsgSeqNum() const EXCEPT(IOException) override { return m_nextSenderSequence; }
  SEQNUM getNextTargetMsgSeqNum() const EXCEPT(IOException) override { return m_nextTargetSequence; }
  void setNextSenderMsgSeqNum(SEQNUM value) EXCEPT(IOException) override { m_nextSenderSequence = value; }
  void setNextTargetMsgSeqNum(SEQNUM value) EXCEPT(IOException) override { m_nextTargetSequence = value; }
  void incrNextSenderMsgSeqNum() EXCEPT(IOException) override { ++m_nextSenderSequence; }
  void incrNextTargetMsgSeqNum() EXCEPT(IOException) override { ++m_nextTargetSequence; }
  UtcTimeStamp getCreationTime() const EXCEPT(IOException) override { return m_creationTime; }
  std::uint64_t infiniteContentRevision() const noexcept override { return m_contentRevision; }

  void reset(const UtcTimeStamp &now) EXCEPT(IOException) override {
    if (m_contentRevision == std::numeric_limits<std::uint64_t>::max()) {
      throw IOException("Infinite adapter message store revision exhausted");
    }
    eraseMessages();
    m_storedBytes = 0;
    m_nextSenderSequence = 1;
    m_nextTargetSequence = 1;
    m_creationTime = now;
    ++m_contentRevision;
  }

  void refresh() EXCEPT(IOException) override {}

private:
  static void erase(std::string &bytes) noexcept {
    volatile char *cursor = bytes.empty() ? nullptr : &bytes[0];
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      cursor[index] = 0;
    }
    bytes.clear();
  }

  void eraseMessages() noexcept {
    for (auto &entry : m_messages) {
      erase(entry.second);
    }
    m_messages.clear();
  }

  std::map<SEQNUM, std::string> m_messages;
  std::size_t m_storedBytes{0};
  std::uint64_t m_contentRevision{0};
  SEQNUM m_nextSenderSequence{1};
  SEQNUM m_nextTargetSequence{1};
  UtcTimeStamp m_creationTime;
};

class BoundedMemoryStoreFactory : public MessageStoreFactory {
public:
  MessageStore *create(const UtcTimeStamp &now, const SessionID &) override { return new BoundedMemoryStore(now); }

  void destroy(MessageStore *store) override { delete store; }
};
} // namespace
} // namespace infinite_frame_adapter_detail
} // namespace FIX
