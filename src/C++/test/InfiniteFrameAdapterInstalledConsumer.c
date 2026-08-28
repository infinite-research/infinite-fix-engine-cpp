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

#include <stdint.h>
#include <string.h>

typedef struct consumer_context {
  int reject_registration;
  int registration_routed;
  int wait_routed;
  int authorization_routed;
  int handoff_routed;
  irfq_infinite_handle_v1 external_token;
  irfq_infinite_handle_v1 external_authorization;
  irfq_infinite_handle_v1 callback_classification;
  uint32_t fence_count;
  uint32_t release_count;
} consumer_context;

static int same_handle(irfq_infinite_handle_v1 lhs, irfq_infinite_handle_v1 rhs) {
  return lhs.object == rhs.object && lhs.generation == rhs.generation;
}

static void initialize_output(void *output, uint32_t size) {
  irfq_infinite_output_header_v1 *header = (irfq_infinite_output_header_v1 *)output;
  memset(output, 0, size);
  header->structure_size = size;
  header->abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
}

static irfq_infinite_status_v1 publish_operation(
    void *output,
    uint64_t capacity,
    irfq_infinite_status_v1 status,
    uint32_t lifecycle) {
  irfq_infinite_operation_response_v1 response;
  irfq_infinite_output_header_v1 *header = (irfq_infinite_output_header_v1 *)output;
  if (output == NULL || capacity < sizeof(response)) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  if (header->structure_size != sizeof(response) || header->abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1) {
    return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
  }
  memset(&response, 0, sizeof(response));
  response.header.structure_size = (uint32_t)sizeof(response);
  response.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  response.header.status = status;
  response.header.written_length = sizeof(response);
  response.lifecycle = lifecycle;
  memcpy(output, &response, sizeof(response));
  return status;
}

static irfq_infinite_status_v1 bootstrap(
    void *opaque,
    const irfq_infinite_bootstrap_request_v1 *request,
    void *output,
    uint64_t capacity) {
  irfq_infinite_bootstrap_response_v1 response;
  irfq_infinite_output_header_v1 *header = (irfq_infinite_output_header_v1 *)output;
  (void)opaque;
  if (request == NULL || request->frame.data == NULL || request->frame.length == 0 || output == NULL
      || capacity < sizeof(response)) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  if (header->structure_size != sizeof(response) || header->abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1) {
    return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
  }
  memset(&response, 0, sizeof(response));
  response.header.structure_size = (uint32_t)sizeof(response);
  response.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  response.header.status = IRFQ_INFINITE_STATUS_OK_V1;
  response.header.written_length = sizeof(response);
  response.connection.object = UINT64_C(1);
  response.connection.generation = UINT64_C(1);
  response.outcome = IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1;
  memcpy(output, &response, sizeof(response));
  return IRFQ_INFINITE_STATUS_OK_V1;
}

static irfq_infinite_status_v1 register_batch(
    void *opaque,
    const irfq_infinite_registration_callback_request_v1 *request,
    void *output,
    uint64_t capacity) {
  consumer_context *context = (consumer_context *)opaque;
  irfq_infinite_dispatch_response_v1 *response = (irfq_infinite_dispatch_response_v1 *)output;
  irfq_infinite_registration_result_v1 *result;
  const irfq_infinite_handle_v1 external_connection = {UINT64_C(1), UINT64_C(1)};
  uint64_t length;
  if (request == NULL || request->frame_count != 1 || output == NULL) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  length = context->reject_registration ? sizeof(*response) : sizeof(*response) + sizeof(*result);
  if (capacity < length || response->header.structure_size != sizeof(*response)
      || response->header.abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1) {
    return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
  }
  memset(output, 0, (size_t)length);
  response->header.structure_size = (uint32_t)sizeof(*response);
  response->header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  response->header.written_length = length;
  response->header.status
      = context->reject_registration ? IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1 : IRFQ_INFINITE_STATUS_OK_V1;
  if (context->reject_registration) {
    return IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1;
  }
  context->registration_routed = same_handle(request->connection, external_connection);
  response->result_count = 1;
  result = (irfq_infinite_registration_result_v1 *)((uint8_t *)output + sizeof(*response));
  result->ordinal = UINT64_C(1);
  result->token.object = UINT64_C(100);
  result->token.generation = UINT64_C(3);
  result->observed_tai_ns = request->frames[0].observed_tai_ns;
  context->external_token = result->token;
  return IRFQ_INFINITE_STATUS_OK_V1;
}

static irfq_infinite_status_v1 wait_head(
    void *opaque,
    const irfq_infinite_head_callback_request_v1 *request,
    void *output,
    uint64_t capacity) {
  consumer_context *context = (consumer_context *)opaque;
  const irfq_infinite_handle_v1 external_connection = {UINT64_C(1), UINT64_C(1)};
  context->wait_routed = request != NULL && same_handle(request->connection, external_connection)
                         && same_handle(request->token, context->external_token);
  return publish_operation(output, capacity, IRFQ_INFINITE_STATUS_AT_HEAD_V1, IRFQ_INFINITE_CONNECTION_OPEN_V1);
}

static irfq_infinite_status_v1 authorize(
    void *opaque,
    const irfq_infinite_classification_callback_request_v1 *request,
    void *output,
    uint64_t capacity) {
  consumer_context *context = (consumer_context *)opaque;
  irfq_infinite_classification_callback_response_v1 response;
  irfq_infinite_output_header_v1 *header = (irfq_infinite_output_header_v1 *)output;
  irfq_infinite_status_v1 outcome;
  const irfq_infinite_handle_v1 external_connection = {UINT64_C(1), UINT64_C(1)};
  if (request == NULL || output == NULL || capacity < sizeof(response)) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  if (header->structure_size != sizeof(response) || header->abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1) {
    return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
  }
  outcome = request->sequence_disposition == IRFQ_INFINITE_SEQUENCE_AT_HEAD_V1
                ? IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1
                : IRFQ_INFINITE_STATUS_AUTHORIZED_NO_CONSUME_V1;
  context->external_authorization.object = UINT64_C(200);
  context->external_authorization.generation = UINT64_C(4);
  context->callback_classification = request->classification;
  context->authorization_routed = same_handle(request->connection, external_connection)
                                  && same_handle(request->token, context->external_token)
                                  && request->classification.object != 0 && request->classification.generation != 0;
  memset(&response, 0, sizeof(response));
  response.header.structure_size = (uint32_t)sizeof(response);
  response.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  response.header.status = outcome;
  response.header.written_length = sizeof(response);
  response.authorization = context->external_authorization;
  response.outcome = outcome;
  memcpy(output, &response, sizeof(response));
  return outcome;
}

static irfq_infinite_status_v1 fence(void *opaque, irfq_infinite_handle_v1 connection, uint32_t reason) {
  consumer_context *context = (consumer_context *)opaque;
  const irfq_infinite_handle_v1 external_connection = {UINT64_C(1), UINT64_C(1)};
  (void)reason;
  if (!same_handle(connection, external_connection)) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
  ++context->fence_count;
  return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
}

static irfq_infinite_status_v1 release(void *opaque, irfq_infinite_handle_v1 connection, uint32_t reason) {
  consumer_context *context = (consumer_context *)opaque;
  const irfq_infinite_handle_v1 external_connection = {UINT64_C(1), UINT64_C(1)};
  (void)reason;
  if (!same_handle(connection, external_connection)) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
  ++context->release_count;
  return IRFQ_INFINITE_STATUS_CLOSED_V1;
}

static irfq_infinite_status_v1 handoff(
    void *opaque,
    irfq_infinite_handle_v1 connection,
    irfq_infinite_handle_v1 token) {
  consumer_context *context = (consumer_context *)opaque;
  const irfq_infinite_handle_v1 external_connection = {UINT64_C(1), UINT64_C(1)};
  context->handoff_routed = same_handle(connection, external_connection) && same_handle(token, context->external_token);
  return context->handoff_routed ? IRFQ_INFINITE_STATUS_OK_V1 : IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
}

static irfq_infinite_status_v1 bootstrap_connection(
    irfq_infinite_handle_v1 engine,
    const char *logon,
    uint64_t nonce,
    irfq_infinite_bootstrap_response_v1 *response) {
  irfq_infinite_bootstrap_request_v1 request;
  memset(&request, 0, sizeof(request));
  request.structure_size = (uint32_t)sizeof(request);
  request.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  request.frame.data = (const uint8_t *)logon;
  request.frame.length = strlen(logon);
  request.observed_tai_ns = INT64_C(1);
  request.transport_nonce.object = nonce;
  request.transport_nonce.generation = nonce + UINT64_C(1);
  initialize_output(response, (uint32_t)sizeof(*response));
  return irfq_infinite_connection_bootstrap_v1(engine, &request, response, sizeof(*response));
}

static irfq_infinite_status_v1 close_connection(irfq_infinite_handle_v1 connection) {
  irfq_infinite_close_request_v1 request;
  irfq_infinite_operation_response_v1 response;
  memset(&request, 0, sizeof(request));
  request.structure_size = (uint32_t)sizeof(request);
  request.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  initialize_output(&response, (uint32_t)sizeof(response));
  return irfq_infinite_connection_close_v1(connection, &request, &response, sizeof(response));
}

int main(void) {
  static const char logon[]
      = "8=FIX.4.2\0019=69\00135=A\00134=1\00149=INSTALLED\00156=VENUE\00152=20260827-00:00:00.000\001"
        "98=0\001108=30\00110=017\001";
  static const char heartbeat[]
      = "8=FIX.4.2\0019=57\00135=0\00134=2\00149=INSTALLED\00156=VENUE\00152=20260827-00:00:01.000\001"
        "10=230\001";
  consumer_context context;
  irfq_infinite_abi_info_v1 info;
  irfq_infinite_callback_table_v1 callbacks;
  irfq_infinite_engine_init_request_v1 initialize;
  irfq_infinite_engine_response_v1 initialized;
  irfq_infinite_bootstrap_response_v1 bootstrapped;
  irfq_infinite_dispatch_request_v1 dispatch_request;
  union {
    uint64_t alignment;
    uint8_t bytes[IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1];
  } dispatch_output;
  irfq_infinite_dispatch_response_v1 *dispatched;
  irfq_infinite_registration_result_v1 *registration;
  irfq_infinite_head_request_v1 head_request;
  irfq_infinite_operation_response_v1 operation;
  irfq_infinite_classification_response_v1 classified;
  irfq_infinite_apply_request_v1 apply_request;

  memset(&info, 0, sizeof(info));
  info.structure_size = (uint32_t)sizeof(info);
  info.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  if (irfq_infinite_frame_adapter_query_v1(&info) != IRFQ_INFINITE_STATUS_OK_V1
      || info.capabilities != IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1
      || info.max_batch_frames != IRFQ_INFINITE_MAX_BATCH_FRAMES_V1
      || info.max_frame_bytes != IRFQ_INFINITE_MAX_FRAME_BYTES_V1) {
    return 1;
  }

  memset(&context, 0, sizeof(context));
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.structure_size = (uint32_t)sizeof(callbacks);
  callbacks.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  callbacks.context = &context;
  callbacks.bootstrap = bootstrap;
  callbacks.register_batch = register_batch;
  callbacks.wait_head = wait_head;
  callbacks.authorize = authorize;
  callbacks.fence = fence;
  callbacks.release = release;
  callbacks.handoff = handoff;
  memset(&initialize, 0, sizeof(initialize));
  initialize.structure_size = (uint32_t)sizeof(initialize);
  initialize.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  initialize.required_capabilities = IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1;
  initialize.callbacks = &callbacks;
  initialize_output(&initialized, (uint32_t)sizeof(initialized));
  if (irfq_infinite_engine_initialize_v1(&initialize, &initialized, sizeof(initialized))
      != IRFQ_INFINITE_STATUS_OK_V1) {
    return 2;
  }

  memset(&dispatch_request, 0, sizeof(dispatch_request));
  dispatch_request.structure_size = (uint32_t)sizeof(dispatch_request);
  dispatch_request.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  dispatch_request.input.data = (const uint8_t *)heartbeat;
  dispatch_request.input.length = sizeof(heartbeat) - 1;

  context.reject_registration = 1;
  if (bootstrap_connection(initialized.engine, logon, UINT64_C(1), &bootstrapped) != IRFQ_INFINITE_STATUS_OK_V1) {
    return 3;
  }
  initialize_output(dispatch_output.bytes, sizeof(irfq_infinite_dispatch_response_v1));
  if (irfq_infinite_connection_dispatch_v1(
          bootstrapped.connection,
          &dispatch_request,
          dispatch_output.bytes,
          sizeof(dispatch_output.bytes))
          != IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1
      || ((irfq_infinite_output_header_v1 *)dispatch_output.bytes)->written_length
             != sizeof(irfq_infinite_dispatch_response_v1)) {
    return 4;
  }
  if (close_connection(bootstrapped.connection) != IRFQ_INFINITE_STATUS_CLOSED_V1) {
    return 5;
  }

  context.reject_registration = 0;
  if (bootstrap_connection(initialized.engine, logon, UINT64_C(3), &bootstrapped) != IRFQ_INFINITE_STATUS_OK_V1) {
    return 6;
  }
  initialize_output(dispatch_output.bytes, sizeof(irfq_infinite_dispatch_response_v1));
  if (irfq_infinite_connection_dispatch_v1(
          bootstrapped.connection,
          &dispatch_request,
          dispatch_output.bytes,
          sizeof(dispatch_output.bytes))
      != IRFQ_INFINITE_STATUS_OK_V1) {
    return 7;
  }
  dispatched = (irfq_infinite_dispatch_response_v1 *)dispatch_output.bytes;
  registration = (irfq_infinite_registration_result_v1 *)(dispatch_output.bytes + sizeof(*dispatched));
  if (dispatched->result_count != 1 || !context.registration_routed
      || same_handle(registration->token, context.external_token)) {
    return 8;
  }

  memset(&head_request, 0, sizeof(head_request));
  head_request.structure_size = (uint32_t)sizeof(head_request);
  head_request.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  head_request.token = registration->token;
  initialize_output(&operation, (uint32_t)sizeof(operation));
  if (irfq_infinite_connection_wait_head_v1(bootstrapped.connection, &head_request, &operation, sizeof(operation))
          != IRFQ_INFINITE_STATUS_AT_HEAD_V1
      || !context.wait_routed) {
    return 9;
  }

  initialize_output(&classified, (uint32_t)sizeof(classified));
  if (irfq_infinite_connection_classify_v1(bootstrapped.connection, &head_request, &classified, sizeof(classified))
          != IRFQ_INFINITE_STATUS_CLASSIFIED_V1
      || classified.outcome != IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1 || !context.authorization_routed
      || !same_handle(classified.classification, context.callback_classification)
      || same_handle(classified.authorization, context.external_authorization)) {
    return 10;
  }

  memset(&apply_request, 0, sizeof(apply_request));
  apply_request.structure_size = (uint32_t)sizeof(apply_request);
  apply_request.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  apply_request.classification = classified.classification;
  apply_request.authorization = classified.authorization;
  initialize_output(&operation, (uint32_t)sizeof(operation));
  if (irfq_infinite_connection_apply_v1(bootstrapped.connection, &apply_request, &operation, sizeof(operation))
          != IRFQ_INFINITE_STATUS_APPLIED_V1
      || !context.handoff_routed) {
    return 11;
  }
  if (close_connection(bootstrapped.connection) != IRFQ_INFINITE_STATUS_CLOSED_V1 || context.fence_count != 2
      || context.release_count != 2) {
    return 12;
  }
  initialize_output(&operation, (uint32_t)sizeof(operation));
  return irfq_infinite_engine_shutdown_v1(initialized.engine, &operation, sizeof(operation))
                 == IRFQ_INFINITE_STATUS_SHUTDOWN_V1
             ? 0
             : 13;
}
