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

#include "InfiniteSessionClassification.h"
#include "Session.h"
#include "catch_amalgamated.hpp"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace FIX {
irfq_infinite_session_v2 *createInfiniteFrameAdapterStockNonconformanceSmokeSession(
    const std::uint8_t *config,
    std::size_t configLength,
    const std::uint8_t *nativeState,
    std::size_t nativeStateLength,
    std::uint64_t epoch,
    std::uint64_t revision,
    std::int64_t creationTaiNs,
    std::int64_t creationUtcNs) noexcept;
bool computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
    const irfq_infinite_session_v2 *session,
    const irfq_infinite_prepare_request_v2 &request,
    std::uint8_t *identity) noexcept;
} // namespace FIX

namespace {
static_assert(sizeof(irfq_infinite_input_header_v2) == 16);
static_assert(sizeof(irfq_infinite_output_header_v2) == 16);
static_assert(sizeof(irfq_infinite_slice_v2) == 16);
static_assert(sizeof(irfq_infinite_buffer_v2) == 24);
static_assert(sizeof(irfq_infinite_scan_cursor_v2) == 32);
static_assert(sizeof(irfq_infinite_scan_request_v2) == 64);
static_assert(sizeof(irfq_infinite_scan_response_v2) == 56);
static_assert(sizeof(irfq_infinite_session_create_request_v2) == 88);
static_assert(sizeof(irfq_infinite_session_create_response_v2) == 40);
static_assert(sizeof(irfq_infinite_prepare_request_v2) == 128);
static_assert(sizeof(irfq_infinite_declarative_action_v2) == 120);
static_assert(sizeof(irfq_infinite_prepare_response_v2) == 320);
static_assert(sizeof(irfq_infinite_store_row_v2) == 112);
static_assert(sizeof(irfq_infinite_resume_request_v2) == 144);
static_assert(sizeof(irfq_infinite_apply_committed_request_v2) == 72);
static_assert(sizeof(irfq_infinite_abort_request_v2) == 32);
static_assert(sizeof(irfq_infinite_operation_response_v2) == 24);
static_assert(offsetof(irfq_infinite_session_create_request_v2, canonical_session_create_config) == 24);
static_assert(offsetof(irfq_infinite_session_create_request_v2, creation_utc_ns) == 64);
static_assert(offsetof(irfq_infinite_prepare_request_v2, event_identity_sha256) == 32);
static_assert(offsetof(irfq_infinite_prepare_request_v2, now_utc_ns) == 88);
static_assert(offsetof(irfq_infinite_prepare_response_v2, result_epoch) == 200);
static_assert(offsetof(irfq_infinite_prepare_response_v2, actions) == 296);

template <typename T> void init(T &value) {
  value = {};
  value.header.structure_size = sizeof(value);
  value.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2;
}

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

irfq_infinite_slice_v2 slice(const std::string &value) {
  return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

void cborArgument(std::vector<std::uint8_t> &bytes, std::uint8_t major, std::uint64_t value) {
  if (value < 24) {
    bytes.push_back(static_cast<std::uint8_t>(major << 5 | value));
  } else if (value <= UINT8_MAX) {
    bytes.push_back(static_cast<std::uint8_t>(major << 5 | 24));
    bytes.push_back(static_cast<std::uint8_t>(value));
  } else if (value <= UINT16_MAX) {
    bytes.push_back(static_cast<std::uint8_t>(major << 5 | 25));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
  } else {
    bytes.push_back(static_cast<std::uint8_t>(major << 5 | 26));
    for (unsigned index = 0; index < 4; ++index) {
      bytes.push_back(static_cast<std::uint8_t>(value >> ((3 - index) * 8)));
    }
  }
}

void cborUnsigned(std::vector<std::uint8_t> &bytes, std::uint32_t value) { cborArgument(bytes, 0, value); }

void cborBytes(std::vector<std::uint8_t> &bytes, const std::string &value) {
  cborArgument(bytes, 2, value.size());
  bytes.insert(bytes.end(), value.begin(), value.end());
}

void cborDigest(std::vector<std::uint8_t> &bytes, std::uint8_t byte) {
  cborArgument(bytes, 2, 32);
  bytes.insert(bytes.end(), 32, byte);
}

void cborBoolean(std::vector<std::uint8_t> &bytes, bool value) { bytes.push_back(value ? 0xf5 : 0xf4); }

std::vector<std::uint8_t> otherwiseValidUnavailableProfile(
    std::uint32_t scheduleMode = 1,
    const std::array<std::uint32_t, 8> &schedule = {}) {
  std::vector<std::uint8_t> bytes{0x98, 0x32};
  cborUnsigned(bytes, 1);
  cborBytes(bytes, "FIXT.1.1");
  cborBytes(bytes, "VENUE");
  cborBytes(bytes, "");
  cborBytes(bytes, "");
  cborBytes(bytes, "PARTICIPANT");
  cborBytes(bytes, "");
  cborBytes(bytes, "");
  cborBytes(bytes, "");
  cborDigest(bytes, 0x11);
  cborUnsigned(bytes, 1);
  cborUnsigned(bytes, scheduleMode);
  for (const auto value : schedule) {
    cborUnsigned(bytes, value);
  }
  cborUnsigned(bytes, 1);
  cborUnsigned(bytes, 30);
  cborUnsigned(bytes, 30);
  cborUnsigned(bytes, 30);
  cborUnsigned(bytes, 10);
  cborUnsigned(bytes, 2);
  cborUnsigned(bytes, 6);
  cborBoolean(bytes, true);
  cborBoolean(bytes, true);
  cborBoolean(bytes, true);
  cborUnsigned(bytes, 120);
  cborBoolean(bytes, false);
  cborBoolean(bytes, false);
  cborBoolean(bytes, false);
  cborBoolean(bytes, false);
  cborBoolean(bytes, true);
  cborBoolean(bytes, true);
  cborBoolean(bytes, true);
  cborBoolean(bytes, true);
  cborBoolean(bytes, true);
  cborBoolean(bytes, true);
  cborBoolean(bytes, false);
  cborBytes(bytes, "INFINITE-RFQ-1.0.0");
  cborUnsigned(bytes, 10);
  cborUnsigned(bytes, 299);
  cborBytes(bytes, "INFINITE-RFQ-1.0.0");
  cborBytes(bytes, "unavailable-transport-dictionary");
  cborDigest(bytes, 0x22);
  cborBytes(bytes, "unavailable-application-dictionary");
  cborDigest(bytes, 0x33);
  return bytes;
}

void write32(std::uint8_t *bytes, std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) {
    bytes[index] = static_cast<std::uint8_t>(value >> ((3 - index) * 8));
  }
}

void write64(std::uint8_t *bytes, std::uint64_t value) {
  for (unsigned index = 0; index < 8; ++index) {
    bytes[index] = static_cast<std::uint8_t>(value >> ((7 - index) * 8));
  }
}

std::uint64_t read64(const std::uint8_t *bytes) {
  std::uint64_t value = 0;
  for (unsigned index = 0; index < 8; ++index) {
    value = value << 8 | bytes[index];
  }
  return value;
}

struct PlanBuffers {
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::array<std::uint8_t, IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2> output{};
  std::array<irfq_infinite_declarative_action_v2, IRFQ_INFINITE_MAX_ACTIONS_V2> actions{};

  irfq_infinite_prepare_response_v2 response() {
    irfq_infinite_prepare_response_v2 value{};
    init(value);
    value.native_state = {state.data(), state.size(), 0};
    value.output = {output.data(), output.size(), 0};
    value.actions = actions.data();
    value.action_capacity = actions.size();
    return value;
  }
};
} // namespace

TEST_CASE("InfiniteFrameAdapterV2 fixture pins C ABI layout with LF bytes", "[infinite][adapter][v2][abi]") {
  std::ifstream stream(INFINITE_FRAME_ADAPTER_ABI_FIXTURE_PATH, std::ios::binary);
  REQUIRE(stream.good());
  const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  CHECK(bytes.find('\r') == std::string::npos);
  CHECK(bytes.find("constant\tabi_version\t131072\n") != std::string::npos);
  CHECK(bytes.find("size\tirfq_infinite_prepare_response_v2\t320\n") != std::string::npos);
  CHECK(bytes.find("offset\tirfq_infinite_prepare_response_v2.actions\t296\n") != std::string::npos);
  CHECK(bytes.find("size\tirfq_infinite_resume_request_v2\t144\n") != std::string::npos);
}

TEST_CASE("InfiniteFrameAdapterV2 exposes the frozen v2 numeric domains", "[infinite][adapter][v2][abi]") {
  CHECK(IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2 == 2);
  CHECK(IRFQ_INFINITE_MAX_SCAN_BYTES_V2 == 65536);
  CHECK(IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2 == 312);
  CHECK(IRFQ_INFINITE_MAX_RESUME_STEPS_V2 == 3);
  CHECK(IRFQ_INFINITE_MAX_ACTIONS_V2 == 258);
  CHECK(IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2 == 15);
  CHECK(IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2 == 16);
  CHECK(IRFQ_INFINITE_STAGE_EVENT_V2 == 5);
  CHECK(IRFQ_INFINITE_EVENT_INBOUND_FRAME_V2 == 1);
  CHECK(IRFQ_INFINITE_EVENT_CONTINUE_RESEND_V2 == 14);
  CHECK(IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2 == 20);
  CHECK(IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2 == 1);
  CHECK(IRFQ_INFINITE_ACTION_TARGET_ADVANCE_V2 == 8);
  CHECK(IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2 == 4);
  CHECK(IRFQ_INFINITE_DISPOSITION_PENDING_RESET_LOGON_V2 == 5);
  CHECK(IRFQ_INFINITE_STORE_CLASS_PROVEN_GAP_V2 == 5);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 scan is stateless over relocated fragmented and coalesced buffers",
    "[infinite][adapter][v2][scan]") {
  const auto first = frame('A', 1, "98=0\001108=30\001");
  const auto second = frame('0', 2);
  const auto combined = first + second;
  irfq_infinite_scan_cursor_v2 cursor{};

  for (std::size_t length = 1; length < first.size(); ++length) {
    INFO("fragment length=" << length << " scan offset=" << cursor.scan_offset << " stage=" << cursor.stage);
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
  CHECK(response.cursor.scan_offset == 0);
  CHECK(response.cursor.body_length == 0);
  CHECK(response.cursor.checksum_begin == 0);
  CHECK(response.cursor.stage == IRFQ_INFINITE_SCAN_BEGIN_STRING_V2);
  CHECK(response.cursor.body_length_has_digit == IRFQ_INFINITE_NO_V2);

  relocated.erase(0, first.size());
  request.input = slice(relocated);
  request.cursor = response.cursor;
  init(response);
  REQUIRE(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_FRAME_READY_V2);
  CHECK(response.complete_prefix_length == second.size());
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects forged cursors aliases and scan over-limit",
    "[infinite][adapter][v2][scan]") {
  const auto message = frame('A', 1, "98=0\001108=30\001");
  irfq_infinite_scan_request_v2 request{};
  init(request);
  request.input = slice(message);
  irfq_infinite_scan_response_v2 response{};
  init(response);

  const std::array<irfq_infinite_scan_cursor_v2, 5> forged{{
      {1, 0, 0, IRFQ_INFINITE_SCAN_BEGIN_STRING_V2, IRFQ_INFINITE_NO_V2},
      {message.size(), 0, 0, IRFQ_INFINITE_SCAN_BEGIN_STRING_V2, IRFQ_INFINITE_NO_V2},
      {0, 1, 0, IRFQ_INFINITE_SCAN_BEGIN_STRING_V2, IRFQ_INFINITE_NO_V2},
      {0, 0, 0, IRFQ_INFINITE_SCAN_BODY_V2, IRFQ_INFINITE_NO_V2},
      {0, 0, 0, IRFQ_INFINITE_SCAN_BEGIN_STRING_V2, IRFQ_INFINITE_YES_V2},
  }};
  for (const auto &cursor : forged) {
    request.cursor = cursor;
    init(response);
    CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }

  request.cursor = {};
  request.input = {reinterpret_cast<const std::uint8_t *>(&request), 1};
  init(response);
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  request.input = {reinterpret_cast<const std::uint8_t *>(1), IRFQ_INFINITE_MAX_SCAN_BYTES_V2 + 1};
  init(response);
  CHECK(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 fails closed for the otherwise-valid unavailable production profile",
    "[infinite][adapter][v2][profile]") {
  auto config = otherwiseValidUnavailableProfile();
  irfq_infinite_session_create_request_v2 request{};
  init(request);
  request.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
  request.canonical_session_create_config = {config.data(), config.size()};
  request.session_epoch = 1;
  request.creation_tai_ns = 1;
  request.creation_utc_ns = 1;
  irfq_infinite_session_create_response_v2 response{};
  init(response);
  REQUIRE(irfq_infinite_session_create_v2(&request, &response) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  CHECK(response.header.status == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  CHECK(response.session == nullptr);
  CHECK(response.cache_epoch == 0);
  CHECK(response.cache_revision == 0);

  config.push_back(0);
  request.canonical_session_create_config = {config.data(), config.size()};
  init(response);
  CHECK(irfq_infinite_session_create_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  config.pop_back();
  request.canonical_session_create_config = {config.data(), config.size()};
  request.creation_utc_ns = 0;
  init(response);
  CHECK(irfq_infinite_session_create_v2(&request, &response) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 profile digest includes canonical CBOR bstr framing",
    "[infinite][adapter][v2][profile][digest]") {
  const auto config = otherwiseValidUnavailableProfile();
  REQUIRE(config.size() == 285);
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(session != nullptr);

  std::array<std::uint8_t, 32> payload{};
  payload.fill(0x55);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  request.expected_epoch = 1;
  request.now_tai_ns = 2;
  request.now_utc_ns = 2;
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  const std::array<std::uint8_t, 32> expected{{0xa8, 0x28, 0x74, 0xcf, 0x4f, 0x74, 0x30, 0xa4, 0x90, 0xcd, 0x17,
                                               0xb7, 0x73, 0x57, 0x09, 0xa9, 0x1e, 0xc5, 0x26, 0xc9, 0x6a, 0x66,
                                               0x87, 0xcc, 0x86, 0x31, 0x4e, 0xa8, 0x8c, 0xac, 0x41, 0x7d}};
  CHECK(std::equal(expected.begin(), expected.end(), result.native_state.data + 16));
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 enforces and uses the configured weekly UTC ranges",
    "[infinite][adapter][v2][profile][schedule]") {
  const auto createStatus = [](const std::vector<std::uint8_t> &config) {
    irfq_infinite_session_create_request_v2 request{};
    init(request);
    request.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
    request.canonical_session_create_config = {config.data(), config.size()};
    request.session_epoch = 1;
    request.creation_tai_ns = 1;
    request.creation_utc_ns = 1;
    irfq_infinite_session_create_response_v2 response{};
    init(response);
    return irfq_infinite_session_create_v2(&request, &response);
  };

  const auto earlyLogon = otherwiseValidUnavailableProfile(2, {1, 32400, 5, 61200, 1, 28800, 5, 57600});
  CHECK(createStatus(earlyLogon) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  const auto lateLogon = otherwiseValidUnavailableProfile(2, {1, 32400, 5, 61200, 1, 36000, 6, 57600});
  CHECK(createStatus(lateLogon) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  const auto config = otherwiseValidUnavailableProfile(2, {1, 32400, 5, 61200, 1, 36000, 5, 57600});
  CHECK(createStatus(config) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(fresh != nullptr);

  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x55);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 2;
  close.payload = {closePayload.data(), closePayload.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(fresh, close, close.event_identity_sha256));
  PlanBuffers closeBuffers;
  auto closed = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);

  constexpr std::int64_t nowTai = INT64_C(475200000000000);
  constexpr std::int64_t lastSentTai = nowTai - INT64_C(30000000000);
  constexpr std::int64_t creationUtc = INT64_C(381600000000000);
  constexpr std::int64_t insideUtc = INT64_C(475200000000000);
  constexpr std::int64_t outsideUtc = INT64_C(907200000000000);
  constexpr std::int64_t nextWindowUtc = INT64_C(1080000000000000);
  const auto runTimer = [&](std::int64_t nowUtc) {
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
    std::copy_n(closed.native_state.data, closed.native_state.length, state.begin());
    write64(state.data() + 72, creationUtc);
    write64(state.data() + 80, nowTai);
    write64(state.data() + 88, nowUtc);
    write64(state.data() + 96, lastSentTai);
    write64(state.data() + 104, nowUtc);
    write64(state.data() + 112, nowTai);
    write64(state.data() + 120, nowUtc);
    write64(state.data() + 180, UINT64_C(1) | UINT64_C(2) | UINT64_C(4) | UINT64_C(128));
    write32(state.data() + 288, 10);
    auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
        config.data(),
        config.size(),
        state.data(),
        state.size(),
        1,
        1,
        0,
        0);
    REQUIRE(session != nullptr);
    std::array<std::uint8_t, 32> payload{};
    payload.fill(0x77);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_TIMER_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_TIMER_TICK_V2;
    request.expected_epoch = 1;
    request.expected_revision = 1;
    request.now_tai_ns = nowTai;
    request.now_utc_ns = nowUtc;
    request.payload = {payload.data(), payload.size()};
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256));
    PlanBuffers buffers;
    auto result = buffers.response();
    REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
    const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    return output;
  };

  CHECK(runTimer(insideUtc).find("\00135=0\001") != std::string::npos);
  const auto outsideOutput = runTimer(outsideUtc);
  CHECK(outsideOutput.find("\00135=5\001") != std::string::npos);
  CHECK(outsideOutput.find("\00135=0\001") == std::string::npos);
  const auto nextWindowOutput = runTimer(nextWindowUtc);
  CHECK(nextWindowOutput.empty());
}

TEST_CASE("InfiniteFrameAdapterV2 exports and executes exactly seven calls", "[infinite][adapter][v2][calls]") {
  irfq_infinite_prepare_request_v2 prepareRequest{};
  init(prepareRequest);
  irfq_infinite_prepare_response_v2 prepareResponse{};
  init(prepareResponse);
  CHECK(
      irfq_infinite_prepare_v2(nullptr, &prepareRequest, &prepareResponse) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  irfq_infinite_resume_request_v2 resumeRequest{};
  init(resumeRequest);
  irfq_infinite_prepare_response_v2 resumeResponse{};
  init(resumeResponse);
  CHECK(irfq_infinite_resume_v2(nullptr, &resumeRequest, &resumeResponse) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  irfq_infinite_apply_committed_request_v2 applyRequest{};
  init(applyRequest);
  irfq_infinite_operation_response_v2 applyResponse{};
  init(applyResponse);
  CHECK(
      irfq_infinite_apply_committed_v2(nullptr, &applyRequest, &applyResponse)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  irfq_infinite_abort_request_v2 abortRequest{};
  init(abortRequest);
  irfq_infinite_operation_response_v2 abortResponse{};
  init(abortResponse);
  CHECK(irfq_infinite_abort_v2(nullptr, &abortRequest, &abortResponse) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(irfq_infinite_destroy_v2(nullptr) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke proves transport-close abort apply and reconstruction",
    "[infinite][adapter][v2][stock-smoke][plan]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(session != nullptr);

  const std::array<std::uint8_t, 32> identity{{0xe8, 0xa4, 0x11, 0xbb, 0x74, 0xb3, 0xea, 0x50, 0xe7, 0x3f, 0xa2,
                                               0xdf, 0x83, 0xe9, 0xc0, 0xb4, 0xf0, 0xe7, 0xd9, 0xf0, 0xfc, 0x8d,
                                               0x75, 0xd6, 0x49, 0xb7, 0xc9, 0x6e, 0x45, 0xc5, 0xe9, 0x71}};
  std::array<std::uint8_t, 32> payload{};
  payload.fill(0x55);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  std::memcpy(request.event_identity_sha256, identity.data(), identity.size());
  request.expected_epoch = 1;
  request.now_tai_ns = 2;
  request.now_utc_ns = 3;
  request.payload = {payload.data(), payload.size()};

  PlanBuffers firstBuffers;
  auto first = firstBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &first) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(first.prepare_id.high != 0);
  CHECK(first.prepare_id.low != 0);
  CHECK(first.step == 0);
  CHECK(first.result_epoch == 1);
  CHECK(first.result_revision == 1);
  CHECK(first.native_state.length == IRFQ_INFINITE_NATIVE_STATE_BYTES_V2);
  CHECK(first.output.length == 0);
  CHECK(first.action_count == 0);

  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  CHECK(irfq_infinite_prepare_v2(session, &request, &pending) == IRFQ_INFINITE_STATUS_PLAN_PENDING_V2);

  irfq_infinite_abort_request_v2 abortRequest{};
  init(abortRequest);
  abortRequest.prepare_id = first.prepare_id;
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(session, &abortRequest, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(operation.cache_revision == 0);

  PlanBuffers secondBuffers;
  auto second = secondBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &second) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(second.prepare_id.low != first.prepare_id.low);
  CHECK(
      std::equal(
          second.native_state.data,
          second.native_state.data + second.native_state.length,
          first.native_state.data));

  irfq_infinite_apply_committed_request_v2 applyRequest{};
  init(applyRequest);
  applyRequest.prepare_id = second.prepare_id;
  applyRequest.result_revision = second.result_revision;
  std::memcpy(applyRequest.native_state_sha256, second.native_state_sha256, 32);
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &applyRequest, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(operation.cache_revision == 1);
  init(operation);
  CHECK(irfq_infinite_apply_committed_v2(session, &applyRequest, &operation) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);

  auto *left = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      second.native_state.data,
      second.native_state.length,
      1,
      1,
      0,
      0);
  auto *right = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      second.native_state.data,
      second.native_state.length,
      1,
      1,
      0,
      0);
  REQUIRE(left != nullptr);
  REQUIRE(right != nullptr);
  request.expected_revision = 1;
  request.now_tai_ns = 4;
  request.now_utc_ns = 5;
  const std::array<std::uint8_t, 32> secondIdentity{{0x6d, 0xfa, 0xf5, 0x1a, 0x19, 0x54, 0x54, 0xf0, 0x71, 0xa4, 0xb6,
                                                     0x2d, 0xbb, 0x75, 0x8c, 0xf0, 0x12, 0x3a, 0xd8, 0x0e, 0x0c, 0x66,
                                                     0x3c, 0x41, 0x3c, 0x91, 0x0d, 0xde, 0xc6, 0xaf, 0x4d, 0xee}};
  std::memcpy(request.event_identity_sha256, secondIdentity.data(), secondIdentity.size());
  request.event_identity_sha256[0] ^= 1;
  PlanBuffers changedIdentityBuffers;
  auto changedIdentity = changedIdentityBuffers.response();
  CHECK(irfq_infinite_prepare_v2(left, &request, &changedIdentity) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  request.event_identity_sha256[0] ^= 1;

  PlanBuffers leftBuffers;
  PlanBuffers rightBuffers;
  auto leftResult = leftBuffers.response();
  auto rightResult = rightBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(left, &request, &leftResult) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_prepare_v2(right, &request, &rightResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(
      std::equal(
          leftResult.native_state.data,
          leftResult.native_state.data + leftResult.native_state.length,
          rightResult.native_state.data));
  CHECK(
      std::equal(
          std::begin(leftResult.native_state_sha256),
          std::end(leftResult.native_state_sha256),
          std::begin(rightResult.native_state_sha256)));

  CHECK(irfq_infinite_destroy_v2(right) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(left) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 detached planner preserves ordinary acceptor Logon and reset response",
    "[infinite][adapter][v2][stock-smoke][session-next][logon]") {
  const auto sessionsBefore = FIX::Session::numSessions();
  const auto ordinary = FIX::InfiniteSessionPlanner::logon(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      1,
      1,
      INT64_C(1700000000123456000),
      false);
  CHECK(ordinary.output.find("\00135=A\001") != std::string::npos);
  CHECK(ordinary.output.find("\001108=30\001") != std::string::npos);
  CHECK(ordinary.output.find("\001141=Y\001") == std::string::npos);
  CHECK(ordinary.nextSenderSequence == 2);
  CHECK(ordinary.nextTargetSequence == 2);

  const auto reset = FIX::InfiniteSessionPlanner::logon(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      9,
      7,
      INT64_C(1700000000123456000),
      true);
  CHECK(reset.output.find("\00135=A\001") != std::string::npos);
  CHECK(reset.output.find("\00134=1\001") != std::string::npos);
  CHECK(reset.output.find("\001108=30\001") != std::string::npos);
  CHECK(reset.output.find("\001141=Y\001") != std::string::npos);
  CHECK(reset.nextSenderSequence == 2);
  CHECK(reset.nextTargetSequence == 2);
  CHECK(FIX::Session::numSessions() == sessionsBefore);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke promotes first Logon through ordinary Session next",
    "[infinite][adapter][v2][stock-smoke][session-next][logon]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(session != nullptr);
  std::array<std::uint8_t, 32> payload{};
  payload.fill(0x7a);
  const std::array<std::uint8_t, 32> identity{{0xb9, 0x5f, 0x65, 0x61, 0xc4, 0xb9, 0x54, 0xae, 0x24, 0x95, 0x8c,
                                               0x1e, 0xe5, 0x88, 0xe2, 0x1c, 0xc9, 0xbb, 0x37, 0xab, 0x64, 0x5b,
                                               0x4f, 0x7b, 0xa5, 0x10, 0x2c, 0x41, 0x1d, 0xbd, 0xfb, 0xd9}};
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2;
  std::copy(identity.begin(), identity.end(), request.event_identity_sha256);
  request.expected_epoch = 1;
  request.now_tai_ns = 2;
  request.now_utc_ns = 2;
  request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
  request.next_original_value = 1;
  request.payload = {payload.data(), payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string wire(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(wire.find("\00135=A\001") != std::string::npos);
  CHECK(wire.find("\001108=30\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 2);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read64(result.native_state.data + 180) == UINT64_C(135));
  CHECK(result.action_count == 1);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke resumes one exact admin output without replanning",
    "[infinite][adapter][v2][stock-smoke][resume]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(fresh != nullptr);

  std::array<std::uint8_t, 32> closeSubject{};
  closeSubject.fill(0x55);
  const std::array<std::uint8_t, 32> closeIdentity{{0xe8, 0xa4, 0x11, 0xbb, 0x74, 0xb3, 0xea, 0x50, 0xe7, 0x3f, 0xa2,
                                                    0xdf, 0x83, 0xe9, 0xc0, 0xb4, 0xf0, 0xe7, 0xd9, 0xf0, 0xfc, 0x8d,
                                                    0x75, 0xd6, 0x49, 0xb7, 0xc9, 0x6e, 0x45, 0xc5, 0xe9, 0x71}};
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  std::memcpy(close.event_identity_sha256, closeIdentity.data(), closeIdentity.size());
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 3;
  close.payload = {closeSubject.data(), closeSubject.size()};
  PlanBuffers closeBuffers;
  auto closeResult = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &closeResult) == IRFQ_INFINITE_STATUS_READY_V2);

  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> attached{};
  std::copy_n(closeResult.native_state.data, closeResult.native_state.length, attached.begin());
  write64(attached.data() + 96, 2);
  write64(attached.data() + 104, 3);
  write64(attached.data() + 112, 2);
  write64(attached.data() + 120, 3);
  write64(attached.data() + 180, UINT64_C(1) | UINT64_C(2) | UINT64_C(4) | UINT64_C(128));
  write32(attached.data() + 288, 10);
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      attached.data(),
      attached.size(),
      1,
      1,
      0,
      0);
  REQUIRE(session != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 36> heartbeatPayload{};
  std::fill_n(heartbeatPayload.begin(), 32, std::uint8_t{0x66});
  const std::array<std::uint8_t, 32> heartbeatIdentity{
      {0xec, 0xf4, 0x91, 0x7f, 0xa7, 0xf6, 0x91, 0xad, 0xb6, 0x26, 0xdc, 0xa7, 0x38, 0x9c, 0xc7, 0xd4,
       0xb1, 0xf7, 0xa2, 0x76, 0xad, 0x50, 0xc8, 0xf4, 0x3e, 0xad, 0x25, 0x37, 0x57, 0x17, 0x7c, 0xf8}};
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ADMIN_HEARTBEAT_V2;
  std::memcpy(request.event_identity_sha256, heartbeatIdentity.data(), heartbeatIdentity.size());
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = 4;
  request.now_utc_ns = 5;
  request.payload = {heartbeatPayload.data(), heartbeatPayload.size()};

  PlanBuffers initialBuffers;
  auto initial = initialBuffers.response();
  initial.output.capacity = 0;
  initial.output.data = nullptr;
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &initial) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  REQUIRE(initial.required_output_capacity > 1);
  CHECK(initial.step == 0);
  CHECK(initial.native_state.length == 0);
  CHECK(initial.action_count == 0);

  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = initial.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_OUTPUT_V2;
  PlanBuffers shortBuffers;
  auto shortResult = shortBuffers.response();
  shortResult.output.capacity = initial.required_output_capacity - 1;
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &shortResult) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  CHECK(shortResult.step == 0);
  CHECK(shortResult.required_output_capacity == initial.required_output_capacity);

  PlanBuffers exactBuffers;
  auto exact = exactBuffers.response();
  exact.output.capacity = initial.required_output_capacity;
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &exact) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(exact.step == 1);
  CHECK(exact.output.length == initial.required_output_capacity);
  CHECK(exact.action_count == 1);
  CHECK(exact.actions[0].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(exact.actions[0].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
  const std::array<std::uint8_t, 32> rawFrameSha256{{0xa4, 0x98, 0x0d, 0x2c, 0x9a, 0xc7, 0x0e, 0x7f, 0x8d, 0x28, 0xe0,
                                                     0xf7, 0x73, 0x35, 0xb0, 0xae, 0x5a, 0xfd, 0x8b, 0xdb, 0xe4, 0x41,
                                                     0xdb, 0x0e, 0xcb, 0x8d, 0xef, 0xb3, 0x5d, 0xd0, 0xf0, 0x11}};
  CHECK(std::equal(rawFrameSha256.begin(), rawFrameSha256.end(), exact.actions[0].binding_sha256));
  const std::string wire(reinterpret_cast<const char *>(exact.output.data), exact.output.length);
  CHECK(wire.find("\00135=0\001") != std::string::npos);
  CHECK(wire.find("\00134=1\001") != std::string::npos);
  CHECK(wire.rfind("\00110=") != std::string::npos);

  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = exact.prepare_id;
  apply.result_revision = exact.result_revision;
  std::copy_n(exact.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(operation.cache_revision == 2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke proposes target CAS only in its closed stage",
    "[infinite][adapter][v2][stock-smoke][target-cas]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(fresh != nullptr);
  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x55);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  const std::array<std::uint8_t, 32> closeIdentity{{0xe8, 0xa4, 0x11, 0xbb, 0x74, 0xb3, 0xea, 0x50, 0xe7, 0x3f, 0xa2,
                                                    0xdf, 0x83, 0xe9, 0xc0, 0xb4, 0xf0, 0xe7, 0xd9, 0xf0, 0xfc, 0x8d,
                                                    0x75, 0xd6, 0x49, 0xb7, 0xc9, 0x6e, 0x45, 0xc5, 0xe9, 0x71}};
  std::copy(closeIdentity.begin(), closeIdentity.end(), close.event_identity_sha256);
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 3;
  close.payload = {closePayload.data(), closePayload.size()};
  PlanBuffers baseBuffers;
  auto base = baseBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &base) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);

  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      base.native_state.data,
      base.native_state.length,
      1,
      1,
      0,
      0);
  REQUIRE(session != nullptr);
  std::array<std::uint8_t, 56> payload{};
  std::fill_n(payload.begin(), 32, std::uint8_t{0x77});
  write32(payload.data() + 32, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(payload.data() + 36, 1);
  write32(payload.data() + 44, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(payload.data() + 48, 2);
  const std::array<std::uint8_t, 32> identity{{0x94, 0x2e, 0x3b, 0xa3, 0xf6, 0x18, 0x81, 0xa2, 0xc7, 0x23, 0x36,
                                               0x89, 0x6c, 0x52, 0x1f, 0x3d, 0xbd, 0x17, 0x2f, 0xa8, 0x36, 0x60,
                                               0x91, 0xc7, 0x18, 0xd5, 0xe8, 0xcf, 0x9b, 0xd5, 0x0d, 0x39}};
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  request.event = IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2;
  std::copy(identity.begin(), identity.end(), request.event_identity_sha256);
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = 4;
  request.now_utc_ns = 5;
  request.payload = {payload.data(), payload.size()};
  PlanBuffers insufficientBuffers;
  auto insufficient = insufficientBuffers.response();
  insufficient.actions = nullptr;
  insufficient.action_capacity = 0;
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &insufficient) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(result.action_count == 1);
  irfq_infinite_declarative_action_v2 expected{};
  expected.kind = IRFQ_INFINITE_ACTION_TARGET_ADVANCE_V2;
  expected.sequence_begin = 1;
  expected.sequence_end_exclusive = 2;
  std::copy_n(payload.data(), 32, expected.binding_sha256);
  CHECK(std::memcmp(result.actions, &expected, sizeof(expected)) == 0);
  CHECK(read64(result.native_state.data + 144) == 2);

  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  CHECK(irfq_infinite_prepare_v2(session, &request, &pending) == IRFQ_INFINITE_STATUS_PLAN_PENDING_V2);

  irfq_infinite_abort_request_v2 abort{};
  init(abort);
  abort.prepare_id = result.prepare_id;
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(session, &abort, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  init(pending);
  pending.native_state = {pendingBuffers.state.data(), pendingBuffers.state.size(), 0};
  pending.output = {pendingBuffers.output.data(), pendingBuffers.output.size(), 0};
  pending.actions = pendingBuffers.actions.data();
  pending.action_capacity = pendingBuffers.actions.size();
  CHECK(irfq_infinite_prepare_v2(session, &request, &pending) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 encodes the last legal sequence successor as exhausted",
    "[infinite][adapter][v2][stock-smoke][sequence-exhaustion]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(fresh != nullptr);
  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x55);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 2;
  close.payload = {closePayload.data(), closePayload.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(fresh, close, close.event_identity_sha256));
  PlanBuffers closeBuffers;
  auto closed = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);

  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::copy_n(closed.native_state.data, closed.native_state.length, state.begin());
  write64(state.data() + 96, 2);
  write64(state.data() + 104, 2);
  write64(state.data() + 112, 2);
  write64(state.data() + 120, 2);
  write32(state.data() + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(state.data() + 132, lastLegal);
  write64(state.data() + 180, UINT64_C(1) | UINT64_C(2) | UINT64_C(4) | UINT64_C(128));
  write32(state.data() + 288, 10);
  auto *sender = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      1,
      0,
      0);
  REQUIRE(sender != nullptr);

  std::array<std::uint8_t, 36> heartbeatPayload{};
  std::fill_n(heartbeatPayload.begin(), 32, std::uint8_t{0x66});
  irfq_infinite_prepare_request_v2 heartbeat{};
  init(heartbeat);
  heartbeat.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  heartbeat.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  heartbeat.event = IRFQ_INFINITE_EVENT_ADMIN_HEARTBEAT_V2;
  heartbeat.expected_epoch = 1;
  heartbeat.expected_revision = 1;
  heartbeat.now_tai_ns = 3;
  heartbeat.now_utc_ns = 3;
  heartbeat.payload = {heartbeatPayload.data(), heartbeatPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          sender,
          heartbeat,
          heartbeat.event_identity_sha256));
  PlanBuffers heartbeatBuffers;
  auto heartbeatResult = heartbeatBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(sender, &heartbeat, &heartbeatResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(heartbeatResult.native_state.data + 132) == 0);
  CHECK(heartbeatResult.native_state.data[131] == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
  CHECK(heartbeatResult.actions[0].sequence_begin == lastLegal);
  CHECK(
      heartbeatResult.actions[0].sequence_end_exclusive
      == static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2));
  CHECK(irfq_infinite_destroy_v2(sender) == IRFQ_INFINITE_STATUS_OK_V2);

  write32(state.data() + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(state.data() + 132, 1);
  write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(state.data() + 144, lastLegal);
  auto *target = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      1,
      0,
      0);
  REQUIRE(target != nullptr);

  std::array<std::uint8_t, 56> casPayload{};
  std::fill_n(casPayload.begin(), 32, std::uint8_t{0x77});
  write32(casPayload.data() + 32, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 36, lastLegal);
  write32(casPayload.data() + 44, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
  write64(casPayload.data() + 48, 1);
  irfq_infinite_prepare_request_v2 cas{};
  init(cas);
  cas.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  cas.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  cas.event = IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2;
  cas.expected_epoch = 1;
  cas.expected_revision = 1;
  cas.now_tai_ns = 3;
  cas.now_utc_ns = 3;
  cas.payload = {casPayload.data(), casPayload.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(target, cas, cas.event_identity_sha256));
  PlanBuffers invalidBuffers;
  auto invalid = invalidBuffers.response();
  CHECK(irfq_infinite_prepare_v2(target, &cas, &invalid) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  write64(casPayload.data() + 48, 0);
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(target, cas, cas.event_identity_sha256));
  PlanBuffers casBuffers;
  auto casResult = casBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(target, &cas, &casResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(casResult.native_state.data[143] == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
  CHECK(read64(casResult.native_state.data + 144) == 0);
  CHECK(irfq_infinite_destroy_v2(target) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke finalizes the exact scheduled reset template",
    "[infinite][adapter][v2][stock-smoke][reset-final]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(fresh != nullptr);
  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x55);
  const std::array<std::uint8_t, 32> closeIdentity{{0xe8, 0xa4, 0x11, 0xbb, 0x74, 0xb3, 0xea, 0x50, 0xe7, 0x3f, 0xa2,
                                                    0xdf, 0x83, 0xe9, 0xc0, 0xb4, 0xf0, 0xe7, 0xd9, 0xf0, 0xfc, 0x8d,
                                                    0x75, 0xd6, 0x49, 0xb7, 0xc9, 0x6e, 0x45, 0xc5, 0xe9, 0x71}};
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  std::copy(closeIdentity.begin(), closeIdentity.end(), close.event_identity_sha256);
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 3;
  close.payload = {closePayload.data(), closePayload.size()};
  PlanBuffers baseBuffers;
  auto base = baseBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &base) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      base.native_state.data,
      base.native_state.length,
      1,
      1,
      0,
      0);
  REQUIRE(session != nullptr);

  std::array<std::uint8_t, 128> payload{};
  std::fill_n(payload.begin(), 32, std::uint8_t{0x99});
  std::fill_n(payload.begin() + 32, 32, std::uint8_t{0xaa});
  write32(payload.data() + 64, 2);
  write64(payload.data() + 68, 2);
  write64(payload.data() + 76, 8);
  write64(payload.data() + 84, 9);
  const std::array<std::uint8_t, 32> identity{{0x0d, 0x2f, 0xb8, 0xb6, 0x38, 0x63, 0x14, 0x70, 0x69, 0x2b, 0x1e,
                                               0xff, 0x19, 0x1c, 0x44, 0xe7, 0xcf, 0xec, 0x31, 0x1f, 0xd2, 0x68,
                                               0xe9, 0xf6, 0x07, 0xda, 0x72, 0x53, 0xe7, 0xdb, 0x73, 0x69}};
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_RESET_FINAL_V2;
  request.event = IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2;
  std::copy(identity.begin(), identity.end(), request.event_identity_sha256);
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = 10;
  request.now_utc_ns = 11;
  request.payload = {payload.data(), payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(result.result_epoch == 2);
  CHECK(result.result_revision == 1);
  CHECK(result.action_count == 0);
  CHECK(read64(result.native_state.data + 48) == 2);
  CHECK(read64(result.native_state.data + 56) == 1);
  CHECK(read64(result.native_state.data + 64) == 8);
  CHECK(read64(result.native_state.data + 72) == 9);
  CHECK(read64(result.native_state.data + 80) == 10);
  CHECK(read64(result.native_state.data + 88) == 11);
  CHECK(read64(result.native_state.data + 132) == 1);
  CHECK(read64(result.native_state.data + 144) == 1);
  CHECK(read64(result.native_state.data + 180) == 1);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 detached planner uses ordinary Session next without registration",
    "[infinite][adapter][v2][stock-smoke][session-next]") {
  const auto sessionsBefore = FIX::Session::numSessions();
  const auto plan = FIX::InfiniteSessionPlanner::heartbeat(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      7,
      9,
      INT64_C(1700000000123456000),
      "");

  CHECK(FIX::Session::numSessions() == sessionsBefore);
  CHECK(plan.nextSenderSequence == 8);
  CHECK(plan.nextTargetSequence == 9);
  CHECK(plan.output.find("\00135=0\001") != std::string::npos);
  CHECK(plan.output.find("\00134=7\001") != std::string::npos);
  CHECK(plan.output.find("\00152=20231114-22:13:20.123456\001") != std::string::npos);
  CHECK(plan.output.rfind("\00110=") != std::string::npos);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 detached planner preserves ordinary timer logout and resend behavior",
    "[infinite][adapter][v2][stock-smoke][session-next]") {
  const auto sessionsBefore = FIX::Session::numSessions();
  const auto testRequest = FIX::InfiniteSessionPlanner::testRequest(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      7,
      9,
      INT64_C(1700000000123456000));
  CHECK(testRequest.output.find("\00135=1\001") != std::string::npos);
  CHECK(testRequest.output.find("\001112=TEST\001") != std::string::npos);
  CHECK(testRequest.nextSenderSequence == 8);
  CHECK(testRequest.nextTargetSequence == 9);

  const auto logout = FIX::InfiniteSessionPlanner::logout(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      8,
      9,
      INT64_C(1700000000123456000),
      "Session time");
  CHECK(logout.output.find("\00135=5\001") != std::string::npos);
  CHECK(logout.output.find("\00158=Session time\001") != std::string::npos);
  CHECK(logout.nextSenderSequence == 9);
  CHECK(logout.nextTargetSequence == 9);

  const auto resend = FIX::InfiniteSessionPlanner::resendRequest(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      9,
      11,
      INT64_C(1700000000123456000));
  CHECK(resend.output.find("\00135=2\001") != std::string::npos);
  CHECK(resend.output.find("\0017=11\001") != std::string::npos);
  CHECK(resend.output.find("\00116=0\001") != std::string::npos);
  CHECK(resend.nextSenderSequence == 10);
  CHECK(resend.nextTargetSequence == 11);
  CHECK(FIX::Session::numSessions() == sessionsBefore);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 detached timer uses TAI boundaries and Logon last-sent",
    "[infinite][adapter][v2][stock-smoke][session-next][timer]") {
  constexpr std::uint64_t enabledLoggedOn = UINT64_C(1) | UINT64_C(2) | UINT64_C(4);
  const auto fractionalWholeSecond = FIX::InfiniteSessionPlanner::timer(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      1,
      4,
      5,
      INT64_C(2000000000),
      INT64_C(1000000000000),
      INT64_C(1900000000),
      INT64_C(2000000000),
      enabledLoggedOn,
      0,
      10,
      2);
  CHECK(fractionalWholeSecond.output.find("\00135=0\001") != std::string::npos);

  const auto largeRange = FIX::InfiniteSessionPlanner::timer(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      1,
      4,
      5,
      INT64_C(9000000000000000000),
      INT64_C(1000000000000),
      INT64_C(1000000000),
      INT64_C(9000000000000000000),
      enabledLoggedOn,
      0,
      10,
      2);
  CHECK(largeRange.output.find("\00135=0\001") != std::string::npos);

  const auto before = FIX::InfiniteSessionPlanner::timer(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      4,
      5,
      INT64_C(100000000000),
      INT64_C(1000000000000),
      INT64_C(71000000000),
      INT64_C(100000000000),
      enabledLoggedOn,
      0,
      10,
      2);
  CHECK(before.output.empty());
  CHECK_FALSE(before.disconnected);
  CHECK(before.nextSenderSequence == 4);

  const auto heartbeat = FIX::InfiniteSessionPlanner::timer(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      4,
      5,
      INT64_C(100000000000),
      INT64_C(1000000000000),
      INT64_C(70000000000),
      INT64_C(100000000000),
      enabledLoggedOn,
      0,
      10,
      2);
  CHECK(heartbeat.output.find("\00135=0\001") != std::string::npos);
  CHECK_FALSE(heartbeat.disconnected);
  CHECK(heartbeat.nextSenderSequence == 5);

  const auto testRequest = FIX::InfiniteSessionPlanner::timer(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      5,
      5,
      INT64_C(100000000000),
      INT64_C(1000000000000),
      INT64_C(100000000000),
      INT64_C(64000000000),
      enabledLoggedOn,
      0,
      10,
      2);
  CHECK(testRequest.output.find("\00135=1\001") != std::string::npos);
  CHECK(testRequest.output.find("\001112=TEST\001") != std::string::npos);
  CHECK(testRequest.testRequestCount == 1);

  constexpr std::uint64_t waitingForLogon = UINT64_C(1) | UINT64_C(4);
  const auto logonTimeout = FIX::InfiniteSessionPlanner::timer(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      6,
      5,
      INT64_C(100000000000),
      INT64_C(1000000000000),
      INT64_C(90000000000),
      INT64_C(100000000000),
      waitingForLogon,
      0,
      10,
      2);
  CHECK(logonTimeout.disconnected);
  CHECK(logonTimeout.output.empty());
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke routes dual-clock timer through ordinary Session next",
    "[infinite][adapter][v2][stock-smoke][session-next][timer]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(fresh != nullptr);
  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x55);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  const std::array<std::uint8_t, 32> closeIdentity{{0xe8, 0xa4, 0x11, 0xbb, 0x74, 0xb3, 0xea, 0x50, 0xe7, 0x3f, 0xa2,
                                                    0xdf, 0x83, 0xe9, 0xc0, 0xb4, 0xf0, 0xe7, 0xd9, 0xf0, 0xfc, 0x8d,
                                                    0x75, 0xd6, 0x49, 0xb7, 0xc9, 0x6e, 0x45, 0xc5, 0xe9, 0x71}};
  std::copy(closeIdentity.begin(), closeIdentity.end(), close.event_identity_sha256);
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 3;
  close.payload = {closePayload.data(), closePayload.size()};
  PlanBuffers closeBuffers;
  auto closed = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::copy_n(closed.native_state.data, closed.native_state.length, state.begin());
  write64(state.data() + 80, UINT64_C(99000000000));
  write64(state.data() + 88, UINT64_C(999000000000));
  write64(state.data() + 96, UINT64_C(70000000000));
  write64(state.data() + 104, UINT64_C(970000000000));
  write64(state.data() + 112, UINT64_C(100000000000));
  write64(state.data() + 120, UINT64_C(1000000000000));
  write64(state.data() + 180, UINT64_C(1) | UINT64_C(2) | UINT64_C(4) | UINT64_C(128));
  write32(state.data() + 288, 10);
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      1,
      0,
      0);
  REQUIRE(session != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 32> payload{};
  payload.fill(0x7b);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_TIMER_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_TIMER_TICK_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = INT64_C(100000000000);
  request.now_utc_ns = INT64_C(1000000000000);
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string wire(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(wire.find("\00135=0\001") != std::string::npos);
  CHECK(wire.find("\00152=19700101-00:16:40.000000\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 96) == UINT64_C(100000000000));
  CHECK(read64(result.native_state.data + 104) == UINT64_C(1000000000000));
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 detached resend planner emits ordinary exact GapFill",
    "[infinite][adapter][v2][stock-smoke][session-next][resend]") {
  const auto sessionsBefore = FIX::Session::numSessions();
  const auto plan = FIX::InfiniteSessionPlanner::gapFill(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      4,
      1,
      INT64_C(1700000000123456000),
      1,
      0);
  CHECK(plan.output.find("\00135=4\001") != std::string::npos);
  CHECK(plan.output.find("\00134=1\001") != std::string::npos);
  CHECK(plan.output.find("\00143=Y\001") != std::string::npos);
  CHECK(plan.output.find("\001122=20231114-22:13:20.123456\001") != std::string::npos);
  CHECK(plan.output.find("\001123=Y\001") != std::string::npos);
  CHECK(plan.output.find("\00136=4\001") != std::string::npos);
  CHECK(plan.nextSenderSequence == 4);
  CHECK(plan.nextTargetSequence == 2);
  CHECK(FIX::Session::numSessions() == sessionsBefore);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 distinct handles run concurrently and transfer sequential ownership",
    "[infinite][adapter][v2][stock-smoke][concurrency]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *left = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  auto *right = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(left != nullptr);
  REQUIRE(right != nullptr);
  std::array<std::uint8_t, 32> leftPayload{};
  std::array<std::uint8_t, 32> rightPayload{};
  leftPayload.fill(0x55);
  rightPayload.fill(0x55);
  const std::array<std::uint8_t, 32> identity{{0xe8, 0xa4, 0x11, 0xbb, 0x74, 0xb3, 0xea, 0x50, 0xe7, 0x3f, 0xa2,
                                               0xdf, 0x83, 0xe9, 0xc0, 0xb4, 0xf0, 0xe7, 0xd9, 0xf0, 0xfc, 0x8d,
                                               0x75, 0xd6, 0x49, 0xb7, 0xc9, 0x6e, 0x45, 0xc5, 0xe9, 0x71}};
  auto request = [&](const std::array<std::uint8_t, 32> &payload) {
    irfq_infinite_prepare_request_v2 value{};
    init(value);
    value.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    value.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    value.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
    std::copy(identity.begin(), identity.end(), value.event_identity_sha256);
    value.expected_epoch = 1;
    value.now_tai_ns = 2;
    value.now_utc_ns = 3;
    value.payload = {payload.data(), payload.size()};
    return value;
  };
  auto leftRequest = request(leftPayload);
  auto rightRequest = request(rightPayload);
  PlanBuffers leftBuffers;
  PlanBuffers rightBuffers;
  auto leftResult = leftBuffers.response();
  auto rightResult = rightBuffers.response();
  irfq_infinite_status_v2 leftStatus = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2;
  irfq_infinite_status_v2 rightStatus = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2;
  std::mutex mutex;
  std::condition_variable readyCondition;
  std::condition_variable startCondition;
  unsigned ready = 0;
  bool start = false;
  auto run = [&](irfq_infinite_session_v2 *session,
                 irfq_infinite_prepare_request_v2 &input,
                 irfq_infinite_prepare_response_v2 &output,
                 irfq_infinite_status_v2 &status) {
    {
      std::unique_lock<std::mutex> lock(mutex);
      ++ready;
      readyCondition.notify_one();
      startCondition.wait(lock, [&] { return start; });
    }
    status = irfq_infinite_prepare_v2(session, &input, &output);
  };
  std::thread leftThread(run, left, std::ref(leftRequest), std::ref(leftResult), std::ref(leftStatus));
  std::thread rightThread(run, right, std::ref(rightRequest), std::ref(rightResult), std::ref(rightStatus));
  {
    std::unique_lock<std::mutex> lock(mutex);
    readyCondition.wait(lock, [&] { return ready == 2; });
    start = true;
  }
  startCondition.notify_all();
  leftThread.join();
  rightThread.join();
  REQUIRE(leftStatus == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(rightStatus == IRFQ_INFINITE_STATUS_READY_V2);

  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = leftResult.prepare_id;
  apply.result_revision = leftResult.result_revision;
  std::copy_n(leftResult.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 applied{};
  init(applied);
  irfq_infinite_status_v2 applyStatus = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2;
  std::thread ownerThread([&] { applyStatus = irfq_infinite_apply_committed_v2(left, &apply, &applied); });
  ownerThread.join();
  CHECK(applyStatus == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(applied.cache_revision == 1);

  irfq_infinite_abort_request_v2 abort{};
  init(abort);
  abort.prepare_id = rightResult.prepare_id;
  irfq_infinite_operation_response_v2 aborted{};
  init(aborted);
  CHECK(irfq_infinite_abort_v2(right, &abort, &aborted) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(right) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(left) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke routes admin event plans through ordinary Session next",
    "[infinite][adapter][v2][stock-smoke][session-next][admin]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  REQUIRE(fresh != nullptr);
  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x55);
  const std::array<std::uint8_t, 32> closeIdentity{{0xe8, 0xa4, 0x11, 0xbb, 0x74, 0xb3, 0xea, 0x50, 0xe7, 0x3f, 0xa2,
                                                    0xdf, 0x83, 0xe9, 0xc0, 0xb4, 0xf0, 0xe7, 0xd9, 0xf0, 0xfc, 0x8d,
                                                    0x75, 0xd6, 0x49, 0xb7, 0xc9, 0x6e, 0x45, 0xc5, 0xe9, 0x71}};
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  std::copy(closeIdentity.begin(), closeIdentity.end(), close.event_identity_sha256);
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 3;
  close.payload = {closePayload.data(), closePayload.size()};
  PlanBuffers closeBuffers;
  auto closed = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> attached{};
  std::copy_n(closed.native_state.data, closed.native_state.length, attached.begin());
  write64(attached.data() + 96, 2);
  write64(attached.data() + 104, 3);
  write64(attached.data() + 112, 2);
  write64(attached.data() + 120, 3);
  write64(attached.data() + 180, UINT64_C(1) | UINT64_C(2) | UINT64_C(4) | UINT64_C(128));
  write32(attached.data() + 288, 10);
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      attached.data(),
      attached.size(),
      1,
      1,
      0,
      0);
  REQUIRE(session != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);

  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = 4;
  request.now_utc_ns = 5;

  std::array<std::uint8_t, 36> regressedPayload{};
  std::fill_n(regressedPayload.begin(), 32, std::uint8_t{0x76});
  request.event = IRFQ_INFINITE_EVENT_ADMIN_HEARTBEAT_V2;
  request.now_tai_ns = 1;
  request.payload = {regressedPayload.data(), regressedPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers regressedBuffers;
  auto regressed = regressedBuffers.response();
  CHECK(irfq_infinite_prepare_v2(session, &request, &regressed) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  request.now_tai_ns = 4;

  std::array<std::uint8_t, 32> testPayload{};
  testPayload.fill(0x77);
  const std::array<std::uint8_t, 32> testIdentity{{0xf7, 0xb2, 0xe8, 0x66, 0xc7, 0x11, 0x40, 0xed, 0x6c, 0x1f, 0xc2,
                                                   0xcd, 0x50, 0xc1, 0x69, 0x48, 0x71, 0xe5, 0x2c, 0x4c, 0xd9, 0x4c,
                                                   0x7d, 0x99, 0xbf, 0x50, 0x27, 0x30, 0x58, 0x59, 0x43, 0x98}};
  request.event = IRFQ_INFINITE_EVENT_ADMIN_TEST_REQUEST_V2;
  std::copy(testIdentity.begin(), testIdentity.end(), request.event_identity_sha256);
  request.payload = {testPayload.data(), testPayload.size()};
  PlanBuffers testBuffers;
  auto testResult = testBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &testResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(
      std::string(reinterpret_cast<char *>(testResult.output.data), testResult.output.length).find("\00135=1\001")
      != std::string::npos);
  CHECK(read64(testResult.native_state.data + 132) == 2);
  CHECK(testResult.native_state.data[195] == 1);
  irfq_infinite_abort_request_v2 abort{};
  init(abort);
  abort.prepare_id = testResult.prepare_id;
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(session, &abort, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 36> logoutPayload{};
  std::fill_n(logoutPayload.begin(), 32, std::uint8_t{0x78});
  write32(logoutPayload.data() + 32, IRFQ_INFINITE_REASON_SESSION_TIME_V2);
  const std::array<std::uint8_t, 32> logoutIdentity{{0x95, 0x6f, 0x63, 0x43, 0x71, 0x6a, 0xd0, 0x2d, 0xbd, 0xd2, 0x13,
                                                     0xe5, 0x9a, 0x3b, 0x77, 0x84, 0x2e, 0xcb, 0x1a, 0x0d, 0x87, 0x42,
                                                     0xe7, 0x42, 0xe3, 0x85, 0xde, 0x8d, 0xb8, 0x80, 0x21, 0x2a}};
  request.event = IRFQ_INFINITE_EVENT_ADMIN_LOGOUT_V2;
  std::copy(logoutIdentity.begin(), logoutIdentity.end(), request.event_identity_sha256);
  request.payload = {logoutPayload.data(), logoutPayload.size()};
  PlanBuffers logoutBuffers;
  auto logoutResult = logoutBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &logoutResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(logoutResult.action_count == 2);
  CHECK(logoutResult.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK((read64(logoutResult.native_state.data + 180) & UINT64_C(0x110)) == UINT64_C(0x110));
  init(abort);
  abort.prepare_id = logoutResult.prepare_id;
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(session, &abort, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 48> resendPayload{};
  std::fill_n(resendPayload.begin(), 32, std::uint8_t{0x79});
  write64(resendPayload.data() + 32, 1);
  const std::array<std::uint8_t, 32> resendIdentity{{0x38, 0xeb, 0x3f, 0xf0, 0xde, 0x76, 0x05, 0xe1, 0x4b, 0xfa, 0xff,
                                                     0x33, 0xe6, 0x97, 0x57, 0xd6, 0x46, 0x8e, 0x5e, 0xb9, 0x53, 0x34,
                                                     0x7c, 0xda, 0xfa, 0xc3, 0x1f, 0x86, 0x1a, 0x21, 0x60, 0x96}};
  request.event = IRFQ_INFINITE_EVENT_ADMIN_RESEND_REQUEST_V2;
  std::copy(resendIdentity.begin(), resendIdentity.end(), request.event_identity_sha256);
  request.payload = {resendPayload.data(), resendPayload.size()};
  std::array<std::uint8_t, 32> computedIdentity{};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, request, computedIdentity.data()));
  CHECK(computedIdentity == resendIdentity);
  PlanBuffers resendBuffers;
  auto resendResult = resendBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &resendResult) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string resendWire(reinterpret_cast<char *>(resendResult.output.data), resendResult.output.length);
  CHECK(resendWire.find("\00135=2\001") != std::string::npos);
  CHECK(resendWire.find("\0017=1\001") != std::string::npos);
  CHECK(resendWire.find("\00116=0\001") != std::string::npos);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}
