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

#include "FieldConvertors.h"
#include "Parser.h"
#include "Utility.h"
#include <algorithm>

namespace FIX {
void Parser::addToStream(const char *str, size_t len) EXCEPT(MessageParseError) {
  // ponytail: 16 MiB bounds unauthenticated buffering; add a transport setting only if production frames need it.
  if (m_buffer.size() > MAX_BUFFER_BYTES || len > MAX_BUFFER_BYTES - m_buffer.size()) {
    m_buffer.clear();
    throw MessageParseError("FIX frame exceeds 16 MiB");
  }
  m_buffer.append(str, len);
}

bool Parser::extractLength(int &length, std::string::size_type &pos, const std::string &buffer)
    EXCEPT(MessageParseError) {
  if (!buffer.size()) {
    return false;
  }

  std::string::size_type startPos = buffer.find("\0019=", 0);
  if (startPos == std::string::npos) {
    return false;
  }
  startPos += 3;
  std::string::size_type endPos = buffer.find("\001", startPos);
  if (endPos == std::string::npos) {
    return false;
  }

  std::string strLength(buffer, startPos, endPos - startPos);

  try {
    length = IntConvertor::convert(strLength);
    if (length < 0) {
      throw MessageParseError();
    }
  } catch (FieldConvertError &) {
    throw MessageParseError();
  }

  pos = endPos + 1;
  // Leave room for the checksum field (10=ddd<SOH>) before adding the body length.
  if (pos > MAX_BUFFER_BYTES - 7 || static_cast<std::size_t>(length) > MAX_BUFFER_BYTES - 7 - pos) {
    throw MessageParseError("FIX frame exceeds 16 MiB");
  }
  return true;
}

bool Parser::readFixMessage(std::string &str) EXCEPT(MessageParseError) {
  try {
    do {
      if (m_buffer.length() < 2) {
        break;
      }
      std::string::size_type pos = m_buffer.find("8=");
      if (pos == std::string::npos) {
        break;
      }
      m_buffer.erase(0, pos);

      int length = 0;
      if (!extractLength(length, pos, m_buffer)) {
        break;
      }
      pos += length;
      if (m_buffer.size() < pos) {
        break;
      }

      pos = m_buffer.find("\00110=", pos - 1);
      if (pos == std::string::npos) {
        break;
      }
      pos += 4;
      pos = m_buffer.find("\001", pos);
      if (pos == std::string::npos) {
        break;
      }
      pos += 1;

      str.assign(m_buffer, 0, pos);
      m_buffer.erase(0, pos);
      return true;
    } while (false);

    if (m_buffer.size() == MAX_BUFFER_BYTES) {
      throw MessageParseError("Incomplete FIX frame at 16 MiB limit");
    }
  } catch (MessageParseError &) {
    m_buffer.clear();
    throw;
  }

  return false;
}
} // namespace FIX
