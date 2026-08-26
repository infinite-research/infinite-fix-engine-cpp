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
#include <map>
#include <string>
#include <vector>

namespace FIX {
namespace infinite_frame_adapter_detail {
class BoundedMemoryStore : public MessageStore {
public:
  explicit BoundedMemoryStore(const UtcTimeStamp &now)
      : m_store(now) {}

  bool set(SEQNUM sequence, const std::string &message) EXCEPT(IOException) override {
    const auto found = m_messageSizes.find(sequence);
    const auto previousSize = found == m_messageSizes.end() ? std::size_t{0} : found->second;
    const auto retainedBytes = m_storedBytes - previousSize;
    if ((found == m_messageSizes.end() && m_messageSizes.size() >= INFINITE_MAX_PLANNED_MESSAGES)
        || message.size() > INFINITE_MAX_PLANNED_BYTES - retainedBytes) {
      throw IOException("Infinite adapter message store bound exceeded");
    }
    if (!m_store.set(sequence, message)) {
      return false;
    }
    m_messageSizes[sequence] = message.size();
    m_storedBytes = retainedBytes + message.size();
    return true;
  }

  void get(SEQNUM begin, SEQNUM end, std::vector<std::string> &messages) const EXCEPT(IOException) override {
    m_store.get(begin, end, messages);
  }

  SEQNUM getNextSenderMsgSeqNum() const EXCEPT(IOException) override { return m_store.getNextSenderMsgSeqNum(); }
  SEQNUM getNextTargetMsgSeqNum() const EXCEPT(IOException) override { return m_store.getNextTargetMsgSeqNum(); }
  void setNextSenderMsgSeqNum(SEQNUM value) EXCEPT(IOException) override { m_store.setNextSenderMsgSeqNum(value); }
  void setNextTargetMsgSeqNum(SEQNUM value) EXCEPT(IOException) override { m_store.setNextTargetMsgSeqNum(value); }
  void incrNextSenderMsgSeqNum() EXCEPT(IOException) override { m_store.incrNextSenderMsgSeqNum(); }
  void incrNextTargetMsgSeqNum() EXCEPT(IOException) override { m_store.incrNextTargetMsgSeqNum(); }
  UtcTimeStamp getCreationTime() const EXCEPT(IOException) override { return m_store.getCreationTime(); }

  void reset(const UtcTimeStamp &now) EXCEPT(IOException) override {
    m_store.reset(now);
    m_messageSizes.clear();
    m_storedBytes = 0;
  }

  void refresh() EXCEPT(IOException) override { m_store.refresh(); }

private:
  MemoryStore m_store;
  std::map<SEQNUM, std::size_t> m_messageSizes;
  std::size_t m_storedBytes{0};
};

class BoundedMemoryStoreFactory : public MessageStoreFactory {
public:
  MessageStore *create(const UtcTimeStamp &now, const SessionID &) override { return new BoundedMemoryStore(now); }

  void destroy(MessageStore *store) override { delete store; }
};
} // namespace infinite_frame_adapter_detail
} // namespace FIX
