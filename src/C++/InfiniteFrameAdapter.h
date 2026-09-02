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
#define IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2 UINT32_C(2)
#define IRFQ_INFINITE_NATIVE_STATE_SCHEMA_VERSION_V2 UINT32_C(1)
#define IRFQ_INFINITE_NATIVE_STATE_BYTES_V2 UINT64_C(312)
#define IRFQ_INFINITE_MAX_SCAN_BYTES_V2 UINT64_C(65536)
#define IRFQ_INFINITE_MAX_FRAME_BYTES_V2 UINT64_C(65536)
#define IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2 IRFQ_INFINITE_NATIVE_STATE_BYTES_V2
#define IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2 UINT64_C(16777216)
#define IRFQ_INFINITE_MAX_STORE_RANGE_BYTES_V2 UINT64_C(16777216)
#define IRFQ_INFINITE_MAX_APPLICATION_WIRE_BYTES_V2 UINT64_C(15728640)
#define IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 UINT64_C(65536)
#define IRFQ_INFINITE_MAX_STORE_ITEMS_V2 UINT32_C(256)
#define IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2 UINT32_C(240)
#define IRFQ_INFINITE_MAX_OUTPUT_FRAMES_V2 UINT32_C(256)
#define IRFQ_INFINITE_MAX_ACTIONS_V2 UINT32_C(258)
#define IRFQ_INFINITE_MAX_RESUME_STEPS_V2 UINT32_C(3)
#define IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2 UINT32_C(8)
#define IRFQ_INFINITE_MAX_TEST_REQUEST_ID_BYTES_V2 UINT32_C(64)
#define IRFQ_INFINITE_MAX_GATEWAY_INBOUND_DISPOSITION_ID_BYTES_V2 UINT32_C(64)
#define IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2 INT64_C(9223372036854775807)

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
#define IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2 UINT32_C(15)
#define IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2 UINT32_C(16)

typedef uint32_t irfq_infinite_boolean_v2;
#define IRFQ_INFINITE_NO_V2 UINT32_C(0)
#define IRFQ_INFINITE_YES_V2 UINT32_C(1)

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
#define IRFQ_INFINITE_STAGE_EVENT_V2 UINT32_C(5)

typedef uint32_t irfq_infinite_event_v2;
#define IRFQ_INFINITE_EVENT_INBOUND_FRAME_V2 UINT32_C(1)
#define IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2 UINT32_C(2)
#define IRFQ_INFINITE_EVENT_STORED_FRAME_RETRANSMIT_V2 UINT32_C(3)
#define IRFQ_INFINITE_EVENT_APPLICATION_REPLAY_BEGIN_V2 UINT32_C(4)
#define IRFQ_INFINITE_EVENT_READ_RESULT_BEGIN_V2 UINT32_C(5)
#define IRFQ_INFINITE_EVENT_TIMER_TICK_V2 UINT32_C(6)
#define IRFQ_INFINITE_EVENT_SCHEDULED_RESET_TRIGGER_V2 UINT32_C(7)
#define IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2 UINT32_C(8)
#define IRFQ_INFINITE_EVENT_ADMIN_LOGOUT_V2 UINT32_C(9)
#define IRFQ_INFINITE_EVENT_ADMIN_REJECT_V2 UINT32_C(10)
#define IRFQ_INFINITE_EVENT_ADMIN_RESEND_REQUEST_V2 UINT32_C(11)
#define IRFQ_INFINITE_EVENT_ADMIN_HEARTBEAT_V2 UINT32_C(12)
#define IRFQ_INFINITE_EVENT_ADMIN_TEST_REQUEST_V2 UINT32_C(13)
#define IRFQ_INFINITE_EVENT_CONTINUE_RESEND_V2 UINT32_C(14)
#define IRFQ_INFINITE_EVENT_CONTINUE_QUEUED_INBOUND_V2 UINT32_C(15)
#define IRFQ_INFINITE_EVENT_CONTINUE_APPLICATION_BLOCK_V2 UINT32_C(16)
#define IRFQ_INFINITE_EVENT_CONTINUE_READ_RESULT_V2 UINT32_C(17)
#define IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2 UINT32_C(18)
#define IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2 UINT32_C(19)
#define IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2 UINT32_C(20)
#define IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2 UINT32_C(21)

typedef uint32_t irfq_infinite_resume_kind_v2;
#define IRFQ_INFINITE_RESUME_STORE_RANGE_V2 UINT32_C(1)
#define IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2 UINT32_C(2)
#define IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2 UINT32_C(3)
#define IRFQ_INFINITE_RESUME_OUTPUT_V2 UINT32_C(4)

typedef uint32_t irfq_infinite_application_decision_v2;
#define IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2 UINT32_C(1)
#define IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2 UINT32_C(2)

typedef uint32_t irfq_infinite_epoch_reset_decision_v2;
#define IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2 UINT32_C(1)
#define IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2 UINT32_C(2)

typedef uint32_t irfq_infinite_sequence_state_v2;
#define IRFQ_INFINITE_SEQUENCE_ABSENT_V2 UINT32_C(0)
#define IRFQ_INFINITE_SEQUENCE_VALUE_V2 UINT32_C(1)
#define IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 UINT32_C(2)

typedef uint32_t irfq_infinite_application_block_mode_v2;
#define IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2 UINT32_C(0)
#define IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2 UINT32_C(1)
#define IRFQ_INFINITE_APPLICATION_BLOCK_SEMANTIC_REPLAY_V2 UINT32_C(2)

typedef uint32_t irfq_infinite_input_source_v2;
#define IRFQ_INFINITE_INPUT_NONE_V2 UINT32_C(0)
#define IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2 UINT32_C(1)
#define IRFQ_INFINITE_INPUT_STORE_ROW_V2 UINT32_C(2)

typedef uint32_t irfq_infinite_store_class_v2;
#define IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2 UINT32_C(1)
#define IRFQ_INFINITE_STORE_CLASS_REVOCABLE_SUPPRESSED_V2 UINT32_C(2)
#define IRFQ_INFINITE_STORE_CLASS_AH0_RESULT_BLOCK_V2 UINT32_C(3)
#define IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2 UINT32_C(4)
#define IRFQ_INFINITE_STORE_CLASS_PROVEN_GAP_V2 UINT32_C(5)

typedef uint32_t irfq_infinite_action_kind_v2;
#define IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2 UINT32_C(1)
#define IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2 UINT32_C(2)
#define IRFQ_INFINITE_ACTION_QUEUE_INSERT_V2 UINT32_C(3)
#define IRFQ_INFINITE_ACTION_QUEUE_ERASE_RANGE_V2 UINT32_C(4)
#define IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2 UINT32_C(5)
#define IRFQ_INFINITE_ACTION_DISCONNECT_V2 UINT32_C(6)
#define IRFQ_INFINITE_ACTION_RESET_TRIGGER_V2 UINT32_C(7)
#define IRFQ_INFINITE_ACTION_TARGET_ADVANCE_V2 UINT32_C(8)

typedef uint32_t irfq_infinite_output_class_v2;
#define IRFQ_INFINITE_OUTPUT_NONE_V2 UINT32_C(0)
#define IRFQ_INFINITE_OUTPUT_ORIGINAL_APPLICATION_V2 UINT32_C(1)
#define IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2 UINT32_C(2)
#define IRFQ_INFINITE_OUTPUT_SEMANTIC_REPLAY_V2 UINT32_C(3)
#define IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2 UINT32_C(4)
#define IRFQ_INFINITE_OUTPUT_GAP_FILL_V2 UINT32_C(5)

typedef uint32_t irfq_infinite_protocol_disposition_v2;
#define IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2 UINT32_C(1)
#define IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2 UINT32_C(2)
#define IRFQ_INFINITE_DISPOSITION_PENDING_CORE_V2 UINT32_C(3)
#define IRFQ_INFINITE_DISPOSITION_PENDING_READ_V2 UINT32_C(4)
#define IRFQ_INFINITE_DISPOSITION_PENDING_RESET_LOGON_V2 UINT32_C(5)

typedef uint32_t irfq_infinite_safe_reason_v2;
#define IRFQ_INFINITE_REASON_NONE_V2 UINT32_C(0)
#define IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2 UINT32_C(1)
#define IRFQ_INFINITE_REASON_SESSION_TIME_V2 UINT32_C(2)
#define IRFQ_INFINITE_REASON_LATENCY_V2 UINT32_C(3)
#define IRFQ_INFINITE_REASON_SEQUENCE_V2 UINT32_C(4)
#define IRFQ_INFINITE_REASON_DICTIONARY_V2 UINT32_C(5)
#define IRFQ_INFINITE_REASON_RESET_REJECTED_V2 UINT32_C(6)
#define IRFQ_INFINITE_REASON_HEARTBEAT_TIMEOUT_V2 UINT32_C(7)
#define IRFQ_INFINITE_REASON_PROTOCOL_V2 UINT32_C(8)
#define IRFQ_INFINITE_REASON_INTEGRITY_V2 UINT32_C(9)

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
  uint64_t scan_offset;
  uint64_t body_length;
  uint64_t checksum_begin;
  irfq_infinite_scan_stage_v2 stage;
  irfq_infinite_boolean_v2 body_length_has_digit;
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
  uint64_t high;
  uint64_t low;
} irfq_infinite_prepare_id_v2;

typedef struct irfq_infinite_session_create_request_v2 {
  irfq_infinite_input_header_v2 header;
  uint32_t snapshot_codec_version;
  uint32_t reserved;
  irfq_infinite_slice_v2 canonical_session_create_config;
  uint64_t session_epoch;
  uint64_t cache_revision;
  int64_t creation_tai_ns;
  int64_t creation_utc_ns;
  irfq_infinite_slice_v2 native_state;
} irfq_infinite_session_create_request_v2;

typedef struct irfq_infinite_session_create_response_v2 {
  irfq_infinite_output_header_v2 header;
  irfq_infinite_session_v2 *session;
  uint64_t cache_epoch;
  uint64_t cache_revision;
} irfq_infinite_session_create_response_v2;

typedef struct irfq_infinite_prepare_request_v2 {
  irfq_infinite_input_header_v2 header;
  irfq_infinite_prepare_kind_v2 kind;
  irfq_infinite_stage_v2 stage;
  irfq_infinite_event_v2 event;
  irfq_infinite_application_block_mode_v2 application_block_mode;
  uint8_t event_identity_sha256[32];
  uint64_t expected_epoch;
  uint64_t expected_revision;
  int64_t now_tai_ns;
  int64_t now_utc_ns;
  irfq_infinite_sequence_state_v2 next_original_state;
  uint32_t reserved;
  uint64_t next_original_value;
  irfq_infinite_slice_v2 payload;
} irfq_infinite_prepare_request_v2;

typedef struct irfq_infinite_declarative_action_v2 {
  irfq_infinite_action_kind_v2 kind;
  irfq_infinite_output_class_v2 output_class;
  irfq_infinite_protocol_disposition_v2 disposition;
  uint32_t msg_type_length;
  uint8_t msg_type[IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2];
  irfq_infinite_input_source_v2 input_source;
  uint32_t input_item_index;
  uint64_t sequence_begin;
  uint64_t sequence_end_exclusive;
  uint64_t input_offset;
  uint64_t input_length;
  uint64_t output_offset;
  uint64_t output_length;
  uint8_t binding_sha256[32];
  irfq_infinite_safe_reason_v2 reason_code;
  uint32_t reserved;
} irfq_infinite_declarative_action_v2;

typedef struct irfq_infinite_prepare_response_v2 {
  irfq_infinite_output_header_v2 header;
  irfq_infinite_prepare_id_v2 prepare_id;
  uint32_t step;
  irfq_infinite_prepare_kind_v2 kind;
  irfq_infinite_stage_v2 stage;
  irfq_infinite_event_v2 event;
  uint8_t event_identity_sha256[32];
  uint64_t base_epoch;
  uint64_t base_revision;
  uint64_t store_range_begin;
  uint64_t store_range_end_exclusive;
  uint64_t subject_sequence;
  uint8_t subject_sha256[32];
  uint8_t msg_type[IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2];
  uint32_t msg_type_length;
  irfq_infinite_input_source_v2 input_source;
  uint32_t input_item_index;
  uint32_t reserved;
  uint64_t input_offset;
  uint64_t input_length;
  uint64_t required_output_capacity;
  uint64_t result_epoch;
  uint64_t result_revision;
  irfq_infinite_buffer_v2 native_state;
  irfq_infinite_buffer_v2 output;
  uint8_t native_state_sha256[32];
  irfq_infinite_declarative_action_v2 *actions;
  uint32_t action_capacity;
  uint32_t action_count;
  uint32_t output_frame_count;
  irfq_infinite_boolean_v2 has_more;
} irfq_infinite_prepare_response_v2;

typedef struct irfq_infinite_store_row_v2 {
  uint64_t sequence;
  irfq_infinite_store_class_v2 store_class;
  uint32_t msg_type_length;
  uint32_t frame_length;
  uint32_t reserved;
  uint8_t frame_sha256[32];
  uint8_t body_sha256[32];
  uint8_t msg_type[IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2];
  irfq_infinite_slice_v2 frame;
} irfq_infinite_store_row_v2;

typedef struct irfq_infinite_resume_request_v2 {
  irfq_infinite_input_header_v2 header;
  irfq_infinite_prepare_id_v2 prepare_id;
  uint32_t step;
  irfq_infinite_resume_kind_v2 kind;
  uint64_t subject_sequence;
  uint8_t subject_sha256[32];
  uint32_t decision;
  irfq_infinite_input_source_v2 input_source;
  uint32_t input_item_index;
  uint32_t reserved;
  irfq_infinite_slice_v2 input_source_bytes;
  uint64_t store_range_begin;
  uint64_t store_range_end_exclusive;
  const irfq_infinite_store_row_v2 *store_rows;
  uint32_t store_row_count;
  uint32_t reserved2;
  /** Borrowed for this call: 1-64 visible ASCII bytes only for an application-dispatch rejection; null/empty otherwise.
   */
  irfq_infinite_slice_v2 gateway_inbound_disposition_id;
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

/// Stateless FIX boundary scan; no input byte, pointer, cursor, or time is retained.
irfq_infinite_status_v2 irfq_infinite_scan_v2(
    const irfq_infinite_scan_request_v2 *request,
    irfq_infinite_scan_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/// Constructs disposable cache state from one closed configuration and fresh or restored native state.
irfq_infinite_status_v2 irfq_infinite_session_create_v2(
    const irfq_infinite_session_create_request_v2 *request,
    irfq_infinite_session_create_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/// Creates the session's sole stage-local, side-effect-free plan.
irfq_infinite_status_v2 irfq_infinite_prepare_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_prepare_request_v2 *request,
    irfq_infinite_prepare_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/// Supplies one exact bounded Rust-owned result or output buffer to the pending plan.
irfq_infinite_status_v2 irfq_infinite_resume_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_resume_request_v2 *request,
    irfq_infinite_prepare_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/// Installs only the retained snapshot already proved durable by Rust.
irfq_infinite_status_v2 irfq_infinite_apply_committed_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_apply_committed_request_v2 *request,
    irfq_infinite_operation_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/// Drops only the pending plan whose corresponding durable stage is proved absent.
irfq_infinite_status_v2 irfq_infinite_abort_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_abort_request_v2 *request,
    irfq_infinite_operation_response_v2 *response) IRFQ_INFINITE_NOEXCEPT;

/// Scrubs and destroys one exclusively owned session cache handle after all calls have joined.
irfq_infinite_status_v2 irfq_infinite_destroy_v2(irfq_infinite_session_v2 *session) IRFQ_INFINITE_NOEXCEPT;

#ifdef __cplusplus
} // extern "C"
#endif

#undef IRFQ_INFINITE_NOEXCEPT
