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

#ifndef FIXT11_MESSAGECRACKER_H
#define FIXT11_MESSAGECRACKER_H


#include "../SessionID.h"
#include "../Exceptions.h"
#include <utility>

#include "../fixt11/Message.h"
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

  class MessageCracker
  {
  public:
  virtual ~MessageCracker() {}
  virtual void onMessage( const Message&, const FIX::SessionID& )
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( Message&, const FIX::SessionID& )
    { throw FIX::UnsupportedMessageType(); }
 virtual void onMessage( const Heartbeat&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const TestRequest&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const ResendRequest&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const Reject&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const SequenceReset&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const Logout&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const Logon&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const XMLnonFIX&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( Heartbeat&, const FIX::SessionID& ) {} 
 virtual void onMessage( TestRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ResendRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( Reject&, const FIX::SessionID& ) {} 
 virtual void onMessage( SequenceReset&, const FIX::SessionID& ) {} 
 virtual void onMessage( Logout&, const FIX::SessionID& ) {} 
 virtual void onMessage( Logon&, const FIX::SessionID& ) {} 
 virtual void onMessage( XMLnonFIX&, const FIX::SessionID& ) {} 

public:
  /// Preserve the version-message entry point while dispatching genuine typed values.
  void crack( const Message& message, 
              const FIX::SessionID& sessionID )
  {
    crack( static_cast<const FIX::Message&>(message), sessionID );
  }

  /// Dispatch a generic message without assuming a derived object lifetime.
  void crack( const FIX::Message& message,
              const FIX::SessionID& sessionID )
  {
    const std::string & msgTypeValue 
      = message.getHeader().getField( FIX::FIELD::MsgType );
    
    
    if( msgTypeValue == "0" )
      return onMessage( Heartbeat(message), sessionID );
    
    if( msgTypeValue == "1" )
      return onMessage( TestRequest(message), sessionID );
    
    if( msgTypeValue == "2" )
      return onMessage( ResendRequest(message), sessionID );
    
    if( msgTypeValue == "3" )
      return onMessage( Reject(message), sessionID );
    
    if( msgTypeValue == "4" )
      return onMessage( SequenceReset(message), sessionID );
    
    if( msgTypeValue == "5" )
      return onMessage( Logout(message), sessionID );
    
    if( msgTypeValue == "A" )
      return onMessage( Logon(message), sessionID );
    
    if( msgTypeValue == "n" )
      return onMessage( XMLnonFIX(message), sessionID );
    
    return onMessage( Message(message), sessionID );
  }
  
  /// Preserve the version-message entry point and mutable callback behavior.
void crack( Message& message, 
            const FIX::SessionID& sessionID )
  {
    crack( static_cast<FIX::Message&>(message), sessionID );
  }

  /// Copy callback changes back on both normal return and exception propagation.
void crack( FIX::Message& message,
            const FIX::SessionID& sessionID )
  {
    const std::string & msgTypeValue 
      = message.getHeader().getField( FIX::FIELD::MsgType );
    
    
    if( msgTypeValue == "0" )
      return dispatch<Heartbeat>( message, sessionID );
    
    if( msgTypeValue == "1" )
      return dispatch<TestRequest>( message, sessionID );
    
    if( msgTypeValue == "2" )
      return dispatch<ResendRequest>( message, sessionID );
    
    if( msgTypeValue == "3" )
      return dispatch<Reject>( message, sessionID );
    
    if( msgTypeValue == "4" )
      return dispatch<SequenceReset>( message, sessionID );
    
    if( msgTypeValue == "5" )
      return dispatch<Logout>( message, sessionID );
    
    if( msgTypeValue == "A" )
      return dispatch<Logon>( message, sessionID );
    
    if( msgTypeValue == "n" )
      return dispatch<XMLnonFIX>( message, sessionID );
    
    return dispatch<Message>( message, sessionID );
  }

private:
  template <typename T>
  void dispatch( FIX::Message& message, const FIX::SessionID& sessionID )
  {
    T typed(message);
    try {
      onMessage( typed, sessionID );
    } catch (...) {
      message = std::move(typed);
      throw;
    }
    message = std::move(typed);
  }

  };
}

#endif
