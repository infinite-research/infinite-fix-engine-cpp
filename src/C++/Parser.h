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

#ifndef FIX_PARSER_H
#define FIX_PARSER_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "Exceptions.h"
#include <iostream>
#include <string>

namespace FIX {
class InfiniteCompleteFrameDispatcher;

/// Parses %FIX messages off an input stream.
class Parser {
public:
  Parser() {}
  ~Parser() {}

  bool extractLength(int &length, std::string::size_type &pos, const std::string &buffer) EXCEPT(MessageParseError);
  bool readFixMessage(std::string &str) EXCEPT(MessageParseError);

  /// Append bytes, throwing MessageParseError and clearing buffered input above 16 MiB.
  void addToStream(const char *str, size_t len) EXCEPT(MessageParseError);
  /// Append bytes subject to the same fixed accumulator limit.
  void addToStream(const std::string &str) { addToStream(str.data(), str.size()); }

private:
  friend class InfiniteCompleteFrameDispatcher;

  static constexpr std::size_t MAX_BUFFER_BYTES = 16U * 1024U * 1024U;
  std::string m_buffer;
};
} // namespace FIX
#endif // FIX_PARSER_H
