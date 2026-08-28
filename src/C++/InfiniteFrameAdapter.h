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

#pragma once

#include <stdint.h>

#ifdef __cplusplus
#define IRFQ_INFINITE_NOEXCEPT noexcept
extern "C" {
#else
#define IRFQ_INFINITE_NOEXCEPT
#endif

#define IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2 UINT32_C(131072)
#define IRFQ_INFINITE_MAX_SCAN_BYTES_V2 UINT64_C(15663104)
#define IRFQ_INFINITE_MAX_FRAME_BYTES_V2 UINT64_C(65536)
#define IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2 UINT64_C(65536)
#define IRFQ_INFINITE_MAX_STORE_RANGE_BYTES_V2 UINT64_C(16777216)
#define IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 UINT64_C(65536)
#define IRFQ_INFINITE_MAX_RESUME_STEPS_V2 UINT32_C(4)
#define IRFQ_INFINITE_MAX_STORE_ITEMS_V2 UINT32_C(256)
#define IRFQ_INFINITE_MAX_ACTIONS_V2 UINT32_C(8)
#define IRFQ_INFINITE_MAX_APPLICATION_MESSAGE_TYPE_BYTES_V2 UINT32_C(8)
#define IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2 UINT32_C(1)
#define IRFQ_INFINITE_APPLICATION_VERSION_FIX_LATEST_V2 UINT32_C(10)

#define IRFQ_INFINITE_SESSION_POLICY_VALIDATE_LENGTH_CHECKSUM_V2 UINT64_C(1)

typedef uint32_t irfq_infinite_status_v2;
#define IRFQ_INFINITE_STATUS_OK_V2 UINT32_C(0)
#define IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2 UINT32_C(1)
#define IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2 UINT32_C(2)
#define IRFQ_INFINITE_STATUS_NEED_MORE_V2 UINT32_C(3)
#define IRFQ_INFINITE_STATUS_FRAME_READY_V2 UINT32_C(4)
#define IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2 UINT32_C(5)
#define IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2 UINT32_C(6)
#define IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2 UINT32_C(7)
#define IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2 UINT32_C(8)
#define IRFQ_INFINITE_STATUS_READY_V2 UINT32_C(9)
#define IRFQ_INFINITE_STATUS_PLAN_PENDING_V2 UINT32_C(10)
#define IRFQ_INFINITE_STATUS_STALE_PLAN_V2 UINT32_C(11)
#define IRFQ_INFINITE_STATUS_REVISION_MISMATCH_V2 UINT32_C(12)
#define IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2 UINT32_C(13)
#define IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2 UINT32_C(14)

typedef uint32_t irfq_infinite_scan_stage_v2;
#define IRFQ_INFINITE_SCAN_BEGIN_STRING_V2 UINT32_C(0)
#define IRFQ_INFINITE_SCAN_BODY_LENGTH_PREFIX_V2 UINT32_C(1)
#define IRFQ_INFINITE_SCAN_BODY_LENGTH_V2 UINT32_C(2)
#define IRFQ_INFINITE_SCAN_BODY_V2 UINT32_C(3)

typedef uint32_t irfq_infinite_prepare_kind_v2;
#define IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2 UINT32_C(1)
#define IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2 UINT32_C(2)
#define IRFQ_INFINITE_PREPARE_RUST_TIMER_V2 UINT32_C(3)
#define IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2 UINT32_C(4)

typedef uint32_t irfq_infinite_stage_v2;
#define IRFQ_INFINITE_STAGE_HEAD_V2 UINT32_C(1)
#define IRFQ_INFINITE_STAGE_READ_R2_V2 UINT32_C(2)
#define IRFQ_INFINITE_STAGE_TARGET_CAS_V2 UINT32_C(3)
#define IRFQ_INFINITE_STAGE_RESET_FINAL_V2 UINT32_C(4)

typedef uint32_t irfq_infinite_resume_kind_v2;
#define IRFQ_INFINITE_RESUME_STORE_RANGE_V2 UINT32_C(1)
#define IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2 UINT32_C(2)
#define IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2 UINT32_C(3)
#define IRFQ_INFINITE_RESUME_OUTPUT_V2 UINT32_C(4)

typedef uint32_t irfq_infinite_decision_v2;
#define IRFQ_INFINITE_DECISION_NONE_V2 UINT32_C(0)
#define IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2 UINT32_C(1)
#define IRFQ_INFINITE_DECISION_REJECT_NO_CONSUME_V2 UINT32_C(2)

typedef uint32_t irfq_infinite_action_v2;
#define IRFQ_INFINITE_ACTION_NONE_V2 UINT32_C(0)
#define IRFQ_INFINITE_ACTION_SESSION_V2 UINT32_C(1)
#define IRFQ_INFINITE_ACTION_APPLICATION_V2 UINT32_C(2)
#define IRFQ_INFINITE_ACTION_RESEND_V2 UINT32_C(3)
#define IRFQ_INFINITE_ACTION_TIMER_V2 UINT32_C(4)
#define IRFQ_INFINITE_ACTION_RESET_V2 UINT32_C(5)

typedef uint32_t irfq_infinite_event_v2;
#define IRFQ_INFINITE_EVENT_NONE_V2 UINT32_C(0)
#define IRFQ_INFINITE_TIMER_HEARTBEAT_DUE_V2 UINT32_C(1)
#define IRFQ_INFINITE_CONTROL_ADVANCE_STAGE_V2 UINT32_C(1)

typedef uint32_t irfq_infinite_store_item_kind_v2;
#define IRFQ_INFINITE_STORE_ITEM_MESSAGE_V2 UINT32_C(1)
#define IRFQ_INFINITE_STORE_ITEM_GAP_V2 UINT32_C(2)

typedef struct irfq_infinite_input_header_v2 {
  uint32_t structure_size;
  uint32_t abi_version;
  uint64_t reserved;
} irfq_infinite_input_header_v2;

typedef struct irfq_infinite_output_header_v2 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_status_v2 status;
  uint32_t reserved;
} irfq_infinite_output_header_v2;

typedef struct irfq_infinite_slice_v2 {
  const uint8_t *data;
  uint64_t length;
} irfq_infinite_slice_v2;

typedef struct irfq_infinite_buffer_v2 {
  uint8_t *data;
  uint64_t capacity;
  uint64_t length;
} irfq_infinite_buffer_v2;

typedef struct irfq_infinite_scan_cursor_v2 {
  uint64_t frame_start;
  uint64_t scan_offset;
  uint64_t body_length;
  uint64_t checksum_begin;
  irfq_infinite_scan_stage_v2 stage;
  uint32_t body_length_has_digit;
} irfq_infinite_scan_cursor_v2;

typedef struct irfq_infinite_scan_request_v2 {
  irfq_infinite_input_header_v2 header;
  irfq_infinite_slice_v2 input;
  irfq_infinite_scan_cursor_v2 cursor;
} irfq_infinite_scan_request_v2;

typedef struct irfq_infinite_scan_response_v2 {
  irfq_infinite_output_header_v2 header;
  irfq_infinite_scan_cursor_v2 cursor;
  uint64_t complete_prefix_length;
} irfq_infinite_scan_response_v2;

typedef struct irfq_infinite_session_v2 irfq_infinite_session_v2;

typedef struct irfq_infinite_prepare_id_v2 {
  uint64_t session;
  uint64_t value;
} irfq_infinite_prepare_id_v2;

typedef struct irfq_infinite_session_create_request_v2 {
  irfq_infinite_input_header_v2 header;
  irfq_infinite_slice_v2 begin_string;
  irfq_infinite_slice_v2 sender_comp_id;
  irfq_infinite_slice_v2 target_comp_id;
  irfq_infinite_slice_v2 session_qualifier;
  irfq_infinite_slice_v2 native_state;
  uint64_t session_epoch;
  uint64_t cache_revision;
  int64_t creation_tai_ns;
  uint32_t snapshot_codec_version;
  uint32_t default_application_version;
  uint64_t session_policy_flags;
  uint8_t transport_dictionary_sha256[32];
  uint8_t application_dictionary_sha256[32];
  uint8_t authenticated_session_binding_sha256[32];
} irfq_infinite_session_create_request_v2;

typedef struct irfq_infinite_session_create_response_v2 {
  irfq_infinite_output_header_v2 header;
  irfq_infinite_session_v2 *session;
  uint64_t cache_revision;
} irfq_infinite_session_create_response_v2;

typedef struct irfq_infinite_prepare_request_v2 {
  irfq_infinite_input_header_v2 header;
  irfq_infinite_prepare_kind_v2 kind;
  irfq_infinite_stage_v2 stage;
  irfq_infinite_event_v2 event_code;
  uint32_t event_flags;
  uint64_t event_identity;
  uint64_t expected_session_epoch;
  uint64_t expected_revision;
  int64_t now_tai_ns;
  uint8_t application_message_type[IRFQ_INFINITE_MAX_APPLICATION_MESSAGE_TYPE_BYTES_V2];
  uint32_t application_message_type_length;
  uint32_t reserved;
  /** Inbound: complete FIX frame. Authorized outbound: ordered SOH-delimited body fields only. */
  irfq_infinite_slice_v2 payload;
} irfq_infinite_prepare_request_v2;

typedef struct irfq_infinite_declarative_action_v2 {
  irfq_infinite_action_v2 kind;
  uint32_t flags;
  uint64_t sequence_begin;
  uint64_t sequence_end;
  uint64_t output_offset;
  uint64_t output_length;
} irfq_infinite_declarative_action_v2;

typedef struct irfq_infinite_prepare_response_v2 {
  irfq_infinite_output_header_v2 header;
  irfq_infinite_prepare_id_v2 prepare_id;
  uint32_t step;
  uint32_t action_count;
  uint64_t base_revision;
  uint64_t result_revision;
  uint64_t store_range_begin;
  uint64_t store_range_end;
  uint64_t required_native_state_capacity;
  uint64_t required_output_capacity;
  irfq_infinite_buffer_v2 native_state;
  irfq_infinite_buffer_v2 output;
  uint8_t native_state_sha256[32];
  irfq_infinite_declarative_action_v2 actions[IRFQ_INFINITE_MAX_ACTIONS_V2];
  uint8_t application_message_type[IRFQ_INFINITE_MAX_APPLICATION_MESSAGE_TYPE_BYTES_V2];
  uint32_t application_message_type_length;
  uint32_t reserved;
} irfq_infinite_prepare_response_v2;

typedef struct irfq_infinite_store_item_v2 {
  uint64_t sequence;
  irfq_infinite_store_item_kind_v2 kind;
  uint32_t reserved;
  irfq_infinite_slice_v2 body;
} irfq_infinite_store_item_v2;

typedef struct irfq_infinite_resume_request_v2 {
  irfq_infinite_input_header_v2 header;
  irfq_infinite_prepare_id_v2 prepare_id;
  uint32_t step;
  irfq_infinite_resume_kind_v2 kind;
  irfq_infinite_decision_v2 decision;
  uint32_t reserved;
  uint64_t store_range_begin;
  uint64_t store_range_end;
  const irfq_infinite_store_item_v2 *store_items;
  uint32_t input_item_count;
  uint32_t reserved2;
} irfq_infinite_resume_request_v2;

typedef struct irfq_infinite_apply_committed_request_v2 {
  irfq_infinite_input_header_v2 header;
  irfq_infinite_prepare_id_v2 prepare_id;
  uint64_t result_revision;
  uint8_t native_state_sha256[32];
} irfq_infinite_apply_committed_request_v2;

typedef struct irfq_infinite_abort_request_v2 {
  irfq_infinite_input_header_v2 header;
  irfq_infinite_prepare_id_v2 prepare_id;
} irfq_infinite_abort_request_v2;

typedef struct irfq_infinite_operation_response_v2 {
  irfq_infinite_output_header_v2 header;
  uint64_t cache_revision;
} irfq_infinite_operation_response_v2;

/** Stateless FIX boundary scan. No input pointer, byte, cursor, or time is retained. */
irfq_infinite_status_v2 irfq_infinite_scan_v2(
    const irfq_infinite_scan_request_v2 *request,
    irfq_infinite_scan_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/**
 * Constructs adapter-owned disposable cache state from authenticated configuration and native state bytes.
 * A handle is exclusively owned: calls must not overlap, and destroy follows the final joined call.
 */
irfq_infinite_status_v2 irfq_infinite_session_create_v2(
    const irfq_infinite_session_create_request_v2 *request,
    irfq_infinite_session_create_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/** Creates one stage-local, side-effect-free plan. */
irfq_infinite_status_v2 irfq_infinite_prepare_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_prepare_request_v2 *request,
    irfq_infinite_prepare_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/** Supplies one closed bounded Rust-owned input to the existing plan. */
irfq_infinite_status_v2 irfq_infinite_resume_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_resume_request_v2 *request,
    irfq_infinite_prepare_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/** Installs only a snapshot already proved durable by Rust. */
irfq_infinite_status_v2 irfq_infinite_apply_committed_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_apply_committed_request_v2 *request,
    irfq_infinite_operation_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/** Drops one plan whose durable stage absence Rust has already proved. */
irfq_infinite_status_v2 irfq_infinite_abort_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_abort_request_v2 *request,
    irfq_infinite_operation_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/** Destroys one exclusively owned cache handle. */
irfq_infinite_status_v2 irfq_infinite_destroy_v2(irfq_infinite_session_v2 *session) IRFQ_INFINITE_NOEXCEPT;

#ifdef __cplusplus
} // extern "C"
#endif

#undef IRFQ_INFINITE_NOEXCEPT
