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

#include "InfiniteCompleteFrame.h"

#include "Exceptions.h"
#include "scope_guard.hpp"

#ifdef HAVE_SSL
#include <openssl/crypto.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <time.h>
#include <utility>

namespace FIX {
namespace {
constexpr std::size_t MAX_FRAME_BYTES = 65'536;
constexpr std::size_t CHECKSUM_FIELD_BYTES = 7;

void cleanse(char *bytes, std::size_t length) noexcept {
#ifdef HAVE_SSL
  OPENSSL_cleanse(bytes, length);
#else
  volatile char *cursor = bytes;
  for (std::size_t index = 0; index < length; ++index) {
    cursor[index] = 0;
  }
#endif
}

std::int64_t clockTaiNow() {
#ifdef CLOCK_TAI
  timespec value{};
  if (clock_gettime(CLOCK_TAI, &value) != 0 || value.tv_sec <= 0 || value.tv_nsec < 0
      || value.tv_nsec >= 1'000'000'000) {
    throw std::runtime_error("CLOCK_TAI unavailable");
  }
  const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
  const auto nanoseconds = static_cast<std::uint64_t>(value.tv_nsec);
  const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if (seconds > (maximum - nanoseconds) / UINT64_C(1000000000)) {
    throw std::runtime_error("CLOCK_TAI unavailable");
  }
  return static_cast<std::int64_t>(seconds * UINT64_C(1000000000) + nanoseconds);
#else
  throw std::runtime_error("CLOCK_TAI unavailable");
#endif
}
} // namespace

InfiniteDeclaredFrameScanResult scanInfiniteDeclaredFrame(
    const char *bytes,
    std::size_t length,
    InfiniteDeclaredFrameCursor &cursor,
    std::size_t &completePrefix) noexcept {
  completePrefix = 0;
  if ((length != 0 && bytes == nullptr) || cursor.stage > 3 || cursor.frameStart > length
      || (cursor.scanOffset > length
          && !(cursor.stage == 0 && cursor.scanOffset == cursor.frameStart + 2 && length - cursor.frameStart < 2))
      || cursor.bodyLength > MAX_FRAME_BYTES
      || (cursor.checksumBegin != 0
          && (cursor.checksumBegin < cursor.frameStart
              || cursor.checksumBegin - cursor.frameStart > MAX_FRAME_BYTES))) {
    return InfiniteDeclaredFrameScanResult::Malformed;
  }
  if (cursor.stage == 0 && cursor.scanOffset == 0) {
    cursor.scanOffset = cursor.frameStart + 2;
  }
  if (cursor.stage == 0) {
    const auto available = length - cursor.frameStart;
    if ((available != 0 && bytes[cursor.frameStart] != '8') || (available > 1 && bytes[cursor.frameStart + 1] != '=')) {
      return InfiniteDeclaredFrameScanResult::Malformed;
    }
    while (cursor.scanOffset < length && bytes[cursor.scanOffset] != '\001') {
      if (bytes[cursor.scanOffset] == '=' && bytes[cursor.scanOffset - 1] == '8') {
        return InfiniteDeclaredFrameScanResult::Malformed;
      }
      ++cursor.scanOffset;
    }
    if (cursor.scanOffset < length) {
      cursor.stage = 1;
    }
  }
  if (cursor.stage == 1) {
    constexpr char prefix[] = "\0019=";
    const auto available = std::min<std::size_t>(3, length - cursor.scanOffset);
    for (std::size_t index = 0; index < available; ++index) {
      if (bytes[cursor.scanOffset + index] != prefix[index]) {
        return InfiniteDeclaredFrameScanResult::Malformed;
      }
    }
    if (available == 3) {
      cursor.scanOffset += 3;
      cursor.stage = 2;
    }
  }
  if (cursor.stage == 2) {
    while (cursor.scanOffset < length && bytes[cursor.scanOffset] != '\001') {
      const auto digit = static_cast<unsigned char>(bytes[cursor.scanOffset]);
      if (digit < '0' || digit > '9') {
        return InfiniteDeclaredFrameScanResult::Malformed;
      }
      const auto value = static_cast<std::size_t>(digit - '0');
      if (cursor.bodyLength > (std::numeric_limits<std::size_t>::max() - value) / 10) {
        return InfiniteDeclaredFrameScanResult::Malformed;
      }
      cursor.bodyLength = cursor.bodyLength * 10 + value;
      cursor.bodyLengthHasDigit = true;
      ++cursor.scanOffset;
    }
    if (cursor.scanOffset < length) {
      if (!cursor.bodyLengthHasDigit) {
        return InfiniteDeclaredFrameScanResult::Malformed;
      }
      const auto bodyBegin = cursor.scanOffset + 1;
      if (bodyBegin > std::numeric_limits<std::size_t>::max() - cursor.bodyLength
          || bodyBegin + cursor.bodyLength > std::numeric_limits<std::size_t>::max() - CHECKSUM_FIELD_BYTES
          || bodyBegin + cursor.bodyLength + CHECKSUM_FIELD_BYTES - cursor.frameStart > MAX_FRAME_BYTES) {
        return InfiniteDeclaredFrameScanResult::TooLarge;
      }
      cursor.checksumBegin = bodyBegin + cursor.bodyLength;
      cursor.stage = 3;
    }
  }
  if (cursor.stage == 3) {
    if (length >= cursor.checksumBegin && cursor.checksumBegin != 0 && bytes[cursor.checksumBegin - 1] != '\001') {
      return InfiniteDeclaredFrameScanResult::Malformed;
    }
    const auto available = std::min(
        CHECKSUM_FIELD_BYTES,
        length > cursor.checksumBegin ? length - cursor.checksumBegin : std::size_t{0});
    constexpr char prefix[] = "10=";
    for (std::size_t index = 0; index < std::min<std::size_t>(3, available); ++index) {
      if (bytes[cursor.checksumBegin + index] != prefix[index]) {
        return InfiniteDeclaredFrameScanResult::Malformed;
      }
    }
    for (std::size_t index = 3; index < std::min<std::size_t>(6, available); ++index) {
      if (bytes[cursor.checksumBegin + index] < '0' || bytes[cursor.checksumBegin + index] > '9') {
        return InfiniteDeclaredFrameScanResult::Malformed;
      }
    }
    if (available == CHECKSUM_FIELD_BYTES) {
      if (bytes[cursor.checksumBegin + 6] != '\001') {
        return InfiniteDeclaredFrameScanResult::Malformed;
      }
      completePrefix = cursor.checksumBegin + CHECKSUM_FIELD_BYTES;
      cursor = {completePrefix, completePrefix + 2, 0, 0, 0, false};
      return InfiniteDeclaredFrameScanResult::Ready;
    }
  }
  return InfiniteDeclaredFrameScanResult::NeedMore;
}

InfiniteCompleteFrame::InfiniteCompleteFrame(InfiniteSensitiveString value, std::int64_t observedTaiNs)
    : bytes(std::move(value)),
      observedTaiNs(observedTaiNs) {}

InfiniteCompleteFrame::InfiniteCompleteFrame(InfiniteCompleteFrame &&other) noexcept
    : bytes(std::move(other.bytes)),
      observedTaiNs(other.observedTaiNs) {}

InfiniteCompleteFrame &InfiniteCompleteFrame::operator=(const InfiniteCompleteFrame &other) {
  if (this != &other) {
    auto replacement = other;
    *this = std::move(replacement);
  }
  return *this;
}

InfiniteCompleteFrame &InfiniteCompleteFrame::operator=(InfiniteCompleteFrame &&other) noexcept {
  if (this != &other) {
    bytes = std::move(other.bytes);
    observedTaiNs = other.observedTaiNs;
  }
  return *this;
}

InfiniteCompleteFrame::~InfiniteCompleteFrame() = default;

InfiniteCompleteFrameDispatcher::InfiniteCompleteFrameDispatcher(InfiniteFrameBatch limits)
    : m_limits(limits) {
  if (limits.maxFrames == 0 || limits.maxBytes == 0) {
    throw std::invalid_argument("Infinite frame batch limits must be positive");
  }
  m_parser.m_buffer.reserve(MAX_FRAME_BYTES);
}

InfiniteCompleteFrameDispatcher::InfiniteCompleteFrameDispatcher(
    InfiniteFrameBatch limits,
    std::int64_t initialObservedTaiNs)
    : InfiniteCompleteFrameDispatcher(limits) {
  if (initialObservedTaiNs <= 0) {
    throw std::invalid_argument("Infinite initial observation must be positive");
  }
  m_lastObservedTaiNs = initialObservedTaiNs;
}

InfiniteCompleteFrameDispatcher::~InfiniteCompleteFrameDispatcher() { clearParserBuffer(); }

std::optional<InfiniteDispatchFault> InfiniteCompleteFrameDispatcher::scanDeclaredFrame() {
  InfiniteDeclaredFrameCursor cursor{
      0,
      m_scanOffset,
      m_bodyLength,
      m_checksumBegin,
      static_cast<std::uint32_t>(m_scanStage),
      m_bodyLengthHasDigit};
  std::size_t completePrefix = 0;
  const auto result
      = scanInfiniteDeclaredFrame(m_parser.m_buffer.data(), m_parser.m_buffer.size(), cursor, completePrefix);
  if (result == InfiniteDeclaredFrameScanResult::Ready) {
    m_checksumBegin = completePrefix - CHECKSUM_FIELD_BYTES;
    m_scanStage = ScanStage::Ready;
    return std::nullopt;
  }
  m_scanOffset = cursor.scanOffset;
  m_bodyLength = cursor.bodyLength;
  m_checksumBegin = cursor.checksumBegin;
  m_bodyLengthHasDigit = cursor.bodyLengthHasDigit;
  m_scanStage = static_cast<ScanStage>(cursor.stage);
  if (result == InfiniteDeclaredFrameScanResult::Malformed) {
    return InfiniteDispatchFault::MalformedFrame;
  }
  if (result == InfiniteDeclaredFrameScanResult::TooLarge) {
    return InfiniteDispatchFault::FrameTooLarge;
  }
  return std::nullopt;
}
void InfiniteCompleteFrameDispatcher::resetDeclaredFrameScan() {
  m_scanStage = ScanStage::BeginString;
  m_scanOffset = 2;
  m_bodyLength = 0;
  m_checksumBegin = 0;
  m_bodyLengthHasDigit = false;
}

void InfiniteCompleteFrameDispatcher::takeDeclaredFrame(std::string &message) {
  const auto length = m_checksumBegin + CHECKSUM_FIELD_BYTES;
  try {
    message.assign(m_parser.m_buffer, 0, length);
  } catch (...) {
    clearParserBuffer();
    throw;
  }
  const auto remaining = m_parser.m_buffer.size() - length;
  std::memmove(m_parser.m_buffer.data(), m_parser.m_buffer.data() + length, remaining);
  cleanse(m_parser.m_buffer.data() + remaining, length);
  m_parser.m_buffer.resize(remaining);
}

void InfiniteCompleteFrameDispatcher::cleanseParserPrefix(std::size_t length) noexcept {
  cleanse(m_parser.m_buffer.data(), std::min(length, m_parser.m_buffer.size()));
}

void InfiniteCompleteFrameDispatcher::clearParserBuffer() noexcept {
  cleanseParserPrefix(m_parser.m_buffer.size());
  m_parser.m_buffer.clear();
}

void InfiniteCompleteFrameDispatcher::discard() noexcept {
  clearParserBuffer();
  m_terminalFault = InfiniteDispatchFault::MalformedFrame;
}

InfiniteDispatchResult InfiniteCompleteFrameDispatcher::terminal(
    InfiniteDispatchResult result,
    InfiniteDispatchFault fault) {
  result.terminalFault = fault;
  m_terminalFault = fault;
  clearParserBuffer();
  return result;
}

InfiniteDispatchResult InfiniteCompleteFrameDispatcher::process(
    const char *bytes,
    std::size_t length,
    const std::function<std::int64_t()> &observeTaiNs) {
  InfiniteDispatchResult result;
  if (m_terminalFault) {
    result.terminalFault = m_terminalFault;
    return result;
  }
  if (bytes == nullptr && length != 0) {
    return terminal(std::move(result), InfiniteDispatchFault::MalformedFrame);
  }

  std::size_t offset = 0;
  std::size_t batchBytes = 0;
  while (offset < length) {
    const auto available = MAX_FRAME_BYTES - m_parser.m_buffer.size();
    const auto appendLength = std::min(available, length - offset);
    m_parser.addToStream(bytes + offset, appendLength);
    offset += appendLength;

    try {
      while (true) {
        if (const auto fault = scanDeclaredFrame()) {
          return terminal(std::move(result), *fault);
        }
        if (m_scanStage != ScanStage::Ready) {
          break;
        }
        std::string message;
        takeDeclaredFrame(message);
        auto messageGuard = sg::make_scope_guard([&message]() { cleanse(message.data(), message.size()); });
        resetDeclaredFrameScan();
        if (message.size() > MAX_FRAME_BYTES) {
          return terminal(std::move(result), InfiniteDispatchFault::FrameTooLarge);
        }
        if (result.frames.size() >= m_limits.maxFrames || message.size() > m_limits.maxBytes - batchBytes) {
          result.frames.clear();
          return terminal(std::move(result), InfiniteDispatchFault::BatchLimit);
        }
        std::int64_t observedTaiNs;
        try {
          observedTaiNs = observeTaiNs();
        } catch (...) {
          return terminal(std::move(result), InfiniteDispatchFault::InvalidObservation);
        }
        if (observedTaiNs <= 0 || (m_lastObservedTaiNs && observedTaiNs < *m_lastObservedTaiNs)) {
          return terminal(std::move(result), InfiniteDispatchFault::InvalidObservation);
        }
        m_lastObservedTaiNs = observedTaiNs;
        batchBytes += message.size();
        result.frames.push_back({message, observedTaiNs});
      }
    } catch (const MessageParseError &) {
      return terminal(std::move(result), InfiniteDispatchFault::MalformedFrame);
    }

    if (m_parser.m_buffer.size() == MAX_FRAME_BYTES) {
      return terminal(std::move(result), InfiniteDispatchFault::AccumulatorOverflow);
    }
  }
  return result;
}

InfiniteDispatchResult InfiniteCompleteFrameDispatcher::process(const char *bytes, std::size_t length) {
  return process(bytes, length, clockTaiNow);
}
} // namespace FIX
