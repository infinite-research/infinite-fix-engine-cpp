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

#include <quickfix/InfiniteFrameAdapter.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void initialize_input(irfq_infinite_input_header_v2 *header, uint32_t size) {
  memset(header, 0, size);
  header->structure_size = size;
  header->abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2;
}

static void initialize_output(irfq_infinite_output_header_v2 *header, uint32_t size) {
  memset(header, 0, size);
  header->structure_size = size;
  header->abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2;
}

int main(void) {
  static const uint8_t heartbeat[]
      = "8=FIXT.1.1\0019=57\00135=0\00149=CLIENT\00156=VENUE\00134=1\00152=20260828-12:00:00.000000\00110=243\001";
  irfq_infinite_scan_request_v2 request;
  irfq_infinite_scan_response_v2 response;

  irfq_infinite_status_v2 (*scan_call)(const irfq_infinite_scan_request_v2 *, irfq_infinite_scan_response_v2 *)
      = irfq_infinite_scan_v2;
  irfq_infinite_status_v2 (
      *create_call)(const irfq_infinite_session_create_request_v2 *, irfq_infinite_session_create_response_v2 *)
      = irfq_infinite_session_create_v2;
  irfq_infinite_status_v2 (*prepare_call)(
      irfq_infinite_session_v2 *,
      const irfq_infinite_prepare_request_v2 *,
      irfq_infinite_prepare_response_v2 *) = irfq_infinite_prepare_v2;
  irfq_infinite_status_v2 (*resume_call)(
      irfq_infinite_session_v2 *,
      const irfq_infinite_resume_request_v2 *,
      irfq_infinite_prepare_response_v2 *) = irfq_infinite_resume_v2;
  irfq_infinite_status_v2 (*apply_call)(
      irfq_infinite_session_v2 *,
      const irfq_infinite_apply_committed_request_v2 *,
      irfq_infinite_operation_response_v2 *) = irfq_infinite_apply_committed_v2;
  irfq_infinite_status_v2 (*abort_call)(
      irfq_infinite_session_v2 *,
      const irfq_infinite_abort_request_v2 *,
      irfq_infinite_operation_response_v2 *) = irfq_infinite_abort_v2;
  irfq_infinite_status_v2 (*destroy_call)(irfq_infinite_session_v2 *) = irfq_infinite_destroy_v2;

  (void)create_call;
  (void)prepare_call;
  (void)resume_call;
  (void)apply_call;
  (void)abort_call;
  (void)destroy_call;
  initialize_input(&request.header, (uint32_t)sizeof(request));
  request.input.data = heartbeat;
  request.input.length = sizeof(heartbeat) - 1;
  initialize_output(&response.header, (uint32_t)sizeof(response));
  return scan_call(&request, &response) == IRFQ_INFINITE_STATUS_FRAME_READY_V2
                 && response.complete_prefix_length == sizeof(heartbeat) - 1
             ? 0
             : 1;
}
