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

static const uint8_t governed_profile[]
    = {0x98, 0x32, 0x01, 0x48, 0x46, 0x49, 0x58, 0x54, 0x2e, 0x31, 0x2e, 0x31, 0x45, 0x56, 0x45, 0x4e, 0x55, 0x45, 0x40,
       0x40, 0x4b, 0x50, 0x41, 0x52, 0x54, 0x49, 0x43, 0x49, 0x50, 0x41, 0x4e, 0x54, 0x40, 0x40, 0x40, 0x58, 0x20, 0x11,
       0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
       0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x01, 0x18, 0x1e, 0x18, 0x1e, 0x18, 0x1e, 0x0a, 0x02, 0x06, 0xf5, 0xf5, 0xf5, 0x18, 0x78, 0xf4,
       0xf4, 0xf4, 0xf4, 0xf5, 0xf5, 0xf5, 0xf5, 0xf5, 0xf5, 0xf4, 0x52, 0x49, 0x4e, 0x46, 0x49, 0x4e, 0x49, 0x54, 0x45,
       0x2d, 0x52, 0x46, 0x51, 0x2d, 0x31, 0x2e, 0x30, 0x2e, 0x30, 0x0a, 0x19, 0x01, 0x2b, 0x52, 0x49, 0x4e, 0x46, 0x49,
       0x4e, 0x49, 0x54, 0x45, 0x2d, 0x52, 0x46, 0x51, 0x2d, 0x31, 0x2e, 0x30, 0x2e, 0x30, 0x4f, 0x49, 0x4e, 0x46, 0x49,
       0x4e, 0x49, 0x54, 0x45, 0x2d, 0x46, 0x49, 0x58, 0x54, 0x31, 0x31, 0x58, 0x20, 0x75, 0xec, 0xae, 0x39, 0x57, 0xf5,
       0xf5, 0xb0, 0xcc, 0x86, 0x13, 0xac, 0x89, 0x76, 0xbb, 0x33, 0xdf, 0xe3, 0xe2, 0xed, 0xf0, 0x12, 0xcd, 0xf3, 0x60,
       0x87, 0xb3, 0x49, 0xad, 0x5f, 0x85, 0xe5, 0x58, 0x18, 0x49, 0x4e, 0x46, 0x49, 0x4e, 0x49, 0x54, 0x45, 0x2d, 0x52,
       0x46, 0x51, 0x2d, 0x31, 0x2e, 0x30, 0x2e, 0x30, 0x2d, 0x45, 0x50, 0x32, 0x39, 0x39, 0x58, 0x20, 0xd9, 0xce, 0x75,
       0xd2, 0x06, 0x57, 0x3a, 0x39, 0x1d, 0xbc, 0xb8, 0x3a, 0x61, 0x66, 0x5f, 0x38, 0x44, 0x91, 0x6c, 0xfd, 0x63, 0x00,
       0x6d, 0x6a, 0xb9, 0x9d, 0x64, 0x5b, 0xac, 0x6d, 0x25, 0x51};

static const uint8_t logon_frame[] = "8=FIXT.1.1\0019=121\00135=A\00149=PARTICIPANT\00156=VENUE\00134=1\001"
                                     "52=20231114-22:13:20.123456\001369=1\00198=0\001108=30\0011137=10\001"
                                     "1407=299\0011408=INFINITE-RFQ-1.0.0\00110=130\001";

_Static_assert(sizeof(governed_profile) == 257, "governed profile bytes changed");
_Static_assert(sizeof(logon_frame) - 1 == 145, "governed Logon frame bytes changed");

static const uint8_t logon_frame_sha256[32]
    = {0xeb, 0x50, 0x25, 0xc8, 0xbd, 0x35, 0x21, 0xf5, 0x7f, 0x75, 0x64, 0x09, 0x1e, 0xa3, 0x0b, 0x60,
       0x4e, 0x79, 0xc7, 0x68, 0x4c, 0xc0, 0xdf, 0xed, 0x2e, 0x0c, 0x30, 0x50, 0x03, 0xe5, 0x1b, 0x83};

static const uint8_t event_identity_sha256[32]
    = {0xcb, 0xb0, 0x95, 0x24, 0xe3, 0x02, 0x41, 0xbc, 0x2f, 0x38, 0xe2, 0xea, 0x67, 0x09, 0x12, 0xe6,
       0x2c, 0x1c, 0x41, 0x0c, 0x31, 0x26, 0x79, 0xdc, 0xa3, 0xb7, 0xde, 0x3b, 0xa0, 0x85, 0x09, 0xf4};
static const uint8_t zero_sha256[32];

typedef struct plan_buffers {
  uint8_t state[IRFQ_INFINITE_NATIVE_STATE_BYTES_V2];
  uint8_t output[IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2];
  irfq_infinite_declarative_action_v2 actions[IRFQ_INFINITE_MAX_ACTIONS_V2];
} plan_buffers;

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

static void initialize_plan_output(
    irfq_infinite_prepare_response_v2 *response,
    plan_buffers *buffers,
    uint64_t output_capacity) {
  initialize_output(&response->header, (uint32_t)sizeof(*response));
  response->native_state.data = buffers->state;
  response->native_state.capacity = sizeof(buffers->state);
  response->output.data = output_capacity == 0 ? NULL : buffers->output;
  response->output.capacity = output_capacity;
  response->actions = buffers->actions;
  response->action_capacity = (uint32_t)(sizeof(buffers->actions) / sizeof(buffers->actions[0]));
}

static int run_lifecycle(int apply, irfq_infinite_prepare_id_v2 *observed_prepare_id) {
  uint8_t payload[68 + sizeof(logon_frame) - 1];
  irfq_infinite_session_create_request_v2 create_request;
  irfq_infinite_session_create_response_v2 create_response;
  irfq_infinite_prepare_request_v2 prepare_request;
  irfq_infinite_prepare_response_v2 pending;
  irfq_infinite_resume_request_v2 resume_request;
  irfq_infinite_prepare_response_v2 ready;
  irfq_infinite_operation_response_v2 operation_response;
  plan_buffers buffers;
  uint64_t required_output_capacity;
  irfq_infinite_status_v2 status;

  memset(payload, 0x52, 32);
  payload[32] = 0;
  payload[33] = 0;
  payload[34] = 0;
  payload[35] = (uint8_t)(sizeof(logon_frame) - 1);
  memcpy(payload + 36, logon_frame_sha256, sizeof(logon_frame_sha256));
  memcpy(payload + 68, logon_frame, sizeof(logon_frame) - 1);

  initialize_input(&create_request.header, (uint32_t)sizeof(create_request));
  create_request.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
  create_request.canonical_session_create_config.data = governed_profile;
  create_request.canonical_session_create_config.length = sizeof(governed_profile);
  create_request.session_epoch = 1;
  create_request.creation_tai_ns = INT64_C(1700000000123456000);
  create_request.creation_utc_ns = INT64_C(1700000000123456000);
  initialize_output(&create_response.header, (uint32_t)sizeof(create_response));
  status = irfq_infinite_session_create_v2(&create_request, &create_response);
  if (status != IRFQ_INFINITE_STATUS_OK_V2 || create_response.session == NULL || create_response.cache_epoch != 1
      || create_response.cache_revision != 0) {
    return 20 + (int)status;
  }

  initialize_input(&prepare_request.header, (uint32_t)sizeof(prepare_request));
  prepare_request.kind = IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2;
  prepare_request.stage = IRFQ_INFINITE_STAGE_HEAD_V2;
  prepare_request.event = IRFQ_INFINITE_EVENT_INBOUND_FRAME_V2;
  memcpy(prepare_request.event_identity_sha256, event_identity_sha256, sizeof(event_identity_sha256));
  prepare_request.expected_epoch = 1;
  prepare_request.now_tai_ns = INT64_C(1700000000123456001);
  prepare_request.now_utc_ns = INT64_C(1700000000123456001);
  prepare_request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
  prepare_request.next_original_value = 1;
  prepare_request.payload.data = payload;
  prepare_request.payload.length = sizeof(payload);
  initialize_plan_output(&pending, &buffers, 0);
  if (irfq_infinite_prepare_v2(create_response.session, &prepare_request, &pending)
          != IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2
      || (pending.prepare_id.high == 0 && pending.prepare_id.low == 0) || pending.step != 0 || pending.base_epoch != 1
      || pending.base_revision != 0
      || memcmp(pending.event_identity_sha256, event_identity_sha256, sizeof(event_identity_sha256)) != 0
      || pending.required_output_capacity == 0
      || pending.required_output_capacity > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2) {
    irfq_infinite_destroy_v2(create_response.session);
    return 11;
  }
  required_output_capacity = pending.required_output_capacity;

  initialize_input(&resume_request.header, (uint32_t)sizeof(resume_request));
  resume_request.prepare_id = pending.prepare_id;
  resume_request.step = pending.step;
  resume_request.kind = IRFQ_INFINITE_RESUME_OUTPUT_V2;
  initialize_plan_output(&ready, &buffers, required_output_capacity);
  if (irfq_infinite_resume_v2(create_response.session, &resume_request, &ready) != IRFQ_INFINITE_STATUS_READY_V2
      || ready.prepare_id.high != pending.prepare_id.high || ready.prepare_id.low != pending.prepare_id.low
      || ready.step != 1 || ready.base_epoch != 1 || ready.base_revision != 0 || ready.result_epoch != 1
      || ready.result_revision != 1 || ready.native_state.length != IRFQ_INFINITE_NATIVE_STATE_BYTES_V2
      || ready.output.length != required_output_capacity || ready.action_count != 2
      || memcmp(ready.event_identity_sha256, event_identity_sha256, sizeof(event_identity_sha256)) != 0
      || memcmp(ready.native_state_sha256, zero_sha256, sizeof(zero_sha256)) == 0) {
    irfq_infinite_destroy_v2(create_response.session);
    return 12;
  }
  *observed_prepare_id = ready.prepare_id;

  initialize_output(&operation_response.header, (uint32_t)sizeof(operation_response));
  if (apply) {
    irfq_infinite_apply_committed_request_v2 apply_request;
    initialize_input(&apply_request.header, (uint32_t)sizeof(apply_request));
    apply_request.prepare_id = ready.prepare_id;
    apply_request.result_revision = ready.result_revision;
    memcpy(apply_request.native_state_sha256, ready.native_state_sha256, sizeof(apply_request.native_state_sha256));
    if (irfq_infinite_apply_committed_v2(create_response.session, &apply_request, &operation_response)
            != IRFQ_INFINITE_STATUS_OK_V2
        || operation_response.cache_revision != 1) {
      irfq_infinite_destroy_v2(create_response.session);
      return 13;
    }
  } else {
    irfq_infinite_abort_request_v2 abort_request;
    initialize_input(&abort_request.header, (uint32_t)sizeof(abort_request));
    abort_request.prepare_id = ready.prepare_id;
    if (irfq_infinite_abort_v2(create_response.session, &abort_request, &operation_response)
            != IRFQ_INFINITE_STATUS_OK_V2
        || operation_response.cache_revision != 0) {
      irfq_infinite_destroy_v2(create_response.session);
      return 14;
    }
  }
  return irfq_infinite_destroy_v2(create_response.session) == IRFQ_INFINITE_STATUS_OK_V2 ? 0 : 15;
}

int main(void) {
  irfq_infinite_scan_request_v2 request;
  irfq_infinite_scan_response_v2 response;
  irfq_infinite_prepare_id_v2 applied_prepare_id;
  irfq_infinite_prepare_id_v2 aborted_prepare_id;
  int result;

  initialize_input(&request.header, (uint32_t)sizeof(request));
  request.input.data = logon_frame;
  request.input.length = sizeof(logon_frame) - 1;
  initialize_output(&response.header, (uint32_t)sizeof(response));
  if (irfq_infinite_scan_v2(&request, &response) != IRFQ_INFINITE_STATUS_FRAME_READY_V2
      || response.complete_prefix_length != sizeof(logon_frame) - 1) {
    return 1;
  }

  result = run_lifecycle(1, &applied_prepare_id);
  if (result != 0) {
    return result;
  }
  result = run_lifecycle(0, &aborted_prepare_id);
  if (result != 0) {
    return result;
  }
  return applied_prepare_id.high != aborted_prepare_id.high || applied_prepare_id.low != aborted_prepare_id.low ? 0
                                                                                                                : 16;
}
