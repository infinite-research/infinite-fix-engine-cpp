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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace AdapterAllocationProbe {
thread_local bool armed = false;
thread_local bool acknowledged = false;
std::mutex mutex;
std::condition_variable condition;
unsigned arrived = 0;

void reset() {
  std::lock_guard<std::mutex> lock(mutex);
  arrived = 0;
}

void arm() noexcept {
  armed = true;
  acknowledged = false;
}

void disarm() noexcept { armed = false; }

void onAllocation() {
  if (!armed || acknowledged) {
    return;
  }
  acknowledged = true;
  std::unique_lock<std::mutex> lock(mutex);
  ++arrived;
  condition.notify_all();
  condition.wait(lock, [] { return arrived == 2; });
}
} // namespace AdapterAllocationProbe

void *operator new(std::size_t size) {
  AdapterAllocationProbe::onAllocation();
  if (void *memory = std::malloc(size == 0 ? 1 : size)) {
    return memory;
  }
  throw std::bad_alloc();
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}

void operator delete(void *memory) noexcept { std::free(memory); }

void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

void operator delete(void *memory, const std::nothrow_t &) noexcept { std::free(memory); }

namespace {
static_assert(sizeof(irfq_infinite_session_create_request_v2) == 232);
static_assert(sizeof(irfq_infinite_prepare_request_v2) == 96);
static_assert(sizeof(irfq_infinite_prepare_response_v2) == 504);
static_assert(sizeof(irfq_infinite_resume_request_v2) == 80);
static_assert(offsetof(irfq_infinite_prepare_response_v2, actions) == 168);
static_assert(offsetof(irfq_infinite_prepare_response_v2, application_message_type_length) == 496);

std::string finishFix(std::string body) {
  std::string message = "8=FIXT.1.1\0019=" + std::to_string(body.size()) + "\001" + std::move(body) + "10=000\001";
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

std::string frame(char type, std::uint64_t sequence, std::string fields = {}) {
  return finishFix(
      "35=" + std::string(1, type) + "\00149=CLIENT\00156=VENUE\00134=" + std::to_string(sequence)
      + "\00152=20260828-12:00:00.000000\001" + std::move(fields));
}

std::string outboundFrame(char type, std::uint64_t sequence, std::string fields = {}) {
  return finishFix(
      "35=" + std::string(1, type) + "\00149=VENUE\00156=CLIENT\00134=" + std::to_string(sequence)
      + "\00152=20260828-12:00:00.000000\001" + std::move(fields));
}

std::string frameWithSize(std::size_t wanted) {
  std::size_t payloadSize = wanted - 128;
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    auto result = frame('0', 1, "58=" + std::string(payloadSize, 'X') + "\001");
    if (result.size() == wanted) {
      return result;
    }
    if (result.size() < wanted) {
      payloadSize += wanted - result.size();
    } else {
      payloadSize -= result.size() - wanted;
    }
  }
  throw std::logic_error("unable to construct sized FIX frame");
}

irfq_infinite_slice_v2 slice(const std::string &value) {
  return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

std::string hex(const std::uint8_t *bytes, std::size_t length) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result(length * 2, '0');
  for (std::size_t index = 0; index < length; ++index) {
    result[index * 2] = digits[bytes[index] >> 4];
    result[index * 2 + 1] = digits[bytes[index] & 0x0f];
  }
  return result;
}

void write64(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint64_t value) {
  REQUIRE(offset + 8 <= bytes.size());
  for (unsigned index = 0; index < 8; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> ((7 - index) * 8));
  }
}

void write32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
  REQUIRE(offset + 4 <= bytes.size());
  for (unsigned index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> ((3 - index) * 8));
  }
}

template <typename T> void init(T &value) {
  value = {};
  value.header.structure_size = sizeof(value);
  value.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2;
}

struct ResultStorage {
  std::array<std::uint8_t, IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2> state{};
  std::array<std::uint8_t, IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2> output{};

  irfq_infinite_prepare_response_v2 response() {
    irfq_infinite_prepare_response_v2 result{};
    init(result);
    result.native_state = {state.data(), state.size(), 0};
    result.output = {output.data(), output.size(), 0};
    return result;
  }
};

struct NativeResult {
  irfq_infinite_prepare_response_v2 response{};
  std::vector<std::uint8_t> state;
  std::vector<std::uint8_t> output;
};

NativeResult copyResult(const irfq_infinite_prepare_response_v2 &response) {
  return {
      response,
      {response.native_state.data, response.native_state.data + response.native_state.length},
      {response.output.data, response.output.data + response.output.length}};
}

irfq_infinite_session_v2 *createSession(
    const std::vector<std::uint8_t> &state = {},
    std::uint64_t revision = 0,
    std::uint64_t epoch = 1) {
  const std::string begin = "FIXT.1.1";
  const std::string sender = "CLIENT";
  const std::string target = "VENUE";
  const std::string qualifier;
  irfq_infinite_session_create_request_v2 request{};
  init(request);
  request.begin_string = slice(begin);
  request.sender_comp_id = slice(sender);
  request.target_comp_id = slice(target);
  request.session_qualifier = slice(qualifier);
  request.native_state = {state.data(), state.size()};
  request.session_epoch = epoch;
  request.cache_revision = revision;
  request.creation_tai_ns = 1;
  request.snapshot_codec_version = 1;
  request.default_application_version = 10;
  request.session_policy_flags = IRFQ_INFINITE_SESSION_POLICY_VALIDATE_LENGTH_CHECKSUM_V2;
  std::fill_n(request.transport_dictionary_sha256, 32, std::uint8_t{0x11});
  std::fill_n(request.application_dictionary_sha256, 32, std::uint8_t{0x22});
  std::fill_n(request.authenticated_session_binding_sha256, 32, std::uint8_t{0x33});
  irfq_infinite_session_create_response_v2 response{};
  init(response);
  REQUIRE(irfq_infinite_session_create_v2(&request, &response) == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(response.session != nullptr);
  REQUIRE(response.cache_revision == revision);
  return response.session;
}

struct CreateFixture {
  std::string begin{"FIXT.1.1"};
  std::string sender{"CLIENT"};
  std::string target{"VENUE"};
  std::string qualifier;
  std::vector<std::uint8_t> state;
  irfq_infinite_session_create_request_v2 request{};
  irfq_infinite_session_create_response_v2 response{};

  CreateFixture() {
    init(request);
    request.begin_string = slice(begin);
    request.sender_comp_id = slice(sender);
    request.target_comp_id = slice(target);
    request.session_qualifier = slice(qualifier);
    request.native_state = {nullptr, 0};
    request.session_epoch = 1;
    request.creation_tai_ns = 1;
    request.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
    request.default_application_version = IRFQ_INFINITE_APPLICATION_VERSION_FIX_LATEST_V2;
    request.session_policy_flags = IRFQ_INFINITE_SESSION_POLICY_VALIDATE_LENGTH_CHECKSUM_V2;
    std::fill_n(request.transport_dictionary_sha256, 32, std::uint8_t{0x11});
    std::fill_n(request.application_dictionary_sha256, 32, std::uint8_t{0x22});
    std::fill_n(request.authenticated_session_binding_sha256, 32, std::uint8_t{0x33});
    init(response);
  }

  void refreshViews() {
    request.begin_string = slice(begin);
    request.sender_comp_id = slice(sender);
    request.target_comp_id = slice(target);
    request.session_qualifier = slice(qualifier);
    request.native_state = {state.data(), state.size()};
  }
};

NativeResult prepare(
    irfq_infinite_session_v2 *session,
    irfq_infinite_prepare_kind_v2 kind,
    irfq_infinite_stage_v2 stage,
    std::uint64_t revision,
    const std::string &payload,
    irfq_infinite_status_v2 expected) {
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = kind;
  request.stage = stage;
  request.expected_session_epoch = 1;
  request.expected_revision = revision;
  request.now_tai_ns = 2 + revision;
  if (kind == IRFQ_INFINITE_PREPARE_RUST_TIMER_V2) {
    request.event_code = IRFQ_INFINITE_TIMER_HEARTBEAT_DUE_V2;
    request.event_identity = revision + 1;
  } else if (kind == IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2) {
    request.event_code = IRFQ_INFINITE_CONTROL_ADVANCE_STAGE_V2;
    request.event_identity = revision + 1;
  }
  request.payload = slice(payload);
  ResultStorage storage;
  auto response = storage.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &response) == expected);
  REQUIRE(response.header.status == expected);
  return copyResult(response);
}

NativeResult resume(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_prepare_id_v2 &prepareId,
    std::uint32_t step,
    irfq_infinite_resume_kind_v2 kind,
    irfq_infinite_decision_v2 decision,
    irfq_infinite_status_v2 expected,
    std::uint64_t rangeBegin = 0,
    std::uint64_t rangeEnd = 0,
    const std::vector<irfq_infinite_store_item_v2> &items = {}) {
  irfq_infinite_resume_request_v2 request{};
  init(request);
  request.prepare_id = prepareId;
  request.step = step;
  request.kind = kind;
  request.decision = decision;
  request.store_range_begin = rangeBegin;
  request.store_range_end = rangeEnd;
  request.store_items = items.data();
  request.input_item_count = items.size();
  ResultStorage storage;
  auto response = storage.response();
  REQUIRE(irfq_infinite_resume_v2(session, &request, &response) == expected);
  REQUIRE(response.header.status == expected);
  return copyResult(response);
}

void apply(irfq_infinite_session_v2 *session, const NativeResult &result) {
  irfq_infinite_apply_committed_request_v2 request{};
  init(request);
  request.prepare_id = result.response.prepare_id;
  request.result_revision = result.response.result_revision;
  std::memcpy(request.native_state_sha256, result.response.native_state_sha256, sizeof(request.native_state_sha256));
  irfq_infinite_operation_response_v2 response{};
  init(response);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &request, &response) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(response.cache_revision == result.response.result_revision);
}

void abortPlan(irfq_infinite_session_v2 *session, const irfq_infinite_prepare_id_v2 &prepareId) {
  irfq_infinite_abort_request_v2 request{};
  init(request);
  request.prepare_id = prepareId;
  irfq_infinite_operation_response_v2 response{};
  init(response);
  REQUIRE(irfq_infinite_abort_v2(session, &request, &response) == IRFQ_INFINITE_STATUS_OK_V2);
}

class Barrier {
public:
  explicit Barrier(unsigned participants)
      : m_remaining(participants) {}

  void arriveAndWait() {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (--m_remaining == 0) {
      m_condition.notify_all();
    } else {
      m_condition.wait(lock, [this] { return m_remaining == 0; });
    }
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_condition;
  unsigned m_remaining;
};

TEST_CASE("InfiniteFrameAdapterV2 fixture pins C ABI layout with LF bytes", "[infinite][adapter][v2][abi]") {
  std::ifstream stream(INFINITE_FRAME_ADAPTER_ABI_FIXTURE_PATH, std::ios::binary);
  REQUIRE(stream.good());
  const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  CHECK(bytes.find('\r') == std::string::npos);
  CHECK(bytes.find("constant\tabi_version\t131072\n") != std::string::npos);
  CHECK(bytes.find("size\tirfq_infinite_prepare_response_v2\t504\n") != std::string::npos);
  CHECK(bytes.find("offset\tirfq_infinite_prepare_response_v2.actions\t168\n") != std::string::npos);
  CHECK(bytes.find("size\tirfq_infinite_resume_request_v2\t80\n") != std::string::npos);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 scan is stateless over relocated fragmented and coalesced buffers",
    "[infinite][adapter][v2][scan]") {
  const auto first = frame('A', 1, "98=0\001108=30\001");
  const auto second = frame('0', 2);
  const auto combined = first + second;
  irfq_infinite_scan_cursor_v2 cursor{};

  for (std::size_t length = 1; length < first.size(); ++length) {
    std::string relocated(combined.data(), length);
    irfq_infinite_scan_request_v2 request{};
    init(request);
    request.input = slice(relocated);
    request.cursor = cursor;
    irfq_infinite_scan_response_v2 response{};
    init(response);
    REQUIRE(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_NEED_MORE_V2);
    cursor = response.cursor;
  }

  std::string relocated = combined;
  irfq_infinite_scan_request_v2 request{};
  init(request);
  request.input = slice(relocated);
  request.cursor = cursor;
  irfq_infinite_scan_response_v2 response{};
  init(response);
  REQUIRE(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_FRAME_READY_V2);
  CHECK(response.complete_prefix_length == first.size());

  request.cursor = response.cursor;
  init(response);
  REQUIRE(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_FRAME_READY_V2);
  CHECK(response.complete_prefix_length == combined.size());
  CHECK(response.cursor.frame_start == combined.size());
}

TEST_CASE("InfiniteFrameAdapterV2 rejects malformed ABI ranges without mutation", "[infinite][adapter][v2][bounds]") {
  const auto message = frame('A', 1, "98=0\001108=30\001");
  irfq_infinite_scan_request_v2 request{};
  init(request);
  request.input = slice(message);
  irfq_infinite_scan_response_v2 response{};
  init(response);

  CHECK(irfq_infinite_scan_v2(nullptr, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(irfq_infinite_scan_v2(&request, nullptr) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  request.input = {nullptr, 1};
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  request.input = {reinterpret_cast<const std::uint8_t *>(std::numeric_limits<std::uintptr_t>::max() - 1), 3};
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  alignas(irfq_infinite_scan_request_v2) std::array<std::uint8_t, sizeof(irfq_infinite_scan_request_v2) + 1> raw{};
  auto *misaligned = reinterpret_cast<irfq_infinite_scan_request_v2 *>(raw.data() + 1);
  CHECK(irfq_infinite_scan_v2(misaligned, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  request = {};
  init(request);
  request.input = {reinterpret_cast<const std::uint8_t *>(&response), sizeof(response)};
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  request.input = slice(message);
  request.cursor.frame_start = message.size() + 1;
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  request.cursor = {};
  request.input.length = IRFQ_INFINITE_MAX_SCAN_BYTES_V2 + 1;
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  std::vector<std::uint8_t> maximumScan(IRFQ_INFINITE_MAX_SCAN_BYTES_V2, std::uint8_t{'A'});
  maximumScan[0] = '8';
  maximumScan[1] = '=';
  request.input = {maximumScan.data(), maximumScan.size()};
  request.cursor = {};
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_NEED_MORE_V2);
  request.input.length++;
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  const auto exactFrame = frameWithSize(IRFQ_INFINITE_MAX_FRAME_BYTES_V2);
  request.input = slice(exactFrame);
  request.cursor = {};
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_FRAME_READY_V2);
  CHECK(response.complete_prefix_length == IRFQ_INFINITE_MAX_FRAME_BYTES_V2);
  const auto overFrame = frameWithSize(IRFQ_INFINITE_MAX_FRAME_BYTES_V2 + 1);
  request.input = slice(overFrame);
  request.cursor = {};
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  CreateFixture fixture;
  fixture.request.header.structure_size--;
  CHECK(irfq_infinite_session_create_v2(&fixture.request, &fixture.response) == IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2);
  fixture.request.header.structure_size = sizeof(fixture.request);
  fixture.request.header.abi_version++;
  CHECK(irfq_infinite_session_create_v2(&fixture.request, &fixture.response) == IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2);
  fixture.request.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2;
  fixture.request.snapshot_codec_version++;
  CHECK(
      irfq_infinite_session_create_v2(&fixture.request, &fixture.response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  fixture.request.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
  fixture.request.transport_dictionary_sha256[0] = 0;
  std::fill_n(fixture.request.transport_dictionary_sha256 + 1, 31, std::uint8_t{0});
  CHECK(
      irfq_infinite_session_create_v2(&fixture.request, &fixture.response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  alignas(irfq_infinite_session_create_request_v2)
      std::array<std::uint8_t, sizeof(irfq_infinite_session_create_request_v2) + 1>
          createRaw{};
  auto *misalignedCreate = reinterpret_cast<irfq_infinite_session_create_request_v2 *>(createRaw.data() + 1);
  init(fixture.response);
  CHECK(
      irfq_infinite_session_create_v2(misalignedCreate, &fixture.response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 creates side-effect-free state and promotes only first Logon",
    "[infinite][adapter][v2][session]") {
  auto *rejected = createSession();
  auto invalid = prepare(
      rejected,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      frame('0', 1),
      IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(invalid.state.empty());
  auto afterFailure = prepare(
      rejected,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      frame('A', 1, "98=0\001108=30\001"),
      IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(afterFailure.response.prepare_id.value == 1);
  abortPlan(rejected, afterFailure.response.prepare_id);
  irfq_infinite_destroy_v2(rejected);

  auto *identityMismatch = createSession();
  const auto mismatchedLogon = finishFix(
      "35=A\00149=ATTACKER\00156=VENUE\00134=1\00152=20260828-12:00:00.000000\00198=0\001108=30\001"
      "553=failure-user\001554=failure-password\001");
  auto mismatch = prepare(
      identityMismatch,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      mismatchedLogon,
      IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(mismatch.state.empty());
  irfq_infinite_destroy_v2(identityMismatch);

  auto *session = createSession();
  const std::string username = "secret-user";
  const std::string password = "secret-pass";
  auto logon = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      frame('A', 1, "98=0\001108=30\001553=" + username + "\001554=" + password + "\001"),
      IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(logon.response.base_revision == 0);
  CHECK(logon.response.result_revision == 1);
  CHECK_FALSE(logon.state.empty());
  CHECK(std::search(logon.state.begin(), logon.state.end(), username.begin(), username.end()) == logon.state.end());
  CHECK(std::search(logon.state.begin(), logon.state.end(), password.begin(), password.end()) == logon.state.end());
  CHECK(logon.output.empty());
  CHECK(
      hex(logon.response.native_state_sha256, 32)
      == "7dd9bc34ac6d67b294e077157d57108bc71ac011e28658ed2f2efecc01766163");
  CHECK(
      hex(logon.state.data(), logon.state.size())
      == "495246514e533200"
         "76788ee9df0d8a12b134db7f1ce3d2748d294e1ab12f185ab094bc9421a38cc5"
         "0000000000000001"
         "0000000000000001"
         "0000000000000001"
         "0000000000000002"
         "0000000000000001"
         "0000000000000002"
         "0000000000000000"
         "0000000000000000"
         "00000001"
         "00000000"
         "00000000"
         "0000000a");

  auto busy = prepare(
      session,
      IRFQ_INFINITE_PREPARE_RUST_TIMER_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      {},
      IRFQ_INFINITE_STATUS_PLAN_PENDING_V2);
  CHECK(busy.state.empty());
  apply(session, logon);

  auto *restored = createSession(logon.state, 1);
  CreateFixture changedBinding;
  changedBinding.state = logon.state;
  changedBinding.request.cache_revision = 1;
  changedBinding.refreshViews();
  changedBinding.request.authenticated_session_binding_sha256[0] ^= 1;
  CHECK(
      irfq_infinite_session_create_v2(&changedBinding.request, &changedBinding.response)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CreateFixture malformedSnapshot;
  malformedSnapshot.state = logon.state;
  malformedSnapshot.state[112] = 0x80;
  malformedSnapshot.request.cache_revision = 1;
  malformedSnapshot.refreshViews();
  CHECK(
      irfq_infinite_session_create_v2(&malformedSnapshot.request, &malformedSnapshot.response)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  auto application = prepare(
      restored,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      frame('S', 2, "117=Q1\001"),
      IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  CHECK(application.response.step == 1);
  auto ready = resume(
      restored,
      application.response.prepare_id,
      1,
      IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2,
      IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2,
      IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(ready.response.result_revision == 2);
  apply(restored, ready);

  auto targetBoundState = logon.state;
  write64(targetBoundState, 64, static_cast<std::uint64_t>(INT64_MAX));
  auto *targetBound = createSession(targetBoundState, 1);
  auto targetOverflow = prepare(
      targetBound,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      frame('0', static_cast<std::uint64_t>(INT64_MAX)),
      IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(targetOverflow.state.empty());
  irfq_infinite_destroy_v2(targetBound);

  auto senderBoundState = logon.state;
  write64(senderBoundState, 56, static_cast<std::uint64_t>(INT64_MAX));
  auto *senderBound = createSession(senderBoundState, 1);
  const std::string senderBody = "117=OVERFLOW\001";
  irfq_infinite_prepare_request_v2 senderRequest{};
  init(senderRequest);
  senderRequest.kind = IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2;
  senderRequest.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  senderRequest.expected_session_epoch = 1;
  senderRequest.expected_revision = 1;
  senderRequest.now_tai_ns = 3;
  senderRequest.application_message_type[0] = 'S';
  senderRequest.application_message_type_length = 1;
  senderRequest.payload = slice(senderBody);
  ResultStorage senderStorage;
  auto senderResponse = senderStorage.response();
  CHECK(
      irfq_infinite_prepare_v2(senderBound, &senderRequest, &senderResponse)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(senderResponse.prepare_id.value == 0);
  irfq_infinite_destroy_v2(senderBound);

  auto timerBoundState = logon.state;
  write32(timerBoundState, 112, static_cast<std::uint32_t>(INT32_MAX));
  auto *timerBound = createSession(timerBoundState, 1);
  auto timerOverflow = prepare(
      timerBound,
      IRFQ_INFINITE_PREPARE_RUST_TIMER_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      {},
      IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(timerOverflow.state.empty());
  irfq_infinite_destroy_v2(timerBound);

  irfq_infinite_destroy_v2(restored);
  irfq_infinite_destroy_v2(session);
}

TEST_CASE("InfiniteFrameAdapterV2 keeps one stage-local plan with monotonic steps", "[infinite][adapter][v2][plan]") {
  auto *session = createSession();
  auto logon = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      frame('A', 1, "98=0\001108=30\001"),
      IRFQ_INFINITE_STATUS_READY_V2);
  apply(session, logon);

  auto store = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      frame('2', 2, "7=1\00116=0\001"),
      IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  CHECK(store.response.store_range_begin == 1);
  CHECK(store.response.store_range_end == 0);

  auto wrong = resume(
      session,
      store.response.prepare_id,
      2,
      IRFQ_INFINITE_RESUME_STORE_RANGE_V2,
      IRFQ_INFINITE_DECISION_NONE_V2,
      IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  CHECK(wrong.state.empty());
  std::vector<irfq_infinite_store_item_v2> tooMany(IRFQ_INFINITE_MAX_STORE_ITEMS_V2 + 1);
  for (std::size_t index = 0; index < tooMany.size(); ++index) {
    tooMany[index] = {index + 1, IRFQ_INFINITE_STORE_ITEM_GAP_V2, 0, {nullptr, 0}};
  }
  auto overBound = resume(
      session,
      store.response.prepare_id,
      1,
      IRFQ_INFINITE_RESUME_STORE_RANGE_V2,
      IRFQ_INFINITE_DECISION_NONE_V2,
      IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2,
      1,
      0,
      tooMany);
  CHECK(overBound.state.empty());
  const auto storedApplication = outboundFrame('S', 1, "117=STORED\001");
  const std::vector<irfq_infinite_store_item_v2> storeItems{
      {1, IRFQ_INFINITE_STORE_ITEM_MESSAGE_V2, 0, slice(storedApplication)},
      {2, IRFQ_INFINITE_STORE_ITEM_GAP_V2, 0, {nullptr, 0}}};
  auto ready = resume(
      session,
      store.response.prepare_id,
      1,
      IRFQ_INFINITE_RESUME_STORE_RANGE_V2,
      IRFQ_INFINITE_DECISION_NONE_V2,
      IRFQ_INFINITE_STATUS_READY_V2,
      1,
      0,
      storeItems);
  CHECK(ready.response.prepare_id.session == store.response.prepare_id.session);
  CHECK(ready.response.prepare_id.value == store.response.prepare_id.value);
  const std::string recoveryOutput(ready.output.begin(), ready.output.end());
  CHECK(recoveryOutput.find("43=Y\001") != std::string::npos);
  CHECK(recoveryOutput.find("122=20260828-12:00:00.000000\001") != std::string::npos);
  CHECK(recoveryOutput.find("35=4\001") != std::string::npos);
  CHECK(recoveryOutput.find("123=Y\001") != std::string::npos);
  CHECK(recoveryOutput.find("36=3\001") != std::string::npos);

  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  irfq_infinite_abort_request_v2 badAbort{};
  init(badAbort);
  badAbort.prepare_id = {store.response.prepare_id.session, store.response.prepare_id.value + 1};
  CHECK(irfq_infinite_abort_v2(session, &badAbort, &operation) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  abortPlan(session, store.response.prepare_id);

  auto exactStore = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      frame('2', 2, "7=1\00116=256\001"),
      IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  std::vector<irfq_infinite_store_item_v2> maximumItems(IRFQ_INFINITE_MAX_STORE_ITEMS_V2);
  for (std::size_t index = 0; index < maximumItems.size(); ++index) {
    maximumItems[index] = {index + 1, IRFQ_INFINITE_STORE_ITEM_GAP_V2, 0, {nullptr, 0}};
  }
  auto maximumReady = resume(
      session,
      exactStore.response.prepare_id,
      1,
      IRFQ_INFINITE_RESUME_STORE_RANGE_V2,
      IRFQ_INFINITE_DECISION_NONE_V2,
      IRFQ_INFINITE_STATUS_READY_V2,
      1,
      256,
      maximumItems);
  CHECK(maximumReady.response.action_count == 1);
  CHECK(maximumReady.response.actions[0].sequence_begin == 1);
  CHECK(maximumReady.response.actions[0].sequence_end == 256);
  abortPlan(session, exactStore.response.prepare_id);

  auto first = prepare(
      session,
      IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      {},
      IRFQ_INFINITE_STATUS_READY_V2);
  abortPlan(session, first.response.prepare_id);
  auto second = prepare(
      session,
      IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2,
      IRFQ_INFINITE_STAGE_READ_R2_V2,
      1,
      {},
      IRFQ_INFINITE_STATUS_READY_V2);
  abortPlan(session, second.response.prepare_id);
  auto third = prepare(
      session,
      IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2,
      IRFQ_INFINITE_STAGE_TARGET_CAS_V2,
      1,
      {},
      IRFQ_INFINITE_STATUS_READY_V2);
  abortPlan(session, third.response.prepare_id);
  auto fourth = prepare(
      session,
      IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2,
      IRFQ_INFINITE_STAGE_RESET_FINAL_V2,
      1,
      {},
      IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(first.response.prepare_id.value != second.response.prepare_id.value);
  CHECK(second.response.prepare_id.value != third.response.prepare_id.value);
  CHECK(third.response.prepare_id.value != fourth.response.prepare_id.value);
  abortPlan(session, fourth.response.prepare_id);
  irfq_infinite_destroy_v2(session);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 resumes application reset and output without repeating prepare",
    "[infinite][adapter][v2][resume]") {
  auto *session = createSession();
  auto logon = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      frame('A', 1, "98=0\001108=30\001"),
      IRFQ_INFINITE_STATUS_READY_V2);
  apply(session, logon);

  auto reset = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      frame('A', 2, "98=0\001108=30\001141=Y\001"),
      IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  auto resetReady = resume(
      session,
      reset.response.prepare_id,
      reset.response.step,
      IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2,
      IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2,
      IRFQ_INFINITE_STATUS_READY_V2);
  abortPlan(session, resetReady.response.prepare_id);

  const std::string outboundBody = "117=AUTHORIZED\001";
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2;
  request.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  request.expected_session_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = 5;
  request.application_message_type[0] = 'S';
  request.application_message_type_length = 1;
  request.payload = slice(outboundBody);
  std::array<std::uint8_t, IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2> state{};
  std::vector<std::uint8_t> shortOutput(1);
  irfq_infinite_prepare_response_v2 aliased{};
  init(aliased);
  aliased.native_state = {state.data(), state.size(), 0};
  aliased.output = {state.data(), state.size(), 0};
  CHECK(irfq_infinite_prepare_v2(session, &request, &aliased) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  auto inputAliased = request;
  inputAliased.payload = {state.data(), 1};
  init(aliased);
  aliased.native_state = {state.data(), state.size(), 0};
  aliased.output = {shortOutput.data(), shortOutput.size(), 0};
  CHECK(irfq_infinite_prepare_v2(session, &inputAliased, &aliased) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  irfq_infinite_prepare_response_v2 response{};
  init(response);
  response.native_state = {state.data(), state.size(), 0};
  response.output = {shortOutput.data(), shortOutput.size(), 0};
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &response) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  CHECK(response.required_output_capacity > shortOutput.size());
  CHECK(response.prepare_id.value == 3);
  const auto prepareId = response.prepare_id;
  const auto step = response.step;

  irfq_infinite_resume_request_v2 outputRequest{};
  init(outputRequest);
  outputRequest.prepare_id = prepareId;
  outputRequest.step = step;
  outputRequest.kind = IRFQ_INFINITE_RESUME_OUTPUT_V2;
  outputRequest.decision = IRFQ_INFINITE_DECISION_NONE_V2;
  std::vector<std::uint8_t> exactState(response.required_native_state_capacity);
  std::vector<std::uint8_t> oneByteShort(response.required_output_capacity - 1);
  init(response);
  response.native_state = {exactState.data(), exactState.size(), 0};
  response.output = {oneByteShort.data(), oneByteShort.size(), 0};
  REQUIRE(irfq_infinite_resume_v2(session, &outputRequest, &response) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  CHECK(response.prepare_id.session == prepareId.session);
  CHECK(response.prepare_id.value == prepareId.value);
  CHECK(response.step == step);

  std::vector<std::uint8_t> exactOutput(response.required_output_capacity);
  init(response);
  response.native_state = {exactState.data(), exactState.size(), 0};
  response.output = {exactOutput.data(), exactOutput.size(), 0};
  REQUIRE(irfq_infinite_resume_v2(session, &outputRequest, &response) == IRFQ_INFINITE_STATUS_READY_V2);
  const auto materialized = copyResult(response);
  CHECK(materialized.response.prepare_id.session == prepareId.session);
  CHECK(materialized.response.prepare_id.value == prepareId.value);
  const std::string outbound(materialized.output.begin(), materialized.output.end());
  const auto expectedOutbound
      = finishFix("35=S\00134=1\00149=VENUE\00152=19700101-00:00:00.000000\00156=CLIENT\001117=AUTHORIZED\001");
  CHECK(outbound == expectedOutbound);
  abortPlan(session, prepareId);

  auto *oracle = createSession(logon.state, 1);
  auto afterAbort = prepare(
      session,
      IRFQ_INFINITE_PREPARE_RUST_TIMER_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      {},
      IRFQ_INFINITE_STATUS_READY_V2);
  auto pristine = prepare(
      oracle,
      IRFQ_INFINITE_PREPARE_RUST_TIMER_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      {},
      IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(afterAbort.state == pristine.state);
  CHECK(afterAbort.output == pristine.output);
  CHECK(
      std::memcmp(
          afterAbort.response.native_state_sha256,
          pristine.response.native_state_sha256,
          sizeof(afterAbort.response.native_state_sha256))
      == 0);
  abortPlan(session, afterAbort.response.prepare_id);
  abortPlan(oracle, pristine.response.prepare_id);
  irfq_infinite_destroy_v2(oracle);
  irfq_infinite_destroy_v2(session);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 validates cross-session IDs revision digest and order",
    "[infinite][adapter][v2][order]") {
  auto *first = createSession();
  auto *second = createSession();
  irfq_infinite_prepare_request_v2 invalid{};
  init(invalid);
  invalid.kind = UINT32_MAX;
  invalid.stage = IRFQ_INFINITE_STAGE_HEAD_V2;
  invalid.expected_session_epoch = 1;
  invalid.now_tai_ns = 1;
  ResultStorage invalidStorage;
  auto invalidResponse = invalidStorage.response();
  CHECK(irfq_infinite_prepare_v2(first, &invalid, &invalidResponse) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  invalid.kind = IRFQ_INFINITE_PREPARE_RUST_TIMER_V2;
  invalid.event_code = UINT32_MAX;
  invalid.event_identity = 1;
  invalidResponse = invalidStorage.response();
  CHECK(irfq_infinite_prepare_v2(first, &invalid, &invalidResponse) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  auto plan = prepare(
      first,
      IRFQ_INFINITE_PREPARE_RUST_TIMER_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      {},
      IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(plan.response.prepare_id.value == 1);

  irfq_infinite_apply_committed_request_v2 request{};
  init(request);
  request.prepare_id = plan.response.prepare_id;
  request.result_revision = plan.response.result_revision;
  std::memcpy(request.native_state_sha256, plan.response.native_state_sha256, sizeof(request.native_state_sha256));
  irfq_infinite_operation_response_v2 response{};
  init(response);
  CHECK(irfq_infinite_apply_committed_v2(second, &request, &response) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  request.result_revision++;
  CHECK(irfq_infinite_apply_committed_v2(first, &request, &response) == IRFQ_INFINITE_STATUS_REVISION_MISMATCH_V2);
  request.result_revision--;
  request.native_state_sha256[0] ^= 1;
  CHECK(irfq_infinite_apply_committed_v2(first, &request, &response) == IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
  request.native_state_sha256[0] ^= 1;
  CHECK(irfq_infinite_apply_committed_v2(first, &request, &response) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_apply_committed_v2(first, &request, &response) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);

  irfq_infinite_destroy_v2(second);
  irfq_infinite_destroy_v2(first);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 abort preserves complete state and reconstruction survives apply-result loss",
    "[infinite][adapter][v2][recovery]") {
  auto *session = createSession();
  auto logon = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      0,
      frame('A', 1, "98=0\001108=30\001"),
      IRFQ_INFINITE_STATUS_READY_V2);
  apply(session, logon);

  const auto applicationFrame = frame('S', 2, "117=Q1\001");
  auto decision = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      1,
      applicationFrame,
      IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  auto firstApplication = resume(
      session,
      decision.response.prepare_id,
      1,
      IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2,
      IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2,
      IRFQ_INFINITE_STATUS_READY_V2);
  apply(session, firstApplication);
  auto heartbeat = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      2,
      frame('0', 3),
      IRFQ_INFINITE_STATUS_READY_V2);
  apply(session, heartbeat);
  auto secondDecision = prepare(
      session,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      3,
      frame('R', 4, "131=Q2\001"),
      IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  auto durable = resume(
      session,
      secondDecision.response.prepare_id,
      1,
      IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2,
      IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2,
      IRFQ_INFINITE_STATUS_READY_V2);
  irfq_infinite_destroy_v2(session); // Simulate a durable result with a lost cache-apply return.

  auto *reconstructed = createSession(durable.state, durable.response.result_revision);
  auto *oracle = createSession(durable.state, durable.response.result_revision);
  const auto resendRequest = frame('2', 5, "7=1\00116=3\001");
  auto actualRequest = prepare(
      reconstructed,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      4,
      resendRequest,
      IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  auto expectedRequest = prepare(
      oracle,
      IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2,
      IRFQ_INFINITE_STAGE_HEAD_V2,
      4,
      resendRequest,
      IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  const auto storedFirst = outboundFrame('S', 1, "117=RECOVERY-1\001");
  const auto storedThird = outboundFrame('R', 3, "131=RECOVERY-3\001");
  const std::vector<irfq_infinite_store_item_v2> items{
      {1, IRFQ_INFINITE_STORE_ITEM_MESSAGE_V2, 0, slice(storedFirst)},
      {2, IRFQ_INFINITE_STORE_ITEM_GAP_V2, 0, {nullptr, 0}},
      {3, IRFQ_INFINITE_STORE_ITEM_MESSAGE_V2, 0, slice(storedThird)}};
  auto actual = resume(
      reconstructed,
      actualRequest.response.prepare_id,
      1,
      IRFQ_INFINITE_RESUME_STORE_RANGE_V2,
      IRFQ_INFINITE_DECISION_NONE_V2,
      IRFQ_INFINITE_STATUS_READY_V2,
      1,
      3,
      items);
  auto expected = resume(
      oracle,
      expectedRequest.response.prepare_id,
      1,
      IRFQ_INFINITE_RESUME_STORE_RANGE_V2,
      IRFQ_INFINITE_DECISION_NONE_V2,
      IRFQ_INFINITE_STATUS_READY_V2,
      1,
      3,
      items);
  CHECK(actual.state == expected.state);
  CHECK(actual.output == expected.output);
  CHECK(actual.response.result_revision == expected.response.result_revision);
  const std::string bytes(actual.output.begin(), actual.output.end());
  const auto expectedBytes = finishFix(
                                 "35=S\00134=1\00143=Y\00149=VENUE\00152=19700101-00:00:00.000000\001"
                                 "56=CLIENT\001122=20260828-12:00:00.000000\001117=RECOVERY-1\001")
                             + finishFix(
                                 "35=4\00134=2\00149=VENUE\00152=19700101-00:00:00.000000\00156=CLIENT\001"
                                 "36=3\001123=Y\001")
                             + finishFix(
                                 "35=R\00134=3\00143=Y\00149=VENUE\00152=19700101-00:00:00.000000\001"
                                 "56=CLIENT\001122=20260828-12:00:00.000000\001131=RECOVERY-3\001");
  CHECK(bytes == expectedBytes);
  abortPlan(reconstructed, actual.response.prepare_id);
  abortPlan(oracle, expected.response.prepare_id);
  irfq_infinite_destroy_v2(reconstructed);
  irfq_infinite_destroy_v2(oracle);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 handles move between threads and distinct sessions progress concurrently",
    "[infinite][adapter][v2][threads]") {
  irfq_infinite_session_v2 *moved = nullptr;
  std::thread creator([&] { moved = createSession(); });
  creator.join();
  NativeResult plan;
  std::thread caller([&] {
    plan = prepare(
        moved,
        IRFQ_INFINITE_PREPARE_RUST_TIMER_V2,
        IRFQ_INFINITE_STAGE_HEAD_V2,
        0,
        {},
        IRFQ_INFINITE_STATUS_READY_V2);
  });
  caller.join();
  std::thread applier([&] { apply(moved, plan); });
  applier.join();
  std::thread destroyer([&] { CHECK(irfq_infinite_destroy_v2(moved) == IRFQ_INFINITE_STATUS_OK_V2); });
  destroyer.join();

  auto *first = createSession();
  auto *second = createSession();
  Barrier start(3);
  std::array<irfq_infinite_status_v2, 2> status{};
  auto run = [&](unsigned index, irfq_infinite_session_v2 *session) {
    start.arriveAndWait();
    AdapterAllocationProbe::arm();
    auto result = prepare(
        session,
        IRFQ_INFINITE_PREPARE_RUST_TIMER_V2,
        IRFQ_INFINITE_STAGE_HEAD_V2,
        0,
        {},
        IRFQ_INFINITE_STATUS_READY_V2);
    AdapterAllocationProbe::disarm();
    status[index] = result.response.header.status;
    abortPlan(session, result.response.prepare_id);
  };
  AdapterAllocationProbe::reset();
  std::thread a(run, 0, first);
  std::thread b(run, 1, second);
  start.arriveAndWait();
  a.join();
  b.join();
  CHECK(AdapterAllocationProbe::arrived == 2);
  CHECK(status[0] == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(status[1] == IRFQ_INFINITE_STATUS_READY_V2);
  irfq_infinite_destroy_v2(first);
  irfq_infinite_destroy_v2(second);
}
} // namespace
