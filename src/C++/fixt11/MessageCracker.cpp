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

#ifdef _MSC_VER
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "MessageCracker.h"

#include <utility>

#include "Heartbeat.h"
#include "TestRequest.h"
#include "ResendRequest.h"
#include "Reject.h"
#include "SequenceReset.h"
#include "Logout.h"
#include "Logon.h"
#include "XMLnonFIX.h"

namespace FIXT11
{
  namespace
  {
    /// Deliver a genuine T to the const callback.
    template <typename T>
    void crackConst( MessageCracker& cracker, const FIX::Message& message, const FIX::SessionID& sessionID )
    {
      if( const T* typed = dynamic_cast<const T*>( &message ) )
      {
        cracker.onMessage( *typed, sessionID );
        return;
      }
      // Viewing an object that is not a T as a T is undefined behaviour, so one copy is unavoidable here.
      cracker.onMessage( T( message ), sessionID );
    }

    /// Deliver a genuine T to the mutable callback without copying the caller's content.
    template <typename T>
    void crackMutable( MessageCracker& cracker, FIX::Message& message, const FIX::SessionID& sessionID )
    {
      if( T* typed = dynamic_cast<T*>( &message ) )
      {
        cracker.onMessage( *typed, sessionID );
        return;
      }
      // Lend the caller's content to an empty T and move it back on return and on exception.
      T typed{ FIX::Message() };
      FIX::Message& content = typed;
      content = std::move( message );
      try
      {
        cracker.onMessage( typed, sessionID );
      }
      catch( ... )
      {
        message = std::move( content );
        throw;
      }
      message = std::move( content );
    }
  }

  void MessageCracker::crack( const Message& message,
                              const FIX::SessionID& sessionID )
  {
    crack( static_cast<const FIX::Message&>( message ), sessionID );
  }

  void MessageCracker::crack( const FIX::Message& message,
                              const FIX::SessionID& sessionID )
  {
    const std::string& msgTypeValue
      = message.getHeader().getField( FIX::FIELD::MsgType );

    if( msgTypeValue == "0" )
      return crackConst<Heartbeat>( *this, message, sessionID );
    if( msgTypeValue == "1" )
      return crackConst<TestRequest>( *this, message, sessionID );
    if( msgTypeValue == "2" )
      return crackConst<ResendRequest>( *this, message, sessionID );
    if( msgTypeValue == "3" )
      return crackConst<Reject>( *this, message, sessionID );
    if( msgTypeValue == "4" )
      return crackConst<SequenceReset>( *this, message, sessionID );
    if( msgTypeValue == "5" )
      return crackConst<Logout>( *this, message, sessionID );
    if( msgTypeValue == "A" )
      return crackConst<Logon>( *this, message, sessionID );
    if( msgTypeValue == "n" )
      return crackConst<XMLnonFIX>( *this, message, sessionID );

    return crackConst<Message>( *this, message, sessionID );
  }

  void MessageCracker::crack( Message& message,
                              const FIX::SessionID& sessionID )
  {
    crack( static_cast<FIX::Message&>( message ), sessionID );
  }

  void MessageCracker::crack( FIX::Message& message,
                              const FIX::SessionID& sessionID )
  {
    const std::string& msgTypeValue
      = message.getHeader().getField( FIX::FIELD::MsgType );

    if( msgTypeValue == "0" )
      return crackMutable<Heartbeat>( *this, message, sessionID );
    if( msgTypeValue == "1" )
      return crackMutable<TestRequest>( *this, message, sessionID );
    if( msgTypeValue == "2" )
      return crackMutable<ResendRequest>( *this, message, sessionID );
    if( msgTypeValue == "3" )
      return crackMutable<Reject>( *this, message, sessionID );
    if( msgTypeValue == "4" )
      return crackMutable<SequenceReset>( *this, message, sessionID );
    if( msgTypeValue == "5" )
      return crackMutable<Logout>( *this, message, sessionID );
    if( msgTypeValue == "A" )
      return crackMutable<Logon>( *this, message, sessionID );
    if( msgTypeValue == "n" )
      return crackMutable<XMLnonFIX>( *this, message, sessionID );

    return crackMutable<Message>( *this, message, sessionID );
  }
}
