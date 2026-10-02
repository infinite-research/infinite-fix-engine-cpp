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

#include "../fix50sp2/Message.h"

namespace FIX50SP2
{
  class IOI;
  class Advertisement;
  class ExecutionReport;
  class OrderCancelReject;
  class News;
  class Email;
  class NewOrderSingle;
  class NewOrderList;
  class OrderCancelRequest;
  class OrderCancelReplaceRequest;
  class OrderStatusRequest;
  class AllocationInstruction;
  class ListCancelRequest;
  class ListExecute;
  class ListStatusRequest;
  class ListStatus;
  class AllocationInstructionAck;
  class DontKnowTrade;
  class QuoteRequest;
  class Quote;
  class SettlementInstructions;
  class MarketDataRequest;
  class MarketDataSnapshotFullRefresh;
  class MarketDataIncrementalRefresh;
  class MarketDataRequestReject;
  class QuoteCancel;
  class QuoteStatusRequest;
  class MassQuoteAck;
  class SecurityDefinitionRequest;
  class SecurityDefinition;
  class SecurityStatusRequest;
  class SecurityStatus;
  class TradingSessionStatusRequest;
  class TradingSessionStatus;
  class MassQuote;
  class BusinessMessageReject;
  class BidRequest;
  class BidResponse;
  class ListStrikePrice;
  class RegistrationInstructions;
  class RegistrationInstructionsResponse;
  class OrderMassCancelRequest;
  class OrderMassCancelReport;
  class NewOrderCross;
  class CrossOrderCancelReplaceRequest;
  class CrossOrderCancelRequest;
  class SecurityTypeRequest;
  class SecurityTypes;
  class SecurityListRequest;
  class SecurityList;
  class DerivativeSecurityListRequest;
  class DerivativeSecurityList;
  class NewOrderMultileg;
  class MultilegOrderCancelReplace;
  class TradeCaptureReportRequest;
  class TradeCaptureReport;
  class OrderMassStatusRequest;
  class QuoteRequestReject;
  class RFQRequest;
  class QuoteStatusReport;
  class QuoteResponse;
  class Confirmation;
  class PositionMaintenanceRequest;
  class PositionMaintenanceReport;
  class RequestForPositions;
  class RequestForPositionsAck;
  class PositionReport;
  class TradeCaptureReportRequestAck;
  class TradeCaptureReportAck;
  class AllocationReport;
  class AllocationReportAck;
  class ConfirmationAck;
  class SettlementInstructionRequest;
  class AssignmentReport;
  class CollateralRequest;
  class CollateralAssignment;
  class CollateralResponse;
  class CollateralReport;
  class CollateralInquiry;
  class NetworkCounterpartySystemStatusRequest;
  class NetworkCounterpartySystemStatusResponse;
  class UserRequest;
  class UserResponse;
  class CollateralInquiryAck;
  class ConfirmationRequest;
  class ContraryIntentionReport;
  class SecurityDefinitionUpdateReport;
  class SecurityListUpdateReport;
  class AdjustedPositionReport;
  class AllocationInstructionAlert;
  class ExecutionAck;
  class TradingSessionList;
  class TradingSessionListRequest;
  class SettlementObligationReport;
  class DerivativeSecurityListUpdateReport;
  class TradingSessionListUpdateReport;
  class MarketDefinitionRequest;
  class MarketDefinition;
  class MarketDefinitionUpdateReport;
  class UserNotification;
  class OrderMassActionReport;
  class OrderMassActionRequest;
  class ApplicationMessageRequest;
  class ApplicationMessageRequestAck;
  class ApplicationMessageReport;
  class StreamAssignmentRequest;
  class StreamAssignmentReport;
  class StreamAssignmentReportACK;
  class MarginRequirementInquiry;
  class MarginRequirementInquiryAck;
  class MarginRequirementReport;
  class PartyDetailsListRequest;
  class PartyDetailsListReport;
  class PartyDetailsListUpdateReport;
  class PartyRiskLimitsRequest;
  class PartyRiskLimitsReport;
  class SecurityMassStatusRequest;
  class SecurityMassStatus;
  class AccountSummaryReport;
  class PartyRiskLimitsUpdateReport;
  class PartyRiskLimitsDefinitionRequest;
  class PartyRiskLimitsDefinitionRequestAck;
  class PartyEntitlementsRequest;
  class PartyEntitlementsReport;
  class QuoteAck;
  class PartyDetailsDefinitionRequest;
  class PartyDetailsDefinitionRequestAck;
  class PartyEntitlementsUpdateReport;
  class PartyEntitlementsDefinitionRequest;
  class PartyEntitlementsDefinitionRequestAck;
  class TradeMatchReport;
  class TradeMatchReportAck;
  class PartyRiskLimitsReportAck;
  class PartyRiskLimitCheckRequest;
  class PartyRiskLimitCheckRequestAck;
  class PartyActionRequest;
  class PartyActionReport;
  class MassOrder;
  class MassOrderAck;
  class PositionTransferInstruction;
  class PositionTransferInstructionAck;
  class PositionTransferReport;
  class MarketDataStatisticsRequest;
  class MarketDataStatisticsReport;
  class CollateralReportAck;
  class MarketDataReport;
  class CrossRequest;
  class CrossRequestAck;
  class AllocationInstructionAlertRequest;
  class AllocationInstructionAlertRequestAck;
  class TradeAggregationRequest;
  class TradeAggregationReport;
  class PayManagementReport;
  class PayManagementReportAck;
  class PayManagementRequest;
  class PayManagementRequestAck;

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
  // Defined in the generated MessageCracker.cpp so this header needs only forward declarations.

  /// Preserve the version-message entry point while dispatching genuine typed values.
  void crack( const Message& message,
              const FIX::SessionID& sessionID );

  /// Dispatch a generic message without assuming a derived object lifetime.
  void crack( const FIX::Message& message,
              const FIX::SessionID& sessionID );

  /// Preserve the version-message entry point and mutable callback behavior.
  void crack( Message& message,
              const FIX::SessionID& sessionID );

  /// Transfer callback changes back on both normal return and exception propagation.
  void crack( FIX::Message& message,
              const FIX::SessionID& sessionID );

  };
}

#endif
