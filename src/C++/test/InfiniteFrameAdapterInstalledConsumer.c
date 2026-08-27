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

static irfq_infinite_status_v1 publish_bootstrap(void *output, uint64_t capacity) {
  irfq_infinite_bootstrap_response_v1 response;
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
  response.header.status = IRFQ_INFINITE_STATUS_OK_V1;
  response.connection.object = UINT64_C(1);
  response.connection.generation = UINT64_C(1);
  response.outcome = IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1;
  memcpy(output, &response, sizeof(response));
  ((irfq_infinite_output_header_v1 *)output)->written_length = sizeof(response);
  return IRFQ_INFINITE_STATUS_OK_V1;
}

static irfq_infinite_status_v1 bootstrap(
    void *context,
    const irfq_infinite_bootstrap_request_v1 *request,
    void *output,
    uint64_t capacity) {
  (void)context;
  if (request == NULL || request->frame.data == NULL || request->frame.length == 0) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  return publish_bootstrap(output, capacity);
}

static irfq_infinite_status_v1 register_batch(
    void *context,
    const irfq_infinite_registration_callback_request_v1 *request,
    void *output,
    uint64_t capacity) {
  (void)context;
  (void)request;
  (void)output;
  (void)capacity;
  return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
}

static irfq_infinite_status_v1 wait_head(
    void *context,
    const irfq_infinite_head_callback_request_v1 *request,
    void *output,
    uint64_t capacity) {
  (void)context;
  (void)request;
  (void)output;
  (void)capacity;
  return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
}

static irfq_infinite_status_v1 authorize(
    void *context,
    const irfq_infinite_classification_callback_request_v1 *request,
    void *output,
    uint64_t capacity) {
  (void)context;
  (void)request;
  (void)output;
  (void)capacity;
  return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
}

static irfq_infinite_status_v1 fence(void *context, irfq_infinite_handle_v1 connection, uint32_t reason) {
  (void)context;
  (void)connection;
  (void)reason;
  return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
}

static irfq_infinite_status_v1 release(void *context, irfq_infinite_handle_v1 connection, uint32_t reason) {
  (void)context;
  (void)connection;
  (void)reason;
  return IRFQ_INFINITE_STATUS_CLOSED_V1;
}

int main(void) {
  static const char logon[]
      = "8=FIX.4.2\0019=69\00135=A\00134=1\00149=INSTALLED\00156=VENUE\00152=20260827-00:00:00.000\001"
        "98=0\001108=30\00110=017\001";
  irfq_infinite_callback_table_v1 callbacks;
  irfq_infinite_engine_init_request_v1 initialize;
  irfq_infinite_engine_response_v1 initialized;
  irfq_infinite_bootstrap_request_v1 bootstrap_request;
  irfq_infinite_bootstrap_response_v1 bootstrapped;
  irfq_infinite_close_request_v1 close_request;
  irfq_infinite_operation_response_v1 operation;

  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.structure_size = (uint32_t)sizeof(callbacks);
  callbacks.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  callbacks.bootstrap = bootstrap;
  callbacks.register_batch = register_batch;
  callbacks.wait_head = wait_head;
  callbacks.authorize = authorize;
  callbacks.fence = fence;
  callbacks.release = release;

  memset(&initialize, 0, sizeof(initialize));
  initialize.structure_size = (uint32_t)sizeof(initialize);
  initialize.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  initialize.required_capabilities = IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1;
  initialize.callbacks = &callbacks;
  memset(&initialized, 0, sizeof(initialized));
  initialized.header.structure_size = (uint32_t)sizeof(initialized);
  initialized.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  if (irfq_infinite_engine_initialize_v1(&initialize, &initialized, sizeof(initialized))
      != IRFQ_INFINITE_STATUS_OK_V1) {
    return 1;
  }

  memset(&bootstrap_request, 0, sizeof(bootstrap_request));
  bootstrap_request.structure_size = (uint32_t)sizeof(bootstrap_request);
  bootstrap_request.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  bootstrap_request.frame.data = (const uint8_t *)logon;
  bootstrap_request.frame.length = sizeof(logon) - 1;
  bootstrap_request.observed_tai_ns = INT64_C(1);
  bootstrap_request.transport_nonce.object = UINT64_C(1);
  bootstrap_request.transport_nonce.generation = UINT64_C(2);
  memset(&bootstrapped, 0, sizeof(bootstrapped));
  bootstrapped.header.structure_size = (uint32_t)sizeof(bootstrapped);
  bootstrapped.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  if (irfq_infinite_connection_bootstrap_v1(initialized.engine, &bootstrap_request, &bootstrapped, sizeof(bootstrapped))
          != IRFQ_INFINITE_STATUS_OK_V1
      || bootstrapped.outcome != IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1) {
    return 2;
  }

  memset(&close_request, 0, sizeof(close_request));
  close_request.structure_size = (uint32_t)sizeof(close_request);
  close_request.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  memset(&operation, 0, sizeof(operation));
  operation.header.structure_size = (uint32_t)sizeof(operation);
  operation.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  if (irfq_infinite_connection_close_v1(bootstrapped.connection, &close_request, &operation, sizeof(operation))
      != IRFQ_INFINITE_STATUS_CLOSED_V1) {
    return 3;
  }

  memset(&operation, 0, sizeof(operation));
  operation.header.structure_size = (uint32_t)sizeof(operation);
  operation.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  return irfq_infinite_engine_shutdown_v1(initialized.engine, &operation, sizeof(operation))
                 == IRFQ_INFINITE_STATUS_SHUTDOWN_V1
             ? 0
             : 4;
}
