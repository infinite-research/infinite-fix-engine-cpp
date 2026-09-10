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

#ifndef FIX42_MESSAGECRACKER_H
#define FIX42_MESSAGECRACKER_H


#include "../SessionID.h"
#include "../Exceptions.h"
#include <utility>

#include "../fix42/Message.h"
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
#include "MarketDataRequest.h"
#include "MarketDataSnapshotFullRefresh.h"
#include "MarketDataIncrementalRefresh.h"
#include "MarketDataRequestReject.h"
#include "QuoteCancel.h"
#include "QuoteStatusRequest.h"
#include "QuoteAcknowledgement.h"
#include "SecurityDefinitionRequest.h"
#include "SecurityDefinition.h"
#include "SecurityStatusRequest.h"
#include "SecurityStatus.h"
#include "TradingSessionStatusRequest.h"
#include "TradingSessionStatus.h"
#include "MassQuote.h"
#include "BusinessMessageReject.h"
#include "BidRequest.h"
#include "BidResponse.h"
#include "ListStrikePrice.h"

namespace FIX42
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
  virtual void onMessage( const IOI&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Advertisement&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ExecutionReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderCancelReject&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Logon&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const News&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Email&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const NewOrderSingle&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const NewOrderList&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderCancelRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderCancelReplaceRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Allocation&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListCancelRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListExecute&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListStatus&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AllocationInstructionAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const DontKnowTrade&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Quote&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SettlementInstructions&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDataRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDataSnapshotFullRefresh&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDataIncrementalRefresh&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDataRequestReject&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteCancel&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteAcknowledgement&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityDefinitionRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityDefinition&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityStatus&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradingSessionStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradingSessionStatus&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MassQuote&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const BusinessMessageReject&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const BidRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const BidResponse&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListStrikePrice&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( Heartbeat&, const FIX::SessionID& ) {} 
 virtual void onMessage( TestRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ResendRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( Reject&, const FIX::SessionID& ) {} 
 virtual void onMessage( SequenceReset&, const FIX::SessionID& ) {} 
 virtual void onMessage( Logout&, const FIX::SessionID& ) {} 
 virtual void onMessage( IOI&, const FIX::SessionID& ) {} 
 virtual void onMessage( Advertisement&, const FIX::SessionID& ) {} 
 virtual void onMessage( ExecutionReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelReject&, const FIX::SessionID& ) {} 
 virtual void onMessage( Logon&, const FIX::SessionID& ) {} 
 virtual void onMessage( News&, const FIX::SessionID& ) {} 
 virtual void onMessage( Email&, const FIX::SessionID& ) {} 
 virtual void onMessage( NewOrderSingle&, const FIX::SessionID& ) {} 
 virtual void onMessage( NewOrderList&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelReplaceRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( Allocation&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListCancelRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListExecute&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListStatus&, const FIX::SessionID& ) {} 
 virtual void onMessage( AllocationInstructionAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( DontKnowTrade&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( Quote&, const FIX::SessionID& ) {} 
 virtual void onMessage( SettlementInstructions&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDataRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDataSnapshotFullRefresh&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDataIncrementalRefresh&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDataRequestReject&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteCancel&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteAcknowledgement&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityDefinitionRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityDefinition&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityStatus&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradingSessionStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradingSessionStatus&, const FIX::SessionID& ) {} 
 virtual void onMessage( MassQuote&, const FIX::SessionID& ) {} 
 virtual void onMessage( BusinessMessageReject&, const FIX::SessionID& ) {} 
 virtual void onMessage( BidRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( BidResponse&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListStrikePrice&, const FIX::SessionID& ) {} 

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
    
    if( msgTypeValue == "6" )
      return onMessage( IOI(message), sessionID );
    
    if( msgTypeValue == "7" )
      return onMessage( Advertisement(message), sessionID );
    
    if( msgTypeValue == "8" )
      return onMessage( ExecutionReport(message), sessionID );
    
    if( msgTypeValue == "9" )
      return onMessage( OrderCancelReject(message), sessionID );
    
    if( msgTypeValue == "A" )
      return onMessage( Logon(message), sessionID );
    
    if( msgTypeValue == "B" )
      return onMessage( News(message), sessionID );
    
    if( msgTypeValue == "C" )
      return onMessage( Email(message), sessionID );
    
    if( msgTypeValue == "D" )
      return onMessage( NewOrderSingle(message), sessionID );
    
    if( msgTypeValue == "E" )
      return onMessage( NewOrderList(message), sessionID );
    
    if( msgTypeValue == "F" )
      return onMessage( OrderCancelRequest(message), sessionID );
    
    if( msgTypeValue == "G" )
      return onMessage( OrderCancelReplaceRequest(message), sessionID );
    
    if( msgTypeValue == "H" )
      return onMessage( OrderStatusRequest(message), sessionID );
    
    if( msgTypeValue == "J" )
      return onMessage( Allocation(message), sessionID );
    
    if( msgTypeValue == "K" )
      return onMessage( ListCancelRequest(message), sessionID );
    
    if( msgTypeValue == "L" )
      return onMessage( ListExecute(message), sessionID );
    
    if( msgTypeValue == "M" )
      return onMessage( ListStatusRequest(message), sessionID );
    
    if( msgTypeValue == "N" )
      return onMessage( ListStatus(message), sessionID );
    
    if( msgTypeValue == "P" )
      return onMessage( AllocationInstructionAck(message), sessionID );
    
    if( msgTypeValue == "Q" )
      return onMessage( DontKnowTrade(message), sessionID );
    
    if( msgTypeValue == "R" )
      return onMessage( QuoteRequest(message), sessionID );
    
    if( msgTypeValue == "S" )
      return onMessage( Quote(message), sessionID );
    
    if( msgTypeValue == "T" )
      return onMessage( SettlementInstructions(message), sessionID );
    
    if( msgTypeValue == "V" )
      return onMessage( MarketDataRequest(message), sessionID );
    
    if( msgTypeValue == "W" )
      return onMessage( MarketDataSnapshotFullRefresh(message), sessionID );
    
    if( msgTypeValue == "X" )
      return onMessage( MarketDataIncrementalRefresh(message), sessionID );
    
    if( msgTypeValue == "Y" )
      return onMessage( MarketDataRequestReject(message), sessionID );
    
    if( msgTypeValue == "Z" )
      return onMessage( QuoteCancel(message), sessionID );
    
    if( msgTypeValue == "a" )
      return onMessage( QuoteStatusRequest(message), sessionID );
    
    if( msgTypeValue == "b" )
      return onMessage( QuoteAcknowledgement(message), sessionID );
    
    if( msgTypeValue == "c" )
      return onMessage( SecurityDefinitionRequest(message), sessionID );
    
    if( msgTypeValue == "d" )
      return onMessage( SecurityDefinition(message), sessionID );
    
    if( msgTypeValue == "e" )
      return onMessage( SecurityStatusRequest(message), sessionID );
    
    if( msgTypeValue == "f" )
      return onMessage( SecurityStatus(message), sessionID );
    
    if( msgTypeValue == "g" )
      return onMessage( TradingSessionStatusRequest(message), sessionID );
    
    if( msgTypeValue == "h" )
      return onMessage( TradingSessionStatus(message), sessionID );
    
    if( msgTypeValue == "i" )
      return onMessage( MassQuote(message), sessionID );
    
    if( msgTypeValue == "j" )
      return onMessage( BusinessMessageReject(message), sessionID );
    
    if( msgTypeValue == "k" )
      return onMessage( BidRequest(message), sessionID );
    
    if( msgTypeValue == "l" )
      return onMessage( BidResponse(message), sessionID );
    
    if( msgTypeValue == "m" )
      return onMessage( ListStrikePrice(message), sessionID );
    
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
    
    if( msgTypeValue == "6" )
      return dispatch<IOI>( message, sessionID );
    
    if( msgTypeValue == "7" )
      return dispatch<Advertisement>( message, sessionID );
    
    if( msgTypeValue == "8" )
      return dispatch<ExecutionReport>( message, sessionID );
    
    if( msgTypeValue == "9" )
      return dispatch<OrderCancelReject>( message, sessionID );
    
    if( msgTypeValue == "A" )
      return dispatch<Logon>( message, sessionID );
    
    if( msgTypeValue == "B" )
      return dispatch<News>( message, sessionID );
    
    if( msgTypeValue == "C" )
      return dispatch<Email>( message, sessionID );
    
    if( msgTypeValue == "D" )
      return dispatch<NewOrderSingle>( message, sessionID );
    
    if( msgTypeValue == "E" )
      return dispatch<NewOrderList>( message, sessionID );
    
    if( msgTypeValue == "F" )
      return dispatch<OrderCancelRequest>( message, sessionID );
    
    if( msgTypeValue == "G" )
      return dispatch<OrderCancelReplaceRequest>( message, sessionID );
    
    if( msgTypeValue == "H" )
      return dispatch<OrderStatusRequest>( message, sessionID );
    
    if( msgTypeValue == "J" )
      return dispatch<Allocation>( message, sessionID );
    
    if( msgTypeValue == "K" )
      return dispatch<ListCancelRequest>( message, sessionID );
    
    if( msgTypeValue == "L" )
      return dispatch<ListExecute>( message, sessionID );
    
    if( msgTypeValue == "M" )
      return dispatch<ListStatusRequest>( message, sessionID );
    
    if( msgTypeValue == "N" )
      return dispatch<ListStatus>( message, sessionID );
    
    if( msgTypeValue == "P" )
      return dispatch<AllocationInstructionAck>( message, sessionID );
    
    if( msgTypeValue == "Q" )
      return dispatch<DontKnowTrade>( message, sessionID );
    
    if( msgTypeValue == "R" )
      return dispatch<QuoteRequest>( message, sessionID );
    
    if( msgTypeValue == "S" )
      return dispatch<Quote>( message, sessionID );
    
    if( msgTypeValue == "T" )
      return dispatch<SettlementInstructions>( message, sessionID );
    
    if( msgTypeValue == "V" )
      return dispatch<MarketDataRequest>( message, sessionID );
    
    if( msgTypeValue == "W" )
      return dispatch<MarketDataSnapshotFullRefresh>( message, sessionID );
    
    if( msgTypeValue == "X" )
      return dispatch<MarketDataIncrementalRefresh>( message, sessionID );
    
    if( msgTypeValue == "Y" )
      return dispatch<MarketDataRequestReject>( message, sessionID );
    
    if( msgTypeValue == "Z" )
      return dispatch<QuoteCancel>( message, sessionID );
    
    if( msgTypeValue == "a" )
      return dispatch<QuoteStatusRequest>( message, sessionID );
    
    if( msgTypeValue == "b" )
      return dispatch<QuoteAcknowledgement>( message, sessionID );
    
    if( msgTypeValue == "c" )
      return dispatch<SecurityDefinitionRequest>( message, sessionID );
    
    if( msgTypeValue == "d" )
      return dispatch<SecurityDefinition>( message, sessionID );
    
    if( msgTypeValue == "e" )
      return dispatch<SecurityStatusRequest>( message, sessionID );
    
    if( msgTypeValue == "f" )
      return dispatch<SecurityStatus>( message, sessionID );
    
    if( msgTypeValue == "g" )
      return dispatch<TradingSessionStatusRequest>( message, sessionID );
    
    if( msgTypeValue == "h" )
      return dispatch<TradingSessionStatus>( message, sessionID );
    
    if( msgTypeValue == "i" )
      return dispatch<MassQuote>( message, sessionID );
    
    if( msgTypeValue == "j" )
      return dispatch<BusinessMessageReject>( message, sessionID );
    
    if( msgTypeValue == "k" )
      return dispatch<BidRequest>( message, sessionID );
    
    if( msgTypeValue == "l" )
      return dispatch<BidResponse>( message, sessionID );
    
    if( msgTypeValue == "m" )
      return dispatch<ListStrikePrice>( message, sessionID );
    
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
