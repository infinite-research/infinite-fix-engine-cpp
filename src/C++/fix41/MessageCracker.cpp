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
#include "IOI.h"
#include "Advertisement.h"
#include "ExecutionReport.h"
#include "OrderCancelReject.h"
#include "Logon.h"
#include "News.h"
#include "Email.h"
#include "NewOrderSingle.h"
#include "NewOrderList.h"
#include "OrderCancelRequest.h"
#include "OrderCancelReplaceRequest.h"
#include "OrderStatusRequest.h"
#include "Allocation.h"
#include "ListCancelRequest.h"
#include "ListExecute.h"
#include "ListStatusRequest.h"
#include "ListStatus.h"
#include "AllocationInstructionAck.h"
#include "DontKnowTrade.h"
#include "QuoteRequest.h"
#include "Quote.h"
#include "SettlementInstructions.h"

namespace FIX41
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
    if( msgTypeValue == "6" )
      return crackConst<IOI>( *this, message, sessionID );
    if( msgTypeValue == "7" )
      return crackConst<Advertisement>( *this, message, sessionID );
    if( msgTypeValue == "8" )
      return crackConst<ExecutionReport>( *this, message, sessionID );
    if( msgTypeValue == "9" )
      return crackConst<OrderCancelReject>( *this, message, sessionID );
    if( msgTypeValue == "A" )
      return crackConst<Logon>( *this, message, sessionID );
    if( msgTypeValue == "B" )
      return crackConst<News>( *this, message, sessionID );
    if( msgTypeValue == "C" )
      return crackConst<Email>( *this, message, sessionID );
    if( msgTypeValue == "D" )
      return crackConst<NewOrderSingle>( *this, message, sessionID );
    if( msgTypeValue == "E" )
      return crackConst<NewOrderList>( *this, message, sessionID );
    if( msgTypeValue == "F" )
      return crackConst<OrderCancelRequest>( *this, message, sessionID );
    if( msgTypeValue == "G" )
      return crackConst<OrderCancelReplaceRequest>( *this, message, sessionID );
    if( msgTypeValue == "H" )
      return crackConst<OrderStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "J" )
      return crackConst<Allocation>( *this, message, sessionID );
    if( msgTypeValue == "K" )
      return crackConst<ListCancelRequest>( *this, message, sessionID );
    if( msgTypeValue == "L" )
      return crackConst<ListExecute>( *this, message, sessionID );
    if( msgTypeValue == "M" )
      return crackConst<ListStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "N" )
      return crackConst<ListStatus>( *this, message, sessionID );
    if( msgTypeValue == "P" )
      return crackConst<AllocationInstructionAck>( *this, message, sessionID );
    if( msgTypeValue == "Q" )
      return crackConst<DontKnowTrade>( *this, message, sessionID );
    if( msgTypeValue == "R" )
      return crackConst<QuoteRequest>( *this, message, sessionID );
    if( msgTypeValue == "S" )
      return crackConst<Quote>( *this, message, sessionID );
    if( msgTypeValue == "T" )
      return crackConst<SettlementInstructions>( *this, message, sessionID );

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
    if( msgTypeValue == "6" )
      return crackMutable<IOI>( *this, message, sessionID );
    if( msgTypeValue == "7" )
      return crackMutable<Advertisement>( *this, message, sessionID );
    if( msgTypeValue == "8" )
      return crackMutable<ExecutionReport>( *this, message, sessionID );
    if( msgTypeValue == "9" )
      return crackMutable<OrderCancelReject>( *this, message, sessionID );
    if( msgTypeValue == "A" )
      return crackMutable<Logon>( *this, message, sessionID );
    if( msgTypeValue == "B" )
      return crackMutable<News>( *this, message, sessionID );
    if( msgTypeValue == "C" )
      return crackMutable<Email>( *this, message, sessionID );
    if( msgTypeValue == "D" )
      return crackMutable<NewOrderSingle>( *this, message, sessionID );
    if( msgTypeValue == "E" )
      return crackMutable<NewOrderList>( *this, message, sessionID );
    if( msgTypeValue == "F" )
      return crackMutable<OrderCancelRequest>( *this, message, sessionID );
    if( msgTypeValue == "G" )
      return crackMutable<OrderCancelReplaceRequest>( *this, message, sessionID );
    if( msgTypeValue == "H" )
      return crackMutable<OrderStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "J" )
      return crackMutable<Allocation>( *this, message, sessionID );
    if( msgTypeValue == "K" )
      return crackMutable<ListCancelRequest>( *this, message, sessionID );
    if( msgTypeValue == "L" )
      return crackMutable<ListExecute>( *this, message, sessionID );
    if( msgTypeValue == "M" )
      return crackMutable<ListStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "N" )
      return crackMutable<ListStatus>( *this, message, sessionID );
    if( msgTypeValue == "P" )
      return crackMutable<AllocationInstructionAck>( *this, message, sessionID );
    if( msgTypeValue == "Q" )
      return crackMutable<DontKnowTrade>( *this, message, sessionID );
    if( msgTypeValue == "R" )
      return crackMutable<QuoteRequest>( *this, message, sessionID );
    if( msgTypeValue == "S" )
      return crackMutable<Quote>( *this, message, sessionID );
    if( msgTypeValue == "T" )
      return crackMutable<SettlementInstructions>( *this, message, sessionID );

    return crackMutable<Message>( *this, message, sessionID );
  }
}
