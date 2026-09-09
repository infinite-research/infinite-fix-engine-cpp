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

#include "Log.h"

namespace FIX {
Mutex ScreenLog::s_mutex;

std::string redactLogonCredentials(const std::string &value) {
  std::string result;
  std::string::size_type copied = 0;
  std::string::size_type position = 0;
  while (position < value.size()) {
    const bool fieldBoundary = position == 0 || value[position - 1] == '\001';
    const bool credential
        = fieldBoundary && (value.compare(position, 4, "553=") == 0 || value.compare(position, 4, "554=") == 0);
    if (!credential) {
      ++position;
      continue;
    }

    const std::string::size_type valueStart = position + 4;
    result.append(value, copied, valueStart - copied);
    result += "<redacted>";
    const std::string::size_type fieldEnd = value.find('\001', valueStart);
    if (fieldEnd == std::string::npos) {
      return result;
    }

    result += '\001';
    position = copied = fieldEnd + 1;
  }

  result.append(value, copied, std::string::npos);
  return result;
}

Log *ScreenLogFactory::create() {
  bool incoming, outgoing, event;
  init(m_settings.get(), incoming, outgoing, event);
  return new ScreenLog(incoming, outgoing, event);
}

Log *ScreenLogFactory::create(const SessionID &sessionID) {
  Dictionary settings;
  if (m_settings.has(sessionID)) {
    settings = m_settings.get(sessionID);
  }

  bool incoming, outgoing, event;
  init(settings, incoming, outgoing, event);
  return new ScreenLog(sessionID, incoming, outgoing, event);
}

void ScreenLogFactory::init(const Dictionary &settings, bool &incoming, bool &outgoing, bool &event) const {
  if (m_useSettings) {
    incoming = true;
    outgoing = true;
    event = true;

    if (settings.has(SCREEN_LOG_SHOW_INCOMING)) {
      incoming = settings.getBool(SCREEN_LOG_SHOW_INCOMING);
    }
    if (settings.has(SCREEN_LOG_SHOW_OUTGOING)) {
      outgoing = settings.getBool(SCREEN_LOG_SHOW_OUTGOING);
    }
    if (settings.has(SCREEN_LOG_SHOW_EVENTS)) {
      event = settings.getBool(SCREEN_LOG_SHOW_EVENTS);
    }
  } else {
    incoming = m_incoming;
    outgoing = m_outgoing;
    event = m_event;
  }
}

void ScreenLogFactory::destroy(Log *pLog) { delete pLog; }
} // namespace FIX
