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

#ifndef FIX_HTTPCONNECTION_H
#define FIX_HTTPCONNECTION_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#endif

#include "HttpParser.h"
#include <stdio.h>

namespace FIX {
class HttpMessage;

/// @deprecated Embedded HTTP administration is no longer supported.
class HttpConnection {
public:
  HttpConnection(socket_handle s);

  socket_handle getSocket() const { return m_socket; }
  bool read();

private:
  socket_handle m_socket;
  char m_buffer[BUFSIZ];

  HttpParser m_parser;
#if _MSC_VER
  fd_set m_fds;
#endif
};
} // namespace FIX

#endif
