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
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <time.h>
#include <utility>

namespace FIX {
namespace {
constexpr std::size_t MAX_FRAME_BYTES = 65'536;
constexpr std::size_t CHECKSUM_FIELD_BYTES = 7;

std::int64_t clockTaiNow() {
#ifdef CLOCK_TAI
  timespec value{};
  if (clock_gettime(CLOCK_TAI, &value) != 0 || value.tv_sec <= 0 || value.tv_nsec < 0 || value.tv_nsec >= 1'000'000'000
      || static_cast<std::uint64_t>(value.tv_sec)
             > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / UINT64_C(1000000000)) {
    throw std::runtime_error("CLOCK_TAI unavailable");
  }
  return static_cast<std::int64_t>(value.tv_sec) * INT64_C(1000000000) + value.tv_nsec;
#else
  throw std::runtime_error("CLOCK_TAI unavailable");
#endif
}
} // namespace

InfiniteCompleteFrameDispatcher::InfiniteCompleteFrameDispatcher(InfiniteFrameBatch limits)
    : m_limits(limits) {
  if (limits.maxFrames == 0 || limits.maxBytes == 0) {
    throw std::invalid_argument("Infinite frame batch limits must be positive");
  }
}

std::optional<InfiniteDispatchFault> InfiniteCompleteFrameDispatcher::scanDeclaredFrame() {
  if (m_scanStage == ScanStage::BeginString) {
    if (m_parser.m_buffer.empty() || (m_parser.m_buffer.size() == 1 && m_parser.m_buffer[0] == '8')) {
      return std::nullopt;
    }
    if (m_parser.m_buffer.size() < 2 || m_parser.m_buffer[0] != '8' || m_parser.m_buffer[1] != '=') {
      return InfiniteDispatchFault::MalformedFrame;
    }
    while (m_scanOffset < m_parser.m_buffer.size()) {
      if (m_parser.m_buffer[m_scanOffset] == '\001') {
        m_scanStage = ScanStage::BodyLengthPrefix;
        break;
      }
      if (m_parser.m_buffer[m_scanOffset] == '=' && m_parser.m_buffer[m_scanOffset - 1] == '8') {
        return InfiniteDispatchFault::MalformedFrame;
      }
      ++m_scanOffset;
    }
  }

  if (m_scanStage == ScanStage::BodyLengthPrefix) {
    constexpr char BODY_LENGTH_PREFIX[] = "\0019=";
    const auto available = std::min(std::size_t{3}, m_parser.m_buffer.size() - m_scanOffset);
    for (std::size_t index = 0; index < available; ++index) {
      if (m_parser.m_buffer[m_scanOffset + index] != BODY_LENGTH_PREFIX[index]) {
        return InfiniteDispatchFault::MalformedFrame;
      }
    }
    if (available < 3) {
      return std::nullopt;
    }
    m_scanOffset += 3;
    m_scanStage = ScanStage::BodyLength;
  }

  if (m_scanStage == ScanStage::BodyLength) {
    while (m_scanOffset < m_parser.m_buffer.size() && m_parser.m_buffer[m_scanOffset] != '\001') {
      const auto digit = static_cast<unsigned char>(m_parser.m_buffer[m_scanOffset]);
      if (digit < '0' || digit > '9') {
        return InfiniteDispatchFault::MalformedFrame;
      }
      const auto value = static_cast<std::size_t>(digit - '0');
      if (m_bodyLength > (std::numeric_limits<std::size_t>::max() - value) / 10) {
        return InfiniteDispatchFault::MalformedFrame;
      }
      m_bodyLength = m_bodyLength * 10 + value;
      m_bodyLengthHasDigit = true;
      ++m_scanOffset;
    }
    if (m_scanOffset == m_parser.m_buffer.size()) {
      return std::nullopt;
    }
    if (!m_bodyLengthHasDigit) {
      return InfiniteDispatchFault::MalformedFrame;
    }
    const auto bodyBegin = m_scanOffset + 1;
    if (m_bodyLength > std::numeric_limits<std::size_t>::max() - bodyBegin
        || bodyBegin + m_bodyLength > std::numeric_limits<std::size_t>::max() - CHECKSUM_FIELD_BYTES
        || bodyBegin + m_bodyLength + CHECKSUM_FIELD_BYTES > MAX_FRAME_BYTES) {
      return InfiniteDispatchFault::FrameTooLarge;
    }
    m_checksumBegin = bodyBegin + m_bodyLength;
    m_scanStage = ScanStage::Body;
  }

  if (m_scanStage == ScanStage::Body) {
    if (m_parser.m_buffer.size() >= m_checksumBegin && m_parser.m_buffer[m_checksumBegin - 1] != '\001') {
      return InfiniteDispatchFault::MalformedFrame;
    }
    const auto available = std::min(
        CHECKSUM_FIELD_BYTES,
        m_parser.m_buffer.size() > m_checksumBegin ? m_parser.m_buffer.size() - m_checksumBegin : std::size_t{0});
    constexpr char CHECKSUM_PREFIX[] = "10=";
    for (std::size_t index = 0; index < std::min(std::size_t{3}, available); ++index) {
      if (m_parser.m_buffer[m_checksumBegin + index] != CHECKSUM_PREFIX[index]) {
        return InfiniteDispatchFault::MalformedFrame;
      }
    }
    for (std::size_t index = 3; index < std::min(std::size_t{6}, available); ++index) {
      if (m_parser.m_buffer[m_checksumBegin + index] < '0' || m_parser.m_buffer[m_checksumBegin + index] > '9') {
        return InfiniteDispatchFault::MalformedFrame;
      }
    }
    if (available == CHECKSUM_FIELD_BYTES) {
      if (m_parser.m_buffer[m_checksumBegin + 6] != '\001') {
        return InfiniteDispatchFault::MalformedFrame;
      }
      m_scanStage = ScanStage::Ready;
    }
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

InfiniteDispatchResult InfiniteCompleteFrameDispatcher::terminal(
    InfiniteDispatchResult result,
    InfiniteDispatchFault fault) {
  result.terminalFault = fault;
  m_terminalFault = fault;
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
        if (!m_parser.readFixMessage(message)) {
          return terminal(std::move(result), InfiniteDispatchFault::MalformedFrame);
        }
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
        result.frames.push_back({std::move(message), observedTaiNs});
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
