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
  bool fenced{false};
  bool released{false};
  bool blockRegistration{false};
  bool registrationEntered{false};
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
    void *,
    const irfq_infinite_bootstrap_request_v1 *,
    void *outputBuffer,
    std::uint64_t capacity) {
  auto response = output<irfq_infinite_bootstrap_response_v1>();
  response.header.status = IRFQ_INFINITE_STATUS_OK_V1;
  response.connection = {UINT64_C(7), UINT64_C(9)};
  response.outcome = IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1;
  return publishFixed(outputBuffer, capacity, response);
}

irfq_infinite_status_v1 registerCallback(
    void *opaque,
    const irfq_infinite_registration_callback_request_v1 *request,
    void *outputBuffer,
    std::uint64_t capacity) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  const auto responseBytes = sizeof(irfq_infinite_dispatch_response_v1)
                             + request->frame_count * sizeof(irfq_infinite_registration_result_v1);
  if (capacity < responseBytes) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  std::vector<std::uint8_t> staged(responseBytes, 0);
  auto *response = reinterpret_cast<irfq_infinite_dispatch_response_v1 *>(staged.data());
  response->header
      = {sizeof(irfq_infinite_dispatch_response_v1),
         IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
         IRFQ_INFINITE_STATUS_OK_V1,
         0,
         0};
  response->result_count = request->frame_count;
  auto *results = reinterpret_cast<irfq_infinite_registration_result_v1 *>(
      staged.data() + sizeof(irfq_infinite_dispatch_response_v1));
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
  std::memcpy(outputBuffer, staged.data(), responseBytes);
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
  response.header.status
      = context.fenced ? IRFQ_INFINITE_STATUS_STREAM_FENCED_V1 : IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1;
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

TEST_CASE("InfiniteFrameAdapterTests fixture freezes the complete ABI", "[infinite][adapter]") {
  const auto fixture = readFixture();
  CHECK(fixture.text("contract", "engine_lifecycle") == "INITIALIZED>CLOSING>SHUTDOWN");
  CHECK(fixture.text("contract", "connection_lifecycle") == "OPEN>CLOSING>CLOSED");
  CHECK(fixture.number("constant", "abi_version") == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1);
  CHECK(fixture.number("constant", "required_capabilities") == IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1);
  CHECK(fixture.number("constant", "max_connections") == IRFQ_INFINITE_MAX_CONNECTIONS_V1);
  CHECK(fixture.number("constant", "max_batch_frames") == IRFQ_INFINITE_MAX_BATCH_FRAMES_V1);
  CHECK(fixture.number("constant", "max_frame_bytes") == IRFQ_INFINITE_MAX_FRAME_BYTES_V1);
  CHECK(fixture.number("constant", "max_batch_bytes") == IRFQ_INFINITE_MAX_BATCH_BYTES_V1);

#define CHECK_LAYOUT(type)                                                                                             \
  CHECK(fixture.number("size", #type) == sizeof(type));                                                                \
  CHECK(fixture.number("align", #type) == alignof(type))
#define CHECK_OFFSET(type, field) CHECK(fixture.number("offset", #type "." #field) == offsetof(type, field))
  CHECK_LAYOUT(irfq_infinite_abi_info_v1);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, structure_size);
  CHECK_OFFSET(irfq_infinite_abi_info_v1, reserved);
  CHECK_LAYOUT(irfq_infinite_output_header_v1);
  CHECK_LAYOUT(irfq_infinite_handle_v1);
  CHECK_LAYOUT(irfq_infinite_slice_v1);
  CHECK_LAYOUT(irfq_infinite_callback_table_v1);
  CHECK_OFFSET(irfq_infinite_callback_table_v1, release);
  CHECK_LAYOUT(irfq_infinite_engine_init_request_v1);
  CHECK_LAYOUT(irfq_infinite_engine_response_v1);
  CHECK_LAYOUT(irfq_infinite_bootstrap_request_v1);
  CHECK_LAYOUT(irfq_infinite_bootstrap_response_v1);
  CHECK_LAYOUT(irfq_infinite_dispatch_request_v1);
  CHECK_LAYOUT(irfq_infinite_frame_descriptor_v1);
  CHECK_LAYOUT(irfq_infinite_registration_callback_request_v1);
  CHECK_LAYOUT(irfq_infinite_registration_result_v1);
  CHECK_LAYOUT(irfq_infinite_dispatch_response_v1);
  CHECK_LAYOUT(irfq_infinite_head_request_v1);
  CHECK_LAYOUT(irfq_infinite_head_callback_request_v1);
  CHECK_LAYOUT(irfq_infinite_operation_response_v1);
  CHECK_LAYOUT(irfq_infinite_classification_callback_request_v1);
  CHECK_LAYOUT(irfq_infinite_classification_callback_response_v1);
  CHECK_LAYOUT(irfq_infinite_classification_response_v1);
  CHECK_LAYOUT(irfq_infinite_apply_request_v1);
  CHECK_LAYOUT(irfq_infinite_close_request_v1);
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

TEST_CASE("InfiniteFrameAdapterTests reject invalid output before publication", "[infinite][adapter]") {
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

TEST_CASE("InfiniteFrameAdapterTests lifecycle dispatches classifies applies and closes", "[infinite][adapter]") {
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
  auto bootstrapped = output<irfq_infinite_bootstrap_response_v1>();
  REQUIRE(
      irfq_infinite_connection_bootstrap_v1(initialized.engine, &bootstrap, &bootstrapped, sizeof(bootstrapped))
      == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(bootstrapped.outcome == IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1);

  const auto heartbeat = finishFix("35=0\00134=2\00149=PARTICIPANT\00156=VENUE\00152=20260826-08:08:09.000\001");
  const irfq_infinite_dispatch_request_v1 dispatch{
      sizeof(dispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(heartbeat.data()), heartbeat.size()},
      {}};
  alignas(8) std::
      array<std::uint8_t, sizeof(irfq_infinite_dispatch_response_v1) + sizeof(irfq_infinite_registration_result_v1)>
          dispatchBytes{};
  auto *dispatchResponse = reinterpret_cast<irfq_infinite_dispatch_response_v1 *>(dispatchBytes.data());
  dispatchResponse->header.structure_size = sizeof(*dispatchResponse);
  dispatchResponse->header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  REQUIRE(
      irfq_infinite_connection_dispatch_v1(
          bootstrapped.connection,
          &dispatch,
          dispatchBytes.data(),
          dispatchBytes.size())
      == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(dispatchResponse->result_count == 1);
  REQUIRE(dispatchResponse->header.written_length == dispatchBytes.size());
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
      == IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1);
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
  applied = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_apply_v1(bootstrapped.connection, &apply, &applied, sizeof(applied))
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

  auto shutdown = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_engine_shutdown_v1(initialized.engine, &shutdown, sizeof(shutdown))
      == IRFQ_INFINITE_STATUS_SHUTDOWN_V1);
}

TEST_CASE("InfiniteFrameAdapterTests shutdown fences and drains an in-flight callback", "[infinite][adapter]") {
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
  std::thread inFlight([&]() {
    alignas(8) std::
        array<std::uint8_t, sizeof(irfq_infinite_dispatch_response_v1) + sizeof(irfq_infinite_registration_result_v1)>
            response{};
    auto *header = reinterpret_cast<irfq_infinite_output_header_v1 *>(response.data());
    header->structure_size = sizeof(irfq_infinite_dispatch_response_v1);
    header->abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
    CHECK(
        irfq_infinite_connection_dispatch_v1(bootstrapped.connection, &dispatch, response.data(), response.size())
        == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
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
  CHECK(context.fenced);
  CHECK(context.released);
}
