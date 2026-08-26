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

#include "InfiniteFrameAdapter.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <time.h>
#include <utility>
#include <vector>

namespace {
struct FixtureRows {
  std::map<std::string, std::string> values;

  std::uint64_t number(const std::string &kind, const std::string &name) const {
    return std::stoull(values.at(kind + "." + name));
  }

  const std::string &text(const std::string &kind, const std::string &name) const {
    return values.at(kind + "." + name);
  }
};

FixtureRows readFixture() {
  std::ifstream input(INFINITE_FRAME_ADAPTER_ABI_FIXTURE_PATH, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Unable to open Infinite frame adapter ABI fixture");
  }
  std::string line;
  std::getline(input, line);
  if (line != "kind\tname\tvalue") {
    throw std::runtime_error("Unexpected Infinite frame adapter ABI fixture header");
  }
  FixtureRows rows;
  while (std::getline(input, line)) {
    const auto first = line.find('\t');
    const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
    if (first == std::string::npos || second == std::string::npos || line.find('\t', second + 1) != std::string::npos) {
      throw std::runtime_error("Malformed Infinite frame adapter ABI fixture row");
    }
    const auto key = line.substr(0, first) + "." + line.substr(first + 1, second - first - 1);
    if (!rows.values.emplace(key, line.substr(second + 1)).second) {
      throw std::runtime_error("Duplicate Infinite frame adapter ABI fixture row");
    }
  }
  return rows;
}

template <typename T> std::string bytesOf(const T &value) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    result << std::setw(2) << static_cast<unsigned>(bytes[index]);
  }
  return result.str();
}

template <typename T> T output() {
  T value{};
  value.header.structure_size = sizeof(T);
  value.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  value.header.written_length = 99;
  return value;
}

struct alignas(8) DispatchOutput {
  std::array<std::uint8_t, IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1> bytes{};

  std::uint8_t *data() { return bytes.data(); }
  const std::uint8_t *data() const { return bytes.data(); }
  std::size_t size() const { return bytes.size(); }
};

DispatchOutput dispatchOutput() {
  DispatchOutput bytes{};
  auto *response = reinterpret_cast<irfq_infinite_dispatch_response_v1 *>(bytes.data());
  response->header.structure_size = sizeof(*response);
  response->header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  response->header.written_length = 99;
  return bytes;
}

std::string finishFix(std::string body) {
  std::string message = "8=FIX.4.2\0019=" + std::to_string(body.size()) + "\001" + std::move(body) + "10=000\001";
  const auto checksum = message.rfind("10=");
  unsigned total = 0;
  for (std::size_t index = 0; index < checksum; ++index) {
    total += static_cast<unsigned char>(message[index]);
  }
  auto encoded = std::to_string(total % 256);
  encoded.insert(encoded.begin(), 3 - encoded.size(), '0');
  message.replace(checksum + 3, 3, encoded);
  return message;
}

struct CallbackContext {
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<std::string> registeredFrames;
  std::vector<std::int64_t> observations;
  std::uint64_t nextOrdinal{1};
  std::uint64_t nextToken{100};
  std::uint64_t nextAuthorization{200};
  std::uint64_t nextConnection{7};
  irfq_infinite_handle_v1 adapterConnection{};
  irfq_infinite_handle_v1 crossConnection{};
  irfq_infinite_status_v1 sameHandleReentryStatus{IRFQ_INFINITE_STATUS_OK_V1};
  irfq_infinite_status_v1 crossConnectionStatus{IRFQ_INFINITE_STATUS_OK_V1};
  bool fenced{false};
  bool released{false};
  bool blockRegistration{false};
  bool registrationEntered{false};
  bool reenterSameHandle{false};
  bool callCrossConnection{false};
  bool omitRegistrationResult{false};
  bool throwAfterBootstrapAccept{false};
};

template <typename T> irfq_infinite_status_v1 publishFixed(void *outputBuffer, std::uint64_t capacity, T response) {
  if (outputBuffer == nullptr || capacity < sizeof(T)) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  auto *header = static_cast<irfq_infinite_output_header_v1 *>(outputBuffer);
  if (header->structure_size != sizeof(T) || header->abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1) {
    return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
  }
  response.header.structure_size = sizeof(T);
  response.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  response.header.written_length = 0;
  std::memcpy(outputBuffer, &response, sizeof(T));
  static_cast<irfq_infinite_output_header_v1 *>(outputBuffer)->written_length = sizeof(T);
  return response.header.status;
}

irfq_infinite_status_v1 bootstrapCallback(
    void *opaque,
    const irfq_infinite_bootstrap_request_v1 *,
    void *outputBuffer,
    std::uint64_t capacity) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  auto response = output<irfq_infinite_bootstrap_response_v1>();
  response.header.status = IRFQ_INFINITE_STATUS_OK_V1;
  response.connection = {context.nextConnection++, UINT64_C(9)};
  response.outcome = IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1;
  const auto status = publishFixed(outputBuffer, capacity, response);
  if (context.throwAfterBootstrapAccept) {
    throw std::runtime_error("bootstrap callback failure after acceptance");
  }
  return status;
}

irfq_infinite_status_v1 registerCallback(
    void *opaque,
    const irfq_infinite_registration_callback_request_v1 *request,
    void *outputBuffer,
    std::uint64_t capacity) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  const auto nestedDispatch = [](irfq_infinite_handle_v1 connection) {
    const irfq_infinite_dispatch_request_v1 nestedRequest{
        sizeof(nestedRequest),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {nullptr, 0},
        {}};
    auto nestedResponse = dispatchOutput();
    return irfq_infinite_connection_dispatch_v1(
        connection,
        &nestedRequest,
        nestedResponse.data(),
        nestedResponse.size());
  };
  if (context.reenterSameHandle) {
    context.sameHandleReentryStatus = nestedDispatch(context.adapterConnection);
  }
  if (context.callCrossConnection) {
    context.crossConnectionStatus = nestedDispatch(context.crossConnection);
  }
  const auto responseBytes = sizeof(irfq_infinite_dispatch_response_v1)
                             + request->frame_count * sizeof(irfq_infinite_registration_result_v1);
  if (capacity < responseBytes) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  std::vector<std::uint64_t> staged(responseBytes / sizeof(std::uint64_t), 0);
  auto *stagedBytes = reinterpret_cast<std::uint8_t *>(staged.data());
  auto *response = reinterpret_cast<irfq_infinite_dispatch_response_v1 *>(stagedBytes);
  response->header
      = {sizeof(irfq_infinite_dispatch_response_v1),
         IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
         IRFQ_INFINITE_STATUS_OK_V1,
         0,
         0};
  response->result_count = context.omitRegistrationResult ? 0 : request->frame_count;
  auto *results = reinterpret_cast<irfq_infinite_registration_result_v1 *>(
      stagedBytes + sizeof(irfq_infinite_dispatch_response_v1));
  {
    std::unique_lock<std::mutex> lock(context.mutex);
    context.registrationEntered = true;
    context.condition.notify_all();
    context.condition.wait(lock, [&context]() { return !context.blockRegistration || context.fenced; });
    if (context.fenced) {
      response->header.status = IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
      response->result_count = 0;
    } else {
      for (std::uint32_t index = 0; index < request->frame_count; ++index) {
        const auto &frame = request->frames[index];
        context.registeredFrames.emplace_back(reinterpret_cast<const char *>(frame.data), frame.length);
        context.observations.push_back(frame.observed_tai_ns);
        results[index] = {context.nextOrdinal++, {context.nextToken++, UINT64_C(1)}, frame.observed_tai_ns};
      }
    }
  }
  auto *callerHeader = static_cast<irfq_infinite_output_header_v1 *>(outputBuffer);
  if (callerHeader->structure_size != sizeof(irfq_infinite_dispatch_response_v1)
      || callerHeader->abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1) {
    return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
  }
  std::memcpy(outputBuffer, stagedBytes, responseBytes);
  static_cast<irfq_infinite_output_header_v1 *>(outputBuffer)->written_length = responseBytes;
  return response->header.status;
}

irfq_infinite_status_v1 waitCallback(
    void *opaque,
    const irfq_infinite_head_callback_request_v1 *,
    void *outputBuffer,
    std::uint64_t capacity) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  auto response = output<irfq_infinite_operation_response_v1>();
  response.header.status = context.fenced ? IRFQ_INFINITE_STATUS_STREAM_FENCED_V1 : IRFQ_INFINITE_STATUS_AT_HEAD_V1;
  response.lifecycle = context.fenced ? IRFQ_INFINITE_CONNECTION_CLOSING_V1 : IRFQ_INFINITE_CONNECTION_OPEN_V1;
  return publishFixed(outputBuffer, capacity, response);
}

irfq_infinite_status_v1 authorizeCallback(
    void *opaque,
    const irfq_infinite_classification_callback_request_v1 *request,
    void *outputBuffer,
    std::uint64_t capacity) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  auto response = output<irfq_infinite_classification_callback_response_v1>();
  response.header.status = context.fenced ? IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
                           : request->sequence_disposition == IRFQ_INFINITE_SEQUENCE_AT_HEAD_V1
                               ? IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1
                               : IRFQ_INFINITE_STATUS_AUTHORIZED_NO_CONSUME_V1;
  response.authorization = {context.nextAuthorization++, request->classification.generation};
  response.outcome = response.header.status;
  return publishFixed(outputBuffer, capacity, response);
}

irfq_infinite_status_v1 fenceCallback(void *opaque, irfq_infinite_handle_v1, std::uint32_t) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  {
    std::lock_guard<std::mutex> lock(context.mutex);
    context.fenced = true;
  }
  context.condition.notify_all();
  return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
}

irfq_infinite_status_v1 releaseCallback(void *opaque, irfq_infinite_handle_v1, std::uint32_t) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  context.released = true;
  return IRFQ_INFINITE_STATUS_CLOSED_V1;
}

irfq_infinite_callback_table_v1 callbacks(CallbackContext &context) {
  return {
      sizeof(irfq_infinite_callback_table_v1),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      &context,
      bootstrapCallback,
      registerCallback,
      waitCallback,
      authorizeCallback,
      fenceCallback,
      releaseCallback};
}
} // namespace

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][fixture]") {
  const auto fixture = readFixture();
  CHECK(fixture.text("contract", "engine_lifecycle") == "INITIALIZED>CLOSING>SHUTDOWN");
  CHECK(fixture.text("contract", "connection_lifecycle") == "OPEN>CLOSING>CLOSED");
  CHECK(fixture.text("contract", "byte_order") == "little_endian");
  CHECK(fixture.text("contract", "connection_lane") == "serialized_dispatch_classify_apply");
  CHECK(fixture.text("contract", "cross_connection_progress") == "concurrent");
  CHECK(fixture.text("contract", "callback_reentry") == "rejected_same_handle");
  CHECK(fixture.text("contract", "callback_argument_lifetime") == "synchronous_only");
  CHECK(fixture.text("contract", "callback_table_lifetime") == "copied_context_valid_until_quiescent_shutdown");
  CHECK(fixture.text("contract", "acquisition") == "open_only_increments_in_flight");
  CHECK(fixture.text("contract", "close_shutdown") == "stop_acquisition>fence_wake>drain>invalidate>release");
  CHECK(fixture.text("contract", "release") == "idempotent");
  CHECK(fixture.text("contract", "output_publication") == "zero_written>validate>stage>copy>publish_written");
  CHECK(fixture.number("constant", "abi_version") == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1);
  CHECK(fixture.number("constant", "required_capabilities") == IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1);
  CHECK(fixture.number("constant", "max_connections") == IRFQ_INFINITE_MAX_CONNECTIONS_V1);
  CHECK(fixture.number("constant", "max_batch_frames") == IRFQ_INFINITE_MAX_BATCH_FRAMES_V1);
  CHECK(fixture.number("constant", "max_frame_bytes") == IRFQ_INFINITE_MAX_FRAME_BYTES_V1);
  CHECK(fixture.number("constant", "max_batch_bytes") == IRFQ_INFINITE_MAX_BATCH_BYTES_V1);
  CHECK(fixture.number("constant", "max_failure_bytes") == IRFQ_INFINITE_MAX_FAILURE_BYTES_V1);
  CHECK(fixture.number("constant", "dispatch_output_capacity") == IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1);

#define CHECK_VALUE(kind, name, value) CHECK(fixture.number(kind, name) == value)
  CHECK_VALUE("status", "OK", IRFQ_INFINITE_STATUS_OK_V1);
  CHECK_VALUE("status", "INVALID_ARGUMENT", IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
  CHECK_VALUE("status", "ABI_MISMATCH", IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1);
  CHECK_VALUE("status", "NOT_READY", IRFQ_INFINITE_STATUS_NOT_READY_V1);
  CHECK_VALUE("status", "NOT_REGISTERED", IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1);
  CHECK_VALUE("status", "STREAM_FENCED", IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK_VALUE("status", "AT_HEAD", IRFQ_INFINITE_STATUS_AT_HEAD_V1);
  CHECK_VALUE("status", "CLASSIFIED", IRFQ_INFINITE_STATUS_CLASSIFIED_V1);
  CHECK_VALUE("status", "AUTHORIZED_CONSUME", IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1);
  CHECK_VALUE("status", "AUTHORIZED_NO_CONSUME", IRFQ_INFINITE_STATUS_AUTHORIZED_NO_CONSUME_V1);
  CHECK_VALUE("status", "APPLIED", IRFQ_INFINITE_STATUS_APPLIED_V1);
  CHECK_VALUE("status", "CLOSED", IRFQ_INFINITE_STATUS_CLOSED_V1);
  CHECK_VALUE("status", "SHUTDOWN", IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
  CHECK_VALUE("status", "INTERNAL_ERROR", IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
  CHECK_VALUE("bootstrap", "ACCEPTED", IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1);
  CHECK_VALUE("bootstrap", "REJECTED", IRFQ_INFINITE_BOOTSTRAP_REJECTED_V1);
  CHECK_VALUE("bootstrap", "FENCED", IRFQ_INFINITE_BOOTSTRAP_FENCED_V1);
  CHECK_VALUE("action", "PROTOCOL_CONTROL", IRFQ_INFINITE_ACTION_PROTOCOL_CONTROL_V1);
  CHECK_VALUE("action", "SEQUENCE_RESET", IRFQ_INFINITE_ACTION_SEQUENCE_RESET_V1);
  CHECK_VALUE("action", "LOGOUT", IRFQ_INFINITE_ACTION_LOGOUT_V1);
  CHECK_VALUE("action", "RESEND_OR_QUEUED_RELEASE", IRFQ_INFINITE_ACTION_RESEND_OR_QUEUED_RELEASE_V1);
  CHECK_VALUE("action", "PROTOCOL_DISPOSITION", IRFQ_INFINITE_ACTION_PROTOCOL_DISPOSITION_V1);
  CHECK_VALUE("action", "APPLICATION", IRFQ_INFINITE_ACTION_APPLICATION_V1);
  CHECK_VALUE("action", "FAILURE", IRFQ_INFINITE_ACTION_FAILURE_V1);
  CHECK_VALUE("sequence", "AT_HEAD", IRFQ_INFINITE_SEQUENCE_AT_HEAD_V1);
  CHECK_VALUE("sequence", "TOO_HIGH", IRFQ_INFINITE_SEQUENCE_TOO_HIGH_V1);
  CHECK_VALUE("sequence", "TOO_LOW", IRFQ_INFINITE_SEQUENCE_TOO_LOW_V1);
  CHECK_VALUE("sequence", "UNAVAILABLE", IRFQ_INFINITE_SEQUENCE_UNAVAILABLE_V1);
  CHECK_VALUE("fault", "NONE", IRFQ_INFINITE_DISPATCH_FAULT_NONE_V1);
  CHECK_VALUE("fault", "FRAME_TOO_LARGE", IRFQ_INFINITE_DISPATCH_FAULT_FRAME_TOO_LARGE_V1);
  CHECK_VALUE("fault", "ACCUMULATOR_OVERFLOW", IRFQ_INFINITE_DISPATCH_FAULT_ACCUMULATOR_OVERFLOW_V1);
  CHECK_VALUE("fault", "MALFORMED_FRAME", IRFQ_INFINITE_DISPATCH_FAULT_MALFORMED_FRAME_V1);
  CHECK_VALUE("fault", "BATCH_LIMIT", IRFQ_INFINITE_DISPATCH_FAULT_BATCH_LIMIT_V1);
  CHECK_VALUE("fault", "INVALID_OBSERVATION", IRFQ_INFINITE_DISPATCH_FAULT_INVALID_OBSERVATION_V1);
#undef CHECK_VALUE

#define CHECK_LAYOUT(type)                                                                                             \
  CHECK(fixture.number("size", #type) == sizeof(type));                                                                \
  CHECK(fixture.number("align", #type) == alignof(type))
#define CHECK_OFFSET(type, field) CHECK(fixture.number("offset", #type "." #field) == offsetof(type, field))
  CHECK_LAYOUT(irfq_infinite_abi_info_v1);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, capabilities);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, max_connections);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, max_batch_frames);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, max_frame_bytes);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, max_batch_bytes);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_output_header_v1);
  CHECK_OFFSET(irfq_infinite_output_header_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_output_header_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_output_header_v1, status);
  CHECK_OFFSET(irfq_infinite_output_header_v1, reserved);
  CHECK_OFFSET(irfq_infinite_output_header_v1, written_length);
  CHECK_LAYOUT(irfq_infinite_handle_v1);
  CHECK_OFFSET(irfq_infinite_handle_v1, object);
  CHECK_OFFSET(irfq_infinite_handle_v1, generation);
  CHECK_LAYOUT(irfq_infinite_slice_v1);
  CHECK_OFFSET(irfq_infinite_slice_v1, data);
  CHECK_OFFSET(irfq_infinite_slice_v1, length);
  CHECK_LAYOUT(irfq_infinite_callback_table_v1);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, context);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, bootstrap);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, register_batch);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, wait_head);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, authorize);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, fence);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, release);
  CHECK_LAYOUT(irfq_infinite_engine_init_request_v1);
  CHECK_OFFSET(irfq_infinite_engine_init_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_engine_init_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_engine_init_request_v1, required_capabilities);
  CHECK_OFFSET(irfq_infinite_engine_init_request_v1, callbacks);
  CHECK_OFFSET(irfq_infinite_engine_init_request_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_engine_response_v1);
  CHECK_OFFSET(irfq_infinite_engine_response_v1, header);
  CHECK_OFFSET(irfq_infinite_engine_response_v1, engine);
  CHECK_OFFSET(irfq_infinite_engine_response_v1, capabilities);
  CHECK_OFFSET(irfq_infinite_engine_response_v1, lifecycle);
  CHECK_OFFSET(irfq_infinite_engine_response_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_bootstrap_request_v1);
  CHECK_OFFSET(irfq_infinite_bootstrap_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_bootstrap_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_bootstrap_request_v1, frame);
  CHECK_OFFSET(irfq_infinite_bootstrap_request_v1, observed_tai_ns);
  CHECK_OFFSET(irfq_infinite_bootstrap_request_v1, transport_nonce);
  CHECK_LAYOUT(irfq_infinite_bootstrap_response_v1);
  CHECK_OFFSET(irfq_infinite_bootstrap_response_v1, header);
  CHECK_OFFSET(irfq_infinite_bootstrap_response_v1, connection);
  CHECK_OFFSET(irfq_infinite_bootstrap_response_v1, outcome);
  CHECK_OFFSET(irfq_infinite_bootstrap_response_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_dispatch_request_v1);
  CHECK_OFFSET(irfq_infinite_dispatch_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_dispatch_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_dispatch_request_v1, input);
  CHECK_OFFSET(irfq_infinite_dispatch_request_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_frame_descriptor_v1);
  CHECK_OFFSET(irfq_infinite_frame_descriptor_v1, data);
  CHECK_OFFSET(irfq_infinite_frame_descriptor_v1, length);
  CHECK_OFFSET(irfq_infinite_frame_descriptor_v1, observed_tai_ns);
  CHECK_LAYOUT(irfq_infinite_registration_callback_request_v1);
  CHECK_OFFSET(irfq_infinite_registration_callback_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_registration_callback_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_registration_callback_request_v1, connection);
  CHECK_OFFSET(irfq_infinite_registration_callback_request_v1, frames);
  CHECK_OFFSET(irfq_infinite_registration_callback_request_v1, frame_count);
  CHECK_OFFSET(irfq_infinite_registration_callback_request_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_registration_result_v1);
  CHECK_OFFSET(irfq_infinite_registration_result_v1, ordinal);
  CHECK_OFFSET(irfq_infinite_registration_result_v1, token);
  CHECK_OFFSET(irfq_infinite_registration_result_v1, observed_tai_ns);
  CHECK_LAYOUT(irfq_infinite_dispatch_response_v1);
  CHECK_OFFSET(irfq_infinite_dispatch_response_v1, header);
  CHECK_OFFSET(irfq_infinite_dispatch_response_v1, result_count);
  CHECK_OFFSET(irfq_infinite_dispatch_response_v1, fault);
  CHECK_LAYOUT(irfq_infinite_head_request_v1);
  CHECK_OFFSET(irfq_infinite_head_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_head_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_head_request_v1, token);
  CHECK_OFFSET(irfq_infinite_head_request_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_head_callback_request_v1);
  CHECK_OFFSET(irfq_infinite_head_callback_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_head_callback_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_head_callback_request_v1, connection);
  CHECK_OFFSET(irfq_infinite_head_callback_request_v1, token);
  CHECK_OFFSET(irfq_infinite_head_callback_request_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_operation_response_v1);
  CHECK_OFFSET(irfq_infinite_operation_response_v1, header);
  CHECK_OFFSET(irfq_infinite_operation_response_v1, lifecycle);
  CHECK_OFFSET(irfq_infinite_operation_response_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_classification_callback_request_v1);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, connection);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, token);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, classification);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, session_revision);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, sender_sequence);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, target_sequence);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, action);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, sequence_disposition);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, operation_count);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, failure_length);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, failure);
  CHECK_OFFSET(irfq_infinite_classification_callback_request_v1, observed_tai_ns);
  CHECK_LAYOUT(irfq_infinite_classification_callback_response_v1);
  CHECK_OFFSET(irfq_infinite_classification_callback_response_v1, header);
  CHECK_OFFSET(irfq_infinite_classification_callback_response_v1, authorization);
  CHECK_OFFSET(irfq_infinite_classification_callback_response_v1, outcome);
  CHECK_OFFSET(irfq_infinite_classification_callback_response_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_classification_response_v1);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, header);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, classification);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, authorization);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, session_revision);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, sender_sequence);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, target_sequence);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, action);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, sequence_disposition);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, outcome);
  CHECK_OFFSET(irfq_infinite_classification_response_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_apply_request_v1);
  CHECK_OFFSET(irfq_infinite_apply_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_apply_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_apply_request_v1, classification);
  CHECK_OFFSET(irfq_infinite_apply_request_v1, authorization);
  CHECK_OFFSET(irfq_infinite_apply_request_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_close_request_v1);
  CHECK_OFFSET(irfq_infinite_close_request_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_close_request_v1, abi_version);
  CHECK_OFFSET(irfq_infinite_close_request_v1, reason);
  CHECK_OFFSET(irfq_infinite_close_request_v1, reserved);
#undef CHECK_OFFSET
#undef CHECK_LAYOUT

  irfq_infinite_abi_info_v1 info{
      sizeof(info),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
      IRFQ_INFINITE_MAX_CONNECTIONS_V1,
      IRFQ_INFINITE_MAX_BATCH_FRAMES_V1,
      IRFQ_INFINITE_MAX_FRAME_BYTES_V1,
      IRFQ_INFINITE_MAX_BATCH_BYTES_V1,
      {}};
  CHECK(bytesOf(info) == fixture.text("bytes", "irfq_infinite_abi_info_v1"));
  const irfq_infinite_output_header_v1
      header{32, IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1, IRFQ_INFINITE_STATUS_CLASSIFIED_V1, 0, 32};
  CHECK(bytesOf(header) == fixture.text("bytes", "irfq_infinite_output_header_v1"));
  const irfq_infinite_handle_v1 handle{UINT64_C(0x0102030405060708), UINT64_C(0x1112131415161718)};
  CHECK(bytesOf(handle) == fixture.text("bytes", "irfq_infinite_handle_v1"));
  const irfq_infinite_bootstrap_request_v1
      bootstrap{sizeof(bootstrap), IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1, {nullptr, 0}, 42, handle};
  CHECK(bytesOf(bootstrap) == fixture.text("bytes", "irfq_infinite_bootstrap_request_v1"));
  const irfq_infinite_registration_result_v1 registration{1, handle, 42};
  CHECK(bytesOf(registration) == fixture.text("bytes", "irfq_infinite_registration_result_v1"));
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][output]") {
  CallbackContext context;
  auto table = callbacks(context);
  const irfq_infinite_engine_init_request_v1 request{
      sizeof(request),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
      &table,
      {}};

  alignas(8) std::array<std::uint8_t, sizeof(irfq_infinite_engine_response_v1) + 1> shortOutput{};
  shortOutput.fill(0xa5);
  CHECK(
      irfq_infinite_engine_initialize_v1(&request, shortOutput.data(), sizeof(irfq_infinite_output_header_v1) - 1)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
  CHECK(std::all_of(shortOutput.begin(), shortOutput.end(), [](std::uint8_t value) { return value == 0xa5; }));

  alignas(8) std::array<std::uint8_t, sizeof(irfq_infinite_engine_response_v1) + 1> misaligned{};
  misaligned.fill(0xa5);
  CHECK(
      irfq_infinite_engine_initialize_v1(&request, misaligned.data() + 1, sizeof(irfq_infinite_engine_response_v1))
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
  CHECK(std::all_of(misaligned.begin(), misaligned.end(), [](std::uint8_t value) { return value == 0xa5; }));

  auto response = output<irfq_infinite_engine_response_v1>();
  auto wrongVersion = request;
  wrongVersion.abi_version += 1;
  CHECK(
      irfq_infinite_engine_initialize_v1(&wrongVersion, &response, sizeof(response))
      == IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1);
  CHECK(response.header.written_length == 0);
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(&response);
  CHECK(std::all_of(bytes + sizeof(irfq_infinite_output_header_v1), bytes + sizeof(response), [](std::uint8_t value) {
    return value == 0;
  }));
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][initialization]") {
  CallbackContext context;
  const auto complete = callbacks(context);
  for (std::uint32_t missing = 0; missing < 5; ++missing) {
    auto table = complete;
    switch (missing) {
    case 0:
      table.register_batch = nullptr;
      break;
    case 1:
      table.wait_head = nullptr;
      break;
    case 2:
      table.authorize = nullptr;
      break;
    case 3:
      table.fence = nullptr;
      break;
    case 4:
      table.release = nullptr;
      break;
    }
    const irfq_infinite_engine_init_request_v1 request{
        sizeof(request),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
        &table,
        {}};
    auto response = output<irfq_infinite_engine_response_v1>();
    CHECK(
        irfq_infinite_engine_initialize_v1(&request, &response, sizeof(response))
        == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
    CHECK(response.header.written_length == 0);
  }

  auto table = complete;
  table.bootstrap = nullptr;
  const irfq_infinite_engine_init_request_v1 request{
      sizeof(request),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
      &table,
      {}};
  auto initialized = output<irfq_infinite_engine_response_v1>();
  REQUIRE(
      irfq_infinite_engine_initialize_v1(&request, &initialized, sizeof(initialized)) == IRFQ_INFINITE_STATUS_OK_V1);
  auto duplicate = output<irfq_infinite_engine_response_v1>();
  REQUIRE(irfq_infinite_engine_initialize_v1(&request, &duplicate, sizeof(duplicate)) == IRFQ_INFINITE_STATUS_OK_V1);
  CHECK_FALSE(duplicate.engine.object == initialized.engine.object);
  CHECK_FALSE(duplicate.engine.generation == initialized.engine.generation);

  const irfq_infinite_bootstrap_request_v1
      disabled{sizeof(disabled), IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1, {nullptr, 0}, 0, {}};
  auto rejected = output<irfq_infinite_bootstrap_response_v1>();
  CHECK(
      irfq_infinite_connection_bootstrap_v1(initialized.engine, &disabled, &rejected, sizeof(rejected))
      == IRFQ_INFINITE_STATUS_NOT_READY_V1);
  CHECK(rejected.outcome == IRFQ_INFINITE_BOOTSTRAP_REJECTED_V1);

  auto shutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(initialized.engine, &shutdown, sizeof(shutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
  shutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(duplicate.engine, &shutdown, sizeof(shutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][lifecycle]") {
  irfq_infinite_abi_info_v1
      invalidInfo{sizeof(invalidInfo), IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1, 0, 0, 0, 0, 0, {1}};
  CHECK(irfq_infinite_frame_adapter_query_v1(&invalidInfo) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
  irfq_infinite_abi_info_v1 info{sizeof(info), IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1, 0, 0, 0, 0, 0, {}};
  REQUIRE(irfq_infinite_frame_adapter_query_v1(&info) == IRFQ_INFINITE_STATUS_OK_V1);
  CHECK(info.capabilities == IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1);

  CallbackContext context;
  auto table = callbacks(context);
  const irfq_infinite_engine_init_request_v1 init{
      sizeof(init),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
      &table,
      {}};
  auto initialized = output<irfq_infinite_engine_response_v1>();
  REQUIRE(irfq_infinite_engine_initialize_v1(&init, &initialized, sizeof(initialized)) == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(initialized.header.written_length == sizeof(initialized));

  const auto logon
      = finishFix("35=A\00134=1\00149=PARTICIPANT\00156=VENUE\00152=20260826-08:08:08.000\00198=0\001108=30\001");
  const irfq_infinite_bootstrap_request_v1 bootstrap{
      sizeof(bootstrap),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(logon.data()), logon.size()},
      1,
      {UINT64_C(10), UINT64_C(20)}};
  context.throwAfterBootstrapAccept = true;
  auto failedBootstrap = output<irfq_infinite_bootstrap_response_v1>();
  CHECK(
      irfq_infinite_connection_bootstrap_v1(initialized.engine, &bootstrap, &failedBootstrap, sizeof(failedBootstrap))
      == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
  CHECK(failedBootstrap.header.written_length == 0);
  CHECK(context.fenced);
  CHECK(context.released);
  context.throwAfterBootstrapAccept = false;
  context.fenced = false;
  context.released = false;

  auto bootstrapped = output<irfq_infinite_bootstrap_response_v1>();
  REQUIRE(
      irfq_infinite_connection_bootstrap_v1(initialized.engine, &bootstrap, &bootstrapped, sizeof(bootstrapped))
      == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(bootstrapped.outcome == IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1);

  const auto heartbeat = finishFix("35=0\00134=2\00149=PARTICIPANT\00156=VENUE\00152=20260826-08:08:09.000\001");
  const irfq_infinite_dispatch_request_v1 oversized{
      sizeof(oversized),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(heartbeat.data()), IRFQ_INFINITE_MAX_BATCH_BYTES_V1 + 1},
      {}};
  auto oversizedBytes = dispatchOutput();
  auto *oversizedResponse = reinterpret_cast<irfq_infinite_dispatch_response_v1 *>(oversizedBytes.data());
  CHECK(
      irfq_infinite_connection_dispatch_v1(
          bootstrapped.connection,
          &oversized,
          oversizedBytes.data(),
          oversizedBytes.size())
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
  CHECK(oversizedResponse->header.written_length == 0);

  const irfq_infinite_dispatch_request_v1 dispatch{
      sizeof(dispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(heartbeat.data()), heartbeat.size()},
      {}};
  auto shortDispatch = output<irfq_infinite_dispatch_response_v1>();
  CHECK(
      irfq_infinite_connection_dispatch_v1(bootstrapped.connection, &dispatch, &shortDispatch, sizeof(shortDispatch))
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
  CHECK(shortDispatch.header.written_length == 0);
#ifndef CLOCK_TAI
  auto unavailableBytes = dispatchOutput();
  auto *unavailable = reinterpret_cast<irfq_infinite_dispatch_response_v1 *>(unavailableBytes.data());
  CHECK(
      irfq_infinite_connection_dispatch_v1(
          bootstrapped.connection,
          &dispatch,
          unavailableBytes.data(),
          unavailableBytes.size())
      == IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1);
  CHECK(unavailable->fault == IRFQ_INFINITE_DISPATCH_FAULT_INVALID_OBSERVATION_V1);
  const irfq_infinite_close_request_v1 unavailableClose{
      sizeof(unavailableClose),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      UINT32_C(77),
      {}};
  auto unavailableClosed = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(
          bootstrapped.connection,
          &unavailableClose,
          &unavailableClosed,
          sizeof(unavailableClosed))
      == IRFQ_INFINITE_STATUS_CLOSED_V1);
  auto finalShutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(initialized.engine, &finalShutdown, sizeof(finalShutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
  return;
#endif
  auto dispatchBytes = dispatchOutput();
  auto *dispatchResponse = reinterpret_cast<irfq_infinite_dispatch_response_v1 *>(dispatchBytes.data());
  REQUIRE(
      irfq_infinite_connection_dispatch_v1(
          bootstrapped.connection,
          &dispatch,
          dispatchBytes.data(),
          dispatchBytes.size())
      == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(dispatchResponse->result_count == 1);
  REQUIRE(
      dispatchResponse->header.written_length
      == sizeof(irfq_infinite_dispatch_response_v1) + sizeof(irfq_infinite_registration_result_v1));
  const auto *registration = reinterpret_cast<const irfq_infinite_registration_result_v1 *>(
      dispatchBytes.data() + sizeof(*dispatchResponse));
  CHECK(context.registeredFrames == std::vector<std::string>{heartbeat});
  REQUIRE(context.observations.size() == 1);
  CHECK(context.observations[0] > 0);
  CHECK(registration->observed_tai_ns == context.observations[0]);

  const irfq_infinite_head_request_v1 head{
      sizeof(head),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      registration->token,
      {}};
  auto atHead = output<irfq_infinite_operation_response_v1>();
  REQUIRE(
      irfq_infinite_connection_wait_head_v1(bootstrapped.connection, &head, &atHead, sizeof(atHead))
      == IRFQ_INFINITE_STATUS_AT_HEAD_V1);

  auto classified = output<irfq_infinite_classification_response_v1>();
  REQUIRE(
      irfq_infinite_connection_classify_v1(bootstrapped.connection, &head, &classified, sizeof(classified))
      == IRFQ_INFINITE_STATUS_CLASSIFIED_V1);
  CHECK(classified.outcome == IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1);
  CHECK(classified.action == IRFQ_INFINITE_ACTION_PROTOCOL_CONTROL_V1);
  CHECK(classified.sequence_disposition == IRFQ_INFINITE_SEQUENCE_AT_HEAD_V1);

  const irfq_infinite_apply_request_v1 apply{
      sizeof(apply),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      classified.classification,
      classified.authorization,
      {}};
  auto applied = output<irfq_infinite_operation_response_v1>();
  REQUIRE(
      irfq_infinite_connection_apply_v1(bootstrapped.connection, &apply, &applied, sizeof(applied))
      == IRFQ_INFINITE_STATUS_APPLIED_V1);

  const auto tooHighHeartbeat = finishFix("35=0\00134=4\00149=PARTICIPANT\00156=VENUE\00152=20260826-08:08:10.000\001");
  const irfq_infinite_dispatch_request_v1 tooHighDispatch{
      sizeof(tooHighDispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(tooHighHeartbeat.data()), tooHighHeartbeat.size()},
      {}};
  auto tooHighBytes = dispatchOutput();
  auto *tooHighResponse = reinterpret_cast<irfq_infinite_dispatch_response_v1 *>(tooHighBytes.data());
  REQUIRE(
      irfq_infinite_connection_dispatch_v1(
          bootstrapped.connection,
          &tooHighDispatch,
          tooHighBytes.data(),
          tooHighBytes.size())
      == IRFQ_INFINITE_STATUS_OK_V1);
  const auto *tooHighRegistration
      = reinterpret_cast<const irfq_infinite_registration_result_v1 *>(tooHighBytes.data() + sizeof(*tooHighResponse));
  const irfq_infinite_head_request_v1 tooHighHead{
      sizeof(tooHighHead),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      tooHighRegistration->token,
      {}};
  atHead = output<irfq_infinite_operation_response_v1>();
  REQUIRE(
      irfq_infinite_connection_wait_head_v1(bootstrapped.connection, &tooHighHead, &atHead, sizeof(atHead))
      == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
  auto tooHighClassified = output<irfq_infinite_classification_response_v1>();
  REQUIRE(
      irfq_infinite_connection_classify_v1(
          bootstrapped.connection,
          &tooHighHead,
          &tooHighClassified,
          sizeof(tooHighClassified))
      == IRFQ_INFINITE_STATUS_CLASSIFIED_V1);
  CHECK(tooHighClassified.sequence_disposition == IRFQ_INFINITE_SEQUENCE_TOO_HIGH_V1);
  CHECK(tooHighClassified.outcome == IRFQ_INFINITE_STATUS_AUTHORIZED_NO_CONSUME_V1);
  const irfq_infinite_apply_request_v1 applyTooHigh{
      sizeof(applyTooHigh),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      tooHighClassified.classification,
      tooHighClassified.authorization,
      {}};
  applied = output<irfq_infinite_operation_response_v1>();
  REQUIRE(
      irfq_infinite_connection_apply_v1(bootstrapped.connection, &applyTooHigh, &applied, sizeof(applied))
      == IRFQ_INFINITE_STATUS_APPLIED_V1);

  applied = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_apply_v1(bootstrapped.connection, &applyTooHigh, &applied, sizeof(applied))
      == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);

  const irfq_infinite_close_request_v1 close{
      sizeof(close),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      UINT32_C(77),
      {}};
  auto closed = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(bootstrapped.connection, &close, &closed, sizeof(closed))
      == IRFQ_INFINITE_STATUS_CLOSED_V1);
  closed = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(bootstrapped.connection, &close, &closed, sizeof(closed))
      == IRFQ_INFINITE_STATUS_CLOSED_V1);
  CHECK(context.fenced);
  CHECK(context.released);

  auto unavailableShutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(initialized.engine, &unavailableShutdown, sizeof(unavailableShutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][shutdown]") {
  CallbackContext context;
  context.blockRegistration = true;
  auto table = callbacks(context);
  const irfq_infinite_engine_init_request_v1 init{
      sizeof(init),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
      &table,
      {}};
  auto initialized = output<irfq_infinite_engine_response_v1>();
  REQUIRE(irfq_infinite_engine_initialize_v1(&init, &initialized, sizeof(initialized)) == IRFQ_INFINITE_STATUS_OK_V1);

  const auto logon
      = finishFix("35=A\00134=1\00149=PARTICIPANT2\00156=VENUE\00152=20260826-08:08:08.000\00198=0\001108=30\001");
  const irfq_infinite_bootstrap_request_v1 bootstrap{
      sizeof(bootstrap),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(logon.data()), logon.size()},
      1,
      {UINT64_C(11), UINT64_C(21)}};
  auto bootstrapped = output<irfq_infinite_bootstrap_response_v1>();
  REQUIRE(
      irfq_infinite_connection_bootstrap_v1(initialized.engine, &bootstrap, &bootstrapped, sizeof(bootstrapped))
      == IRFQ_INFINITE_STATUS_OK_V1);

  const auto heartbeat = finishFix("35=0\00134=2\00149=PARTICIPANT2\00156=VENUE\00152=20260826-08:08:09.000\001");
  const irfq_infinite_dispatch_request_v1 dispatch{
      sizeof(dispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(heartbeat.data()), heartbeat.size()},
      {}};
#ifndef CLOCK_TAI
  auto unavailableShutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(initialized.engine, &unavailableShutdown, sizeof(unavailableShutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
  return;
#endif
  irfq_infinite_status_v1 inFlightStatus = IRFQ_INFINITE_STATUS_OK_V1;
  std::thread inFlight([&]() {
    auto response = dispatchOutput();
    inFlightStatus
        = irfq_infinite_connection_dispatch_v1(bootstrapped.connection, &dispatch, response.data(), response.size());
  });
  {
    std::unique_lock<std::mutex> lock(context.mutex);
    context.condition.wait(lock, [&context]() { return context.registrationEntered; });
  }
  auto shutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(initialized.engine, &shutdown, sizeof(shutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
  inFlight.join();
  CHECK(inFlightStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.fenced);
  CHECK(context.released);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][reentry]") {
  CallbackContext context;
  auto table = callbacks(context);
  const irfq_infinite_engine_init_request_v1 init{
      sizeof(init),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
      &table,
      {}};
  auto initialized = output<irfq_infinite_engine_response_v1>();
  REQUIRE(irfq_infinite_engine_initialize_v1(&init, &initialized, sizeof(initialized)) == IRFQ_INFINITE_STATUS_OK_V1);

  const auto bootstrap = [&](const std::string &sender, std::uint64_t nonce) {
    const auto logon
        = finishFix("35=A\00134=1\00149=" + sender + "\00156=VENUE\00152=20260826-08:08:08.000\00198=0\001108=30\001");
    const irfq_infinite_bootstrap_request_v1 request{
        sizeof(request),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {reinterpret_cast<const std::uint8_t *>(logon.data()), logon.size()},
        1,
        {nonce, nonce + 1}};
    auto response = output<irfq_infinite_bootstrap_response_v1>();
    REQUIRE(
        irfq_infinite_connection_bootstrap_v1(initialized.engine, &request, &response, sizeof(response))
        == IRFQ_INFINITE_STATUS_OK_V1);
    return response.connection;
  };
  const auto first = bootstrap("PARTICIPANT3", 30);
  const auto second = bootstrap("PARTICIPANT4", 40);
  const auto third = bootstrap("PARTICIPANT5", 50);

#ifdef CLOCK_TAI
  context.adapterConnection = first;
  context.crossConnection = second;
  context.reenterSameHandle = true;
  context.callCrossConnection = true;
  const auto firstHeartbeat = finishFix("35=0\00134=2\00149=PARTICIPANT3\00156=VENUE\00152=20260826-08:08:09.000\001");
  const irfq_infinite_dispatch_request_v1 firstDispatch{
      sizeof(firstDispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(firstHeartbeat.data()), firstHeartbeat.size()},
      {}};
  auto firstBytes = dispatchOutput();
  CHECK(
      irfq_infinite_connection_dispatch_v1(first, &firstDispatch, firstBytes.data(), firstBytes.size())
      == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.sameHandleReentryStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.crossConnectionStatus == IRFQ_INFINITE_STATUS_OK_V1);

  context.reenterSameHandle = false;
  context.callCrossConnection = false;
  const auto secondHeartbeat = finishFix("35=0\00134=2\00149=PARTICIPANT4\00156=VENUE\00152=20260826-08:08:09.000\001");
  const irfq_infinite_dispatch_request_v1 secondDispatch{
      sizeof(secondDispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(secondHeartbeat.data()), secondHeartbeat.size()},
      {}};
  auto secondBytes = dispatchOutput();
  CHECK(
      irfq_infinite_connection_dispatch_v1(second, &secondDispatch, secondBytes.data(), secondBytes.size())
      == IRFQ_INFINITE_STATUS_OK_V1);

  context.omitRegistrationResult = true;
  const auto thirdHeartbeat = finishFix("35=0\00134=2\00149=PARTICIPANT5\00156=VENUE\00152=20260826-08:08:09.000\001");
  const irfq_infinite_dispatch_request_v1 thirdDispatch{
      sizeof(thirdDispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(thirdHeartbeat.data()), thirdHeartbeat.size()},
      {}};
  auto thirdBytes = dispatchOutput();
  CHECK(
      irfq_infinite_connection_dispatch_v1(third, &thirdDispatch, thirdBytes.data(), thirdBytes.size())
      == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
  CHECK(context.fenced);
#endif

  const irfq_infinite_close_request_v1 close{
      sizeof(close),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      UINT32_C(88),
      {}};
  auto closed = output<irfq_infinite_operation_response_v1>();
  CHECK(irfq_infinite_connection_close_v1(first, &close, &closed, sizeof(closed)) == IRFQ_INFINITE_STATUS_CLOSED_V1);
  closed = output<irfq_infinite_operation_response_v1>();
  CHECK(irfq_infinite_connection_close_v1(second, &close, &closed, sizeof(closed)) == IRFQ_INFINITE_STATUS_CLOSED_V1);
  closed = output<irfq_infinite_operation_response_v1>();
  CHECK(irfq_infinite_connection_close_v1(third, &close, &closed, sizeof(closed)) == IRFQ_INFINITE_STATUS_CLOSED_V1);
  auto shutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(initialized.engine, &shutdown, sizeof(shutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][connection-bound]") {
  CallbackContext context;
  auto table = callbacks(context);
  const irfq_infinite_engine_init_request_v1 init{
      sizeof(init),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
      &table,
      {}};
  auto initialized = output<irfq_infinite_engine_response_v1>();
  REQUIRE(irfq_infinite_engine_initialize_v1(&init, &initialized, sizeof(initialized)) == IRFQ_INFINITE_STATUS_OK_V1);

  const auto bootstrap = [&](std::uint32_t index) {
    const auto sender = "BOUND" + std::to_string(index);
    const auto logon
        = finishFix("35=A\00134=1\00149=" + sender + "\00156=VENUE\00152=20260826-08:08:08.000\00198=0\001108=30\001");
    const irfq_infinite_bootstrap_request_v1 request{
        sizeof(request),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {reinterpret_cast<const std::uint8_t *>(logon.data()), logon.size()},
        1,
        {UINT64_C(1000) + index, UINT64_C(2000) + index}};
    auto response = output<irfq_infinite_bootstrap_response_v1>();
    const auto status
        = irfq_infinite_connection_bootstrap_v1(initialized.engine, &request, &response, sizeof(response));
    return std::make_pair(status, response);
  };

  std::vector<irfq_infinite_handle_v1> connections;
  connections.reserve(IRFQ_INFINITE_MAX_CONNECTIONS_V1);
  for (std::uint32_t index = 0; index < IRFQ_INFINITE_MAX_CONNECTIONS_V1; ++index) {
    const auto accepted = bootstrap(index);
    REQUIRE(accepted.first == IRFQ_INFINITE_STATUS_OK_V1);
    REQUIRE(accepted.second.outcome == IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1);
    connections.push_back(accepted.second.connection);
  }
  const auto over = bootstrap(IRFQ_INFINITE_MAX_CONNECTIONS_V1);
  CHECK(over.first == IRFQ_INFINITE_STATUS_NOT_READY_V1);
  CHECK(over.second.outcome == IRFQ_INFINITE_BOOTSTRAP_REJECTED_V1);

  const irfq_infinite_close_request_v1 close{
      sizeof(close),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      UINT32_C(99),
      {}};
  for (const auto connection : connections) {
    auto closed = output<irfq_infinite_operation_response_v1>();
    CHECK(
        irfq_infinite_connection_close_v1(connection, &close, &closed, sizeof(closed))
        == IRFQ_INFINITE_STATUS_CLOSED_V1);
  }
  auto shutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(initialized.engine, &shutdown, sizeof(shutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
}
