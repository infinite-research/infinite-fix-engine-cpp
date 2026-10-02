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

#include "IOI.h"
#include "Advertisement.h"
#include "ExecutionReport.h"
#include "OrderCancelReject.h"
#include "News.h"
#include "Email.h"
#include "NewOrderSingle.h"
#include "NewOrderList.h"
#include "OrderCancelRequest.h"
#include "OrderCancelReplaceRequest.h"
#include "OrderStatusRequest.h"
#include "AllocationInstruction.h"
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
#include "MassQuoteAck.h"
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
#include "RegistrationInstructions.h"
#include "RegistrationInstructionsResponse.h"
#include "OrderMassCancelRequest.h"
#include "OrderMassCancelReport.h"
#include "NewOrderCross.h"
#include "CrossOrderCancelReplaceRequest.h"
#include "CrossOrderCancelRequest.h"
#include "SecurityTypeRequest.h"
#include "SecurityTypes.h"
#include "SecurityListRequest.h"
#include "SecurityList.h"
#include "DerivativeSecurityListRequest.h"
#include "DerivativeSecurityList.h"
#include "NewOrderMultileg.h"
#include "MultilegOrderCancelReplace.h"
#include "TradeCaptureReportRequest.h"
#include "TradeCaptureReport.h"
#include "OrderMassStatusRequest.h"
#include "QuoteRequestReject.h"
#include "RFQRequest.h"
#include "QuoteStatusReport.h"
#include "QuoteResponse.h"
#include "Confirmation.h"
#include "PositionMaintenanceRequest.h"
#include "PositionMaintenanceReport.h"
#include "RequestForPositions.h"
#include "RequestForPositionsAck.h"
#include "PositionReport.h"
#include "TradeCaptureReportRequestAck.h"
#include "TradeCaptureReportAck.h"
#include "AllocationReport.h"
#include "AllocationReportAck.h"
#include "ConfirmationAck.h"
#include "SettlementInstructionRequest.h"
#include "AssignmentReport.h"
#include "CollateralRequest.h"
#include "CollateralAssignment.h"
#include "CollateralResponse.h"
#include "CollateralReport.h"
#include "CollateralInquiry.h"
#include "NetworkCounterpartySystemStatusRequest.h"
#include "NetworkCounterpartySystemStatusResponse.h"
#include "UserRequest.h"
#include "UserResponse.h"
#include "CollateralInquiryAck.h"
#include "ConfirmationRequest.h"
#include "ContraryIntentionReport.h"
#include "SecurityDefinitionUpdateReport.h"
#include "SecurityListUpdateReport.h"
#include "AdjustedPositionReport.h"
#include "AllocationInstructionAlert.h"
#include "ExecutionAck.h"
#include "TradingSessionList.h"
#include "TradingSessionListRequest.h"
#include "SettlementObligationReport.h"
#include "DerivativeSecurityListUpdateReport.h"
#include "TradingSessionListUpdateReport.h"
#include "MarketDefinitionRequest.h"
#include "MarketDefinition.h"
#include "MarketDefinitionUpdateReport.h"
#include "UserNotification.h"
#include "OrderMassActionReport.h"
#include "OrderMassActionRequest.h"
#include "ApplicationMessageRequest.h"
#include "ApplicationMessageRequestAck.h"
#include "ApplicationMessageReport.h"
#include "StreamAssignmentRequest.h"
#include "StreamAssignmentReport.h"
#include "StreamAssignmentReportACK.h"
#include "MarginRequirementInquiry.h"
#include "MarginRequirementInquiryAck.h"
#include "MarginRequirementReport.h"
#include "PartyDetailsListRequest.h"
#include "PartyDetailsListReport.h"
#include "PartyDetailsListUpdateReport.h"
#include "PartyRiskLimitsRequest.h"
#include "PartyRiskLimitsReport.h"
#include "SecurityMassStatusRequest.h"
#include "SecurityMassStatus.h"
#include "AccountSummaryReport.h"
#include "PartyRiskLimitsUpdateReport.h"
#include "PartyRiskLimitsDefinitionRequest.h"
#include "PartyRiskLimitsDefinitionRequestAck.h"
#include "PartyEntitlementsRequest.h"
#include "PartyEntitlementsReport.h"
#include "QuoteAck.h"
#include "PartyDetailsDefinitionRequest.h"
#include "PartyDetailsDefinitionRequestAck.h"
#include "PartyEntitlementsUpdateReport.h"
#include "PartyEntitlementsDefinitionRequest.h"
#include "PartyEntitlementsDefinitionRequestAck.h"
#include "TradeMatchReport.h"
#include "TradeMatchReportAck.h"
#include "PartyRiskLimitsReportAck.h"
#include "PartyRiskLimitCheckRequest.h"
#include "PartyRiskLimitCheckRequestAck.h"
#include "PartyActionRequest.h"
#include "PartyActionReport.h"
#include "MassOrder.h"
#include "MassOrderAck.h"
#include "PositionTransferInstruction.h"
#include "PositionTransferInstructionAck.h"
#include "PositionTransferReport.h"
#include "MarketDataStatisticsRequest.h"
#include "MarketDataStatisticsReport.h"
#include "CollateralReportAck.h"
#include "MarketDataReport.h"
#include "CrossRequest.h"
#include "CrossRequestAck.h"
#include "AllocationInstructionAlertRequest.h"
#include "AllocationInstructionAlertRequestAck.h"
#include "TradeAggregationRequest.h"
#include "TradeAggregationReport.h"
#include "PayManagementReport.h"
#include "PayManagementReportAck.h"
#include "PayManagementRequest.h"
#include "PayManagementRequestAck.h"

namespace FIX50SP2
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

    if( msgTypeValue == "6" )
      return crackConst<IOI>( *this, message, sessionID );
    if( msgTypeValue == "7" )
      return crackConst<Advertisement>( *this, message, sessionID );
    if( msgTypeValue == "8" )
      return crackConst<ExecutionReport>( *this, message, sessionID );
    if( msgTypeValue == "9" )
      return crackConst<OrderCancelReject>( *this, message, sessionID );
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
      return crackConst<AllocationInstruction>( *this, message, sessionID );
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
    if( msgTypeValue == "V" )
      return crackConst<MarketDataRequest>( *this, message, sessionID );
    if( msgTypeValue == "W" )
      return crackConst<MarketDataSnapshotFullRefresh>( *this, message, sessionID );
    if( msgTypeValue == "X" )
      return crackConst<MarketDataIncrementalRefresh>( *this, message, sessionID );
    if( msgTypeValue == "Y" )
      return crackConst<MarketDataRequestReject>( *this, message, sessionID );
    if( msgTypeValue == "Z" )
      return crackConst<QuoteCancel>( *this, message, sessionID );
    if( msgTypeValue == "a" )
      return crackConst<QuoteStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "b" )
      return crackConst<MassQuoteAck>( *this, message, sessionID );
    if( msgTypeValue == "c" )
      return crackConst<SecurityDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "d" )
      return crackConst<SecurityDefinition>( *this, message, sessionID );
    if( msgTypeValue == "e" )
      return crackConst<SecurityStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "f" )
      return crackConst<SecurityStatus>( *this, message, sessionID );
    if( msgTypeValue == "g" )
      return crackConst<TradingSessionStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "h" )
      return crackConst<TradingSessionStatus>( *this, message, sessionID );
    if( msgTypeValue == "i" )
      return crackConst<MassQuote>( *this, message, sessionID );
    if( msgTypeValue == "j" )
      return crackConst<BusinessMessageReject>( *this, message, sessionID );
    if( msgTypeValue == "k" )
      return crackConst<BidRequest>( *this, message, sessionID );
    if( msgTypeValue == "l" )
      return crackConst<BidResponse>( *this, message, sessionID );
    if( msgTypeValue == "m" )
      return crackConst<ListStrikePrice>( *this, message, sessionID );
    if( msgTypeValue == "o" )
      return crackConst<RegistrationInstructions>( *this, message, sessionID );
    if( msgTypeValue == "p" )
      return crackConst<RegistrationInstructionsResponse>( *this, message, sessionID );
    if( msgTypeValue == "q" )
      return crackConst<OrderMassCancelRequest>( *this, message, sessionID );
    if( msgTypeValue == "r" )
      return crackConst<OrderMassCancelReport>( *this, message, sessionID );
    if( msgTypeValue == "s" )
      return crackConst<NewOrderCross>( *this, message, sessionID );
    if( msgTypeValue == "t" )
      return crackConst<CrossOrderCancelReplaceRequest>( *this, message, sessionID );
    if( msgTypeValue == "u" )
      return crackConst<CrossOrderCancelRequest>( *this, message, sessionID );
    if( msgTypeValue == "v" )
      return crackConst<SecurityTypeRequest>( *this, message, sessionID );
    if( msgTypeValue == "w" )
      return crackConst<SecurityTypes>( *this, message, sessionID );
    if( msgTypeValue == "x" )
      return crackConst<SecurityListRequest>( *this, message, sessionID );
    if( msgTypeValue == "y" )
      return crackConst<SecurityList>( *this, message, sessionID );
    if( msgTypeValue == "z" )
      return crackConst<DerivativeSecurityListRequest>( *this, message, sessionID );
    if( msgTypeValue == "AA" )
      return crackConst<DerivativeSecurityList>( *this, message, sessionID );
    if( msgTypeValue == "AB" )
      return crackConst<NewOrderMultileg>( *this, message, sessionID );
    if( msgTypeValue == "AC" )
      return crackConst<MultilegOrderCancelReplace>( *this, message, sessionID );
    if( msgTypeValue == "AD" )
      return crackConst<TradeCaptureReportRequest>( *this, message, sessionID );
    if( msgTypeValue == "AE" )
      return crackConst<TradeCaptureReport>( *this, message, sessionID );
    if( msgTypeValue == "AF" )
      return crackConst<OrderMassStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "AG" )
      return crackConst<QuoteRequestReject>( *this, message, sessionID );
    if( msgTypeValue == "AH" )
      return crackConst<RFQRequest>( *this, message, sessionID );
    if( msgTypeValue == "AI" )
      return crackConst<QuoteStatusReport>( *this, message, sessionID );
    if( msgTypeValue == "AJ" )
      return crackConst<QuoteResponse>( *this, message, sessionID );
    if( msgTypeValue == "AK" )
      return crackConst<Confirmation>( *this, message, sessionID );
    if( msgTypeValue == "AL" )
      return crackConst<PositionMaintenanceRequest>( *this, message, sessionID );
    if( msgTypeValue == "AM" )
      return crackConst<PositionMaintenanceReport>( *this, message, sessionID );
    if( msgTypeValue == "AN" )
      return crackConst<RequestForPositions>( *this, message, sessionID );
    if( msgTypeValue == "AO" )
      return crackConst<RequestForPositionsAck>( *this, message, sessionID );
    if( msgTypeValue == "AP" )
      return crackConst<PositionReport>( *this, message, sessionID );
    if( msgTypeValue == "AQ" )
      return crackConst<TradeCaptureReportRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "AR" )
      return crackConst<TradeCaptureReportAck>( *this, message, sessionID );
    if( msgTypeValue == "AS" )
      return crackConst<AllocationReport>( *this, message, sessionID );
    if( msgTypeValue == "AT" )
      return crackConst<AllocationReportAck>( *this, message, sessionID );
    if( msgTypeValue == "AU" )
      return crackConst<ConfirmationAck>( *this, message, sessionID );
    if( msgTypeValue == "AV" )
      return crackConst<SettlementInstructionRequest>( *this, message, sessionID );
    if( msgTypeValue == "AW" )
      return crackConst<AssignmentReport>( *this, message, sessionID );
    if( msgTypeValue == "AX" )
      return crackConst<CollateralRequest>( *this, message, sessionID );
    if( msgTypeValue == "AY" )
      return crackConst<CollateralAssignment>( *this, message, sessionID );
    if( msgTypeValue == "AZ" )
      return crackConst<CollateralResponse>( *this, message, sessionID );
    if( msgTypeValue == "BA" )
      return crackConst<CollateralReport>( *this, message, sessionID );
    if( msgTypeValue == "BB" )
      return crackConst<CollateralInquiry>( *this, message, sessionID );
    if( msgTypeValue == "BC" )
      return crackConst<NetworkCounterpartySystemStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "BD" )
      return crackConst<NetworkCounterpartySystemStatusResponse>( *this, message, sessionID );
    if( msgTypeValue == "BE" )
      return crackConst<UserRequest>( *this, message, sessionID );
    if( msgTypeValue == "BF" )
      return crackConst<UserResponse>( *this, message, sessionID );
    if( msgTypeValue == "BG" )
      return crackConst<CollateralInquiryAck>( *this, message, sessionID );
    if( msgTypeValue == "BH" )
      return crackConst<ConfirmationRequest>( *this, message, sessionID );
    if( msgTypeValue == "BO" )
      return crackConst<ContraryIntentionReport>( *this, message, sessionID );
    if( msgTypeValue == "BP" )
      return crackConst<SecurityDefinitionUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "BK" )
      return crackConst<SecurityListUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "BL" )
      return crackConst<AdjustedPositionReport>( *this, message, sessionID );
    if( msgTypeValue == "BM" )
      return crackConst<AllocationInstructionAlert>( *this, message, sessionID );
    if( msgTypeValue == "BN" )
      return crackConst<ExecutionAck>( *this, message, sessionID );
    if( msgTypeValue == "BJ" )
      return crackConst<TradingSessionList>( *this, message, sessionID );
    if( msgTypeValue == "BI" )
      return crackConst<TradingSessionListRequest>( *this, message, sessionID );
    if( msgTypeValue == "BQ" )
      return crackConst<SettlementObligationReport>( *this, message, sessionID );
    if( msgTypeValue == "BR" )
      return crackConst<DerivativeSecurityListUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "BS" )
      return crackConst<TradingSessionListUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "BT" )
      return crackConst<MarketDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "BU" )
      return crackConst<MarketDefinition>( *this, message, sessionID );
    if( msgTypeValue == "BV" )
      return crackConst<MarketDefinitionUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "CB" )
      return crackConst<UserNotification>( *this, message, sessionID );
    if( msgTypeValue == "BZ" )
      return crackConst<OrderMassActionReport>( *this, message, sessionID );
    if( msgTypeValue == "CA" )
      return crackConst<OrderMassActionRequest>( *this, message, sessionID );
    if( msgTypeValue == "BW" )
      return crackConst<ApplicationMessageRequest>( *this, message, sessionID );
    if( msgTypeValue == "BX" )
      return crackConst<ApplicationMessageRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "BY" )
      return crackConst<ApplicationMessageReport>( *this, message, sessionID );
    if( msgTypeValue == "CC" )
      return crackConst<StreamAssignmentRequest>( *this, message, sessionID );
    if( msgTypeValue == "CD" )
      return crackConst<StreamAssignmentReport>( *this, message, sessionID );
    if( msgTypeValue == "CE" )
      return crackConst<StreamAssignmentReportACK>( *this, message, sessionID );
    if( msgTypeValue == "CH" )
      return crackConst<MarginRequirementInquiry>( *this, message, sessionID );
    if( msgTypeValue == "CI" )
      return crackConst<MarginRequirementInquiryAck>( *this, message, sessionID );
    if( msgTypeValue == "CJ" )
      return crackConst<MarginRequirementReport>( *this, message, sessionID );
    if( msgTypeValue == "CF" )
      return crackConst<PartyDetailsListRequest>( *this, message, sessionID );
    if( msgTypeValue == "CG" )
      return crackConst<PartyDetailsListReport>( *this, message, sessionID );
    if( msgTypeValue == "CK" )
      return crackConst<PartyDetailsListUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "CL" )
      return crackConst<PartyRiskLimitsRequest>( *this, message, sessionID );
    if( msgTypeValue == "CM" )
      return crackConst<PartyRiskLimitsReport>( *this, message, sessionID );
    if( msgTypeValue == "CN" )
      return crackConst<SecurityMassStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "CO" )
      return crackConst<SecurityMassStatus>( *this, message, sessionID );
    if( msgTypeValue == "CQ" )
      return crackConst<AccountSummaryReport>( *this, message, sessionID );
    if( msgTypeValue == "CR" )
      return crackConst<PartyRiskLimitsUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "CS" )
      return crackConst<PartyRiskLimitsDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "CT" )
      return crackConst<PartyRiskLimitsDefinitionRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "CU" )
      return crackConst<PartyEntitlementsRequest>( *this, message, sessionID );
    if( msgTypeValue == "CV" )
      return crackConst<PartyEntitlementsReport>( *this, message, sessionID );
    if( msgTypeValue == "CW" )
      return crackConst<QuoteAck>( *this, message, sessionID );
    if( msgTypeValue == "CX" )
      return crackConst<PartyDetailsDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "CY" )
      return crackConst<PartyDetailsDefinitionRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "CZ" )
      return crackConst<PartyEntitlementsUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "DA" )
      return crackConst<PartyEntitlementsDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "DB" )
      return crackConst<PartyEntitlementsDefinitionRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "DC" )
      return crackConst<TradeMatchReport>( *this, message, sessionID );
    if( msgTypeValue == "DD" )
      return crackConst<TradeMatchReportAck>( *this, message, sessionID );
    if( msgTypeValue == "DE" )
      return crackConst<PartyRiskLimitsReportAck>( *this, message, sessionID );
    if( msgTypeValue == "DF" )
      return crackConst<PartyRiskLimitCheckRequest>( *this, message, sessionID );
    if( msgTypeValue == "DG" )
      return crackConst<PartyRiskLimitCheckRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "DH" )
      return crackConst<PartyActionRequest>( *this, message, sessionID );
    if( msgTypeValue == "DI" )
      return crackConst<PartyActionReport>( *this, message, sessionID );
    if( msgTypeValue == "DJ" )
      return crackConst<MassOrder>( *this, message, sessionID );
    if( msgTypeValue == "DK" )
      return crackConst<MassOrderAck>( *this, message, sessionID );
    if( msgTypeValue == "DL" )
      return crackConst<PositionTransferInstruction>( *this, message, sessionID );
    if( msgTypeValue == "DM" )
      return crackConst<PositionTransferInstructionAck>( *this, message, sessionID );
    if( msgTypeValue == "DN" )
      return crackConst<PositionTransferReport>( *this, message, sessionID );
    if( msgTypeValue == "DO" )
      return crackConst<MarketDataStatisticsRequest>( *this, message, sessionID );
    if( msgTypeValue == "DP" )
      return crackConst<MarketDataStatisticsReport>( *this, message, sessionID );
    if( msgTypeValue == "DQ" )
      return crackConst<CollateralReportAck>( *this, message, sessionID );
    if( msgTypeValue == "DR" )
      return crackConst<MarketDataReport>( *this, message, sessionID );
    if( msgTypeValue == "DS" )
      return crackConst<CrossRequest>( *this, message, sessionID );
    if( msgTypeValue == "DT" )
      return crackConst<CrossRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "DU" )
      return crackConst<AllocationInstructionAlertRequest>( *this, message, sessionID );
    if( msgTypeValue == "DV" )
      return crackConst<AllocationInstructionAlertRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "DW" )
      return crackConst<TradeAggregationRequest>( *this, message, sessionID );
    if( msgTypeValue == "DX" )
      return crackConst<TradeAggregationReport>( *this, message, sessionID );
    if( msgTypeValue == "EA" )
      return crackConst<PayManagementReport>( *this, message, sessionID );
    if( msgTypeValue == "EB" )
      return crackConst<PayManagementReportAck>( *this, message, sessionID );
    if( msgTypeValue == "DY" )
      return crackConst<PayManagementRequest>( *this, message, sessionID );
    if( msgTypeValue == "DZ" )
      return crackConst<PayManagementRequestAck>( *this, message, sessionID );

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

    if( msgTypeValue == "6" )
      return crackMutable<IOI>( *this, message, sessionID );
    if( msgTypeValue == "7" )
      return crackMutable<Advertisement>( *this, message, sessionID );
    if( msgTypeValue == "8" )
      return crackMutable<ExecutionReport>( *this, message, sessionID );
    if( msgTypeValue == "9" )
      return crackMutable<OrderCancelReject>( *this, message, sessionID );
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
      return crackMutable<AllocationInstruction>( *this, message, sessionID );
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
    if( msgTypeValue == "V" )
      return crackMutable<MarketDataRequest>( *this, message, sessionID );
    if( msgTypeValue == "W" )
      return crackMutable<MarketDataSnapshotFullRefresh>( *this, message, sessionID );
    if( msgTypeValue == "X" )
      return crackMutable<MarketDataIncrementalRefresh>( *this, message, sessionID );
    if( msgTypeValue == "Y" )
      return crackMutable<MarketDataRequestReject>( *this, message, sessionID );
    if( msgTypeValue == "Z" )
      return crackMutable<QuoteCancel>( *this, message, sessionID );
    if( msgTypeValue == "a" )
      return crackMutable<QuoteStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "b" )
      return crackMutable<MassQuoteAck>( *this, message, sessionID );
    if( msgTypeValue == "c" )
      return crackMutable<SecurityDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "d" )
      return crackMutable<SecurityDefinition>( *this, message, sessionID );
    if( msgTypeValue == "e" )
      return crackMutable<SecurityStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "f" )
      return crackMutable<SecurityStatus>( *this, message, sessionID );
    if( msgTypeValue == "g" )
      return crackMutable<TradingSessionStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "h" )
      return crackMutable<TradingSessionStatus>( *this, message, sessionID );
    if( msgTypeValue == "i" )
      return crackMutable<MassQuote>( *this, message, sessionID );
    if( msgTypeValue == "j" )
      return crackMutable<BusinessMessageReject>( *this, message, sessionID );
    if( msgTypeValue == "k" )
      return crackMutable<BidRequest>( *this, message, sessionID );
    if( msgTypeValue == "l" )
      return crackMutable<BidResponse>( *this, message, sessionID );
    if( msgTypeValue == "m" )
      return crackMutable<ListStrikePrice>( *this, message, sessionID );
    if( msgTypeValue == "o" )
      return crackMutable<RegistrationInstructions>( *this, message, sessionID );
    if( msgTypeValue == "p" )
      return crackMutable<RegistrationInstructionsResponse>( *this, message, sessionID );
    if( msgTypeValue == "q" )
      return crackMutable<OrderMassCancelRequest>( *this, message, sessionID );
    if( msgTypeValue == "r" )
      return crackMutable<OrderMassCancelReport>( *this, message, sessionID );
    if( msgTypeValue == "s" )
      return crackMutable<NewOrderCross>( *this, message, sessionID );
    if( msgTypeValue == "t" )
      return crackMutable<CrossOrderCancelReplaceRequest>( *this, message, sessionID );
    if( msgTypeValue == "u" )
      return crackMutable<CrossOrderCancelRequest>( *this, message, sessionID );
    if( msgTypeValue == "v" )
      return crackMutable<SecurityTypeRequest>( *this, message, sessionID );
    if( msgTypeValue == "w" )
      return crackMutable<SecurityTypes>( *this, message, sessionID );
    if( msgTypeValue == "x" )
      return crackMutable<SecurityListRequest>( *this, message, sessionID );
    if( msgTypeValue == "y" )
      return crackMutable<SecurityList>( *this, message, sessionID );
    if( msgTypeValue == "z" )
      return crackMutable<DerivativeSecurityListRequest>( *this, message, sessionID );
    if( msgTypeValue == "AA" )
      return crackMutable<DerivativeSecurityList>( *this, message, sessionID );
    if( msgTypeValue == "AB" )
      return crackMutable<NewOrderMultileg>( *this, message, sessionID );
    if( msgTypeValue == "AC" )
      return crackMutable<MultilegOrderCancelReplace>( *this, message, sessionID );
    if( msgTypeValue == "AD" )
      return crackMutable<TradeCaptureReportRequest>( *this, message, sessionID );
    if( msgTypeValue == "AE" )
      return crackMutable<TradeCaptureReport>( *this, message, sessionID );
    if( msgTypeValue == "AF" )
      return crackMutable<OrderMassStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "AG" )
      return crackMutable<QuoteRequestReject>( *this, message, sessionID );
    if( msgTypeValue == "AH" )
      return crackMutable<RFQRequest>( *this, message, sessionID );
    if( msgTypeValue == "AI" )
      return crackMutable<QuoteStatusReport>( *this, message, sessionID );
    if( msgTypeValue == "AJ" )
      return crackMutable<QuoteResponse>( *this, message, sessionID );
    if( msgTypeValue == "AK" )
      return crackMutable<Confirmation>( *this, message, sessionID );
    if( msgTypeValue == "AL" )
      return crackMutable<PositionMaintenanceRequest>( *this, message, sessionID );
    if( msgTypeValue == "AM" )
      return crackMutable<PositionMaintenanceReport>( *this, message, sessionID );
    if( msgTypeValue == "AN" )
      return crackMutable<RequestForPositions>( *this, message, sessionID );
    if( msgTypeValue == "AO" )
      return crackMutable<RequestForPositionsAck>( *this, message, sessionID );
    if( msgTypeValue == "AP" )
      return crackMutable<PositionReport>( *this, message, sessionID );
    if( msgTypeValue == "AQ" )
      return crackMutable<TradeCaptureReportRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "AR" )
      return crackMutable<TradeCaptureReportAck>( *this, message, sessionID );
    if( msgTypeValue == "AS" )
      return crackMutable<AllocationReport>( *this, message, sessionID );
    if( msgTypeValue == "AT" )
      return crackMutable<AllocationReportAck>( *this, message, sessionID );
    if( msgTypeValue == "AU" )
      return crackMutable<ConfirmationAck>( *this, message, sessionID );
    if( msgTypeValue == "AV" )
      return crackMutable<SettlementInstructionRequest>( *this, message, sessionID );
    if( msgTypeValue == "AW" )
      return crackMutable<AssignmentReport>( *this, message, sessionID );
    if( msgTypeValue == "AX" )
      return crackMutable<CollateralRequest>( *this, message, sessionID );
    if( msgTypeValue == "AY" )
      return crackMutable<CollateralAssignment>( *this, message, sessionID );
    if( msgTypeValue == "AZ" )
      return crackMutable<CollateralResponse>( *this, message, sessionID );
    if( msgTypeValue == "BA" )
      return crackMutable<CollateralReport>( *this, message, sessionID );
    if( msgTypeValue == "BB" )
      return crackMutable<CollateralInquiry>( *this, message, sessionID );
    if( msgTypeValue == "BC" )
      return crackMutable<NetworkCounterpartySystemStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "BD" )
      return crackMutable<NetworkCounterpartySystemStatusResponse>( *this, message, sessionID );
    if( msgTypeValue == "BE" )
      return crackMutable<UserRequest>( *this, message, sessionID );
    if( msgTypeValue == "BF" )
      return crackMutable<UserResponse>( *this, message, sessionID );
    if( msgTypeValue == "BG" )
      return crackMutable<CollateralInquiryAck>( *this, message, sessionID );
    if( msgTypeValue == "BH" )
      return crackMutable<ConfirmationRequest>( *this, message, sessionID );
    if( msgTypeValue == "BO" )
      return crackMutable<ContraryIntentionReport>( *this, message, sessionID );
    if( msgTypeValue == "BP" )
      return crackMutable<SecurityDefinitionUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "BK" )
      return crackMutable<SecurityListUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "BL" )
      return crackMutable<AdjustedPositionReport>( *this, message, sessionID );
    if( msgTypeValue == "BM" )
      return crackMutable<AllocationInstructionAlert>( *this, message, sessionID );
    if( msgTypeValue == "BN" )
      return crackMutable<ExecutionAck>( *this, message, sessionID );
    if( msgTypeValue == "BJ" )
      return crackMutable<TradingSessionList>( *this, message, sessionID );
    if( msgTypeValue == "BI" )
      return crackMutable<TradingSessionListRequest>( *this, message, sessionID );
    if( msgTypeValue == "BQ" )
      return crackMutable<SettlementObligationReport>( *this, message, sessionID );
    if( msgTypeValue == "BR" )
      return crackMutable<DerivativeSecurityListUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "BS" )
      return crackMutable<TradingSessionListUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "BT" )
      return crackMutable<MarketDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "BU" )
      return crackMutable<MarketDefinition>( *this, message, sessionID );
    if( msgTypeValue == "BV" )
      return crackMutable<MarketDefinitionUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "CB" )
      return crackMutable<UserNotification>( *this, message, sessionID );
    if( msgTypeValue == "BZ" )
      return crackMutable<OrderMassActionReport>( *this, message, sessionID );
    if( msgTypeValue == "CA" )
      return crackMutable<OrderMassActionRequest>( *this, message, sessionID );
    if( msgTypeValue == "BW" )
      return crackMutable<ApplicationMessageRequest>( *this, message, sessionID );
    if( msgTypeValue == "BX" )
      return crackMutable<ApplicationMessageRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "BY" )
      return crackMutable<ApplicationMessageReport>( *this, message, sessionID );
    if( msgTypeValue == "CC" )
      return crackMutable<StreamAssignmentRequest>( *this, message, sessionID );
    if( msgTypeValue == "CD" )
      return crackMutable<StreamAssignmentReport>( *this, message, sessionID );
    if( msgTypeValue == "CE" )
      return crackMutable<StreamAssignmentReportACK>( *this, message, sessionID );
    if( msgTypeValue == "CH" )
      return crackMutable<MarginRequirementInquiry>( *this, message, sessionID );
    if( msgTypeValue == "CI" )
      return crackMutable<MarginRequirementInquiryAck>( *this, message, sessionID );
    if( msgTypeValue == "CJ" )
      return crackMutable<MarginRequirementReport>( *this, message, sessionID );
    if( msgTypeValue == "CF" )
      return crackMutable<PartyDetailsListRequest>( *this, message, sessionID );
    if( msgTypeValue == "CG" )
      return crackMutable<PartyDetailsListReport>( *this, message, sessionID );
    if( msgTypeValue == "CK" )
      return crackMutable<PartyDetailsListUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "CL" )
      return crackMutable<PartyRiskLimitsRequest>( *this, message, sessionID );
    if( msgTypeValue == "CM" )
      return crackMutable<PartyRiskLimitsReport>( *this, message, sessionID );
    if( msgTypeValue == "CN" )
      return crackMutable<SecurityMassStatusRequest>( *this, message, sessionID );
    if( msgTypeValue == "CO" )
      return crackMutable<SecurityMassStatus>( *this, message, sessionID );
    if( msgTypeValue == "CQ" )
      return crackMutable<AccountSummaryReport>( *this, message, sessionID );
    if( msgTypeValue == "CR" )
      return crackMutable<PartyRiskLimitsUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "CS" )
      return crackMutable<PartyRiskLimitsDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "CT" )
      return crackMutable<PartyRiskLimitsDefinitionRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "CU" )
      return crackMutable<PartyEntitlementsRequest>( *this, message, sessionID );
    if( msgTypeValue == "CV" )
      return crackMutable<PartyEntitlementsReport>( *this, message, sessionID );
    if( msgTypeValue == "CW" )
      return crackMutable<QuoteAck>( *this, message, sessionID );
    if( msgTypeValue == "CX" )
      return crackMutable<PartyDetailsDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "CY" )
      return crackMutable<PartyDetailsDefinitionRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "CZ" )
      return crackMutable<PartyEntitlementsUpdateReport>( *this, message, sessionID );
    if( msgTypeValue == "DA" )
      return crackMutable<PartyEntitlementsDefinitionRequest>( *this, message, sessionID );
    if( msgTypeValue == "DB" )
      return crackMutable<PartyEntitlementsDefinitionRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "DC" )
      return crackMutable<TradeMatchReport>( *this, message, sessionID );
    if( msgTypeValue == "DD" )
      return crackMutable<TradeMatchReportAck>( *this, message, sessionID );
    if( msgTypeValue == "DE" )
      return crackMutable<PartyRiskLimitsReportAck>( *this, message, sessionID );
    if( msgTypeValue == "DF" )
      return crackMutable<PartyRiskLimitCheckRequest>( *this, message, sessionID );
    if( msgTypeValue == "DG" )
      return crackMutable<PartyRiskLimitCheckRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "DH" )
      return crackMutable<PartyActionRequest>( *this, message, sessionID );
    if( msgTypeValue == "DI" )
      return crackMutable<PartyActionReport>( *this, message, sessionID );
    if( msgTypeValue == "DJ" )
      return crackMutable<MassOrder>( *this, message, sessionID );
    if( msgTypeValue == "DK" )
      return crackMutable<MassOrderAck>( *this, message, sessionID );
    if( msgTypeValue == "DL" )
      return crackMutable<PositionTransferInstruction>( *this, message, sessionID );
    if( msgTypeValue == "DM" )
      return crackMutable<PositionTransferInstructionAck>( *this, message, sessionID );
    if( msgTypeValue == "DN" )
      return crackMutable<PositionTransferReport>( *this, message, sessionID );
    if( msgTypeValue == "DO" )
      return crackMutable<MarketDataStatisticsRequest>( *this, message, sessionID );
    if( msgTypeValue == "DP" )
      return crackMutable<MarketDataStatisticsReport>( *this, message, sessionID );
    if( msgTypeValue == "DQ" )
      return crackMutable<CollateralReportAck>( *this, message, sessionID );
    if( msgTypeValue == "DR" )
      return crackMutable<MarketDataReport>( *this, message, sessionID );
    if( msgTypeValue == "DS" )
      return crackMutable<CrossRequest>( *this, message, sessionID );
    if( msgTypeValue == "DT" )
      return crackMutable<CrossRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "DU" )
      return crackMutable<AllocationInstructionAlertRequest>( *this, message, sessionID );
    if( msgTypeValue == "DV" )
      return crackMutable<AllocationInstructionAlertRequestAck>( *this, message, sessionID );
    if( msgTypeValue == "DW" )
      return crackMutable<TradeAggregationRequest>( *this, message, sessionID );
    if( msgTypeValue == "DX" )
      return crackMutable<TradeAggregationReport>( *this, message, sessionID );
    if( msgTypeValue == "EA" )
      return crackMutable<PayManagementReport>( *this, message, sessionID );
    if( msgTypeValue == "EB" )
      return crackMutable<PayManagementReportAck>( *this, message, sessionID );
    if( msgTypeValue == "DY" )
      return crackMutable<PayManagementRequest>( *this, message, sessionID );
    if( msgTypeValue == "DZ" )
      return crackMutable<PayManagementRequestAck>( *this, message, sessionID );

    return crackMutable<Message>( *this, message, sessionID );
  }
}
