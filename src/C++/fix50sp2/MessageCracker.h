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

#ifndef FIX50SP2_MESSAGECRACKER_H
#define FIX50SP2_MESSAGECRACKER_H


#include "../SessionID.h"
#include "../Exceptions.h"
#include <utility>

#include "../fix50sp2/Message.h"
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

  class MessageCracker
  {
  public:
  virtual ~MessageCracker() {}
  virtual void onMessage( const Message&, const FIX::SessionID& )
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( Message&, const FIX::SessionID& )
    { throw FIX::UnsupportedMessageType(); }
 virtual void onMessage( const IOI&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Advertisement&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ExecutionReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderCancelReject&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
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
  virtual void onMessage( const AllocationInstruction&, const FIX::SessionID& ) 
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
  virtual void onMessage( const MassQuoteAck&, const FIX::SessionID& ) 
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
  virtual void onMessage( const RegistrationInstructions&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const RegistrationInstructionsResponse&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderMassCancelRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderMassCancelReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const NewOrderCross&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CrossOrderCancelReplaceRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CrossOrderCancelRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityTypeRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityTypes&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityListRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityList&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const DerivativeSecurityListRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const DerivativeSecurityList&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const NewOrderMultileg&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MultilegOrderCancelReplace&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradeCaptureReportRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradeCaptureReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderMassStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteRequestReject&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const RFQRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteStatusReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteResponse&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Confirmation&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PositionMaintenanceRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PositionMaintenanceReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const RequestForPositions&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const RequestForPositionsAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PositionReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradeCaptureReportRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradeCaptureReportAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AllocationReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AllocationReportAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ConfirmationAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SettlementInstructionRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AssignmentReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CollateralRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CollateralAssignment&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CollateralResponse&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CollateralReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CollateralInquiry&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const NetworkCounterpartySystemStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const NetworkCounterpartySystemStatusResponse&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const UserRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const UserResponse&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CollateralInquiryAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ConfirmationRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ContraryIntentionReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityDefinitionUpdateReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityListUpdateReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AdjustedPositionReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AllocationInstructionAlert&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ExecutionAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradingSessionList&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradingSessionListRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SettlementObligationReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const DerivativeSecurityListUpdateReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradingSessionListUpdateReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDefinitionRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDefinition&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDefinitionUpdateReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const UserNotification&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderMassActionReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderMassActionRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ApplicationMessageRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ApplicationMessageRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ApplicationMessageReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const StreamAssignmentRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const StreamAssignmentReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const StreamAssignmentReportACK&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarginRequirementInquiry&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarginRequirementInquiryAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarginRequirementReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyDetailsListRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyDetailsListReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyDetailsListUpdateReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyRiskLimitsRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyRiskLimitsReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityMassStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const SecurityMassStatus&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AccountSummaryReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyRiskLimitsUpdateReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyRiskLimitsDefinitionRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyRiskLimitsDefinitionRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyEntitlementsRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyEntitlementsReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyDetailsDefinitionRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyDetailsDefinitionRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyEntitlementsUpdateReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyEntitlementsDefinitionRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyEntitlementsDefinitionRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradeMatchReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradeMatchReportAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyRiskLimitsReportAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyRiskLimitCheckRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyRiskLimitCheckRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyActionRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PartyActionReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MassOrder&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MassOrderAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PositionTransferInstruction&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PositionTransferInstructionAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PositionTransferReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDataStatisticsRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDataStatisticsReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CollateralReportAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const MarketDataReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CrossRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const CrossRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AllocationInstructionAlertRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AllocationInstructionAlertRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradeAggregationRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const TradeAggregationReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PayManagementReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PayManagementReportAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PayManagementRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const PayManagementRequestAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( IOI&, const FIX::SessionID& ) {} 
 virtual void onMessage( Advertisement&, const FIX::SessionID& ) {} 
 virtual void onMessage( ExecutionReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelReject&, const FIX::SessionID& ) {} 
 virtual void onMessage( News&, const FIX::SessionID& ) {} 
 virtual void onMessage( Email&, const FIX::SessionID& ) {} 
 virtual void onMessage( NewOrderSingle&, const FIX::SessionID& ) {} 
 virtual void onMessage( NewOrderList&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelReplaceRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( AllocationInstruction&, const FIX::SessionID& ) {} 
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
 virtual void onMessage( MassQuoteAck&, const FIX::SessionID& ) {} 
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
 virtual void onMessage( RegistrationInstructions&, const FIX::SessionID& ) {} 
 virtual void onMessage( RegistrationInstructionsResponse&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderMassCancelRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderMassCancelReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( NewOrderCross&, const FIX::SessionID& ) {} 
 virtual void onMessage( CrossOrderCancelReplaceRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( CrossOrderCancelRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityTypeRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityTypes&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityListRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityList&, const FIX::SessionID& ) {} 
 virtual void onMessage( DerivativeSecurityListRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( DerivativeSecurityList&, const FIX::SessionID& ) {} 
 virtual void onMessage( NewOrderMultileg&, const FIX::SessionID& ) {} 
 virtual void onMessage( MultilegOrderCancelReplace&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradeCaptureReportRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradeCaptureReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderMassStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteRequestReject&, const FIX::SessionID& ) {} 
 virtual void onMessage( RFQRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteStatusReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteResponse&, const FIX::SessionID& ) {} 
 virtual void onMessage( Confirmation&, const FIX::SessionID& ) {} 
 virtual void onMessage( PositionMaintenanceRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PositionMaintenanceReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( RequestForPositions&, const FIX::SessionID& ) {} 
 virtual void onMessage( RequestForPositionsAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PositionReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradeCaptureReportRequestAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradeCaptureReportAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( AllocationReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( AllocationReportAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( ConfirmationAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( SettlementInstructionRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( AssignmentReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( CollateralRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( CollateralAssignment&, const FIX::SessionID& ) {} 
 virtual void onMessage( CollateralResponse&, const FIX::SessionID& ) {} 
 virtual void onMessage( CollateralReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( CollateralInquiry&, const FIX::SessionID& ) {} 
 virtual void onMessage( NetworkCounterpartySystemStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( NetworkCounterpartySystemStatusResponse&, const FIX::SessionID& ) {} 
 virtual void onMessage( UserRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( UserResponse&, const FIX::SessionID& ) {} 
 virtual void onMessage( CollateralInquiryAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( ConfirmationRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ContraryIntentionReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityDefinitionUpdateReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityListUpdateReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( AdjustedPositionReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( AllocationInstructionAlert&, const FIX::SessionID& ) {} 
 virtual void onMessage( ExecutionAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradingSessionList&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradingSessionListRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( SettlementObligationReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( DerivativeSecurityListUpdateReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradingSessionListUpdateReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDefinitionRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDefinition&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDefinitionUpdateReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( UserNotification&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderMassActionReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderMassActionRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ApplicationMessageRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ApplicationMessageRequestAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( ApplicationMessageReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( StreamAssignmentRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( StreamAssignmentReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( StreamAssignmentReportACK&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarginRequirementInquiry&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarginRequirementInquiryAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarginRequirementReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyDetailsListRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyDetailsListReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyDetailsListUpdateReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyRiskLimitsRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyRiskLimitsReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityMassStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( SecurityMassStatus&, const FIX::SessionID& ) {} 
 virtual void onMessage( AccountSummaryReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyRiskLimitsUpdateReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyRiskLimitsDefinitionRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyRiskLimitsDefinitionRequestAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyEntitlementsRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyEntitlementsReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyDetailsDefinitionRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyDetailsDefinitionRequestAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyEntitlementsUpdateReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyEntitlementsDefinitionRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyEntitlementsDefinitionRequestAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradeMatchReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradeMatchReportAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyRiskLimitsReportAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyRiskLimitCheckRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyRiskLimitCheckRequestAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyActionRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PartyActionReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( MassOrder&, const FIX::SessionID& ) {} 
 virtual void onMessage( MassOrderAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PositionTransferInstruction&, const FIX::SessionID& ) {} 
 virtual void onMessage( PositionTransferInstructionAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PositionTransferReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDataStatisticsRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDataStatisticsReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( CollateralReportAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( MarketDataReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( CrossRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( CrossRequestAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( AllocationInstructionAlertRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( AllocationInstructionAlertRequestAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradeAggregationRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( TradeAggregationReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( PayManagementReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( PayManagementReportAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( PayManagementRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( PayManagementRequestAck&, const FIX::SessionID& ) {} 

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
    
    
    if( msgTypeValue == "6" )
      return onMessage( IOI(message), sessionID );
    
    if( msgTypeValue == "7" )
      return onMessage( Advertisement(message), sessionID );
    
    if( msgTypeValue == "8" )
      return onMessage( ExecutionReport(message), sessionID );
    
    if( msgTypeValue == "9" )
      return onMessage( OrderCancelReject(message), sessionID );
    
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
      return onMessage( AllocationInstruction(message), sessionID );
    
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
      return onMessage( MassQuoteAck(message), sessionID );
    
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
    
    if( msgTypeValue == "o" )
      return onMessage( RegistrationInstructions(message), sessionID );
    
    if( msgTypeValue == "p" )
      return onMessage( RegistrationInstructionsResponse(message), sessionID );
    
    if( msgTypeValue == "q" )
      return onMessage( OrderMassCancelRequest(message), sessionID );
    
    if( msgTypeValue == "r" )
      return onMessage( OrderMassCancelReport(message), sessionID );
    
    if( msgTypeValue == "s" )
      return onMessage( NewOrderCross(message), sessionID );
    
    if( msgTypeValue == "t" )
      return onMessage( CrossOrderCancelReplaceRequest(message), sessionID );
    
    if( msgTypeValue == "u" )
      return onMessage( CrossOrderCancelRequest(message), sessionID );
    
    if( msgTypeValue == "v" )
      return onMessage( SecurityTypeRequest(message), sessionID );
    
    if( msgTypeValue == "w" )
      return onMessage( SecurityTypes(message), sessionID );
    
    if( msgTypeValue == "x" )
      return onMessage( SecurityListRequest(message), sessionID );
    
    if( msgTypeValue == "y" )
      return onMessage( SecurityList(message), sessionID );
    
    if( msgTypeValue == "z" )
      return onMessage( DerivativeSecurityListRequest(message), sessionID );
    
    if( msgTypeValue == "AA" )
      return onMessage( DerivativeSecurityList(message), sessionID );
    
    if( msgTypeValue == "AB" )
      return onMessage( NewOrderMultileg(message), sessionID );
    
    if( msgTypeValue == "AC" )
      return onMessage( MultilegOrderCancelReplace(message), sessionID );
    
    if( msgTypeValue == "AD" )
      return onMessage( TradeCaptureReportRequest(message), sessionID );
    
    if( msgTypeValue == "AE" )
      return onMessage( TradeCaptureReport(message), sessionID );
    
    if( msgTypeValue == "AF" )
      return onMessage( OrderMassStatusRequest(message), sessionID );
    
    if( msgTypeValue == "AG" )
      return onMessage( QuoteRequestReject(message), sessionID );
    
    if( msgTypeValue == "AH" )
      return onMessage( RFQRequest(message), sessionID );
    
    if( msgTypeValue == "AI" )
      return onMessage( QuoteStatusReport(message), sessionID );
    
    if( msgTypeValue == "AJ" )
      return onMessage( QuoteResponse(message), sessionID );
    
    if( msgTypeValue == "AK" )
      return onMessage( Confirmation(message), sessionID );
    
    if( msgTypeValue == "AL" )
      return onMessage( PositionMaintenanceRequest(message), sessionID );
    
    if( msgTypeValue == "AM" )
      return onMessage( PositionMaintenanceReport(message), sessionID );
    
    if( msgTypeValue == "AN" )
      return onMessage( RequestForPositions(message), sessionID );
    
    if( msgTypeValue == "AO" )
      return onMessage( RequestForPositionsAck(message), sessionID );
    
    if( msgTypeValue == "AP" )
      return onMessage( PositionReport(message), sessionID );
    
    if( msgTypeValue == "AQ" )
      return onMessage( TradeCaptureReportRequestAck(message), sessionID );
    
    if( msgTypeValue == "AR" )
      return onMessage( TradeCaptureReportAck(message), sessionID );
    
    if( msgTypeValue == "AS" )
      return onMessage( AllocationReport(message), sessionID );
    
    if( msgTypeValue == "AT" )
      return onMessage( AllocationReportAck(message), sessionID );
    
    if( msgTypeValue == "AU" )
      return onMessage( ConfirmationAck(message), sessionID );
    
    if( msgTypeValue == "AV" )
      return onMessage( SettlementInstructionRequest(message), sessionID );
    
    if( msgTypeValue == "AW" )
      return onMessage( AssignmentReport(message), sessionID );
    
    if( msgTypeValue == "AX" )
      return onMessage( CollateralRequest(message), sessionID );
    
    if( msgTypeValue == "AY" )
      return onMessage( CollateralAssignment(message), sessionID );
    
    if( msgTypeValue == "AZ" )
      return onMessage( CollateralResponse(message), sessionID );
    
    if( msgTypeValue == "BA" )
      return onMessage( CollateralReport(message), sessionID );
    
    if( msgTypeValue == "BB" )
      return onMessage( CollateralInquiry(message), sessionID );
    
    if( msgTypeValue == "BC" )
      return onMessage( NetworkCounterpartySystemStatusRequest(message), sessionID );
    
    if( msgTypeValue == "BD" )
      return onMessage( NetworkCounterpartySystemStatusResponse(message), sessionID );
    
    if( msgTypeValue == "BE" )
      return onMessage( UserRequest(message), sessionID );
    
    if( msgTypeValue == "BF" )
      return onMessage( UserResponse(message), sessionID );
    
    if( msgTypeValue == "BG" )
      return onMessage( CollateralInquiryAck(message), sessionID );
    
    if( msgTypeValue == "BH" )
      return onMessage( ConfirmationRequest(message), sessionID );
    
    if( msgTypeValue == "BO" )
      return onMessage( ContraryIntentionReport(message), sessionID );
    
    if( msgTypeValue == "BP" )
      return onMessage( SecurityDefinitionUpdateReport(message), sessionID );
    
    if( msgTypeValue == "BK" )
      return onMessage( SecurityListUpdateReport(message), sessionID );
    
    if( msgTypeValue == "BL" )
      return onMessage( AdjustedPositionReport(message), sessionID );
    
    if( msgTypeValue == "BM" )
      return onMessage( AllocationInstructionAlert(message), sessionID );
    
    if( msgTypeValue == "BN" )
      return onMessage( ExecutionAck(message), sessionID );
    
    if( msgTypeValue == "BJ" )
      return onMessage( TradingSessionList(message), sessionID );
    
    if( msgTypeValue == "BI" )
      return onMessage( TradingSessionListRequest(message), sessionID );
    
    if( msgTypeValue == "BQ" )
      return onMessage( SettlementObligationReport(message), sessionID );
    
    if( msgTypeValue == "BR" )
      return onMessage( DerivativeSecurityListUpdateReport(message), sessionID );
    
    if( msgTypeValue == "BS" )
      return onMessage( TradingSessionListUpdateReport(message), sessionID );
    
    if( msgTypeValue == "BT" )
      return onMessage( MarketDefinitionRequest(message), sessionID );
    
    if( msgTypeValue == "BU" )
      return onMessage( MarketDefinition(message), sessionID );
    
    if( msgTypeValue == "BV" )
      return onMessage( MarketDefinitionUpdateReport(message), sessionID );
    
    if( msgTypeValue == "CB" )
      return onMessage( UserNotification(message), sessionID );
    
    if( msgTypeValue == "BZ" )
      return onMessage( OrderMassActionReport(message), sessionID );
    
    if( msgTypeValue == "CA" )
      return onMessage( OrderMassActionRequest(message), sessionID );
    
    if( msgTypeValue == "BW" )
      return onMessage( ApplicationMessageRequest(message), sessionID );
    
    if( msgTypeValue == "BX" )
      return onMessage( ApplicationMessageRequestAck(message), sessionID );
    
    if( msgTypeValue == "BY" )
      return onMessage( ApplicationMessageReport(message), sessionID );
    
    if( msgTypeValue == "CC" )
      return onMessage( StreamAssignmentRequest(message), sessionID );
    
    if( msgTypeValue == "CD" )
      return onMessage( StreamAssignmentReport(message), sessionID );
    
    if( msgTypeValue == "CE" )
      return onMessage( StreamAssignmentReportACK(message), sessionID );
    
    if( msgTypeValue == "CH" )
      return onMessage( MarginRequirementInquiry(message), sessionID );
    
    if( msgTypeValue == "CI" )
      return onMessage( MarginRequirementInquiryAck(message), sessionID );
    
    if( msgTypeValue == "CJ" )
      return onMessage( MarginRequirementReport(message), sessionID );
    
    if( msgTypeValue == "CF" )
      return onMessage( PartyDetailsListRequest(message), sessionID );
    
    if( msgTypeValue == "CG" )
      return onMessage( PartyDetailsListReport(message), sessionID );
    
    if( msgTypeValue == "CK" )
      return onMessage( PartyDetailsListUpdateReport(message), sessionID );
    
    if( msgTypeValue == "CL" )
      return onMessage( PartyRiskLimitsRequest(message), sessionID );
    
    if( msgTypeValue == "CM" )
      return onMessage( PartyRiskLimitsReport(message), sessionID );
    
    if( msgTypeValue == "CN" )
      return onMessage( SecurityMassStatusRequest(message), sessionID );
    
    if( msgTypeValue == "CO" )
      return onMessage( SecurityMassStatus(message), sessionID );
    
    if( msgTypeValue == "CQ" )
      return onMessage( AccountSummaryReport(message), sessionID );
    
    if( msgTypeValue == "CR" )
      return onMessage( PartyRiskLimitsUpdateReport(message), sessionID );
    
    if( msgTypeValue == "CS" )
      return onMessage( PartyRiskLimitsDefinitionRequest(message), sessionID );
    
    if( msgTypeValue == "CT" )
      return onMessage( PartyRiskLimitsDefinitionRequestAck(message), sessionID );
    
    if( msgTypeValue == "CU" )
      return onMessage( PartyEntitlementsRequest(message), sessionID );
    
    if( msgTypeValue == "CV" )
      return onMessage( PartyEntitlementsReport(message), sessionID );
    
    if( msgTypeValue == "CW" )
      return onMessage( QuoteAck(message), sessionID );
    
    if( msgTypeValue == "CX" )
      return onMessage( PartyDetailsDefinitionRequest(message), sessionID );
    
    if( msgTypeValue == "CY" )
      return onMessage( PartyDetailsDefinitionRequestAck(message), sessionID );
    
    if( msgTypeValue == "CZ" )
      return onMessage( PartyEntitlementsUpdateReport(message), sessionID );
    
    if( msgTypeValue == "DA" )
      return onMessage( PartyEntitlementsDefinitionRequest(message), sessionID );
    
    if( msgTypeValue == "DB" )
      return onMessage( PartyEntitlementsDefinitionRequestAck(message), sessionID );
    
    if( msgTypeValue == "DC" )
      return onMessage( TradeMatchReport(message), sessionID );
    
    if( msgTypeValue == "DD" )
      return onMessage( TradeMatchReportAck(message), sessionID );
    
    if( msgTypeValue == "DE" )
      return onMessage( PartyRiskLimitsReportAck(message), sessionID );
    
    if( msgTypeValue == "DF" )
      return onMessage( PartyRiskLimitCheckRequest(message), sessionID );
    
    if( msgTypeValue == "DG" )
      return onMessage( PartyRiskLimitCheckRequestAck(message), sessionID );
    
    if( msgTypeValue == "DH" )
      return onMessage( PartyActionRequest(message), sessionID );
    
    if( msgTypeValue == "DI" )
      return onMessage( PartyActionReport(message), sessionID );
    
    if( msgTypeValue == "DJ" )
      return onMessage( MassOrder(message), sessionID );
    
    if( msgTypeValue == "DK" )
      return onMessage( MassOrderAck(message), sessionID );
    
    if( msgTypeValue == "DL" )
      return onMessage( PositionTransferInstruction(message), sessionID );
    
    if( msgTypeValue == "DM" )
      return onMessage( PositionTransferInstructionAck(message), sessionID );
    
    if( msgTypeValue == "DN" )
      return onMessage( PositionTransferReport(message), sessionID );
    
    if( msgTypeValue == "DO" )
      return onMessage( MarketDataStatisticsRequest(message), sessionID );
    
    if( msgTypeValue == "DP" )
      return onMessage( MarketDataStatisticsReport(message), sessionID );
    
    if( msgTypeValue == "DQ" )
      return onMessage( CollateralReportAck(message), sessionID );
    
    if( msgTypeValue == "DR" )
      return onMessage( MarketDataReport(message), sessionID );
    
    if( msgTypeValue == "DS" )
      return onMessage( CrossRequest(message), sessionID );
    
    if( msgTypeValue == "DT" )
      return onMessage( CrossRequestAck(message), sessionID );
    
    if( msgTypeValue == "DU" )
      return onMessage( AllocationInstructionAlertRequest(message), sessionID );
    
    if( msgTypeValue == "DV" )
      return onMessage( AllocationInstructionAlertRequestAck(message), sessionID );
    
    if( msgTypeValue == "DW" )
      return onMessage( TradeAggregationRequest(message), sessionID );
    
    if( msgTypeValue == "DX" )
      return onMessage( TradeAggregationReport(message), sessionID );
    
    if( msgTypeValue == "EA" )
      return onMessage( PayManagementReport(message), sessionID );
    
    if( msgTypeValue == "EB" )
      return onMessage( PayManagementReportAck(message), sessionID );
    
    if( msgTypeValue == "DY" )
      return onMessage( PayManagementRequest(message), sessionID );
    
    if( msgTypeValue == "DZ" )
      return onMessage( PayManagementRequestAck(message), sessionID );
    
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
    
    
    if( msgTypeValue == "6" )
      return dispatch<IOI>( message, sessionID );
    
    if( msgTypeValue == "7" )
      return dispatch<Advertisement>( message, sessionID );
    
    if( msgTypeValue == "8" )
      return dispatch<ExecutionReport>( message, sessionID );
    
    if( msgTypeValue == "9" )
      return dispatch<OrderCancelReject>( message, sessionID );
    
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
      return dispatch<AllocationInstruction>( message, sessionID );
    
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
      return dispatch<MassQuoteAck>( message, sessionID );
    
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
    
    if( msgTypeValue == "o" )
      return dispatch<RegistrationInstructions>( message, sessionID );
    
    if( msgTypeValue == "p" )
      return dispatch<RegistrationInstructionsResponse>( message, sessionID );
    
    if( msgTypeValue == "q" )
      return dispatch<OrderMassCancelRequest>( message, sessionID );
    
    if( msgTypeValue == "r" )
      return dispatch<OrderMassCancelReport>( message, sessionID );
    
    if( msgTypeValue == "s" )
      return dispatch<NewOrderCross>( message, sessionID );
    
    if( msgTypeValue == "t" )
      return dispatch<CrossOrderCancelReplaceRequest>( message, sessionID );
    
    if( msgTypeValue == "u" )
      return dispatch<CrossOrderCancelRequest>( message, sessionID );
    
    if( msgTypeValue == "v" )
      return dispatch<SecurityTypeRequest>( message, sessionID );
    
    if( msgTypeValue == "w" )
      return dispatch<SecurityTypes>( message, sessionID );
    
    if( msgTypeValue == "x" )
      return dispatch<SecurityListRequest>( message, sessionID );
    
    if( msgTypeValue == "y" )
      return dispatch<SecurityList>( message, sessionID );
    
    if( msgTypeValue == "z" )
      return dispatch<DerivativeSecurityListRequest>( message, sessionID );
    
    if( msgTypeValue == "AA" )
      return dispatch<DerivativeSecurityList>( message, sessionID );
    
    if( msgTypeValue == "AB" )
      return dispatch<NewOrderMultileg>( message, sessionID );
    
    if( msgTypeValue == "AC" )
      return dispatch<MultilegOrderCancelReplace>( message, sessionID );
    
    if( msgTypeValue == "AD" )
      return dispatch<TradeCaptureReportRequest>( message, sessionID );
    
    if( msgTypeValue == "AE" )
      return dispatch<TradeCaptureReport>( message, sessionID );
    
    if( msgTypeValue == "AF" )
      return dispatch<OrderMassStatusRequest>( message, sessionID );
    
    if( msgTypeValue == "AG" )
      return dispatch<QuoteRequestReject>( message, sessionID );
    
    if( msgTypeValue == "AH" )
      return dispatch<RFQRequest>( message, sessionID );
    
    if( msgTypeValue == "AI" )
      return dispatch<QuoteStatusReport>( message, sessionID );
    
    if( msgTypeValue == "AJ" )
      return dispatch<QuoteResponse>( message, sessionID );
    
    if( msgTypeValue == "AK" )
      return dispatch<Confirmation>( message, sessionID );
    
    if( msgTypeValue == "AL" )
      return dispatch<PositionMaintenanceRequest>( message, sessionID );
    
    if( msgTypeValue == "AM" )
      return dispatch<PositionMaintenanceReport>( message, sessionID );
    
    if( msgTypeValue == "AN" )
      return dispatch<RequestForPositions>( message, sessionID );
    
    if( msgTypeValue == "AO" )
      return dispatch<RequestForPositionsAck>( message, sessionID );
    
    if( msgTypeValue == "AP" )
      return dispatch<PositionReport>( message, sessionID );
    
    if( msgTypeValue == "AQ" )
      return dispatch<TradeCaptureReportRequestAck>( message, sessionID );
    
    if( msgTypeValue == "AR" )
      return dispatch<TradeCaptureReportAck>( message, sessionID );
    
    if( msgTypeValue == "AS" )
      return dispatch<AllocationReport>( message, sessionID );
    
    if( msgTypeValue == "AT" )
      return dispatch<AllocationReportAck>( message, sessionID );
    
    if( msgTypeValue == "AU" )
      return dispatch<ConfirmationAck>( message, sessionID );
    
    if( msgTypeValue == "AV" )
      return dispatch<SettlementInstructionRequest>( message, sessionID );
    
    if( msgTypeValue == "AW" )
      return dispatch<AssignmentReport>( message, sessionID );
    
    if( msgTypeValue == "AX" )
      return dispatch<CollateralRequest>( message, sessionID );
    
    if( msgTypeValue == "AY" )
      return dispatch<CollateralAssignment>( message, sessionID );
    
    if( msgTypeValue == "AZ" )
      return dispatch<CollateralResponse>( message, sessionID );
    
    if( msgTypeValue == "BA" )
      return dispatch<CollateralReport>( message, sessionID );
    
    if( msgTypeValue == "BB" )
      return dispatch<CollateralInquiry>( message, sessionID );
    
    if( msgTypeValue == "BC" )
      return dispatch<NetworkCounterpartySystemStatusRequest>( message, sessionID );
    
    if( msgTypeValue == "BD" )
      return dispatch<NetworkCounterpartySystemStatusResponse>( message, sessionID );
    
    if( msgTypeValue == "BE" )
      return dispatch<UserRequest>( message, sessionID );
    
    if( msgTypeValue == "BF" )
      return dispatch<UserResponse>( message, sessionID );
    
    if( msgTypeValue == "BG" )
      return dispatch<CollateralInquiryAck>( message, sessionID );
    
    if( msgTypeValue == "BH" )
      return dispatch<ConfirmationRequest>( message, sessionID );
    
    if( msgTypeValue == "BO" )
      return dispatch<ContraryIntentionReport>( message, sessionID );
    
    if( msgTypeValue == "BP" )
      return dispatch<SecurityDefinitionUpdateReport>( message, sessionID );
    
    if( msgTypeValue == "BK" )
      return dispatch<SecurityListUpdateReport>( message, sessionID );
    
    if( msgTypeValue == "BL" )
      return dispatch<AdjustedPositionReport>( message, sessionID );
    
    if( msgTypeValue == "BM" )
      return dispatch<AllocationInstructionAlert>( message, sessionID );
    
    if( msgTypeValue == "BN" )
      return dispatch<ExecutionAck>( message, sessionID );
    
    if( msgTypeValue == "BJ" )
      return dispatch<TradingSessionList>( message, sessionID );
    
    if( msgTypeValue == "BI" )
      return dispatch<TradingSessionListRequest>( message, sessionID );
    
    if( msgTypeValue == "BQ" )
      return dispatch<SettlementObligationReport>( message, sessionID );
    
    if( msgTypeValue == "BR" )
      return dispatch<DerivativeSecurityListUpdateReport>( message, sessionID );
    
    if( msgTypeValue == "BS" )
      return dispatch<TradingSessionListUpdateReport>( message, sessionID );
    
    if( msgTypeValue == "BT" )
      return dispatch<MarketDefinitionRequest>( message, sessionID );
    
    if( msgTypeValue == "BU" )
      return dispatch<MarketDefinition>( message, sessionID );
    
    if( msgTypeValue == "BV" )
      return dispatch<MarketDefinitionUpdateReport>( message, sessionID );
    
    if( msgTypeValue == "CB" )
      return dispatch<UserNotification>( message, sessionID );
    
    if( msgTypeValue == "BZ" )
      return dispatch<OrderMassActionReport>( message, sessionID );
    
    if( msgTypeValue == "CA" )
      return dispatch<OrderMassActionRequest>( message, sessionID );
    
    if( msgTypeValue == "BW" )
      return dispatch<ApplicationMessageRequest>( message, sessionID );
    
    if( msgTypeValue == "BX" )
      return dispatch<ApplicationMessageRequestAck>( message, sessionID );
    
    if( msgTypeValue == "BY" )
      return dispatch<ApplicationMessageReport>( message, sessionID );
    
    if( msgTypeValue == "CC" )
      return dispatch<StreamAssignmentRequest>( message, sessionID );
    
    if( msgTypeValue == "CD" )
      return dispatch<StreamAssignmentReport>( message, sessionID );
    
    if( msgTypeValue == "CE" )
      return dispatch<StreamAssignmentReportACK>( message, sessionID );
    
    if( msgTypeValue == "CH" )
      return dispatch<MarginRequirementInquiry>( message, sessionID );
    
    if( msgTypeValue == "CI" )
      return dispatch<MarginRequirementInquiryAck>( message, sessionID );
    
    if( msgTypeValue == "CJ" )
      return dispatch<MarginRequirementReport>( message, sessionID );
    
    if( msgTypeValue == "CF" )
      return dispatch<PartyDetailsListRequest>( message, sessionID );
    
    if( msgTypeValue == "CG" )
      return dispatch<PartyDetailsListReport>( message, sessionID );
    
    if( msgTypeValue == "CK" )
      return dispatch<PartyDetailsListUpdateReport>( message, sessionID );
    
    if( msgTypeValue == "CL" )
      return dispatch<PartyRiskLimitsRequest>( message, sessionID );
    
    if( msgTypeValue == "CM" )
      return dispatch<PartyRiskLimitsReport>( message, sessionID );
    
    if( msgTypeValue == "CN" )
      return dispatch<SecurityMassStatusRequest>( message, sessionID );
    
    if( msgTypeValue == "CO" )
      return dispatch<SecurityMassStatus>( message, sessionID );
    
    if( msgTypeValue == "CQ" )
      return dispatch<AccountSummaryReport>( message, sessionID );
    
    if( msgTypeValue == "CR" )
      return dispatch<PartyRiskLimitsUpdateReport>( message, sessionID );
    
    if( msgTypeValue == "CS" )
      return dispatch<PartyRiskLimitsDefinitionRequest>( message, sessionID );
    
    if( msgTypeValue == "CT" )
      return dispatch<PartyRiskLimitsDefinitionRequestAck>( message, sessionID );
    
    if( msgTypeValue == "CU" )
      return dispatch<PartyEntitlementsRequest>( message, sessionID );
    
    if( msgTypeValue == "CV" )
      return dispatch<PartyEntitlementsReport>( message, sessionID );
    
    if( msgTypeValue == "CW" )
      return dispatch<QuoteAck>( message, sessionID );
    
    if( msgTypeValue == "CX" )
      return dispatch<PartyDetailsDefinitionRequest>( message, sessionID );
    
    if( msgTypeValue == "CY" )
      return dispatch<PartyDetailsDefinitionRequestAck>( message, sessionID );
    
    if( msgTypeValue == "CZ" )
      return dispatch<PartyEntitlementsUpdateReport>( message, sessionID );
    
    if( msgTypeValue == "DA" )
      return dispatch<PartyEntitlementsDefinitionRequest>( message, sessionID );
    
    if( msgTypeValue == "DB" )
      return dispatch<PartyEntitlementsDefinitionRequestAck>( message, sessionID );
    
    if( msgTypeValue == "DC" )
      return dispatch<TradeMatchReport>( message, sessionID );
    
    if( msgTypeValue == "DD" )
      return dispatch<TradeMatchReportAck>( message, sessionID );
    
    if( msgTypeValue == "DE" )
      return dispatch<PartyRiskLimitsReportAck>( message, sessionID );
    
    if( msgTypeValue == "DF" )
      return dispatch<PartyRiskLimitCheckRequest>( message, sessionID );
    
    if( msgTypeValue == "DG" )
      return dispatch<PartyRiskLimitCheckRequestAck>( message, sessionID );
    
    if( msgTypeValue == "DH" )
      return dispatch<PartyActionRequest>( message, sessionID );
    
    if( msgTypeValue == "DI" )
      return dispatch<PartyActionReport>( message, sessionID );
    
    if( msgTypeValue == "DJ" )
      return dispatch<MassOrder>( message, sessionID );
    
    if( msgTypeValue == "DK" )
      return dispatch<MassOrderAck>( message, sessionID );
    
    if( msgTypeValue == "DL" )
      return dispatch<PositionTransferInstruction>( message, sessionID );
    
    if( msgTypeValue == "DM" )
      return dispatch<PositionTransferInstructionAck>( message, sessionID );
    
    if( msgTypeValue == "DN" )
      return dispatch<PositionTransferReport>( message, sessionID );
    
    if( msgTypeValue == "DO" )
      return dispatch<MarketDataStatisticsRequest>( message, sessionID );
    
    if( msgTypeValue == "DP" )
      return dispatch<MarketDataStatisticsReport>( message, sessionID );
    
    if( msgTypeValue == "DQ" )
      return dispatch<CollateralReportAck>( message, sessionID );
    
    if( msgTypeValue == "DR" )
      return dispatch<MarketDataReport>( message, sessionID );
    
    if( msgTypeValue == "DS" )
      return dispatch<CrossRequest>( message, sessionID );
    
    if( msgTypeValue == "DT" )
      return dispatch<CrossRequestAck>( message, sessionID );
    
    if( msgTypeValue == "DU" )
      return dispatch<AllocationInstructionAlertRequest>( message, sessionID );
    
    if( msgTypeValue == "DV" )
      return dispatch<AllocationInstructionAlertRequestAck>( message, sessionID );
    
    if( msgTypeValue == "DW" )
      return dispatch<TradeAggregationRequest>( message, sessionID );
    
    if( msgTypeValue == "DX" )
      return dispatch<TradeAggregationReport>( message, sessionID );
    
    if( msgTypeValue == "EA" )
      return dispatch<PayManagementReport>( message, sessionID );
    
    if( msgTypeValue == "EB" )
      return dispatch<PayManagementReportAck>( message, sessionID );
    
    if( msgTypeValue == "DY" )
      return dispatch<PayManagementRequest>( message, sessionID );
    
    if( msgTypeValue == "DZ" )
      return dispatch<PayManagementRequestAck>( message, sessionID );
    
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
