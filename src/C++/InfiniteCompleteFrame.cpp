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

#include "Exceptions.h"
#include "InfiniteCompleteFrame.h"
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
  const auto begin = m_parser.m_buffer.find("8=");
  const auto lengthBegin = m_parser.m_buffer.find("\0019=", begin);
  if (lengthBegin == std::string::npos) {
    return std::nullopt;
  }
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
      std::string message;
      while (m_parser.readFixMessage(message)) {
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

    if (offset < length) {
      result.terminalFault = declaredFrameFault().value_or(InfiniteDispatchFault::AccumulatorOverflow);
      return result;
    }
  }
  return result;
}
} // namespace FIX
