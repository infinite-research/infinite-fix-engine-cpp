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

#define IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1 UINT32_C(65536)
#define IRFQ_INFINITE_CAP_ENGINE_LIFECYCLE_V1 UINT64_C(1)
#define IRFQ_INFINITE_CAP_BOOTSTRAP_V1 UINT64_C(2)
#define IRFQ_INFINITE_CAP_FRAME_DISPATCH_V1 UINT64_C(4)
#define IRFQ_INFINITE_CAP_REGISTRATION_CALLBACK_V1 UINT64_C(8)
#define IRFQ_INFINITE_CAP_HEAD_WAIT_V1 UINT64_C(16)
#define IRFQ_INFINITE_CAP_CLASSIFY_APPLY_V1 UINT64_C(32)
#define IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1 UINT64_C(63)

#define IRFQ_INFINITE_MAX_CONNECTIONS_V1 UINT32_C(64)
#define IRFQ_INFINITE_MAX_BATCH_FRAMES_V1 UINT32_C(239)
#define IRFQ_INFINITE_MAX_FRAME_BYTES_V1 UINT64_C(65536)
#define IRFQ_INFINITE_MAX_BATCH_BYTES_V1 UINT64_C(15663104)
#define IRFQ_INFINITE_MAX_FAILURE_BYTES_V1 UINT32_C(1024)
#define IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1 UINT64_C(7680)

typedef uint32_t irfq_infinite_status_v1;
#define IRFQ_INFINITE_STATUS_OK_V1 UINT32_C(0)
#define IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1 UINT32_C(1)
#define IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1 UINT32_C(2)
#define IRFQ_INFINITE_STATUS_NOT_READY_V1 UINT32_C(3)
#define IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1 UINT32_C(4)
#define IRFQ_INFINITE_STATUS_STREAM_FENCED_V1 UINT32_C(5)
#define IRFQ_INFINITE_STATUS_AT_HEAD_V1 UINT32_C(6)
#define IRFQ_INFINITE_STATUS_CLASSIFIED_V1 UINT32_C(7)
#define IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1 UINT32_C(8)
#define IRFQ_INFINITE_STATUS_AUTHORIZED_NO_CONSUME_V1 UINT32_C(9)
#define IRFQ_INFINITE_STATUS_APPLIED_V1 UINT32_C(10)
#define IRFQ_INFINITE_STATUS_CLOSED_V1 UINT32_C(11)
#define IRFQ_INFINITE_STATUS_SHUTDOWN_V1 UINT32_C(12)
#define IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1 UINT32_C(13)

typedef uint32_t irfq_infinite_bootstrap_outcome_v1;
#define IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1 UINT32_C(1)
#define IRFQ_INFINITE_BOOTSTRAP_REJECTED_V1 UINT32_C(2)
#define IRFQ_INFINITE_BOOTSTRAP_FENCED_V1 UINT32_C(3)

typedef uint32_t irfq_infinite_action_v1;
#define IRFQ_INFINITE_ACTION_PROTOCOL_CONTROL_V1 UINT32_C(1)
#define IRFQ_INFINITE_ACTION_SEQUENCE_RESET_V1 UINT32_C(2)
#define IRFQ_INFINITE_ACTION_LOGOUT_V1 UINT32_C(3)
#define IRFQ_INFINITE_ACTION_RESEND_OR_QUEUED_RELEASE_V1 UINT32_C(4)
#define IRFQ_INFINITE_ACTION_PROTOCOL_DISPOSITION_V1 UINT32_C(5)
#define IRFQ_INFINITE_ACTION_APPLICATION_V1 UINT32_C(6)
#define IRFQ_INFINITE_ACTION_FAILURE_V1 UINT32_C(7)

typedef uint32_t irfq_infinite_sequence_disposition_v1;
#define IRFQ_INFINITE_SEQUENCE_AT_HEAD_V1 UINT32_C(1)
#define IRFQ_INFINITE_SEQUENCE_TOO_HIGH_V1 UINT32_C(2)
#define IRFQ_INFINITE_SEQUENCE_TOO_LOW_V1 UINT32_C(3)
#define IRFQ_INFINITE_SEQUENCE_UNAVAILABLE_V1 UINT32_C(4)

typedef uint32_t irfq_infinite_dispatch_fault_v1;
#define IRFQ_INFINITE_DISPATCH_FAULT_NONE_V1 UINT32_C(0)
#define IRFQ_INFINITE_DISPATCH_FAULT_FRAME_TOO_LARGE_V1 UINT32_C(1)
#define IRFQ_INFINITE_DISPATCH_FAULT_ACCUMULATOR_OVERFLOW_V1 UINT32_C(2)
#define IRFQ_INFINITE_DISPATCH_FAULT_MALFORMED_FRAME_V1 UINT32_C(3)
#define IRFQ_INFINITE_DISPATCH_FAULT_BATCH_LIMIT_V1 UINT32_C(4)
#define IRFQ_INFINITE_DISPATCH_FAULT_INVALID_OBSERVATION_V1 UINT32_C(5)

typedef uint32_t irfq_infinite_engine_lifecycle_v1;
#define IRFQ_INFINITE_ENGINE_INITIALIZED_V1 UINT32_C(1)
#define IRFQ_INFINITE_ENGINE_CLOSING_V1 UINT32_C(2)
#define IRFQ_INFINITE_ENGINE_SHUTDOWN_V1 UINT32_C(3)

typedef uint32_t irfq_infinite_connection_lifecycle_v1;
#define IRFQ_INFINITE_CONNECTION_OPEN_V1 UINT32_C(1)
#define IRFQ_INFINITE_CONNECTION_CLOSING_V1 UINT32_C(2)
#define IRFQ_INFINITE_CONNECTION_CLOSED_V1 UINT32_C(3)

typedef struct irfq_infinite_handle_v1 {
  uint64_t object;
  uint64_t generation;
} irfq_infinite_handle_v1;

typedef struct irfq_infinite_slice_v1 {
  const uint8_t *data;
  uint64_t length;
} irfq_infinite_slice_v1;

typedef struct irfq_infinite_output_header_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_status_v1 status;
  uint32_t reserved;
  uint64_t written_length;
} irfq_infinite_output_header_v1;

typedef struct irfq_infinite_abi_info_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  uint64_t capabilities;
  uint32_t max_connections;
  uint32_t max_batch_frames;
  uint64_t max_frame_bytes;
  uint64_t max_batch_bytes;
  uint8_t reserved[24];
} irfq_infinite_abi_info_v1;

typedef struct irfq_infinite_callback_table_v1 irfq_infinite_callback_table_v1;

typedef struct irfq_infinite_engine_init_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  uint64_t required_capabilities;
  const irfq_infinite_callback_table_v1 *callbacks;
  uint8_t reserved[8];
} irfq_infinite_engine_init_request_v1;

typedef struct irfq_infinite_engine_response_v1 {
  irfq_infinite_output_header_v1 header;
  irfq_infinite_handle_v1 engine;
  uint64_t capabilities;
  irfq_infinite_engine_lifecycle_v1 lifecycle;
  uint8_t reserved[12];
} irfq_infinite_engine_response_v1;

typedef struct irfq_infinite_bootstrap_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_slice_v1 frame;
  int64_t observed_tai_ns;
  irfq_infinite_handle_v1 transport_nonce;
} irfq_infinite_bootstrap_request_v1;

typedef struct irfq_infinite_bootstrap_response_v1 {
  irfq_infinite_output_header_v1 header;
  irfq_infinite_handle_v1 connection;
  irfq_infinite_bootstrap_outcome_v1 outcome;
  uint8_t reserved[20];
} irfq_infinite_bootstrap_response_v1;

typedef struct irfq_infinite_dispatch_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_slice_v1 input;
  uint8_t reserved[8];
} irfq_infinite_dispatch_request_v1;

typedef struct irfq_infinite_frame_descriptor_v1 {
  const uint8_t *data;
  uint64_t length;
  int64_t observed_tai_ns;
} irfq_infinite_frame_descriptor_v1;

typedef struct irfq_infinite_registration_callback_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_handle_v1 connection;
  const irfq_infinite_frame_descriptor_v1 *frames;
  uint32_t frame_count;
  uint32_t reserved;
} irfq_infinite_registration_callback_request_v1;

typedef struct irfq_infinite_registration_result_v1 {
  uint64_t ordinal;
  irfq_infinite_handle_v1 token;
  int64_t observed_tai_ns;
} irfq_infinite_registration_result_v1;

/** Results immediately follow this prefix as `result_count` registration records. */
typedef struct irfq_infinite_dispatch_response_v1 {
  irfq_infinite_output_header_v1 header;
  uint32_t result_count;
  irfq_infinite_dispatch_fault_v1 fault;
} irfq_infinite_dispatch_response_v1;

typedef struct irfq_infinite_head_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_handle_v1 token;
  uint8_t reserved[8];
} irfq_infinite_head_request_v1;

typedef struct irfq_infinite_head_callback_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_handle_v1 connection;
  irfq_infinite_handle_v1 token;
  uint8_t reserved[8];
} irfq_infinite_head_callback_request_v1;

typedef struct irfq_infinite_operation_response_v1 {
  irfq_infinite_output_header_v1 header;
  uint32_t lifecycle;
  uint32_t reserved;
} irfq_infinite_operation_response_v1;

typedef struct irfq_infinite_classification_callback_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_handle_v1 connection;
  irfq_infinite_handle_v1 token;
  irfq_infinite_handle_v1 classification;
  uint64_t session_revision;
  uint64_t sender_sequence;
  uint64_t target_sequence;
  irfq_infinite_action_v1 action;
  irfq_infinite_sequence_disposition_v1 sequence_disposition;
  uint32_t operation_count;
  uint32_t failure_length;
  const uint8_t *failure;
  int64_t observed_tai_ns;
} irfq_infinite_classification_callback_request_v1;

typedef struct irfq_infinite_classification_callback_response_v1 {
  irfq_infinite_output_header_v1 header;
  irfq_infinite_handle_v1 authorization;
  irfq_infinite_status_v1 outcome;
  uint32_t reserved;
} irfq_infinite_classification_callback_response_v1;

typedef struct irfq_infinite_classification_response_v1 {
  irfq_infinite_output_header_v1 header;
  irfq_infinite_handle_v1 classification;
  irfq_infinite_handle_v1 authorization;
  uint64_t session_revision;
  uint64_t sender_sequence;
  uint64_t target_sequence;
  irfq_infinite_action_v1 action;
  irfq_infinite_sequence_disposition_v1 sequence_disposition;
  irfq_infinite_status_v1 outcome;
  uint32_t reserved;
} irfq_infinite_classification_response_v1;

typedef struct irfq_infinite_apply_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  irfq_infinite_handle_v1 classification;
  irfq_infinite_handle_v1 authorization;
  uint8_t reserved[8];
} irfq_infinite_apply_request_v1;

typedef struct irfq_infinite_close_request_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  uint32_t reason;
  uint8_t reserved[20];
} irfq_infinite_close_request_v1;

/**
 * Accepts or rejects one validated Logon frame. `request` and its frame are
 * borrowed for this invocation. `output` is a preinitialized
 * `irfq_infinite_bootstrap_response_v1`; on return its header status must equal
 * the function result, `written_length` must equal the response size, and the
 * status/outcome pair must be OK/ACCEPTED, NOT_READY/REJECTED, or
 * STREAM_FENCED/FENCED. ACCEPTED must publish a nonzero caller-owned connection
 * handle. Any nonzero handle returned on another outcome is fenced and released.
 */
typedef irfq_infinite_status_v1 (*irfq_infinite_bootstrap_callback_v1)(
    void *context,
    const irfq_infinite_bootstrap_request_v1 *request,
    void *output,
    uint64_t output_capacity);
/**
 * Registers a complete-frame batch. All request, descriptor, and frame pointers
 * are borrowed for this invocation. `output` has `output_capacity` bytes and
 * starts with a preinitialized `irfq_infinite_dispatch_response_v1`. OK requires
 * one immediately following registration result per input frame; ordinals must
 * increase exactly, tokens must be live and unique, and observation values must
 * match. NOT_REGISTERED and STREAM_FENCED require zero results. The returned
 * status, header status, and exact written length must agree.
 */
typedef irfq_infinite_status_v1 (*irfq_infinite_registration_callback_v1)(
    void *context,
    const irfq_infinite_registration_callback_request_v1 *request,
    void *output,
    uint64_t output_capacity);
/**
 * Waits for one registered external token. `request` is borrowed. `output` is a
 * preinitialized `irfq_infinite_operation_response_v1`. Return AT_HEAD with an
 * OPEN lifecycle, or STREAM_FENCED with a CLOSING lifecycle; the return value,
 * header status, and exact written length must agree.
 */
typedef irfq_infinite_status_v1 (*irfq_infinite_head_callback_v1)(
    void *context,
    const irfq_infinite_head_callback_request_v1 *request,
    void *output,
    uint64_t output_capacity);
/**
 * Authorizes the supplied classification plan. The request and failure bytes
 * are borrowed for this invocation. `output` is a preinitialized
 * `irfq_infinite_classification_callback_response_v1`. Return AUTHORIZED_CONSUME
 * or AUTHORIZED_NO_CONSUME with a nonzero caller-owned authorization handle, or
 * STREAM_FENCED. `outcome`, header status, function result, and exact written
 * length must agree.
 */
typedef irfq_infinite_status_v1 (*irfq_infinite_authorize_callback_v1)(
    void *context,
    const irfq_infinite_classification_callback_request_v1 *request,
    void *output,
    uint64_t output_capacity);
/**
 * Acknowledges connection fencing or release for the external connection
 * handle. The fence slot must return STREAM_FENCED; the release slot must return
 * CLOSED. `reason` is the unchanged caller-supplied opaque reason. No output
 * buffer is used. Release transfers the final external ownership back to the
 * caller after a successful fence; failed acknowledgements terminalize cleanup.
 */
typedef irfq_infinite_status_v1 (
    *irfq_infinite_connection_callback_v1)(void *context, irfq_infinite_handle_v1 connection, uint32_t reason);

struct irfq_infinite_callback_table_v1 {
  uint32_t structure_size;
  uint32_t abi_version;
  void *context;
  irfq_infinite_bootstrap_callback_v1 bootstrap;
  irfq_infinite_registration_callback_v1 register_batch;
  irfq_infinite_head_callback_v1 wait_head;
  irfq_infinite_authorize_callback_v1 authorize;
  irfq_infinite_connection_callback_v1 fence;
  irfq_infinite_connection_callback_v1 release;
};

/**
 * The engine lifecycle is INITIALIZED -> CLOSING -> SHUTDOWN. A connection is
 * OPEN -> CLOSING -> CLOSED. Dispatch/classify/apply are serialized for one
 * connection; different connections may progress concurrently. Callbacks are
 * synchronous, may not re-enter the same handle, and may retain no argument
 * pointer. Engine shutdown is forbidden from every callback ancestry, including
 * callbacks for another engine, because shutdown drains callbacks. The callback
 * table is copied at initialization; its caller-owned
 * context must remain valid until quiescent engine shutdown.
 * Close and shutdown stop acquisition, fence and wake waiters, drain callbacks
 * and in-flight calls, invalidate the generation, then release storage. Close
 * is idempotent while its engine handle remains live.
 *
 * Every output pointer must be naturally aligned and start with a caller-set
 * exact `structure_size` and ABI version. Once that minimal header is valid,
 * the adapter zeros `written_length`, validates all remaining inputs and
 * capacity, stages the complete result, copies it once, and publishes length
 * last. Reserved input bytes must be zero. No borrowed pointer survives its
 * synchronous call. Dispatch output capacity is exactly bounded by
 * `IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1`; callers must provide at least
 * that capacity even when a particular call publishes fewer bytes.
 */
/**
 * Negotiates ABI v1 in place. Initialize `info.structure_size`, `abi_version`,
 * and zero reserved bytes. OK replaces the structure with the exact capability
 * and bound values; ABI_MISMATCH or INVALID_ARGUMENT leaves no usable result.
 */
irfq_infinite_status_v1 irfq_infinite_frame_adapter_query_v1(irfq_infinite_abi_info_v1 *info) IRFQ_INFINITE_NOEXCEPT;

/**
 * Creates one engine and copies the callback table. `register_batch`,
 * `wait_head`, `authorize`, `fence`, and `release` are mandatory; `bootstrap`
 * may be null, in which case bootstrap returns NOT_READY. On OK, `output` is an
 * `irfq_infinite_engine_response_v1` with INITIALIZED lifecycle and a nonzero
 * engine handle. The callback context remains caller-owned until shutdown has
 * quiesced. `output_capacity` must cover the exact response.
 */
irfq_infinite_status_v1 irfq_infinite_engine_initialize_v1(
    const irfq_infinite_engine_init_request_v1 *request,
    void *output,
    uint64_t output_capacity) IRFQ_INFINITE_NOEXCEPT;

/**
 * Stops acquisition, closes all live connections, drains calls and callbacks,
 * and invalidates `engine`. This function must not be called from any adapter
 * callback. `output` is an `irfq_infinite_operation_response_v1`: success is
 * SHUTDOWN/ENGINE_SHUTDOWN; cleanup failure is INTERNAL_ERROR/ENGINE_CLOSING;
 * an unknown handle is NOT_REGISTERED with lifecycle zero. Capacity must cover
 * the exact response.
 */
irfq_infinite_status_v1 irfq_infinite_engine_shutdown_v1(
    irfq_infinite_handle_v1 engine,
    void *output,
    uint64_t output_capacity) IRFQ_INFINITE_NOEXCEPT;

/**
 * Parses exactly one complete Logon frame and delegates external connection
 * ownership to the bootstrap callback. The frame pointer is borrowed for this
 * call, its length is bounded by `IRFQ_INFINITE_MAX_FRAME_BYTES_V1`, observation
 * time must be positive, and `transport_nonce` must be nonzero. `output` is an
 * `irfq_infinite_bootstrap_response_v1`: OK/ACCEPTED publishes a new adapter
 * connection handle, NOT_READY/REJECTED publishes none, and
 * STREAM_FENCED/FENCED publishes none. Credentials reach only the synchronous
 * bootstrap authority and are scrubbed from adapter parse state before session
 * ownership. Capacity must cover the exact response.
 */
irfq_infinite_status_v1 irfq_infinite_connection_bootstrap_v1(
    irfq_infinite_handle_v1 engine,
    const irfq_infinite_bootstrap_request_v1 *request,
    void *output,
    uint64_t output_capacity) IRFQ_INFINITE_NOEXCEPT;

/**
 * Feeds a bounded byte slice to one connection's complete-frame parser and
 * synchronously registers every completed frame. Partial input may produce an
 * OK response with zero results. The caller must always provide
 * `IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1` bytes. `output` begins with
 * `irfq_infinite_dispatch_response_v1`; exactly `result_count` registration
 * records follow. OK leaves the connection OPEN, while terminal parse,
 * registration, or fence failure returns a fenced/not-registered/internal
 * status and no optimistic ownership claim. Input and output may not overlap.
 */
irfq_infinite_status_v1 irfq_infinite_connection_dispatch_v1(
    irfq_infinite_handle_v1 connection,
    const irfq_infinite_dispatch_request_v1 *request,
    void *output,
    uint64_t output_capacity) IRFQ_INFINITE_NOEXCEPT;

/**
 * Waits until the registered token is authoritative at head. `output` is an
 * `irfq_infinite_operation_response_v1`; AT_HEAD pairs with CONNECTION_OPEN,
 * while STREAM_FENCED or INTERNAL_ERROR pairs with CONNECTION_CLOSING. The
 * request and output may not overlap, and capacity must cover the response.
 */
irfq_infinite_status_v1 irfq_infinite_connection_wait_head_v1(
    irfq_infinite_handle_v1 connection,
    const irfq_infinite_head_request_v1 *request,
    void *output,
    uint64_t output_capacity) IRFQ_INFINITE_NOEXCEPT;

/**
 * Classifies an at-head token and obtains external effect authorization.
 * `output` is an `irfq_infinite_classification_response_v1`. CLASSIFIED publishes
 * nonzero classification and authorization handles plus the exact session plan
 * and an AUTHORIZED outcome. Protocol failure or refusal fences the stream;
 * failed fence acknowledgement publishes INTERNAL_ERROR. The token remains
 * single-use and capacity must cover the exact response.
 */
irfq_infinite_status_v1 irfq_infinite_connection_classify_v1(
    irfq_infinite_handle_v1 connection,
    const irfq_infinite_head_request_v1 *request,
    void *output,
    uint64_t output_capacity) IRFQ_INFINITE_NOEXCEPT;

/**
 * Applies the exact classification/authorization pair once and consumes its
 * candidate bytes. `output` is an `irfq_infinite_operation_response_v1`:
 * APPLIED pairs with CONNECTION_OPEN; STREAM_FENCED or INTERNAL_ERROR pairs
 * with CONNECTION_CLOSING. Handles from another or earlier operation are
 * rejected fail-closed. Capacity must cover the exact response.
 */
irfq_infinite_status_v1 irfq_infinite_connection_apply_v1(
    irfq_infinite_handle_v1 connection,
    const irfq_infinite_apply_request_v1 *request,
    void *output,
    uint64_t output_capacity) IRFQ_INFINITE_NOEXCEPT;

/**
 * Fences, drains, releases, and invalidates one connection. `reason` is passed
 * unchanged to lifecycle callbacks. `output` is an
 * `irfq_infinite_operation_response_v1`: CLOSED pairs with CONNECTION_CLOSED
 * and is idempotent while the engine remains live; cleanup failure is
 * INTERNAL_ERROR/CONNECTION_CLOSING; an unknown handle is NOT_REGISTERED with
 * lifecycle zero. Capacity must cover the exact response.
 */
irfq_infinite_status_v1 irfq_infinite_connection_close_v1(
    irfq_infinite_handle_v1 connection,
    const irfq_infinite_close_request_v1 *request,
    void *output,
    uint64_t output_capacity) IRFQ_INFINITE_NOEXCEPT;

#ifdef __cplusplus
} // extern "C"
#endif

#undef IRFQ_INFINITE_NOEXCEPT
