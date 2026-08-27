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

#include "InfiniteSensitiveString.h"
#include "Parser.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace FIX {
class InfiniteCompleteFrameTestAccess;

struct InfiniteCompleteFrame {
  InfiniteSensitiveString bytes;
  std::int64_t observedTaiNs;

  InfiniteCompleteFrame(InfiniteSensitiveString bytes, std::int64_t observedTaiNs);
  InfiniteCompleteFrame(const InfiniteCompleteFrame &) = default;
  InfiniteCompleteFrame &operator=(const InfiniteCompleteFrame &other);
  InfiniteCompleteFrame(InfiniteCompleteFrame &&other) noexcept;
  InfiniteCompleteFrame &operator=(InfiniteCompleteFrame &&other) noexcept;
  ~InfiniteCompleteFrame();

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
  InfiniteCompleteFrameDispatcher(InfiniteFrameBatch limits, std::int64_t initialObservedTaiNs);
  ~InfiniteCompleteFrameDispatcher();
  InfiniteCompleteFrameDispatcher(const InfiniteCompleteFrameDispatcher &) = delete;
  InfiniteCompleteFrameDispatcher &operator=(const InfiniteCompleteFrameDispatcher &) = delete;

  InfiniteDispatchResult process(
      const char *bytes,
      std::size_t length,
      const std::function<std::int64_t()> &observeTaiNs);
  InfiniteDispatchResult process(const char *bytes, std::size_t length);
  void discard() noexcept;

private:
  friend class InfiniteCompleteFrameTestAccess;

  enum class ScanStage : std::uint8_t {
    BeginString,
    BodyLengthPrefix,
    BodyLength,
    Body,
    Ready
  };

  std::optional<InfiniteDispatchFault> scanDeclaredFrame();
  void takeDeclaredFrame(std::string &message);
  void cleanseParserPrefix(std::size_t length) noexcept;
  void clearParserBuffer() noexcept;
  const std::string &parserBufferForTest() const noexcept { return m_parser.m_buffer; }
  InfiniteDispatchResult terminal(InfiniteDispatchResult result, InfiniteDispatchFault fault);
  void resetDeclaredFrameScan();

  Parser m_parser;
  InfiniteFrameBatch m_limits;
  ScanStage m_scanStage{ScanStage::BeginString};
  std::size_t m_scanOffset{2};
  std::size_t m_bodyLength{0};
  std::size_t m_checksumBegin{0};
  bool m_bodyLengthHasDigit{false};
  std::optional<std::int64_t> m_lastObservedTaiNs;
  std::optional<InfiniteDispatchFault> m_terminalFault;
};
} // namespace FIX
