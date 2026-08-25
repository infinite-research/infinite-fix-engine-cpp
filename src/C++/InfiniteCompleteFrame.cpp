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

namespace FIX {
namespace {
constexpr std::size_t MAX_FRAME_BYTES = 65'536;
constexpr std::size_t CHECKSUM_FIELD_BYTES = 7;
} // namespace

InfiniteCompleteFrameDispatcher::InfiniteCompleteFrameDispatcher(InfiniteFrameBatch limits)
    : m_limits(limits) {
  if (limits.maxFrames == 0 || limits.maxBytes == 0) {
    throw std::invalid_argument("Infinite frame batch limits must be positive");
  }
}

std::optional<InfiniteDispatchFault> InfiniteCompleteFrameDispatcher::declaredFrameFault() const {
  if (m_parser.m_buffer.empty() || (m_parser.m_buffer.size() == 1 && m_parser.m_buffer[0] == '8')) {
    return std::nullopt;
  }
  if (m_parser.m_buffer.size() < 2 || m_parser.m_buffer[0] != '8' || m_parser.m_buffer[1] != '=') {
    return InfiniteDispatchFault::MalformedFrame;
  }
  constexpr std::size_t begin = 0;
  const auto beginStringEnd = m_parser.m_buffer.find('\001', 2);
  if (beginStringEnd == std::string::npos) {
    return std::nullopt;
  }
  for (std::size_t index = 2; index + 1 < beginStringEnd; ++index) {
    if (m_parser.m_buffer[index] == '8' && m_parser.m_buffer[index + 1] == '=') {
      return InfiniteDispatchFault::MalformedFrame;
    }
  }
  constexpr char BODY_LENGTH_PREFIX[] = "\0019=";
  const auto lengthPrefixAvailable = std::min(std::size_t{3}, m_parser.m_buffer.size() - beginStringEnd);
  for (std::size_t index = 0; index < lengthPrefixAvailable; ++index) {
    if (m_parser.m_buffer[beginStringEnd + index] != BODY_LENGTH_PREFIX[index]) {
      return InfiniteDispatchFault::MalformedFrame;
    }
  }
  if (lengthPrefixAvailable < 3) {
    return std::nullopt;
  }
  const auto lengthBegin = beginStringEnd;
  const auto digitsBegin = lengthBegin + 3;
  const auto digitsEnd = m_parser.m_buffer.find('\001', digitsBegin);
  if (digitsEnd == std::string::npos) {
    return std::nullopt;
  }
  std::size_t bodyLength = 0;
  for (auto index = digitsBegin; index < digitsEnd; ++index) {
    const auto digit = static_cast<unsigned char>(m_parser.m_buffer[index]);
    if (digit < '0' || digit > '9') {
      return InfiniteDispatchFault::MalformedFrame;
    }
    const auto value = static_cast<std::size_t>(digit - '0');
    if (bodyLength > (std::numeric_limits<std::size_t>::max() - value) / 10) {
      return InfiniteDispatchFault::MalformedFrame;
    }
    bodyLength = bodyLength * 10 + value;
  }

  const auto bodyBegin = digitsEnd + 1;
  if (bodyLength > std::numeric_limits<std::size_t>::max() - bodyBegin
      || bodyBegin + bodyLength > std::numeric_limits<std::size_t>::max() - CHECKSUM_FIELD_BYTES
      || bodyBegin + bodyLength + CHECKSUM_FIELD_BYTES - begin > MAX_FRAME_BYTES) {
    return InfiniteDispatchFault::FrameTooLarge;
  }

  const auto checksumBegin = bodyBegin + bodyLength;
  if (m_parser.m_buffer.size() >= checksumBegin && m_parser.m_buffer[checksumBegin - 1] != '\001') {
    return InfiniteDispatchFault::MalformedFrame;
  }
  const auto checksumAvailable = std::min(
      CHECKSUM_FIELD_BYTES,
      m_parser.m_buffer.size() > checksumBegin ? m_parser.m_buffer.size() - checksumBegin : std::size_t{0});
  constexpr char CHECKSUM_PREFIX[] = "10=";
  for (std::size_t index = 0; index < std::min(std::size_t{3}, checksumAvailable); ++index) {
    if (m_parser.m_buffer[checksumBegin + index] != CHECKSUM_PREFIX[index]) {
      return InfiniteDispatchFault::MalformedFrame;
    }
  }
  for (std::size_t index = 3; index < std::min(std::size_t{6}, checksumAvailable); ++index) {
    if (m_parser.m_buffer[checksumBegin + index] < '0' || m_parser.m_buffer[checksumBegin + index] > '9') {
      return InfiniteDispatchFault::MalformedFrame;
    }
  }
  if (checksumAvailable == CHECKSUM_FIELD_BYTES && m_parser.m_buffer[checksumBegin + 6] != '\001') {
    return InfiniteDispatchFault::MalformedFrame;
  }
  return std::nullopt;
}

InfiniteDispatchResult InfiniteCompleteFrameDispatcher::process(
    const char *bytes,
    std::size_t length,
    std::int64_t observedTaiNs) {
  InfiniteDispatchResult result;
  if (observedTaiNs <= 0) {
    result.terminalFault = InfiniteDispatchFault::InvalidObservation;
    return result;
  }
  if (bytes == nullptr && length != 0) {
    result.terminalFault = InfiniteDispatchFault::MalformedFrame;
    return result;
  }

  std::size_t offset = 0;
  std::size_t batchBytes = 0;
  while (offset < length) {
    const auto available = MAX_FRAME_BYTES - m_parser.m_buffer.size();
    const auto appendLength = std::min(available, length - offset);
    m_parser.addToStream(bytes + offset, appendLength);
    offset += appendLength;

    if (const auto fault = declaredFrameFault()) {
      result.terminalFault = fault;
      return result;
    }

    try {
      while (true) {
        if (const auto fault = declaredFrameFault()) {
          result.terminalFault = fault;
          return result;
        }
        std::string message;
        if (!m_parser.readFixMessage(message)) {
          break;
        }
        if (message.size() > MAX_FRAME_BYTES) {
          result.terminalFault = InfiniteDispatchFault::FrameTooLarge;
          return result;
        }
        if (result.frames.size() >= m_limits.maxFrames || message.size() > m_limits.maxBytes - batchBytes) {
          result.frames.clear();
          result.terminalFault = InfiniteDispatchFault::BatchLimit;
          return result;
        }
        batchBytes += message.size();
        result.frames.push_back({std::move(message), observedTaiNs});
      }
    } catch (const MessageParseError &) {
      result.terminalFault = InfiniteDispatchFault::MalformedFrame;
      return result;
    }

    if (const auto fault = declaredFrameFault()) {
      result.terminalFault = fault;
      return result;
    }
    if (m_parser.m_buffer.size() == MAX_FRAME_BYTES) {
      result.terminalFault = InfiniteDispatchFault::AccumulatorOverflow;
      return result;
    }
  }
  return result;
}
} // namespace FIX
