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
#include "InfiniteFrameAdapterStore.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
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
  irfq_infinite_handle_v1 firstAdapterConnection{};
  irfq_infinite_handle_v1 secondAdapterConnection{};
  std::string firstNestedFrame;
  std::string secondNestedFrame;
  irfq_infinite_status_v1 sameHandleReentryStatus{IRFQ_INFINITE_STATUS_OK_V1};
  irfq_infinite_status_v1 waitReentryStatus{IRFQ_INFINITE_STATUS_OK_V1};
  irfq_infinite_status_v1 authorizeReentryStatus{IRFQ_INFINITE_STATUS_OK_V1};
  irfq_infinite_status_v1 crossConnectionStatus{IRFQ_INFINITE_STATUS_OK_V1};
  bool fenced{false};
  bool released{false};
  std::size_t fenceCount{0};
  std::size_t releaseCount{0};
  bool blockRegistration{false};
  bool registrationEntered{false};
  bool blockRelease{false};
  bool releaseEntered{false};
  bool allowRelease{false};
  bool blockBootstrap{false};
  std::size_t bootstrapEntered{0};
  bool allowBootstrap{false};
  bool reenterSameHandle{false};
  bool reenterWait{false};
  bool reenterAuthorize{false};
  bool callCrossConnection{false};
  bool ancestryCycle{false};
  bool concurrentCrossCalls{false};
  bool concurrentCrossCloses{false};
  std::size_t concurrentCallbacksEntered{0};
  std::size_t concurrentNestedCompleted{0};
  irfq_infinite_status_v1 firstNestedStatus{IRFQ_INFINITE_STATUS_OK_V1};
  irfq_infinite_status_v1 secondNestedStatus{IRFQ_INFINITE_STATUS_OK_V1};
  irfq_infinite_status_v1 firstCloseStatus{IRFQ_INFINITE_STATUS_OK_V1};
  irfq_infinite_status_v1 secondCloseStatus{IRFQ_INFINITE_STATUS_OK_V1};
  bool crossCloseOnRelease{false};
  std::size_t releaseCrossCallbacksEntered{0};
  irfq_infinite_status_v1 firstReleaseCloseStatus{IRFQ_INFINITE_STATUS_OK_V1};
  irfq_infinite_status_v1 secondReleaseCloseStatus{IRFQ_INFINITE_STATUS_OK_V1};
  bool omitRegistrationResult{false};
  bool gapRegistrationOrdinal{false};
  bool bootstrapReservedNonzero{false};
  bool waitReservedNonzero{false};
  bool authorizeReservedNonzero{false};
  bool throwAfterBootstrapAccept{false};
  bool registrationNotRegistered{false};
  bool wrongWaitLifecycle{false};
  bool reuseRegistrationToken{false};
  bool reuseAuthorization{false};
  bool waitForFenceAfterReentry{false};
  bool credentialsObserved{false};
  bool fenceThrows{false};
  bool releaseThrows{false};
  irfq_infinite_status_v1 fenceAcknowledgement{IRFQ_INFINITE_STATUS_STREAM_FENCED_V1};
  irfq_infinite_status_v1 releaseAcknowledgement{IRFQ_INFINITE_STATUS_CLOSED_V1};
};

irfq_infinite_status_v1 nestedDispatch(irfq_infinite_handle_v1 connection) {
  const irfq_infinite_dispatch_request_v1 nestedRequest{
      sizeof(nestedRequest),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {nullptr, 0},
      {}};
  auto nestedResponse = dispatchOutput();
  return irfq_infinite_connection_dispatch_v1(connection, &nestedRequest, nestedResponse.data(), nestedResponse.size());
}

irfq_infinite_status_v1 nestedDispatch(irfq_infinite_handle_v1 connection, const std::string &frame) {
  const irfq_infinite_dispatch_request_v1 nestedRequest{
      sizeof(nestedRequest),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(frame.data()), frame.size()},
      {}};
  auto nestedResponse = dispatchOutput();
  return irfq_infinite_connection_dispatch_v1(connection, &nestedRequest, nestedResponse.data(), nestedResponse.size());
}

irfq_infinite_status_v1 nestedClose(irfq_infinite_handle_v1 connection, std::uint32_t reason = 1) {
  const irfq_infinite_close_request_v1 request{sizeof(request), IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1, reason, {}};
  auto response = output<irfq_infinite_operation_response_v1>();
  return irfq_infinite_connection_close_v1(connection, &request, &response, sizeof(response));
}

thread_local std::size_t registrationCallbackDepth{0};

class RegistrationCallbackScope {
public:
  RegistrationCallbackScope() { ++registrationCallbackDepth; }
  ~RegistrationCallbackScope() { --registrationCallbackDepth; }
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
    const irfq_infinite_bootstrap_request_v1 *request,
    void *outputBuffer,
    std::uint64_t capacity) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  const std::string frame(
      reinterpret_cast<const char *>(request->frame.data),
      static_cast<std::size_t>(request->frame.length));
  context.credentialsObserved = frame.find("\001553=adapter-user\001") != std::string::npos
                                && frame.find("\001554=adapter-password\001") != std::string::npos;
  auto response = output<irfq_infinite_bootstrap_response_v1>();
  response.header.status = IRFQ_INFINITE_STATUS_OK_V1;
  {
    std::unique_lock<std::mutex> lock(context.mutex);
    response.connection = {context.nextConnection++, UINT64_C(9)};
    if (context.blockBootstrap) {
      ++context.bootstrapEntered;
      context.condition.notify_all();
      context.condition.wait(lock, [&context]() { return context.allowBootstrap; });
    }
  }
  response.outcome = IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1;
  response.reserved[0] = context.bootstrapReservedNonzero ? 1 : 0;
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
  RegistrationCallbackScope callbackScope;
  if (context.reenterSameHandle) {
    context.sameHandleReentryStatus = nestedDispatch(context.adapterConnection);
  }
  if (context.callCrossConnection) {
    context.crossConnectionStatus = nestedDispatch(context.crossConnection);
  }
  if (context.ancestryCycle) {
    const auto first = request->connection.object == UINT64_C(7);
    const auto status = first ? nestedDispatch(context.secondAdapterConnection, context.secondNestedFrame)
                              : nestedDispatch(context.firstAdapterConnection, context.firstNestedFrame);
    if (first) {
      context.firstNestedStatus = status;
    } else {
      context.secondNestedStatus = status;
    }
  }
  if (context.concurrentCrossCalls && registrationCallbackDepth == 1) {
    const auto first = request->connection.object == UINT64_C(7);
    {
      std::unique_lock<std::mutex> lock(context.mutex);
      ++context.concurrentCallbacksEntered;
      context.condition.notify_all();
      context.condition.wait(lock, [&context]() { return context.concurrentCallbacksEntered == 2; });
    }
    const auto status = first ? nestedDispatch(context.secondAdapterConnection, context.secondNestedFrame)
                              : nestedDispatch(context.firstAdapterConnection, context.firstNestedFrame);
    {
      std::unique_lock<std::mutex> lock(context.mutex);
      if (first) {
        context.firstNestedStatus = status;
      } else {
        context.secondNestedStatus = status;
      }
      ++context.concurrentNestedCompleted;
      context.condition.notify_all();
      context.condition.wait(lock, [&context]() { return context.concurrentNestedCompleted == 2; });
    }
  }
  if (context.concurrentCrossCloses && registrationCallbackDepth == 1) {
    const auto first = request->connection.object == UINT64_C(7);
    {
      std::unique_lock<std::mutex> lock(context.mutex);
      ++context.concurrentCallbacksEntered;
      context.condition.notify_all();
      context.condition.wait(lock, [&context]() { return context.concurrentCallbacksEntered == 2; });
    }
    const auto status
        = nestedClose(first ? context.secondAdapterConnection : context.firstAdapterConnection, first ? 41 : 42);
    {
      std::unique_lock<std::mutex> lock(context.mutex);
      if (first) {
        context.firstCloseStatus = status;
      } else {
        context.secondCloseStatus = status;
      }
      ++context.concurrentNestedCompleted;
      context.condition.notify_all();
      context.condition.wait(lock, [&context]() { return context.concurrentNestedCompleted == 2; });
    }
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
    if (context.registrationNotRegistered) {
      response->header.status = IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1;
      response->result_count = 0;
    } else if (context.fenced) {
      response->header.status = IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
      response->result_count = 0;
    } else {
      for (std::uint32_t index = 0; index < request->frame_count; ++index) {
        const auto &frame = request->frames[index];
        context.registeredFrames.emplace_back(reinterpret_cast<const char *>(frame.data), frame.length);
        context.observations.push_back(frame.observed_tai_ns);
        if (context.gapRegistrationOrdinal && index == 1) {
          ++context.nextOrdinal;
        }
        const auto token = context.reuseRegistrationToken ? context.nextToken : context.nextToken++;
        results[index] = {context.nextOrdinal++, {token, UINT64_C(1)}, frame.observed_tai_ns};
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
  if (context.reenterWait) {
    context.waitReentryStatus = nestedDispatch(context.adapterConnection);
    if (context.waitForFenceAfterReentry) {
      std::unique_lock<std::mutex> lock(context.mutex);
      context.condition.wait(lock, [&context]() { return context.fenced; });
    }
  }
  auto response = output<irfq_infinite_operation_response_v1>();
  response.header.status = context.fenced ? IRFQ_INFINITE_STATUS_STREAM_FENCED_V1 : IRFQ_INFINITE_STATUS_AT_HEAD_V1;
  response.lifecycle = context.fenced ? IRFQ_INFINITE_CONNECTION_CLOSING_V1 : IRFQ_INFINITE_CONNECTION_OPEN_V1;
  if (context.wrongWaitLifecycle) {
    response.lifecycle = response.header.status == IRFQ_INFINITE_STATUS_AT_HEAD_V1 ? IRFQ_INFINITE_CONNECTION_CLOSING_V1
                                                                                   : IRFQ_INFINITE_CONNECTION_OPEN_V1;
  }
  response.reserved = context.waitReservedNonzero ? 1 : 0;
  return publishFixed(outputBuffer, capacity, response);
}

irfq_infinite_status_v1 authorizeCallback(
    void *opaque,
    const irfq_infinite_classification_callback_request_v1 *request,
    void *outputBuffer,
    std::uint64_t capacity) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  if (context.reenterAuthorize) {
    context.authorizeReentryStatus = nestedDispatch(context.adapterConnection);
    if (context.waitForFenceAfterReentry) {
      std::unique_lock<std::mutex> lock(context.mutex);
      context.condition.wait(lock, [&context]() { return context.fenced; });
    }
  }
  auto response = output<irfq_infinite_classification_callback_response_v1>();
  response.header.status = context.fenced ? IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
                           : request->sequence_disposition == IRFQ_INFINITE_SEQUENCE_AT_HEAD_V1
                               ? IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1
                               : IRFQ_INFINITE_STATUS_AUTHORIZED_NO_CONSUME_V1;
  const auto authorization = context.reuseAuthorization ? context.nextAuthorization : context.nextAuthorization++;
  response.authorization
      = {authorization, context.reuseAuthorization ? UINT64_C(1) : request->classification.generation};
  response.outcome = response.header.status;
  response.reserved = context.authorizeReservedNonzero ? 1 : 0;
  return publishFixed(outputBuffer, capacity, response);
}

irfq_infinite_status_v1 fenceCallback(void *opaque, irfq_infinite_handle_v1, std::uint32_t) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  {
    std::lock_guard<std::mutex> lock(context.mutex);
    context.fenced = true;
    ++context.fenceCount;
  }
  context.condition.notify_all();
  if (context.fenceThrows) {
    throw std::runtime_error("fence callback failure");
  }
  return context.fenceAcknowledgement;
}

irfq_infinite_status_v1 releaseCallback(void *opaque, irfq_infinite_handle_v1 externalConnection, std::uint32_t) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  const auto first = externalConnection.object == UINT64_C(7);
  {
    std::unique_lock<std::mutex> lock(context.mutex);
    context.released = true;
    ++context.releaseCount;
    context.releaseEntered = true;
    context.condition.notify_all();
    context.condition.wait(lock, [&context]() { return !context.blockRelease || context.allowRelease; });
  }
  if (context.crossCloseOnRelease) {
    {
      std::unique_lock<std::mutex> lock(context.mutex);
      ++context.releaseCrossCallbacksEntered;
      context.condition.notify_all();
      context.condition.wait(lock, [&context]() { return context.releaseCrossCallbacksEntered == 2; });
    }
    const auto status
        = nestedClose(first ? context.secondAdapterConnection : context.firstAdapterConnection, first ? 51 : 52);
    std::lock_guard<std::mutex> lock(context.mutex);
    if (first) {
      context.firstReleaseCloseStatus = status;
    } else {
      context.secondReleaseCloseStatus = status;
    }
  }
  if (context.releaseThrows) {
    throw std::runtime_error("release callback failure");
  }
  return context.releaseAcknowledgement;
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

irfq_infinite_engine_response_v1 initializeEngine(CallbackContext &context) {
  auto table = callbacks(context);
  const irfq_infinite_engine_init_request_v1 request{
      sizeof(request),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
      &table,
      {}};
  auto response = output<irfq_infinite_engine_response_v1>();
  const auto status = irfq_infinite_engine_initialize_v1(&request, &response, sizeof(response));
  if (status != IRFQ_INFINITE_STATUS_OK_V1) {
    throw std::runtime_error("Unable to initialize Infinite adapter test engine");
  }
  return response;
}

struct BootstrapResult {
  irfq_infinite_status_v1 status;
  irfq_infinite_bootstrap_response_v1 response;
};

BootstrapResult bootstrapFrame(
    irfq_infinite_handle_v1 engine,
    const std::string &logon,
    std::uint64_t nonce,
    std::int64_t observation = 1) {
  const irfq_infinite_bootstrap_request_v1 request{
      sizeof(request),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(logon.data()), logon.size()},
      observation,
      {nonce, nonce + 1}};
  auto response = output<irfq_infinite_bootstrap_response_v1>();
  const auto status = irfq_infinite_connection_bootstrap_v1(engine, &request, &response, sizeof(response));
  return {status, response};
}

BootstrapResult bootstrapConnection(irfq_infinite_handle_v1 engine, const std::string &sender, std::uint64_t nonce) {
  return bootstrapFrame(
      engine,
      finishFix("35=A\00134=1\00149=" + sender + "\00156=VENUE\00152=20260826-08:08:08.000\00198=0\001108=30\001"),
      nonce);
}

struct RegistrationResult {
  irfq_infinite_status_v1 status;
  irfq_infinite_registration_result_v1 registration;
  std::uint32_t count;
  irfq_infinite_dispatch_fault_v1 fault;
  std::uint64_t writtenLength;
};

RegistrationResult dispatchFrame(irfq_infinite_handle_v1 connection, const std::string &frame) {
  const irfq_infinite_dispatch_request_v1 request{
      sizeof(request),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(frame.data()), frame.size()},
      {}};
  auto bytes = dispatchOutput();
  const auto status = irfq_infinite_connection_dispatch_v1(connection, &request, bytes.data(), bytes.size());
  const auto *response = reinterpret_cast<const irfq_infinite_dispatch_response_v1 *>(bytes.data());
  irfq_infinite_registration_result_v1 registration{};
  if (response->result_count != 0) {
    registration = *reinterpret_cast<const irfq_infinite_registration_result_v1 *>(bytes.data() + sizeof(*response));
  }
  return {status, registration, response->result_count, response->fault, response->header.written_length};
}

irfq_infinite_status_v1 waitAtHead(irfq_infinite_handle_v1 connection, irfq_infinite_handle_v1 token) {
  const irfq_infinite_head_request_v1 request{sizeof(request), IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1, token, {}};
  auto response = output<irfq_infinite_operation_response_v1>();
  return irfq_infinite_connection_wait_head_v1(connection, &request, &response, sizeof(response));
}

std::pair<irfq_infinite_status_v1, irfq_infinite_classification_response_v1> classifyAtHead(
    irfq_infinite_handle_v1 connection,
    irfq_infinite_handle_v1 token) {
  const irfq_infinite_head_request_v1 request{sizeof(request), IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1, token, {}};
  auto response = output<irfq_infinite_classification_response_v1>();
  const auto status = irfq_infinite_connection_classify_v1(connection, &request, &response, sizeof(response));
  return {status, response};
}

irfq_infinite_status_v1 applyClassification(
    irfq_infinite_handle_v1 connection,
    const irfq_infinite_classification_response_v1 &classification) {
  const irfq_infinite_apply_request_v1 request{
      sizeof(request),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      classification.classification,
      classification.authorization,
      {}};
  auto response = output<irfq_infinite_operation_response_v1>();
  return irfq_infinite_connection_apply_v1(connection, &request, &response, sizeof(response));
}

void closeConnection(irfq_infinite_handle_v1 connection, std::uint32_t reason = 1) { nestedClose(connection, reason); }

void shutdownEngine(irfq_infinite_handle_v1 engine) {
  auto response = output<irfq_infinite_operation_response_v1>();
  irfq_infinite_engine_shutdown_v1(engine, &response, sizeof(response));
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
  CHECK(fixture.text("contract", "callback_ancestry") == "complete_nonblocking");
  CHECK(fixture.text("contract", "callback_close") == "try_idle_else_fence_wake_nonblocking");
  CHECK(fixture.text("contract", "callback_contention") == "both_outer_lanes_held_through_nested_attempts");
  CHECK(fixture.text("contract", "external_fence") == "exactly_once_before_fault_return");
  CHECK(fixture.text("contract", "callback_argument_lifetime") == "synchronous_only");
  CHECK(fixture.text("contract", "callback_table_lifetime") == "copied_context_valid_until_quiescent_shutdown");
  CHECK(fixture.text("contract", "acquisition") == "open_only_increments_in_flight");
  CHECK(fixture.text("contract", "close_shutdown") == "stop_acquisition>fence_wake>drain>invalidate>release");
  CHECK(fixture.text("contract", "release_quiescence") == "release_complete_before_shutdown");
  CHECK(fixture.text("contract", "release") == "idempotent");
  CHECK(fixture.text("contract", "registration_ordinals") == "first_increasing_then_checked_successors");
  CHECK(fixture.text("contract", "bootstrap_capacity") == "installed_plus_pending_le_64");
  CHECK(fixture.text("contract", "callback_reserved") == "zero_required");
  CHECK(fixture.text("contract", "session_store_bound") == "fence_before_exceeding");
  CHECK(
      fixture.text("contract", "session_store_semantics") == "exact_bytes>replacement_delta>reset_reuse>retained_get");
  CHECK(fixture.text("contract", "output_publication") == "zero_written>validate>stage>copy>publish_written");
  CHECK(fixture.text("contract", "registration_rejection") == "fence_after_consumed_batch");
  CHECK(fixture.text("contract", "wait_pair") == "AT_HEAD+OPEN|STREAM_FENCED+CLOSING");
  CHECK(fixture.text("contract", "close_identity") == "exact_tombstone_only_until_shutdown");
  CHECK(fixture.text("contract", "session_identity") == "process_global_single_owner");
  CHECK(fixture.text("contract", "bootstrap_acceptance") == "logged_on+connected+dictionary_valid");
  CHECK(fixture.text("contract", "dictionaries") == "approved_installed_immutable_only");
  CHECK(fixture.text("contract", "lifecycle_callback_failure") == "CLOSING+terminal_error+waiters_woken");
  CHECK(fixture.text("contract", "bootstrap_credentials") == "callback_only>scrub_before_session");
  CHECK(fixture.text("contract", "bootstrap_observation") == "connection_clock_baseline");
  CHECK(fixture.text("contract", "input_output_alias") == "reject_before_output_mutation");
  CHECK(fixture.text("contract", "capability_retirement") == "fresh_adapter_handles+live_external_mapping");
  CHECK(fixture.text("contract", "pending_plan_bound") == "one_per_connection");
  CHECK(fixture.text("contract", "candidate_storage") == "move_after_validation");
  CHECK(fixture.number("constant", "abi_version") == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1);
  CHECK(fixture.number("constant", "required_capabilities") == IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1);
  CHECK(fixture.number("constant", "max_connections") == IRFQ_INFINITE_MAX_CONNECTIONS_V1);
  CHECK(fixture.number("constant", "max_batch_frames") == IRFQ_INFINITE_MAX_BATCH_FRAMES_V1);
  CHECK(fixture.number("constant", "max_frame_bytes") == IRFQ_INFINITE_MAX_FRAME_BYTES_V1);
  CHECK(fixture.number("constant", "max_batch_bytes") == IRFQ_INFINITE_MAX_BATCH_BYTES_V1);
  CHECK(fixture.number("constant", "max_failure_bytes") == IRFQ_INFINITE_MAX_FAILURE_BYTES_V1);
  CHECK(fixture.number("constant", "dispatch_output_capacity") == IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1);
  CHECK(fixture.number("constant", "max_session_store_messages") == 256);
  CHECK(fixture.number("constant", "max_session_store_bytes") == UINT64_C(16777216));

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
  std::atomic<bool> registrationFenceWake{false};
  std::thread registrationWaiter([&]() {
    std::unique_lock<std::mutex> lock(context.mutex);
    context.condition.wait(lock, [&context]() { return context.fenced; });
    registrationFenceWake.store(true, std::memory_order_release);
  });
  auto firstBytes = dispatchOutput();
  CHECK(
      irfq_infinite_connection_dispatch_v1(first, &firstDispatch, firstBytes.data(), firstBytes.size())
      == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  registrationWaiter.join();
  CHECK(registrationFenceWake.load(std::memory_order_acquire));
  CHECK(context.sameHandleReentryStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.crossConnectionStatus == IRFQ_INFINITE_STATUS_OK_V1);
  CHECK(context.fenced);
  CHECK(context.fenceCount == 1);

  context.reenterSameHandle = false;
  context.callCrossConnection = false;
  context.fenced = false;
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

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][release-quiescence]") {
  CallbackContext context;
  context.blockRelease = true;
  const auto initialized = initializeEngine(context);
  const auto bootstrapped = bootstrapConnection(initialized.engine, "RELEASEBOUND", 3000);
  REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);

  std::thread closing([&]() { closeConnection(bootstrapped.response.connection); });
  {
    std::unique_lock<std::mutex> lock(context.mutex);
    context.condition.wait(lock, [&context]() { return context.releaseEntered; });
  }

  std::atomic<bool> shutdownReturned{false};
  std::thread shuttingDown([&]() {
    shutdownEngine(initialized.engine);
    shutdownReturned.store(true, std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_FALSE(shutdownReturned.load(std::memory_order_acquire));

  {
    std::lock_guard<std::mutex> lock(context.mutex);
    context.allowRelease = true;
  }
  context.condition.notify_all();
  closing.join();
  shuttingDown.join();
  CHECK(shutdownReturned.load(std::memory_order_acquire));
  CHECK(context.releaseCount == 1);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][bootstrap-capacity]") {
  CallbackContext context;
  context.blockBootstrap = true;
  const auto initialized = initializeEngine(context);
  constexpr std::size_t attempts = IRFQ_INFINITE_MAX_CONNECTIONS_V1 + 1;
  std::array<BootstrapResult, attempts> results{};
  std::vector<std::thread> threads;
  threads.reserve(attempts);
  for (std::size_t index = 0; index < attempts; ++index) {
    threads.emplace_back([&, index]() {
      results[index] = bootstrapConnection(initialized.engine, "PENDING" + std::to_string(index), 4000 + index * 2);
    });
  }
  bool capacityReached = false;
  {
    std::unique_lock<std::mutex> lock(context.mutex);
    capacityReached = context.condition.wait_for(lock, std::chrono::seconds(10), [&context]() {
      return context.bootstrapEntered >= IRFQ_INFINITE_MAX_CONNECTIONS_V1;
    });
    if (!capacityReached) {
      context.allowBootstrap = true;
    }
  }
  if (!capacityReached) {
    context.condition.notify_all();
    for (auto &thread : threads) {
      thread.join();
    }
    shutdownEngine(initialized.engine);
    REQUIRE(capacityReached);
    return;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  {
    std::lock_guard<std::mutex> lock(context.mutex);
    CHECK(context.bootstrapEntered == IRFQ_INFINITE_MAX_CONNECTIONS_V1);
    context.allowBootstrap = true;
  }
  context.condition.notify_all();
  for (auto &thread : threads) {
    thread.join();
  }

  std::size_t accepted = 0;
  std::size_t notReady = 0;
  for (const auto &result : results) {
    if (result.status == IRFQ_INFINITE_STATUS_OK_V1) {
      ++accepted;
      closeConnection(result.response.connection);
    } else if (result.status == IRFQ_INFINITE_STATUS_NOT_READY_V1) {
      ++notReady;
    }
  }
  CHECK(accepted == IRFQ_INFINITE_MAX_CONNECTIONS_V1);
  CHECK(notReady == 1);
  shutdownEngine(initialized.engine);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][callback-reserved]") {
  SECTION("bootstrap") {
    CallbackContext context;
    context.bootstrapReservedNonzero = true;
    const auto initialized = initializeEngine(context);
    const auto bootstrapped = bootstrapConnection(initialized.engine, "RESERVEDBOOT", 5000);
    CHECK(bootstrapped.status == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
    CHECK(context.fenceCount == 1);
    CHECK(context.releaseCount == 1);
    shutdownEngine(initialized.engine);
  }

#ifdef CLOCK_TAI
  const auto exercise = [](bool corruptWait, bool corruptAuthorize) {
    CallbackContext context;
    const auto initialized = initializeEngine(context);
    const auto bootstrapped = bootstrapConnection(initialized.engine, "RESERVEDCALLBACK", 5100);
    REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
    const auto heartbeat = finishFix("35=0\00134=2\00149=RESERVEDCALLBACK\00156=VENUE\00152=20260826-08:08:09.000\001");
    const irfq_infinite_dispatch_request_v1 dispatch{
        sizeof(dispatch),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {reinterpret_cast<const std::uint8_t *>(heartbeat.data()), heartbeat.size()},
        {}};
    auto bytes = dispatchOutput();
    REQUIRE(
        irfq_infinite_connection_dispatch_v1(bootstrapped.response.connection, &dispatch, bytes.data(), bytes.size())
        == IRFQ_INFINITE_STATUS_OK_V1);
    const auto *response = reinterpret_cast<const irfq_infinite_dispatch_response_v1 *>(bytes.data());
    const auto *registration
        = reinterpret_cast<const irfq_infinite_registration_result_v1 *>(bytes.data() + sizeof(*response));
    const irfq_infinite_head_request_v1 head{
        sizeof(head),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        registration->token,
        {}};
    context.waitReservedNonzero = corruptWait;
    auto waited = output<irfq_infinite_operation_response_v1>();
    const auto waitStatus
        = irfq_infinite_connection_wait_head_v1(bootstrapped.response.connection, &head, &waited, sizeof(waited));
    if (corruptWait) {
      CHECK(waitStatus == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
    } else {
      REQUIRE(waitStatus == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
      context.authorizeReservedNonzero = corruptAuthorize;
      auto classified = output<irfq_infinite_classification_response_v1>();
      CHECK(
          irfq_infinite_connection_classify_v1(bootstrapped.response.connection, &head, &classified, sizeof(classified))
          == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
    }
    CHECK(context.fenceCount == 1);
    closeConnection(bootstrapped.response.connection);
    shutdownEngine(initialized.engine);
  };
  SECTION("wait") { exercise(true, false); }
  SECTION("authorization") { exercise(false, true); }
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][ordinal-gap]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  context.gapRegistrationOrdinal = true;
  const auto initialized = initializeEngine(context);
  const auto bootstrapped = bootstrapConnection(initialized.engine, "ORDINALGAP", 6000);
  REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
  const auto first = finishFix("35=0\00134=2\00149=ORDINALGAP\00156=VENUE\00152=20260826-08:08:09.000\001");
  const auto second = finishFix("35=0\00134=3\00149=ORDINALGAP\00156=VENUE\00152=20260826-08:08:10.000\001");
  const auto batch = first + second;
  const irfq_infinite_dispatch_request_v1 dispatch{
      sizeof(dispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(batch.data()), batch.size()},
      {}};
  auto bytes = dispatchOutput();
  CHECK(
      irfq_infinite_connection_dispatch_v1(bootstrapped.response.connection, &dispatch, bytes.data(), bytes.size())
      == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
  CHECK(context.fenceCount == 1);
  closeConnection(bootstrapped.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][ordinal-wrap]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  context.nextOrdinal = std::numeric_limits<std::uint64_t>::max();
  const auto initialized = initializeEngine(context);
  const auto bootstrapped = bootstrapConnection(initialized.engine, "ORDINALWRAP", 6050);
  REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
  const auto first = finishFix("35=0\00134=2\00149=ORDINALWRAP\00156=VENUE\00152=20260826-08:08:09.000\001");
  const auto second = finishFix("35=0\00134=3\00149=ORDINALWRAP\00156=VENUE\00152=20260826-08:08:10.000\001");
  const auto batch = first + second;
  const irfq_infinite_dispatch_request_v1 dispatch{
      sizeof(dispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(batch.data()), batch.size()},
      {}};
  auto bytes = dispatchOutput();
  CHECK(
      irfq_infinite_connection_dispatch_v1(bootstrapped.response.connection, &dispatch, bytes.data(), bytes.size())
      == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
  const auto *response = reinterpret_cast<const irfq_infinite_dispatch_response_v1 *>(bytes.data());
  CHECK(response->header.written_length == 0);
  CHECK(response->result_count == 0);
  CHECK(context.registeredFrames.size() == 2);
  CHECK(context.fenceCount == 1);
  const irfq_infinite_head_request_v1 head{
      sizeof(head),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {UINT64_C(100), UINT64_C(1)},
      {}};
  auto waited = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_wait_head_v1(bootstrapped.response.connection, &head, &waited, sizeof(waited))
      == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  closeConnection(bootstrapped.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][reentry-fence]") {
#ifdef CLOCK_TAI
  const auto exercise = [](bool duringWait) {
    CallbackContext context;
    const auto initialized = initializeEngine(context);
    const auto sender = duringWait ? "REENTERWAIT" : "REENTERAUTH";
    const auto bootstrapped = bootstrapConnection(initialized.engine, sender, duringWait ? 7000 : 7100);
    REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
    context.adapterConnection = bootstrapped.response.connection;
    context.waitForFenceAfterReentry = true;
    const auto heartbeat
        = finishFix("35=0\00134=2\00149=" + std::string(sender) + "\00156=VENUE\00152=20260826-08:08:09.000\001");
    const irfq_infinite_dispatch_request_v1 dispatch{
        sizeof(dispatch),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {reinterpret_cast<const std::uint8_t *>(heartbeat.data()), heartbeat.size()},
        {}};
    auto bytes = dispatchOutput();
    REQUIRE(
        irfq_infinite_connection_dispatch_v1(bootstrapped.response.connection, &dispatch, bytes.data(), bytes.size())
        == IRFQ_INFINITE_STATUS_OK_V1);
    const auto *response = reinterpret_cast<const irfq_infinite_dispatch_response_v1 *>(bytes.data());
    const auto *registration
        = reinterpret_cast<const irfq_infinite_registration_result_v1 *>(bytes.data() + sizeof(*response));
    const irfq_infinite_head_request_v1 head{
        sizeof(head),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        registration->token,
        {}};
    context.reenterWait = duringWait;
    std::atomic<bool> fenceWake{false};
    std::thread fenceWaiter([&]() {
      std::unique_lock<std::mutex> lock(context.mutex);
      context.condition.wait(lock, [&context]() { return context.fenced; });
      fenceWake.store(true, std::memory_order_release);
    });
    auto waited = output<irfq_infinite_operation_response_v1>();
    const auto waitStatus
        = irfq_infinite_connection_wait_head_v1(bootstrapped.response.connection, &head, &waited, sizeof(waited));
    if (duringWait) {
      CHECK(waitStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
      CHECK(context.waitReentryStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
    } else {
      REQUIRE(waitStatus == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
      context.reenterAuthorize = true;
      auto classified = output<irfq_infinite_classification_response_v1>();
      CHECK(
          irfq_infinite_connection_classify_v1(bootstrapped.response.connection, &head, &classified, sizeof(classified))
          == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
      CHECK(context.authorizeReentryStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
    }
    fenceWaiter.join();
    CHECK(fenceWake.load(std::memory_order_acquire));
    CHECK(context.fenced);
    CHECK(context.fenceCount == 1);
    closeConnection(bootstrapped.response.connection);
    CHECK(context.fenceCount == 1);
    shutdownEngine(initialized.engine);
  };
  SECTION("wait") { exercise(true); }
  SECTION("authorization") { exercise(false); }
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][ancestry-cycle]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto first = bootstrapConnection(initialized.engine, "CYCLEA", 8000);
  const auto second = bootstrapConnection(initialized.engine, "CYCLEB", 8100);
  REQUIRE(first.status == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(second.status == IRFQ_INFINITE_STATUS_OK_V1);
  context.firstAdapterConnection = first.response.connection;
  context.secondAdapterConnection = second.response.connection;
  context.firstNestedFrame = finishFix("35=0\00134=2\00149=CYCLEA\00156=VENUE\00152=20260826-08:08:09.000\001");
  context.secondNestedFrame = finishFix("35=0\00134=2\00149=CYCLEB\00156=VENUE\00152=20260826-08:08:09.000\001");
  context.ancestryCycle = true;
  const irfq_infinite_dispatch_request_v1 dispatch{
      sizeof(dispatch),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      {reinterpret_cast<const std::uint8_t *>(context.firstNestedFrame.data()), context.firstNestedFrame.size()},
      {}};
  auto bytes = dispatchOutput();
  CHECK(
      irfq_infinite_connection_dispatch_v1(first.response.connection, &dispatch, bytes.data(), bytes.size())
      == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.secondNestedStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.firstNestedStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.fenceCount == 2);
  context.ancestryCycle = false;
  closeConnection(first.response.connection);
  closeConnection(second.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][callback-contention]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto first = bootstrapConnection(initialized.engine, "CONTENDERA", 9000);
  const auto second = bootstrapConnection(initialized.engine, "CONTENDERB", 9100);
  REQUIRE(first.status == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(second.status == IRFQ_INFINITE_STATUS_OK_V1);
  context.firstAdapterConnection = first.response.connection;
  context.secondAdapterConnection = second.response.connection;
  context.firstNestedFrame = finishFix("35=0\00134=2\00149=CONTENDERA\00156=VENUE\00152=20260826-08:08:09.000\001");
  context.secondNestedFrame = finishFix("35=0\00134=2\00149=CONTENDERB\00156=VENUE\00152=20260826-08:08:09.000\001");
  context.concurrentCrossCalls = true;
  std::array<irfq_infinite_status_v1, 2> statuses{};
  const auto run = [&](std::size_t index, irfq_infinite_handle_v1 connection, const std::string &frame) {
    const irfq_infinite_dispatch_request_v1 dispatch{
        sizeof(dispatch),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {reinterpret_cast<const std::uint8_t *>(frame.data()), frame.size()},
        {}};
    auto bytes = dispatchOutput();
    statuses[index] = irfq_infinite_connection_dispatch_v1(connection, &dispatch, bytes.data(), bytes.size());
  };
  std::thread firstDispatch(run, 0, first.response.connection, std::cref(context.firstNestedFrame));
  std::thread secondDispatch(run, 1, second.response.connection, std::cref(context.secondNestedFrame));
  firstDispatch.join();
  secondDispatch.join();
  CHECK((statuses[0] == IRFQ_INFINITE_STATUS_OK_V1 || statuses[0] == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1));
  CHECK((statuses[1] == IRFQ_INFINITE_STATUS_OK_V1 || statuses[1] == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1));
  CHECK((statuses[0] == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1 || statuses[1] == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1));
  CHECK(context.firstNestedStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.secondNestedStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.fenceCount == 2);
  context.concurrentCrossCalls = false;
  closeConnection(first.response.connection);
  closeConnection(second.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][callback-close-cycle]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto first = bootstrapConnection(initialized.engine, "CLOSECYCLEA", 9200);
  const auto second = bootstrapConnection(initialized.engine, "CLOSECYCLEB", 9300);
  REQUIRE(first.status == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(second.status == IRFQ_INFINITE_STATUS_OK_V1);
  context.firstAdapterConnection = first.response.connection;
  context.secondAdapterConnection = second.response.connection;
  context.firstNestedFrame = finishFix("35=0\00134=2\00149=CLOSECYCLEA\00156=VENUE\00152=20260826-08:08:09.000\001");
  context.secondNestedFrame = finishFix("35=0\00134=2\00149=CLOSECYCLEB\00156=VENUE\00152=20260826-08:08:09.000\001");
  context.concurrentCrossCloses = true;
  std::array<irfq_infinite_status_v1, 2> statuses{};
  const auto run = [&](std::size_t index, irfq_infinite_handle_v1 connection, const std::string &frame) {
    const irfq_infinite_dispatch_request_v1 dispatch{
        sizeof(dispatch),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {reinterpret_cast<const std::uint8_t *>(frame.data()), frame.size()},
        {}};
    auto bytes = dispatchOutput();
    statuses[index] = irfq_infinite_connection_dispatch_v1(connection, &dispatch, bytes.data(), bytes.size());
  };
  std::thread firstDispatch(run, 0, first.response.connection, std::cref(context.firstNestedFrame));
  std::thread secondDispatch(run, 1, second.response.connection, std::cref(context.secondNestedFrame));
  firstDispatch.join();
  secondDispatch.join();
  CHECK(context.firstCloseStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.secondCloseStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.concurrentNestedCompleted == 2);
  CHECK((statuses[0] == IRFQ_INFINITE_STATUS_OK_V1 || statuses[0] == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1));
  CHECK((statuses[1] == IRFQ_INFINITE_STATUS_OK_V1 || statuses[1] == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1));
  CHECK((statuses[0] == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1 || statuses[1] == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1));
  CHECK(context.fenceCount == 2);
  context.concurrentCrossCloses = false;
  closeConnection(first.response.connection);
  closeConnection(second.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][release-close-cycle]") {
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto first = bootstrapConnection(initialized.engine, "RELEASECYCLEA", 9400);
  const auto second = bootstrapConnection(initialized.engine, "RELEASECYCLEB", 9500);
  REQUIRE(first.status == IRFQ_INFINITE_STATUS_OK_V1);
  REQUIRE(second.status == IRFQ_INFINITE_STATUS_OK_V1);
  context.firstAdapterConnection = first.response.connection;
  context.secondAdapterConnection = second.response.connection;
  context.crossCloseOnRelease = true;
  std::thread firstClose([&]() { closeConnection(first.response.connection, 61); });
  std::thread secondClose([&]() { closeConnection(second.response.connection, 62); });
  firstClose.join();
  secondClose.join();
  CHECK(context.releaseCrossCallbacksEntered == 2);
  CHECK(
      (context.firstReleaseCloseStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
       || context.firstReleaseCloseStatus == IRFQ_INFINITE_STATUS_CLOSED_V1));
  CHECK(
      (context.secondReleaseCloseStatus == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
       || context.secondReleaseCloseStatus == IRFQ_INFINITE_STATUS_CLOSED_V1));
  CHECK(context.releaseCount == 2);
  shutdownEngine(initialized.engine);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][store-bound]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto bootstrapped = bootstrapConnection(initialized.engine, "STOREBOUND", 10000);
  REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);

  const auto applyTestRequest = [&](std::uint64_t sequence) {
    const auto message = finishFix(
        "35=1\00134=" + std::to_string(sequence)
        + "\00149=STOREBOUND\00156=VENUE\00152=20260826-08:08:09.000\001112=BOUND\001");
    const irfq_infinite_dispatch_request_v1 dispatch{
        sizeof(dispatch),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {reinterpret_cast<const std::uint8_t *>(message.data()), message.size()},
        {}};
    auto bytes = dispatchOutput();
    const auto dispatchStatus
        = irfq_infinite_connection_dispatch_v1(bootstrapped.response.connection, &dispatch, bytes.data(), bytes.size());
    if (dispatchStatus != IRFQ_INFINITE_STATUS_OK_V1) {
      return dispatchStatus;
    }
    const auto *response = reinterpret_cast<const irfq_infinite_dispatch_response_v1 *>(bytes.data());
    const auto *registration
        = reinterpret_cast<const irfq_infinite_registration_result_v1 *>(bytes.data() + sizeof(*response));
    const irfq_infinite_head_request_v1 head{
        sizeof(head),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        registration->token,
        {}};
    auto waited = output<irfq_infinite_operation_response_v1>();
    const auto waitStatus
        = irfq_infinite_connection_wait_head_v1(bootstrapped.response.connection, &head, &waited, sizeof(waited));
    if (waitStatus != IRFQ_INFINITE_STATUS_AT_HEAD_V1) {
      return waitStatus;
    }
    auto classified = output<irfq_infinite_classification_response_v1>();
    const auto classifyStatus = irfq_infinite_connection_classify_v1(
        bootstrapped.response.connection,
        &head,
        &classified,
        sizeof(classified));
    if (classifyStatus != IRFQ_INFINITE_STATUS_CLASSIFIED_V1) {
      return classifyStatus;
    }
    const irfq_infinite_apply_request_v1 apply{
        sizeof(apply),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        classified.classification,
        classified.authorization,
        {}};
    auto applied = output<irfq_infinite_operation_response_v1>();
    return irfq_infinite_connection_apply_v1(bootstrapped.response.connection, &apply, &applied, sizeof(applied));
  };

  for (std::uint64_t sequence = 2; sequence <= 256; ++sequence) {
    INFO("sequence=" << sequence);
    REQUIRE(applyTestRequest(sequence) == IRFQ_INFINITE_STATUS_APPLIED_V1);
  }
  CHECK(applyTestRequest(257) == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  CHECK(context.fenceCount == 1);
  closeConnection(bootstrapped.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][store-byte-accounting]") {
  using FIX::INFINITE_MAX_PLANNED_BYTES;
  using FIX::infinite_frame_adapter_detail::BoundedMemoryStore;
  const auto now = FIX::UtcTimeStamp::now();
  BoundedMemoryStore store(now);

  SECTION("exact byte bound retains exact bytes for resend reads and rejects one over") {
    const std::string exact(INFINITE_MAX_PLANNED_BYTES, '\x5a');
    REQUIRE(store.set(1, exact));
    CHECK_THROWS(store.set(2, "x"));
    std::vector<std::string> retained;
    store.get(1, 2, retained);
    REQUIRE(retained.size() == 1);
    CHECK(retained.front().size() == INFINITE_MAX_PLANNED_BYTES);
    CHECK(retained.front() == exact);
  }

  SECTION("replacement growth and shrink use byte deltas") {
    constexpr std::size_t originalSmall = 1024;
    const std::string first(originalSmall, '\x11');
    const std::string second(INFINITE_MAX_PLANNED_BYTES - originalSmall, '\x22');
    REQUIRE(store.set(1, first));
    REQUIRE(store.set(2, second));
    CHECK_THROWS(store.set(1, std::string(originalSmall + 1, '\x33')));
    std::vector<std::string> beforeShrink;
    store.get(1, 2, beforeShrink);
    REQUIRE(beforeShrink.size() == 2);
    CHECK(beforeShrink[0] == first);
    CHECK(beforeShrink[1] == second);

    constexpr std::size_t replacementSmall = 512;
    const std::string shrunk(replacementSmall, '\x44');
    const std::string expanded(INFINITE_MAX_PLANNED_BYTES - replacementSmall, '\x55');
    REQUIRE(store.set(1, shrunk));
    REQUIRE(store.set(2, expanded));
    std::vector<std::string> retained;
    store.get(1, 2, retained);
    REQUIRE(retained.size() == 2);
    CHECK(retained[0] == shrunk);
    CHECK(retained[1] == expanded);
  }

  SECTION("reset clears accounting and permits exact-bound reuse") {
    REQUIRE(store.set(1, std::string(INFINITE_MAX_PLANNED_BYTES, '\x66')));
    store.reset(now);
    const std::string reused(INFINITE_MAX_PLANNED_BYTES, '\x77');
    REQUIRE(store.set(1, reused));
    std::vector<std::string> retained;
    store.get(1, 2, retained);
    REQUIRE(retained.size() == 1);
    CHECK(retained.front() == reused);
  }
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][registration-rejection-fences]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto bootstrapped = bootstrapConnection(initialized.engine, "REGREJECT", 11000);
  REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
  context.registrationNotRegistered = true;
  const auto heartbeat = finishFix("35=0\00134=2\00149=REGREJECT\00156=VENUE\00152=20260826-08:08:09.000\001");
  const auto rejected = dispatchFrame(bootstrapped.response.connection, heartbeat);
  CHECK(rejected.status == IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1);
  CHECK(rejected.count == 0);
  CHECK(context.fenceCount == 1);
  CHECK(dispatchFrame(bootstrapped.response.connection, heartbeat).status == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
  closeConnection(bootstrapped.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][wait-status-lifecycle-pair]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto bootstrapped = bootstrapConnection(initialized.engine, "WAITPAIR", 11100);
  REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
  const auto heartbeat = finishFix("35=0\00134=2\00149=WAITPAIR\00156=VENUE\00152=20260826-08:08:09.000\001");
  const auto registered = dispatchFrame(bootstrapped.response.connection, heartbeat);
  REQUIRE(registered.status == IRFQ_INFINITE_STATUS_OK_V1);
  context.wrongWaitLifecycle = true;
  CHECK(
      waitAtHead(bootstrapped.response.connection, registered.registration.token)
      == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
  CHECK(context.fenceCount == 1);
  closeConnection(bootstrapped.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][close-handle-identity]") {
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto bootstrapped = bootstrapConnection(initialized.engine, "CLOSEIDENTITY", 11200);
  REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
  CHECK(
      (bootstrapped.response.connection.object != initialized.engine.object
       || bootstrapped.response.connection.generation != initialized.engine.generation));
  const irfq_infinite_close_request_v1 request{
      sizeof(request),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      UINT32_C(73),
      {}};
  auto response = output<irfq_infinite_operation_response_v1>();
  REQUIRE(
      irfq_infinite_connection_close_v1(bootstrapped.response.connection, &request, &response, sizeof(response))
      == IRFQ_INFINITE_STATUS_CLOSED_V1);
  response = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(bootstrapped.response.connection, &request, &response, sizeof(response))
      == IRFQ_INFINITE_STATUS_CLOSED_V1);
  CHECK(response.lifecycle == IRFQ_INFINITE_CONNECTION_CLOSED_V1);

  auto forged = bootstrapped.response.connection;
  ++forged.generation;
  response = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(forged, &request, &response, sizeof(response))
      == IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1);
  CHECK(response.lifecycle != IRFQ_INFINITE_CONNECTION_CLOSED_V1);

  const irfq_infinite_handle_v1 forgedLow{bootstrapped.response.connection.object, initialized.engine.generation};
  response = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(forgedLow, &request, &response, sizeof(response))
      == IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1);
  CHECK(response.lifecycle != IRFQ_INFINITE_CONNECTION_CLOSED_V1);

  auto invalid = request;
  invalid.reserved[0] = 1;
  response = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(bootstrapped.response.connection, &invalid, &response, sizeof(response))
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
  shutdownEngine(initialized.engine);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][sequential-connection-capacity]") {
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  irfq_infinite_handle_v1 first{};
  irfq_infinite_handle_v1 last{};
  for (std::uint64_t index = 0; index <= IRFQ_INFINITE_MAX_CONNECTIONS_V1; ++index) {
    const auto bootstrapped
        = bootstrapConnection(initialized.engine, "SERIAL" + std::to_string(index), UINT64_C(13000) + index * 2);
    REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
    if (index == 0) {
      first = bootstrapped.response.connection;
    }
    last = bootstrapped.response.connection;
    REQUIRE(nestedClose(last, UINT32_C(81)) == IRFQ_INFINITE_STATUS_CLOSED_V1);
  }

  const irfq_infinite_close_request_v1 request{
      sizeof(request),
      IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
      UINT32_C(82),
      {}};
  auto response = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(first, &request, &response, sizeof(response))
      == IRFQ_INFINITE_STATUS_CLOSED_V1);
  auto future = last;
  ++future.generation;
  response = output<irfq_infinite_operation_response_v1>();
  CHECK(
      irfq_infinite_connection_close_v1(future, &request, &response, sizeof(response))
      == IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1);
  shutdownEngine(initialized.engine);
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][bootstrap-session-validation]") {
  SECTION("required dictionary validation rejects an incomplete logon") {
    CallbackContext context;
    const auto initialized = initializeEngine(context);
    const auto missingHeartbeat
        = finishFix("35=A\00134=1\00149=MISSINGHEARTBEAT\00156=VENUE\00152=20260826-08:08:08.000\00198=0\001");
    const auto result = bootstrapFrame(initialized.engine, missingHeartbeat, 11300);
    CHECK(result.status == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
    CHECK(context.fenceCount == 1);
    CHECK(context.releaseCount == 1);
    shutdownEngine(initialized.engine);
  }

  SECTION("dictionary enum validation rejects invalid encryption method") {
    CallbackContext context;
    const auto initialized = initializeEngine(context);
    const auto invalidEnum
        = finishFix("35=A\00134=1\00149=INVALIDENUM\00156=VENUE\00152=20260826-08:08:08.000\00198=999\001108=30\001");
    const auto result = bootstrapFrame(initialized.engine, invalidEnum, 11400);
    CHECK(result.status == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
    CHECK(context.fenceCount == 1);
    CHECK(context.releaseCount == 1);
    shutdownEngine(initialized.engine);
  }

  SECTION("a process-global session identity has one owner") {
    CallbackContext firstContext;
    CallbackContext secondContext;
    const auto firstEngine = initializeEngine(firstContext);
    const auto secondEngine = initializeEngine(secondContext);
    const auto first = bootstrapConnection(firstEngine.engine, "SINGLEOWNER", 11500);
    REQUIRE(first.status == IRFQ_INFINITE_STATUS_OK_V1);
    const auto duplicate = bootstrapConnection(secondEngine.engine, "SINGLEOWNER", 11600);
    CHECK(duplicate.status == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
    CHECK(secondContext.fenceCount == 1);
    CHECK(secondContext.releaseCount == 1);
    closeConnection(first.response.connection);
    shutdownEngine(firstEngine.engine);
    shutdownEngine(secondEngine.engine);
  }

  SECTION("credentials are visible to the synchronous bootstrap authority") {
    CallbackContext context;
    const auto initialized = initializeEngine(context);
    const auto credentialLogon = finishFix(
        "35=A\00134=1\00149=CREDENTIALS\00156=VENUE\00152=20260826-08:08:08.000\00198=0\001108=30\001"
        "553=adapter-user\001554=adapter-password\001");
    const auto result = bootstrapFrame(initialized.engine, credentialLogon, 11700);
    REQUIRE(result.status == IRFQ_INFINITE_STATUS_OK_V1);
    CHECK(context.credentialsObserved);
    closeConnection(result.response.connection);
    shutdownEngine(initialized.engine);
  }
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][lifecycle-callback-failure]") {
  const auto exercise = [](bool releaseFailure) {
    CallbackContext context;
    const auto initialized = initializeEngine(context);
    const auto bootstrapped
        = bootstrapConnection(initialized.engine, releaseFailure ? "RELEASEFAILURE" : "FENCEFAILURE", 11800);
    REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
    if (releaseFailure) {
      context.releaseThrows = true;
    } else {
      context.fenceAcknowledgement = IRFQ_INFINITE_STATUS_OK_V1;
    }
    const irfq_infinite_close_request_v1 request{
        sizeof(request),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        UINT32_C(79),
        {}};
    auto response = output<irfq_infinite_operation_response_v1>();
    CHECK(
        irfq_infinite_connection_close_v1(bootstrapped.response.connection, &request, &response, sizeof(response))
        == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
    CHECK(response.header.status == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
    CHECK(response.lifecycle == IRFQ_INFINITE_CONNECTION_CLOSING_V1);
    auto dispatch = dispatchOutput();
    const irfq_infinite_dispatch_request_v1 empty{
        sizeof(empty),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {nullptr, 0},
        {}};
    CHECK(
        irfq_infinite_connection_dispatch_v1(bootstrapped.response.connection, &empty, dispatch.data(), dispatch.size())
        == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
    response = output<irfq_infinite_operation_response_v1>();
    CHECK(
        irfq_infinite_connection_close_v1(bootstrapped.response.connection, &request, &response, sizeof(response))
        == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
    auto shutdown = output<irfq_infinite_operation_response_v1>();
    CHECK(
        irfq_infinite_engine_shutdown_v1(initialized.engine, &shutdown, sizeof(shutdown))
        == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1);
    CHECK(shutdown.lifecycle == IRFQ_INFINITE_ENGINE_CLOSING_V1);

    CallbackContext replacementContext;
    const auto replacementEngine = initializeEngine(replacementContext);
    const auto replacement = bootstrapConnection(
        replacementEngine.engine,
        releaseFailure ? "RELEASEFAILURE" : "FENCEFAILURE",
        UINT64_C(11850));
    REQUIRE(replacement.status == IRFQ_INFINITE_STATUS_OK_V1);
    closeConnection(replacement.response.connection);
    shutdownEngine(replacementEngine.engine);
  };
  SECTION("wrong fence acknowledgement") { exercise(false); }
  SECTION("throwing release callback") { exercise(true); }
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][bootstrap-observation-baseline]") {
#ifdef CLOCK_TAI
  CallbackContext context;
  const auto initialized = initializeEngine(context);
  const auto logon
      = finishFix("35=A\00134=1\00149=OBSBASE\00156=VENUE\00152=20260826-08:08:08.000\00198=0\001108=30\001");
  const auto bootstrapped = bootstrapFrame(initialized.engine, logon, 11900, std::numeric_limits<std::int64_t>::max());
  REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
  const auto heartbeat = finishFix("35=0\00134=2\00149=OBSBASE\00156=VENUE\00152=20260826-08:08:09.000\001");
  const auto result = dispatchFrame(bootstrapped.response.connection, heartbeat);
  CHECK(result.status == IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1);
  CHECK(result.fault == IRFQ_INFINITE_DISPATCH_FAULT_INVALID_OBSERVATION_V1);
  CHECK(context.fenceCount == 1);
  closeConnection(bootstrapped.response.connection);
  shutdownEngine(initialized.engine);
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][input-output-alias]") {
  SECTION("request and output overlap") {
    alignas(8) std::array<std::uint8_t, IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1> bytes{};
    auto *request = reinterpret_cast<irfq_infinite_dispatch_request_v1 *>(bytes.data());
    request->structure_size = sizeof(*request);
    request->abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
    request->input = {nullptr, UINT64_C(17)};
    const auto before = bytes;
    CHECK(
        irfq_infinite_connection_dispatch_v1({UINT64_C(9), UINT64_C(9)}, request, bytes.data(), bytes.size())
        == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
    CHECK(bytes == before);
  }

#ifdef CLOCK_TAI
  SECTION("input and output overlap") {
    CallbackContext context;
    const auto initialized = initializeEngine(context);
    const auto bootstrapped = bootstrapConnection(initialized.engine, "SLICEALIAS", 12000);
    REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
    auto bytes = dispatchOutput();
    const auto before = bytes.bytes;
    const irfq_infinite_dispatch_request_v1 request{
        sizeof(request),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {bytes.data(), sizeof(irfq_infinite_dispatch_response_v1)},
        {}};
    CHECK(
        irfq_infinite_connection_dispatch_v1(bootstrapped.response.connection, &request, bytes.data(), bytes.size())
        == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1);
    CHECK(bytes.bytes == before);
    closeConnection(bootstrapped.response.connection);
    shutdownEngine(initialized.engine);
  }
#endif
}

TEST_CASE("InfiniteFrameAdapterTests", "[infinite][adapter][capability-retirement-and-plan-bound]") {
#ifdef CLOCK_TAI
  SECTION("reused external gate tokens receive fresh adapter handles") {
    CallbackContext context;
    context.reuseRegistrationToken = true;
    const auto initialized = initializeEngine(context);
    const auto bootstrapped = bootstrapConnection(initialized.engine, "TOKRETIRE", 12100);
    REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
    const auto first = dispatchFrame(
        bootstrapped.response.connection,
        finishFix("35=0\00134=2\00149=TOKRETIRE\00156=VENUE\00152=20260826-08:08:09.000\001"));
    REQUIRE(first.status == IRFQ_INFINITE_STATUS_OK_V1);
    REQUIRE(waitAtHead(bootstrapped.response.connection, first.registration.token) == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
    const auto firstClassification = classifyAtHead(bootstrapped.response.connection, first.registration.token);
    REQUIRE(firstClassification.first == IRFQ_INFINITE_STATUS_CLASSIFIED_V1);
    REQUIRE(
        applyClassification(bootstrapped.response.connection, firstClassification.second)
        == IRFQ_INFINITE_STATUS_APPLIED_V1);
    const auto second = dispatchFrame(
        bootstrapped.response.connection,
        finishFix("35=0\00134=3\00149=TOKRETIRE\00156=VENUE\00152=20260826-08:08:10.000\001"));
    REQUIRE(second.status == IRFQ_INFINITE_STATUS_OK_V1);
    CHECK(
        (second.registration.token.object != first.registration.token.object
         || second.registration.token.generation != first.registration.token.generation));
    REQUIRE(waitAtHead(bootstrapped.response.connection, second.registration.token) == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
    const auto secondClassification = classifyAtHead(bootstrapped.response.connection, second.registration.token);
    REQUIRE(secondClassification.first == IRFQ_INFINITE_STATUS_CLASSIFIED_V1);
    CHECK(
        applyClassification(bootstrapped.response.connection, secondClassification.second)
        == IRFQ_INFINITE_STATUS_APPLIED_V1);
    CHECK(context.fenceCount == 0);
    closeConnection(bootstrapped.response.connection);
    shutdownEngine(initialized.engine);
  }

  SECTION("reused external authorizations receive fresh adapter handles") {
    CallbackContext context;
    context.reuseAuthorization = true;
    const auto initialized = initializeEngine(context);
    const auto bootstrapped = bootstrapConnection(initialized.engine, "AUTHRETIRE", 12200);
    REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
    const auto first = dispatchFrame(
        bootstrapped.response.connection,
        finishFix("35=0\00134=2\00149=AUTHRETIRE\00156=VENUE\00152=20260826-08:08:09.000\001"));
    REQUIRE(waitAtHead(bootstrapped.response.connection, first.registration.token) == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
    const auto firstClassification = classifyAtHead(bootstrapped.response.connection, first.registration.token);
    REQUIRE(firstClassification.first == IRFQ_INFINITE_STATUS_CLASSIFIED_V1);
    REQUIRE(
        applyClassification(bootstrapped.response.connection, firstClassification.second)
        == IRFQ_INFINITE_STATUS_APPLIED_V1);
    const auto second = dispatchFrame(
        bootstrapped.response.connection,
        finishFix("35=0\00134=3\00149=AUTHRETIRE\00156=VENUE\00152=20260826-08:08:10.000\001"));
    REQUIRE(second.status == IRFQ_INFINITE_STATUS_OK_V1);
    REQUIRE(waitAtHead(bootstrapped.response.connection, second.registration.token) == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
    const auto secondClassification = classifyAtHead(bootstrapped.response.connection, second.registration.token);
    REQUIRE(secondClassification.first == IRFQ_INFINITE_STATUS_CLASSIFIED_V1);
    CHECK(
        (secondClassification.second.authorization.object != firstClassification.second.authorization.object
         || secondClassification.second.authorization.generation
                != firstClassification.second.authorization.generation));
    CHECK(
        applyClassification(bootstrapped.response.connection, secondClassification.second)
        == IRFQ_INFINITE_STATUS_APPLIED_V1);
    CHECK(context.fenceCount == 0);
    closeConnection(bootstrapped.response.connection);
    shutdownEngine(initialized.engine);
  }

  SECTION("only one deep-owned classification may be pending") {
    CallbackContext context;
    const auto initialized = initializeEngine(context);
    const auto bootstrapped = bootstrapConnection(initialized.engine, "ONEPLAN", 12300);
    REQUIRE(bootstrapped.status == IRFQ_INFINITE_STATUS_OK_V1);
    const auto first = finishFix("35=0\00134=2\00149=ONEPLAN\00156=VENUE\00152=20260826-08:08:09.000\001");
    const auto second = finishFix("35=0\00134=3\00149=ONEPLAN\00156=VENUE\00152=20260826-08:08:10.000\001");
    const auto batch = first + second;
    const irfq_infinite_dispatch_request_v1 dispatch{
        sizeof(dispatch),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        {reinterpret_cast<const std::uint8_t *>(batch.data()), batch.size()},
        {}};
    auto bytes = dispatchOutput();
    REQUIRE(
        irfq_infinite_connection_dispatch_v1(bootstrapped.response.connection, &dispatch, bytes.data(), bytes.size())
        == IRFQ_INFINITE_STATUS_OK_V1);
    const auto *response = reinterpret_cast<const irfq_infinite_dispatch_response_v1 *>(bytes.data());
    REQUIRE(response->result_count == 2);
    const auto *registrations
        = reinterpret_cast<const irfq_infinite_registration_result_v1 *>(bytes.data() + sizeof(*response));
    REQUIRE(waitAtHead(bootstrapped.response.connection, registrations[0].token) == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
    REQUIRE(waitAtHead(bootstrapped.response.connection, registrations[1].token) == IRFQ_INFINITE_STATUS_AT_HEAD_V1);
    REQUIRE(
        classifyAtHead(bootstrapped.response.connection, registrations[0].token).first
        == IRFQ_INFINITE_STATUS_CLASSIFIED_V1);
    CHECK(
        classifyAtHead(bootstrapped.response.connection, registrations[1].token).first
        == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1);
    CHECK(context.fenceCount == 1);
    closeConnection(bootstrapped.response.connection);
    shutdownEngine(initialized.engine);
  }
#endif
}
