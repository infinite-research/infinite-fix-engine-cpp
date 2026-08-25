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

#include "Parser.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace FIX {
struct InfiniteCompleteFrame {
  std::string bytes;
  std::int64_t observedTaiNs;

  bool operator==(const InfiniteCompleteFrame &other) const {
    return bytes == other.bytes && observedTaiNs == other.observedTaiNs;
  }
};

struct InfiniteFrameBatch {
  std::size_t maxFrames;
  std::size_t maxBytes;
};

enum class InfiniteDispatchFault : std::uint8_t {
  FrameTooLarge = 1,
  AccumulatorOverflow = 2,
  MalformedFrame = 3,
  BatchLimit = 4,
  InvalidObservation = 5,
};

struct InfiniteDispatchResult {
  std::vector<InfiniteCompleteFrame> frames;
  std::optional<InfiniteDispatchFault> terminalFault;
};

class InfiniteCompleteFrameDispatcher {
public:
  explicit InfiniteCompleteFrameDispatcher(InfiniteFrameBatch limits);
  InfiniteCompleteFrameDispatcher(const InfiniteCompleteFrameDispatcher &) = delete;
  InfiniteCompleteFrameDispatcher &operator=(const InfiniteCompleteFrameDispatcher &) = delete;

  InfiniteDispatchResult process(const char *bytes, std::size_t length, std::int64_t observedTaiNs);

private:
  std::optional<InfiniteDispatchFault> declaredFrameFault() const;

  Parser m_parser;
  InfiniteFrameBatch m_limits;
};
} // namespace FIX
