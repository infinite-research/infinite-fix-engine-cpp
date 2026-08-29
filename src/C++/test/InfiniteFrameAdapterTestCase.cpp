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

#include "DataDictionaryProvider.h"
#include "InfiniteSessionClassification.h"
#include "Session.h"
#include "TestHelper.h"
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
irfq_infinite_session_v2 *createInfiniteFrameAdapterStockNonconformanceSmokeSessionWithDataDictionaries(
    const std::uint8_t *config,
    std::size_t configLength,
    const std::uint8_t *nativeState,
    std::size_t nativeStateLength,
    std::uint64_t epoch,
    std::uint64_t revision,
    std::int64_t creationTaiNs,
    std::int64_t creationUtcNs,
    const DataDictionaryProvider &dictionaries) noexcept;
irfq_infinite_session_v2 *createInfiniteFrameAdapterStockNonconformanceSmokeSession(
    const std::uint8_t *config,
    std::size_t configLength,
    const std::uint8_t *nativeState,
    std::size_t nativeStateLength,
    std::uint64_t epoch,
    std::uint64_t revision,
    std::int64_t creationTaiNs,
    std::int64_t creationUtcNs) noexcept {
  static const DataDictionaryProvider dictionaries = [] {
    DataDictionaryProvider selected;
    selected.addTransportDataDictionary(BeginString("FIXT.1.1"), TestSettings::pathForSpec("FIXT11"));
    selected.addApplicationDataDictionary(ApplVerID("10"), TestSettings::pathForSpec("FIX50SP2"));
    return selected;
  }();
  return createInfiniteFrameAdapterStockNonconformanceSmokeSessionWithDataDictionaries(
      config,
      configLength,
      nativeState,
      nativeStateLength,
      epoch,
      revision,
      creationTaiNs,
      creationUtcNs,
      dictionaries);
}
bool computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
    const irfq_infinite_session_v2 *session,
    const irfq_infinite_prepare_request_v2 &request,
    std::uint8_t *identity) noexcept;
std::array<std::uint8_t, 32> computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
    const std::uint8_t *bytes,
    std::size_t length) noexcept;
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

std::string participantFrame(const std::string &type, std::uint64_t sequence, std::string fields = {}) {
  const auto lastProcessed = fields.find("369=") == std::string::npos ? "369=1\001" : "";
  return finishFix(
      "35=" + type + "\00149=PARTICIPANT\00156=VENUE\00134=" + std::to_string(sequence)
      + "\00152=20231114-22:13:20.123456\001" + lastProcessed + std::move(fields));
}

std::string participantFrame(char type, std::uint64_t sequence, std::string fields = {}) {
  return participantFrame(std::string(1, type), sequence, std::move(fields));
}

std::string quoteResponseBody(const std::string &id) { return "693=" + id + "\001694=1\001"; }

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
    const std::array<std::uint32_t, 8> &schedule = {},
    std::uint32_t heartbeatMode = 1,
    std::uint32_t configuredHeartbeat = 30,
    std::uint32_t minimumHeartbeat = 30,
    std::uint32_t maximumHeartbeat = 30,
    const std::string &venueSubId = "",
    const std::string &venueLocationId = "",
    const std::string &participantSubId = "",
    const std::string &participantLocationId = "",
    const std::string &qualifier = "") {
  std::vector<std::uint8_t> bytes{0x98, 0x32};
  cborUnsigned(bytes, 1);
  cborBytes(bytes, "FIXT.1.1");
  cborBytes(bytes, "VENUE");
  cborBytes(bytes, venueSubId);
  cborBytes(bytes, venueLocationId);
  cborBytes(bytes, "PARTICIPANT");
  cborBytes(bytes, participantSubId);
  cborBytes(bytes, participantLocationId);
  cborBytes(bytes, qualifier);
  cborDigest(bytes, 0x11);
  cborUnsigned(bytes, 1);
  cborUnsigned(bytes, scheduleMode);
  for (const auto value : schedule) {
    cborUnsigned(bytes, value);
  }
  cborUnsigned(bytes, heartbeatMode);
  cborUnsigned(bytes, configuredHeartbeat);
  cborUnsigned(bytes, minimumHeartbeat);
  cborUnsigned(bytes, maximumHeartbeat);
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

std::uint32_t read32(const std::uint8_t *bytes) {
  return static_cast<std::uint32_t>(bytes[0]) << 24 | static_cast<std::uint32_t>(bytes[1]) << 16
         | static_cast<std::uint32_t>(bytes[2]) << 8 | bytes[3];
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

irfq_infinite_session_v2 *stockLoggedOnSession(
    const std::vector<std::uint8_t> &config,
    std::uint64_t senderSequence = 2) {
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  if (session == nullptr) {
    return nullptr;
  }
  std::array<std::uint8_t, 32> payload{};
  payload.fill(0x61);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2;
  request.expected_epoch = 1;
  request.now_tai_ns = INT64_C(1700000000123456000);
  request.now_utc_ns = INT64_C(1700000000123456000);
  request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
  request.next_original_value = 1;
  request.payload = {payload.data(), payload.size()};
  if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256)) {
    irfq_infinite_destroy_v2(session);
    return nullptr;
  }
  PlanBuffers buffers;
  auto result = buffers.response();
  if (irfq_infinite_prepare_v2(session, &request, &result) != IRFQ_INFINITE_STATUS_READY_V2) {
    irfq_infinite_destroy_v2(session);
    return nullptr;
  }
  if (irfq_infinite_destroy_v2(session) != IRFQ_INFINITE_STATUS_OK_V2) {
    return nullptr;
  }
  write32(result.native_state.data + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(result.native_state.data + 132, senderSequence);
  write64(result.native_state.data + 144, 2);
  write64(result.native_state.data + 152, 1);
  return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      result.native_state.data,
      result.native_state.length,
      1,
      1,
      0,
      0);
}

struct InboundCall {
  std::string wire;
  std::vector<std::uint8_t> payload;
  irfq_infinite_prepare_request_v2 request{};

  InboundCall(irfq_infinite_session_v2 *session, std::string value, std::uint8_t subject = 0x71)
      : wire(std::move(value)),
        payload(68 + wire.size()) {
    std::fill_n(payload.begin(), 32, subject);
    write32(payload.data() + 32, wire.size());
    const auto digest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
        reinterpret_cast<const std::uint8_t *>(wire.data()),
        wire.size());
    std::copy(digest.begin(), digest.end(), payload.begin() + 36);
    std::copy(wire.begin(), wire.end(), payload.begin() + 68);
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2;
    request.stage = IRFQ_INFINITE_STAGE_HEAD_V2;
    request.event = IRFQ_INFINITE_EVENT_INBOUND_FRAME_V2;
    request.expected_epoch = 1;
    request.expected_revision = 1;
    request.now_tai_ns = INT64_C(1700000000123456001);
    request.now_utc_ns = INT64_C(1700000000123456001);
    request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
    request.next_original_value = 2;
    request.payload = {payload.data(), payload.size()};
    if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256)) {
      throw std::logic_error("Inbound identity");
    }
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
  CHECK(IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2 == 21);
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
  CHECK(outsideOutput.empty());
  const auto nextWindowOutput = runTimer(nextWindowUtc);
  CHECK(nextWindowOutput.empty());
}

TEST_CASE(
    "InfiniteFrameAdapterV2 accepts only weekly epochs with a representable next scheduled boundary",
    "[infinite][adapter][v2][profile][schedule][boundary]") {
  constexpr std::int64_t greatestWholeSecond = INT64_C(9223372036000000000);
  const auto config = otherwiseValidUnavailableProfile(2, {5, 85635, 5, 85636, 5, 85635, 5, 85636});
  const auto createStatus = [&](std::int64_t creationUtc) {
    irfq_infinite_session_create_request_v2 request{};
    init(request);
    request.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
    request.canonical_session_create_config = {config.data(), config.size()};
    request.session_epoch = 1;
    request.creation_tai_ns = creationUtc;
    request.creation_utc_ns = creationUtc;
    irfq_infinite_session_create_response_v2 response{};
    init(response);
    const auto status = irfq_infinite_session_create_v2(&request, &response);
    CHECK(response.session == nullptr);
    CHECK(response.cache_epoch == 0);
    CHECK(response.cache_revision == 0);
    return status;
  };

  CHECK(createStatus(greatestWholeSecond - 1) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  CHECK(createStatus(greatestWholeSecond) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  auto *greatest = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      greatestWholeSecond - 1,
      greatestWholeSecond - 1);
  REQUIRE(greatest != nullptr);
  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x55);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = 1;
  close.now_tai_ns = greatestWholeSecond - 1;
  close.now_utc_ns = greatestWholeSecond - 1;
  close.payload = {closePayload.data(), closePayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(greatest, close, close.event_identity_sha256));
  PlanBuffers closeBuffers;
  auto closed = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(greatest, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(greatest) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(
      FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          nullptr,
          0,
          1,
          0,
          greatestWholeSecond,
          greatestWholeSecond)
      == nullptr);

  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::copy_n(closed.native_state.data, closed.native_state.length, state.begin());
  write64(state.data() + 72, greatestWholeSecond);
  write64(state.data() + 88, greatestWholeSecond);
  irfq_infinite_session_create_request_v2 restore{};
  init(restore);
  restore.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
  restore.canonical_session_create_config = {config.data(), config.size()};
  restore.native_state = {state.data(), state.size()};
  restore.session_epoch = 1;
  restore.cache_revision = 1;
  irfq_infinite_session_create_response_v2 restoreResponse{};
  init(restoreResponse);
  CHECK(irfq_infinite_session_create_v2(&restore, &restoreResponse) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(
      FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          state.data(),
          state.size(),
          1,
          1,
          0,
          0)
      == nullptr);
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
    "InfiniteFrameAdapterV2 detached planner preserves ordinary acceptor Logon",
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
      0);
  CHECK(ordinary.output.find("\00135=A\001") != std::string::npos);
  CHECK(ordinary.output.find("\001108=30\001") != std::string::npos);
  CHECK(ordinary.output.find("\001141=Y\001") == std::string::npos);
  CHECK(ordinary.nextSenderSequence == 2);
  CHECK(ordinary.nextTargetSequence == 2);

  CHECK(FIX::Session::numSessions() == sessionsBefore);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 detached planning store reaches one terminal sentinel without rendering it",
    "[infinite][adapter][v2][stock-smoke][session-next][sequence-store-boundary]") {
  constexpr auto bound = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2);
  constexpr auto lastLegal = bound - 1;
  const auto final = FIX::InfiniteSessionPlanner::heartbeat(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      lastLegal,
      lastLegal,
      INT64_C(1700000000123456000),
      "",
      lastLegal);
  CHECK(final.output.find("\00134=" + std::to_string(lastLegal) + "\001") != std::string::npos);
  CHECK(final.output.find("\001369=" + std::to_string(lastLegal) + "\001") != std::string::npos);
  CHECK(final.nextSenderSequence == bound);
  CHECK(final.nextTargetSequence == lastLegal);

  CHECK_THROWS_AS(
      FIX::InfiniteSessionPlanner::heartbeat(
          "FIXT.1.1",
          "VENUE",
          "PARTICIPANT",
          30,
          bound,
          1,
          INT64_C(1700000000123456000),
          ""),
      std::invalid_argument);
  CHECK_THROWS_AS(
      FIX::InfiniteSessionPlanner::heartbeat(
          "FIXT.1.1",
          "VENUE",
          "PARTICIPANT",
          30,
          1,
          bound,
          INT64_C(1700000000123456000),
          ""),
      std::invalid_argument);
  CHECK_THROWS_AS(
      FIX::InfiniteSessionPlanner::
          heartbeat("FIXT.1.1", "VENUE", "PARTICIPANT", 30, 1, 1, INT64_C(1700000000123456000), "", bound),
      std::invalid_argument);
  CHECK_THROWS_AS(
      FIX::InfiniteSessionPlanner::logon(
          "FIXT.1.1",
          "VENUE",
          "PARTICIPANT",
          30,
          1,
          lastLegal,
          INT64_C(1700000000123456000)),
      std::invalid_argument);

  FIX::DataDictionaryProvider dictionaries;
  dictionaries.addTransportDataDictionary(FIX::BeginString("FIXT.1.1"), FIX::TestSettings::pathForSpec("FIXT11"));
  dictionaries.addApplicationDataDictionary(FIX::ApplVerID("10"), FIX::TestSettings::pathForSpec("FIX50SP2"));
  FIX::InfiniteSessionStaticProfile profile{};
  profile.defaultCustomApplicationVersion = "INFINITE-RFQ-1.0.0";
  profile.scheduleMode = 1;
  profile.heartbeatMode = 1;
  profile.configuredHeartbeat = 30;
  profile.minimumHeartbeat = 30;
  profile.maximumHeartbeat = 30;
  profile.timestampPrecision = 6;
  profile.maximumLatency = 120;
  profile.checkCompId = true;
  profile.checkLatency = true;
  profile.persistMessages = true;
  profile.validateLengthAndChecksum = true;
  profile.sendNextExpectedMsgSeqNum = true;
  const auto wrongCompId = finishFix("35=0\00149=EVIL\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\001");
  CHECK_THROWS_AS(
      FIX::InfiniteSessionPlanner::inbound(
          "FIXT.1.1",
          "VENUE",
          "PARTICIPANT",
          30,
          lastLegal,
          2,
          INT64_C(1700000000123456001),
          INT64_C(1700000000123456000),
          INT64_C(1700000000123456000),
          UINT64_C(135),
          0,
          1,
          wrongCompId,
          dictionaries,
          profile),
      std::invalid_argument);

  const auto invalidProfileLogon
      = participantFrame('A', 1, "369=0\00198=0\001108=30\0011407=299\0011408=INFINITE-RFQ-1.0.0\001");
  CHECK_THROWS_AS(
      FIX::InfiniteSessionPlanner::inbound(
          "FIXT.1.1",
          "VENUE",
          "PARTICIPANT",
          30,
          lastLegal,
          1,
          INT64_C(1700000000123456001),
          INT64_C(1700000000123456000),
          INT64_C(1700000000123456000),
          UINT64_C(1),
          0,
          0,
          invalidProfileLogon,
          dictionaries,
          profile),
      std::invalid_argument);

  const auto finalTarget = FIX::InfiniteSessionPlanner::inbound(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      1,
      lastLegal,
      INT64_C(1700000000123456001),
      INT64_C(1700000000123456000),
      INT64_C(1700000000123456000),
      UINT64_C(135),
      0,
      lastLegal - 1,
      participantFrame('0', lastLegal, "369=" + std::to_string(lastLegal - 1) + "\001"),
      dictionaries,
      profile);
  CHECK(finalTarget.nextTargetSequence == bound);
  CHECK(finalTarget.outputs.empty());
}

TEST_CASE(
    "InfiniteFrameAdapterV2 does not render a Logon response NextExpectedMsgSeqNum at the sequence bound",
    "[infinite][adapter][v2][stock-smoke][inbound-logon][sequence-boundary]") {
  constexpr auto bound = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2);
  constexpr auto lastLegal = bound - 1;
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
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::copy_n(closed.native_state.data, closed.native_state.length, state.begin());
  write64(state.data() + 144, lastLegal);
  write64(state.data() + 152, lastLegal - 1);
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
  InboundCall inbound(
      session,
      participantFrame(
          'A',
          lastLegal,
          "369=" + std::to_string(lastLegal - 1)
              + "\00198=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0xb8);
  inbound.request.next_original_value = lastLegal;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          inbound.request,
          inbound.request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=A\001") == std::string::npos);
  CHECK(output.find("\00135=5\001") != std::string::npos);
  CHECK(output.find("\001789=" + std::to_string(bound) + "\001") == std::string::npos);
  CHECK(read64(result.native_state.data + 144) == lastLegal);
  CHECK(read64(result.native_state.data + 152) == lastLegal - 1);
  REQUIRE(result.action_count == 3);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
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
  CHECK(wire.find("\001369=0\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 2);
  CHECK(read64(result.native_state.data + 144) == 1);
  CHECK(read64(result.native_state.data + 152) == 0);
  CHECK(read64(result.native_state.data + 180) == UINT64_C(135));
  CHECK(result.action_count == 1);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 ADMIN_LOGON preserves an earlier venue frontier when target is ahead",
    "[infinite][adapter][v2][stock-smoke][session-next][logon][frontier]") {
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
  PlanBuffers baseBuffers;
  auto base = baseBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &base) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);
  write64(base.native_state.data + 144, 5);
  write64(base.native_state.data + 152, 2);
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

  std::array<std::uint8_t, 32> payload{};
  payload.fill(0xa6);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = 3;
  request.now_utc_ns = 3;
  request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
  request.next_original_value = 1;
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\001369=2\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 144) == 5);
  CHECK(read64(result.native_state.data + 152) == 2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 ADMIN_LOGON uses the selected static identity profile",
    "[infinite][adapter][v2][stock-smoke][session-next][logon][static-profile]") {
  const auto config = otherwiseValidUnavailableProfile(
      1,
      {},
      1,
      30,
      30,
      30,
      "VENUE-SUB",
      "VENUE-LOC",
      "PARTICIPANT-SUB",
      "PARTICIPANT-LOC",
      "QUALIFIER");
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
  payload.fill(0xa7);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2;
  request.expected_epoch = 1;
  request.now_tai_ns = 2;
  request.now_utc_ns = 2;
  request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
  request.next_original_value = 1;
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00150=VENUE-SUB\001") != std::string::npos);
  CHECK(output.find("\00157=PARTICIPANT-SUB\001") != std::string::npos);
  CHECK(output.find("\001142=VENUE-LOC\001") != std::string::npos);
  CHECK(output.find("\001143=PARTICIPANT-LOC\001") != std::string::npos);
  CHECK(output.find("\0011407=299\001") != std::string::npos);
  CHECK(output.find("\0011408=INFINITE-RFQ-1.0.0\001") != std::string::npos);
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
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 3;
  close.payload = {closeSubject.data(), closeSubject.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(fresh, close, close.event_identity_sha256));
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
  const auto rawFrameSha256
      = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(exact.output.data, exact.output.length);
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
  close.expected_epoch = 1;
  close.now_tai_ns = 2;
  close.now_utc_ns = 3;
  close.payload = {closePayload.data(), closePayload.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(fresh, close, close.event_identity_sha256));
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(fresh, close, close.event_identity_sha256));
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
  CHECK(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      base.native_state.data,
      base.native_state.length,
      1,
      1,
      0,
      0);
  REQUIRE(session != nullptr);
  result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(result.action_count == 1);
  irfq_infinite_declarative_action_v2 expected{};
  expected.kind = IRFQ_INFINITE_ACTION_TARGET_ADVANCE_V2;
  expected.sequence_begin = 1;
  expected.sequence_end_exclusive = 2;
  std::copy_n(payload.data(), 32, expected.binding_sha256);
  CHECK(std::memcmp(result.actions, &expected, sizeof(expected)) == 0);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read64(result.native_state.data + 152) == read64(base.native_state.data + 152));

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
    "InfiniteFrameAdapterV2 advances only the processing frontier from its exact checkpoint event",
    "[infinite][adapter][v2][stock-smoke][processing-frontier]") {
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
  PlanBuffers baseBuffers;
  auto base = baseBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &base) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::copy_n(base.native_state.data, base.native_state.length, state.begin());
  write64(state.data() + 144, 4);
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

  std::array<std::uint8_t, 80> payload{};
  std::fill_n(payload.begin(), 32, std::uint8_t{0x76});
  write64(payload.data() + 32, 0);
  write64(payload.data() + 40, 3);
  std::fill_n(payload.begin() + 48, 32, std::uint8_t{0x91});
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = 3;
  request.now_utc_ns = 3;
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(result.result_revision == 2);
  CHECK(result.output.length == 0);
  CHECK(result.action_count == 0);
  CHECK(read64(result.native_state.data + 144) == 4);
  CHECK(read64(result.native_state.data + 152) == 3);
  for (std::size_t index = 0; index < state.size(); ++index) {
    const bool changed = (index >= 56 && index < 64) || (index >= 80 && index < 96) || (index >= 152 && index < 160);
    if (!changed) {
      CHECK(result.native_state.data[index] == state[index]);
    }
  }
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects mutated or out-of-bound processing-frontier checkpoints without a pending plan",
    "[infinite][adapter][v2][stock-smoke][processing-frontier][invalid]") {
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
  PlanBuffers baseBuffers;
  auto base = baseBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(fresh, &close, &base) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::copy_n(base.native_state.data, base.native_state.length, state.begin());
  write64(state.data() + 144, 4);

  for (const std::string variant :
       {"zero-digest", "stale-expected", "non-increasing", "target-crossing", "short", "trailing", "identity"}) {
    DYNAMIC_SECTION(variant) {
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
      std::array<std::uint8_t, 81> payload{};
      std::fill_n(payload.begin(), 32, std::uint8_t{0x76});
      write64(payload.data() + 32, variant == "stale-expected" ? 1 : 0);
      write64(payload.data() + 40, variant == "non-increasing" ? 0 : variant == "target-crossing" ? 4 : 3);
      if (variant != "zero-digest") {
        std::fill_n(payload.begin() + 48, 32, std::uint8_t{0x91});
      }
      irfq_infinite_prepare_request_v2 request{};
      init(request);
      request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
      request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
      request.event = IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2;
      request.expected_epoch = 1;
      request.expected_revision = 1;
      request.now_tai_ns = 3;
      request.now_utc_ns = 3;
      request.payload
          = {payload.data(),
             variant == "short"      ? UINT64_C(79)
             : variant == "trailing" ? UINT64_C(81)
                                     : UINT64_C(80)};
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              request,
              request.event_identity_sha256));
      if (variant == "identity") {
        payload[79] ^= 1;
      }
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      CHECK(result.native_state.length == 0);
      CHECK(result.output.length == 0);
      CHECK(result.action_count == 0);

      close.expected_revision = 1;
      close.now_tai_ns = 4;
      close.now_utc_ns = 4;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              close,
              close.event_identity_sha256));
      PlanBuffers retryBuffers;
      auto retry = retryBuffers.response();
      CHECK(irfq_infinite_prepare_v2(session, &close, &retry) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
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
  const auto config = otherwiseValidUnavailableProfile(2, {4, 0, 3, 86399, 4, 0, 3, 86399});
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
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(fresh, close, close.event_identity_sha256));
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
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_RESET_FINAL_V2;
  request.event = IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = 10;
  request.now_utc_ns = 11;
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
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
    "InfiniteFrameAdapterV2 advances scheduled reset Logon target and frontier only in their closed stages",
    "[infinite][adapter][v2][stock-smoke][reset-final][scheduled-logon-flow]") {
  const auto config = otherwiseValidUnavailableProfile(2, {4, 0, 3, 86399, 4, 0, 3, 86399});
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

  std::array<std::uint8_t, 128> resetPayload{};
  std::fill_n(resetPayload.begin(), 32, std::uint8_t{0x99});
  std::fill_n(resetPayload.begin() + 32, 32, std::uint8_t{0xaa});
  write32(resetPayload.data() + 64, 2);
  write64(resetPayload.data() + 68, 2);
  write64(resetPayload.data() + 76, 8);
  write64(resetPayload.data() + 84, 9);
  irfq_infinite_prepare_request_v2 reset{};
  init(reset);
  reset.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  reset.stage = IRFQ_INFINITE_STAGE_RESET_FINAL_V2;
  reset.event = IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2;
  reset.expected_epoch = 1;
  reset.now_tai_ns = 10;
  reset.now_utc_ns = 11;
  reset.payload = {resetPayload.data(), resetPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, reset, reset.event_identity_sha256));
  PlanBuffers resetBuffers;
  auto resetResult = resetBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &reset, &resetResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(resetResult.result_epoch == 2);
  CHECK(resetResult.result_revision == 1);
  CHECK(read64(resetResult.native_state.data + 144) == 1);
  CHECK(read64(resetResult.native_state.data + 152) == 0);
  REQUIRE(resetResult.action_count == 0);

  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = resetResult.prepare_id;
  apply.result_revision = resetResult.result_revision;
  std::copy_n(resetResult.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  InboundCall logon(
      session,
      participantFrame('A', 1, "369=0\00198=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0xb3);
  logon.request.expected_epoch = 2;
  logon.request.expected_revision = 1;
  logon.request.next_original_value = 1;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          logon.request,
          logon.request.event_identity_sha256));
  PlanBuffers logonBuffers;
  auto logonResult = logonBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &logon.request, &logonResult) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(logonResult.output.data), logonResult.output.length);
  CHECK(output.find("\00135=A\001") != std::string::npos);
  CHECK(output.find("\00134=1\001") != std::string::npos);
  CHECK(output.find("\001369=0\001") != std::string::npos);
  CHECK(read64(logonResult.native_state.data + 144) == 1);
  CHECK(read64(logonResult.native_state.data + 152) == 0);
  REQUIRE(logonResult.action_count == 2);
  CHECK(logonResult.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(logonResult.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
  CHECK(logonResult.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);

  init(apply);
  apply.prepare_id = logonResult.prepare_id;
  apply.result_revision = logonResult.result_revision;
  std::copy_n(logonResult.native_state_sha256, 32, apply.native_state_sha256);
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 56> casPayload{};
  std::copy_n(logonResult.actions[0].binding_sha256, 32, casPayload.begin());
  write32(casPayload.data() + 32, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 36, 1);
  write32(casPayload.data() + 44, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 48, 2);
  irfq_infinite_prepare_request_v2 cas{};
  init(cas);
  cas.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  cas.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  cas.event = IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2;
  cas.expected_epoch = 2;
  cas.expected_revision = 2;
  cas.now_tai_ns = logon.request.now_tai_ns + 1;
  cas.now_utc_ns = logon.request.now_utc_ns + 1;
  cas.payload = {casPayload.data(), casPayload.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, cas, cas.event_identity_sha256));
  PlanBuffers casBuffers;
  auto casResult = casBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &cas, &casResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(casResult.native_state.data + 144) == 2);
  CHECK(read64(casResult.native_state.data + 152) == 0);

  init(apply);
  apply.prepare_id = casResult.prepare_id;
  apply.result_revision = casResult.result_revision;
  std::copy_n(casResult.native_state_sha256, 32, apply.native_state_sha256);
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 80> frontierPayload{};
  std::fill_n(frontierPayload.begin(), 32, std::uint8_t{0xa1});
  write64(frontierPayload.data() + 32, 0);
  write64(frontierPayload.data() + 40, 1);
  std::fill_n(frontierPayload.begin() + 48, 32, std::uint8_t{0xa2});
  irfq_infinite_prepare_request_v2 frontier{};
  init(frontier);
  frontier.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  frontier.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  frontier.event = IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2;
  frontier.expected_epoch = 2;
  frontier.expected_revision = 3;
  frontier.now_tai_ns = logon.request.now_tai_ns + 2;
  frontier.now_utc_ns = logon.request.now_utc_ns + 2;
  frontier.payload = {frontierPayload.data(), frontierPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          frontier,
          frontier.event_identity_sha256));
  PlanBuffers frontierBuffers;
  auto frontierResult = frontierBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &frontier, &frontierResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(frontierResult.native_state.data + 144) == 2);
  CHECK(read64(frontierResult.native_state.data + 152) == 1);
  CHECK(frontierResult.output.length == 0);
  CHECK(frontierResult.action_count == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects both reset-final modes when their next weekly boundary is unrepresentable",
    "[infinite][adapter][v2][stock-smoke][reset-final][schedule-boundary]") {
  constexpr std::int64_t greatestWholeSecond = INT64_C(9223372036000000000);
  const auto config = otherwiseValidUnavailableProfile(2, {5, 85635, 5, 85636, 5, 85635, 5, 85636});
  for (const std::uint32_t responseMode : {1, 2}) {
    for (const bool representable : {true, false}) {
      DYNAMIC_SECTION("mode=" << responseMode << " representable=" << representable) {
        const auto creation = greatestWholeSecond - (representable ? 1 : 0);
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
        const auto held
            = responseMode == 1
                  ? finishFix(
                        "35=A\00134=1\00149=PARTICIPANT\00152="
                        + std::string(representable ? "22620411-23:47:15.999999\001" : "22620411-23:47:16.000000\001")
                        + "56=VENUE\001369=0\00198=0\001108=30\001141=Y\0011137=10\0011407=299\001"
                          "1408=INFINITE-RFQ-1.0.0\001")
                  : std::string{};
        std::vector<std::uint8_t> payload(128, 0);
        payload.insert(payload.end(), held.begin(), held.end());
        std::fill_n(payload.begin(), 32, std::uint8_t{0x99});
        std::fill_n(payload.begin() + 32, 32, std::uint8_t{0xaa});
        write32(payload.data() + 64, responseMode);
        write64(payload.data() + 68, 2);
        write64(payload.data() + 76, creation);
        write64(payload.data() + 84, creation);
        write32(payload.data() + 92, held.size());
        if (!held.empty()) {
          const auto heldDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
              reinterpret_cast<const std::uint8_t *>(held.data()),
              held.size());
          std::copy(heldDigest.begin(), heldDigest.end(), payload.begin() + 96);
          std::copy(held.begin(), held.end(), payload.begin() + 128);
        }
        irfq_infinite_prepare_request_v2 request{};
        init(request);
        request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
        request.stage = IRFQ_INFINITE_STAGE_RESET_FINAL_V2;
        request.event = IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2;
        request.expected_epoch = 1;
        request.now_tai_ns = creation;
        request.now_utc_ns = creation;
        request.payload = {payload.data(), payload.size()};
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                session,
                request,
                request.event_identity_sha256));
        PlanBuffers buffers;
        auto result = buffers.response();
        const auto expected = representable ? IRFQ_INFINITE_STATUS_READY_V2 : IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
        REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == expected);
        if (representable) {
          CHECK(result.result_epoch == 2);
          CHECK(result.result_revision == 1);
          CHECK(result.action_count == (responseMode == 1 ? 1 : 0));
        } else {
          CHECK(result.native_state.length == 0);
          CHECK(result.output.length == 0);
          CHECK(result.action_count == 0);
        }
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      }
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects scheduled RESET_FINAL for a nonstop profile",
    "[infinite][adapter][v2][stock-smoke][reset-final][schedule-mode]") {
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
  std::array<std::uint8_t, 128> payload{};
  std::fill_n(payload.begin(), 32, std::uint8_t{0x99});
  std::fill_n(payload.begin() + 32, 32, std::uint8_t{0xaa});
  write32(payload.data() + 64, 2);
  write64(payload.data() + 68, 2);
  write64(payload.data() + 76, 1);
  write64(payload.data() + 84, 1);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_RESET_FINAL_V2;
  request.event = IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2;
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
  CHECK(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(result.native_state.length == 0);
  CHECK(result.output.length == 0);
  CHECK(result.action_count == 0);
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

  const FIX::TimeRange thursdayToFriday(FIX::UtcTimeOnly(0, 0, 0), FIX::UtcTimeOnly(0, 0, 0), 5, 6);
  const auto afterWeeklyBoundary = FIX::InfiniteSessionPlanner::timer(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      7,
      9,
      1,
      INT64_C(172800000000000),
      INT64_C(172800000000000),
      1,
      1,
      enabledLoggedOn,
      0,
      10,
      2,
      thursdayToFriday,
      thursdayToFriday,
      false);
  CHECK(afterWeeklyBoundary.output.empty());
  CHECK_FALSE(afterWeeklyBoundary.disconnected);
  CHECK(afterWeeklyBoundary.nextSenderSequence == 7);
  CHECK(afterWeeklyBoundary.nextTargetSequence == 9);
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

TEST_CASE(
    "InfiniteFrameAdapterV2 detached planner renders exact ordinary Session Reject",
    "[infinite][adapter][v2][stock-smoke][session-next][admin-reject]") {
  const auto sessionsBefore = FIX::Session::numSessions();
  const auto plan = FIX::InfiniteSessionPlanner::reject(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      7,
      9,
      INT64_C(1700000000123456000),
      9,
      108,
      5,
      "D");
  const auto expected = finishFix(
      "35=3\00134=7\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\001369=0\00145=9\001"
      "58=Value is incorrect (out of range) for this tag\001371=108\001372=D\001373=5\001");

  CHECK(plan.output == expected);
  CHECK(plan.nextSenderSequence == 8);
  CHECK(plan.nextTargetSequence == 10);
  CHECK_FALSE(plan.disconnected);
  CHECK(FIX::Session::numSessions() == sessionsBefore);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke plans ADMIN_REJECT with exact payload state and action",
    "[infinite][adapter][v2][stock-smoke][admin-reject]") {
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

  std::array<std::uint8_t, 32> logonPayload{};
  logonPayload.fill(0x61);
  irfq_infinite_prepare_request_v2 logon{};
  init(logon);
  logon.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  logon.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  logon.event = IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2;
  logon.expected_epoch = 1;
  logon.now_tai_ns = INT64_C(1700000000123456000);
  logon.now_utc_ns = INT64_C(1700000000123456000);
  logon.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
  logon.next_original_value = 1;
  logon.payload = {logonPayload.data(), logonPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, logon, logon.event_identity_sha256));
  PlanBuffers logonBuffers;
  auto loggedOn = logonBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &logon, &loggedOn) == IRFQ_INFINITE_STATUS_READY_V2);
  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = loggedOn.prepare_id;
  apply.result_revision = loggedOn.result_revision;
  std::copy_n(loggedOn.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 53> payload{};
  std::fill_n(payload.begin(), 32, std::uint8_t{0x62});
  write64(payload.data() + 32, 2);
  write32(payload.data() + 40, 108);
  write32(payload.data() + 44, 5);
  write32(payload.data() + 48, 1);
  payload[52] = 'D';
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ADMIN_REJECT_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = INT64_C(1700000000123456000);
  request.now_utc_ns = INT64_C(1700000000123456000);
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  const auto expected = finishFix(
      "35=3\00134=2\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\001369=0\00145=2\001"
      "58=Value is incorrect (out of range) for this tag\001371=108\001372=D\001373=5\001");
  CHECK(std::string(reinterpret_cast<char *>(result.output.data), result.output.length) == expected);
  CHECK(read64(result.native_state.data + 132) == 3);
  CHECK(read64(result.native_state.data + 144) == 1);
  REQUIRE(result.action_count == 1);
  irfq_infinite_declarative_action_v2 expectedAction{};
  expectedAction.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
  expectedAction.output_class = IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2;
  expectedAction.msg_type_length = 1;
  expectedAction.msg_type[0] = '3';
  expectedAction.sequence_begin = 2;
  expectedAction.sequence_end_exclusive = 3;
  expectedAction.output_length = expected.size();
  const auto expectedDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
      reinterpret_cast<const std::uint8_t *>(expected.data()),
      expected.size());
  std::copy(expectedDigest.begin(), expectedDigest.end(), expectedAction.binding_sha256);
  CHECK(std::memcmp(result.actions, &expectedAction, sizeof(expectedAction)) == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke plans registered at-target Heartbeat through Session next",
    "[infinite][adapter][v2][stock-smoke][inbound-head]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto wire = participantFrame('0', 2);
  std::vector<std::uint8_t> payload(68 + wire.size());
  std::fill_n(payload.begin(), 32, std::uint8_t{0x71});
  write32(payload.data() + 32, wire.size());
  const auto frameDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
      reinterpret_cast<const std::uint8_t *>(wire.data()),
      wire.size());
  std::copy(frameDigest.begin(), frameDigest.end(), payload.begin() + 36);
  std::copy(wire.begin(), wire.end(), payload.begin() + 68);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2;
  request.stage = IRFQ_INFINITE_STAGE_HEAD_V2;
  request.event = IRFQ_INFINITE_EVENT_INBOUND_FRAME_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = INT64_C(1700000000123456001);
  request.now_utc_ns = INT64_C(1700000000123456001);
  request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
  request.next_original_value = 2;
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  CHECK(result.output.length == 0);
  CHECK(read64(result.native_state.data + 112) == static_cast<std::uint64_t>(request.now_tai_ns));
  CHECK(read64(result.native_state.data + 120) == static_cast<std::uint64_t>(request.now_utc_ns));
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 1);
  irfq_infinite_declarative_action_v2 expected{};
  expected.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
  expected.disposition = IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2;
  expected.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
  expected.sequence_begin = 2;
  expected.sequence_end_exclusive = 3;
  expected.input_offset = 68;
  expected.input_length = wire.size();
  std::fill_n(expected.binding_sha256, 32, std::uint8_t{0x71});
  CHECK(std::memcmp(result.actions, &expected, sizeof(expected)) == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke answers at-target TestRequest through Session next",
    "[infinite][adapter][v2][stock-smoke][inbound-test-request]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('1', 2, "112=PING\001"), 0x7b);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const auto expected
      = finishFix("35=0\00134=2\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\001369=1\001112=PING\001");
  CHECK(std::string(reinterpret_cast<char *>(result.output.data), result.output.length) == expected);
  CHECK(read64(result.native_state.data + 132) == 3);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 2);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[1].msg_type[0] == '0');
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke closes at-target Logout with canonical terminal state",
    "[infinite][adapter][v2][stock-smoke][inbound-logout]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('5', 2), 0x7c);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(
      std::string(reinterpret_cast<char *>(result.output.data), result.output.length).find("\00135=5\001")
      != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 3);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read64(result.native_state.data + 180) == UINT64_C(415));
  CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_PROTOCOL_V2);
  REQUIRE(result.action_count == 3);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke completes allowed at-target GapFill through target and frontier stages",
    "[infinite][adapter][v2][stock-smoke][inbound-gap-fill]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('4', 2, "36=4\001123=Y\001"), 0x7d);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  CHECK(pending.subject_sequence == 2);
  CHECK(pending.msg_type_length == 1);
  CHECK(pending.msg_type[0] == '4');
  CHECK(pending.input_source == IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  resume.subject_sequence = pending.subject_sequence;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2;
  resume.input_source = pending.input_source;
  resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(result.output.length == 0);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 1);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
  CHECK(std::equal(pending.subject_sha256, pending.subject_sha256 + 32, result.actions[0].binding_sha256));

  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = result.prepare_id;
  apply.result_revision = result.result_revision;
  std::copy_n(result.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 56> casPayload{};
  std::copy_n(result.actions[0].binding_sha256, 32, casPayload.begin());
  write32(casPayload.data() + 32, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 36, 2);
  write32(casPayload.data() + 44, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 48, 4);
  irfq_infinite_prepare_request_v2 cas{};
  init(cas);
  cas.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  cas.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  cas.event = IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2;
  cas.expected_epoch = 1;
  cas.expected_revision = 2;
  cas.now_tai_ns = inbound.request.now_tai_ns + 1;
  cas.now_utc_ns = inbound.request.now_utc_ns + 1;
  cas.payload = {casPayload.data(), casPayload.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, cas, cas.event_identity_sha256));
  PlanBuffers casBuffers;
  auto casResult = casBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &cas, &casResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(casResult.native_state.data + 144) == 4);
  CHECK(read64(casResult.native_state.data + 152) == 1);
  REQUIRE(casResult.action_count == 1);
  CHECK(casResult.actions[0].kind == IRFQ_INFINITE_ACTION_TARGET_ADVANCE_V2);
  CHECK(casResult.actions[0].sequence_begin == 2);
  CHECK(casResult.actions[0].sequence_end_exclusive == 4);

  init(apply);
  apply.prepare_id = casResult.prepare_id;
  apply.result_revision = casResult.result_revision;
  std::copy_n(casResult.native_state_sha256, 32, apply.native_state_sha256);
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 80> frontierPayload{};
  std::fill_n(frontierPayload.begin(), 32, std::uint8_t{0xa1});
  write64(frontierPayload.data() + 32, 1);
  write64(frontierPayload.data() + 40, 3);
  std::fill_n(frontierPayload.begin() + 48, 32, std::uint8_t{0xa2});
  irfq_infinite_prepare_request_v2 frontier{};
  init(frontier);
  frontier.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  frontier.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  frontier.event = IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2;
  frontier.expected_epoch = 1;
  frontier.expected_revision = 3;
  frontier.now_tai_ns = inbound.request.now_tai_ns + 2;
  frontier.now_utc_ns = inbound.request.now_utc_ns + 2;
  frontier.payload = {frontierPayload.data(), frontierPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          frontier,
          frontier.event_identity_sha256));
  PlanBuffers frontierBuffers;
  auto frontierResult = frontierBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &frontier, &frontierResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(frontierResult.native_state.data + 144) == 4);
  CHECK(read64(frontierResult.native_state.data + 152) == 3);
  CHECK(frontierResult.output.length == 0);
  CHECK(frontierResult.action_count == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke denies at-target GapFill with one non-consuming ResendRequest",
    "[infinite][adapter][v2][stock-smoke][inbound-gap-fill][reject]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('4', 2, "36=4\001123=Y\001"), 0x7d);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  resume.subject_sequence = pending.subject_sequence;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2;
  resume.input_source = pending.input_source;
  resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=2\001") != std::string::npos);
  CHECK(output.find("\0017=2\001") != std::string::npos);
  CHECK(output.find("\00116=0\001") != std::string::npos);
  CHECK(output.find("\001369=1\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 3);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read64(result.native_state.data + 152) == 1);
  REQUIRE(result.action_count == 2);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[1].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
  CHECK(result.actions[1].msg_type[0] == '2');
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 denies the final eligible GapFill without fabricating an out-of-domain trigger",
    "[infinite][adapter][v2][stock-smoke][inbound-gap-fill][reject][sequence-boundary]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *loggedOn = stockLoggedOnSession(config);
  REQUIRE(loggedOn != nullptr);
  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x55);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = 1;
  close.expected_revision = 1;
  close.now_tai_ns = INT64_C(1700000000123456001);
  close.now_utc_ns = INT64_C(1700000000123456001);
  close.payload = {closePayload.data(), closePayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(loggedOn, close, close.event_identity_sha256));
  PlanBuffers closeBuffers;
  auto closed = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(loggedOn, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(loggedOn) == IRFQ_INFINITE_STATUS_OK_V2);

  constexpr auto sequence = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 2;
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::copy_n(closed.native_state.data, closed.native_state.length, state.begin());
  write64(state.data() + 144, sequence);
  write64(state.data() + 180, UINT64_C(1) | UINT64_C(2) | UINT64_C(4) | UINT64_C(128));
  write32(state.data() + 288, 10);
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      2,
      0,
      0);
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      participantFrame('4', sequence, "36=" + std::to_string(sequence + 1) + "\001123=Y\001"),
      0x7d);
  inbound.request.expected_revision = 2;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          inbound.request,
          inbound.request.event_identity_sha256));
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  resume.subject_sequence = pending.subject_sequence;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2;
  resume.input_source = pending.input_source;
  resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\0017=" + std::to_string(sequence) + "\001") != std::string::npos);
  CHECK(output.find("\00116=0\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 144) == sequence);
  CHECK(read64(result.native_state.data + 152) == 1);
  REQUIRE(result.action_count == 2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
  CHECK(result.actions[1].msg_type[0] == '2');
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke handles at-target ResendRequest through Session next",
    "[infinite][adapter][v2][stock-smoke][inbound-resend-request]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('2', 2, "7=1\00116=0\001"), 0x7e);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=4\001") != std::string::npos);
  CHECK(output.find("\001123=Y\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 2);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[1].msg_type[0] == '4');
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes ResendRequest ranges outside the closed sequence domain",
    "[infinite][adapter][v2][stock-smoke][inbound-resend-request][sequence-boundary]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::string, 5> variants{{
      "7=0\00116=0\001",
      "7=9223372036854775807\00116=0\001",
      "7=18446744073709551615\00116=0\001",
      "7=1\00116=9223372036854775807\001",
      "7=1\00116=18446744073709551615\001",
  }};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION(variant) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, participantFrame('2', 2, variant), 0xb4);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      REQUIRE(result.action_count >= 3);
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(std::none_of(result.actions, result.actions + result.action_count, [](const auto &action) {
        return action.kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2
               && action.sequence_end_exclusive >= static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2);
      }));
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 requires a closed-domain LastMsgSeqNumProcessed at the Infinite trust boundary",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-fields][last-processed]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::pair<std::string, bool>, 5> variants{{
      {"", false},
      {"369=0\001", true},
      {"369=9223372036854775807\001", false},
      {"369=18446744073709551615\001", false},
      {"369=0000000000000000000001\001", false},
  }};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION("field=" << variant.first) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      const auto wire
          = finishFix("35=0\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001" + variant.first);
      InboundCall inbound(session, wire, 0xb5);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      if (variant.second) {
        CHECK(output.find("\00135=5\001") == std::string::npos);
        CHECK(result.action_count == 1);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
      } else {
        CHECK(output.find("\00135=5\001") != std::string::npos);
        CHECK(result.action_count >= 3);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
        CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      }
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects reset Logon without a valid prior-epoch frontier",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-fields][reset-logon-frontier]") {
  const auto config = otherwiseValidUnavailableProfile();
  for (const std::string lastProcessed : {"", "369=9223372036854775807\001", "369=0000000000000000000001\001"}) {
    DYNAMIC_SECTION("field=" << lastProcessed) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      const auto wire = finishFix(
          "35=A\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001" + lastProcessed
          + "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001");
      InboundCall inbound(session, wire, 0xb6);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      REQUIRE(result.action_count >= 3);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes invalid RefSeqNum and NextExpectedMsgSeqNum fields",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-fields][admin-fields]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::pair<char, std::string>, 5> variants{{
      {'3', "45=0\001371=34\001373=5\001"},
      {'3', "45=9223372036854775807\001371=34\001373=5\001"},
      {'A', "98=0\001108=30\001789=0\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"},
      {'A',
       "98=0\001108=30\001789=9223372036854775807\0011137=10\0011407=299\001"
       "1408=INFINITE-RFQ-1.0.0\001"},
      {'A',
       "369=9223372036854775807\00198=0\001108=30\0011137=10\0011407=299\001"
       "1408=INFINITE-RFQ-1.0.0\001"},
  }};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION("type=" << variant.first << " fields=" << variant.second) {
      auto *session = variant.first == '3' ? stockLoggedOnSession(config)
                                           : FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
                                                 config.data(),
                                                 config.size(),
                                                 nullptr,
                                                 0,
                                                 1,
                                                 0,
                                                 1,
                                                 1);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, participantFrame(variant.first, variant.first == '3' ? 2 : 1, variant.second), 0xb7);
      if (variant.first == 'A') {
        inbound.request.expected_revision = 0;
        inbound.request.next_original_value = 1;
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                session,
                inbound.request,
                inbound.request.event_identity_sha256));
      }
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(output.find("\00135=A\001") == std::string::npos);
      CHECK(read64(result.native_state.data + 144) == (variant.first == '3' ? 2 : 1));
      CHECK(read64(result.native_state.data + 152) == (variant.first == '3' ? 1 : 0));
      REQUIRE(result.action_count >= 3);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke preserves terminal CompID validation through Session next",
    "[infinite][adapter][v2][stock-smoke][inbound-terminal-compid]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto wire = finishFix("35=0\00149=EVIL\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\001");
  InboundCall inbound(session, wire, 0x7f);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=3\001") != std::string::npos);
  CHECK(output.find("\001373=9\001") != std::string::npos);
  CHECK(output.find("\00135=5\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 4);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2);
  REQUIRE(result.action_count == 4);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(result.actions[3].reason_code == IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 does not request an application decision before successful fromApp",
    "[infinite][adapter][v2][stock-smoke][inbound-application][terminal-compid]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto wire = finishFix(
      "35=AJ\00149=EVIL\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\001693=Q-EVIL\001694=1\001");
  InboundCall inbound(session, wire, 0xa3);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=3\001") != std::string::npos);
  CHECK(output.find("\001373=9\001") != std::string::npos);
  CHECK(output.find("\00135=5\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 4);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2);
  CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke terminalizes bad SendingTime with the exact safe reason",
    "[infinite][adapter][v2][stock-smoke][inbound-terminal-sending-time]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto wire = finishFix("35=0\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20231114-22:20:00.000000\001369=1\001");
  InboundCall inbound(session, wire, 0x98);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=3\001") != std::string::npos);
  CHECK(output.find("\001373=10\001") != std::string::npos);
  CHECK(output.find("\00135=5\001") != std::string::npos);
  CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_LATENCY_V2);
  REQUIRE(result.action_count == 4);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_LATENCY_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(result.actions[3].reason_code == IRFQ_INFINITE_REASON_LATENCY_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 validates terminal identity before too-high sequence handling",
    "[infinite][adapter][v2][stock-smoke][inbound-terminal-before-sequence]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto wire = finishFix("35=0\00149=EVIL\00156=VENUE\00134=4\00152=20231114-22:13:20.123456\001369=1\001");
  InboundCall inbound(session, wire, 0x9b);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read64(result.native_state.data + 196) == 0);
  REQUIRE(result.action_count == 4);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(result.actions[3].reason_code == IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke disconnects malformed head with no live sequence disposition",
    "[infinite][adapter][v2][stock-smoke][inbound-terminal-no-sequence]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto wire = finishFix("35=0\00149=PARTICIPANT\00156=VENUE\00152=20231114-22:13:20.123456\001");
  InboundCall inbound(session, wire, 0x99);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_PROTOCOL_V2);
  REQUIRE(result.action_count >= 1);
  CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(std::none_of(result.actions, result.actions + result.action_count, [](const auto &action) {
    return action.kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
  }));
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke attaches fresh authenticated inbound Logon through Session next",
    "[infinite][adapter][v2][stock-smoke][inbound-logon-attach]") {
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
  InboundCall inbound(
      session,
      participantFrame('A', 1, "98=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x78);
  inbound.request.expected_revision = 0;
  inbound.request.next_original_value = 1;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          inbound.request,
          inbound.request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  const auto expected = finishFix(
      "35=A\00134=1\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\001369=0\00198=0\001"
      "108=30\001789=2\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001");
  CHECK(std::string(reinterpret_cast<char *>(result.output.data), result.output.length) == expected);
  CHECK(read64(result.native_state.data + 132) == 2);
  CHECK(read64(result.native_state.data + 144) == 1);
  CHECK(read64(result.native_state.data + 180) == UINT64_C(135));
  CHECK(read64(result.native_state.data + 152) == 0);
  REQUIRE(result.action_count == 2);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[1].msg_type[0] == 'A');
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 transport close restore and fresh Logon preserve closed continuation semantics",
    "[infinite][adapter][v2][stock-smoke][inbound-logon-reattach]") {
  const auto config = otherwiseValidUnavailableProfile();
  for (const bool recovery : {false, true}) {
    DYNAMIC_SECTION("recovery=" << recovery) {
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
      std::array<std::uint8_t, 32> firstClosePayload{};
      firstClosePayload.fill(0x55);
      irfq_infinite_prepare_request_v2 firstClose{};
      init(firstClose);
      firstClose.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
      firstClose.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
      firstClose.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
      firstClose.expected_epoch = 1;
      firstClose.now_tai_ns = 2;
      firstClose.now_utc_ns = 3;
      firstClose.payload = {firstClosePayload.data(), firstClosePayload.size()};
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              fresh,
              firstClose,
              firstClose.event_identity_sha256));
      PlanBuffers templateBuffers;
      auto stateTemplate = templateBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(fresh, &firstClose, &stateTemplate) == IRFQ_INFINITE_STATUS_READY_V2);
      REQUIRE(irfq_infinite_destroy_v2(fresh) == IRFQ_INFINITE_STATUS_OK_V2);

      std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> attached{};
      std::copy_n(stateTemplate.native_state.data, stateTemplate.native_state.length, attached.begin());
      write64(attached.data() + 96, 2);
      write64(attached.data() + 104, 3);
      write64(attached.data() + 112, 2);
      write64(attached.data() + 120, 3);
      write64(attached.data() + 180, UINT64_C(135));
      write32(attached.data() + 288, 10);
      if (recovery) {
        write32(attached.data() + 220, 1);
        write32(attached.data() + 224, 3);
        write64(attached.data() + 228, 2);
        write64(attached.data() + 236, 5);
        write64(attached.data() + 244, 3);
        std::fill_n(attached.data() + 252, 32, std::uint8_t{0x95});
        write32(attached.data() + 292, 1);
        write64(attached.data() + 300, 3);
      } else {
        write32(attached.data() + 292, 4);
        write64(attached.data() + 300, 7);
      }
      auto *attachedSession = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          attached.data(),
          attached.size(),
          1,
          1,
          0,
          0);
      REQUIRE(attachedSession != nullptr);
      std::array<std::uint8_t, 32> closePayload{};
      closePayload.fill(0x96);
      irfq_infinite_prepare_request_v2 close{};
      init(close);
      close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
      close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
      close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
      close.expected_epoch = 1;
      close.expected_revision = 1;
      close.now_tai_ns = 4;
      close.now_utc_ns = 4;
      close.payload = {closePayload.data(), closePayload.size()};
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              attachedSession,
              close,
              close.event_identity_sha256));
      PlanBuffers closedBuffers;
      auto closed = closedBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(attachedSession, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(closed.output.length == 0);
      CHECK(closed.action_count == 0);
      CHECK(read64(closed.native_state.data + 180) == UINT64_C(1));
      if (recovery) {
        CHECK(read32(closed.native_state.data + 220) == 1);
        CHECK(read32(closed.native_state.data + 224) == 3);
        CHECK(read64(closed.native_state.data + 228) == 2);
        CHECK(read64(closed.native_state.data + 236) == 5);
        CHECK(read64(closed.native_state.data + 244) == 3);
        CHECK(read32(closed.native_state.data + 292) == 0);
        CHECK(read64(closed.native_state.data + 300) == 0);
      } else {
        CHECK(read32(closed.native_state.data + 292) == 4);
        CHECK(read64(closed.native_state.data + 300) == 7);
      }
      REQUIRE(irfq_infinite_destroy_v2(attachedSession) == IRFQ_INFINITE_STATUS_OK_V2);

      auto *restored = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          closed.native_state.data,
          closed.native_state.length,
          1,
          2,
          0,
          0);
      REQUIRE(restored != nullptr);
      InboundCall inbound(
          restored,
          participantFrame('A', 1, "98=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x97);
      inbound.request.expected_revision = 2;
      inbound.request.next_original_value = 1;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              restored,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers reattachedBuffers;
      auto reattached = reattachedBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(restored, &inbound.request, &reattached) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(read64(reattached.native_state.data + 144) == 1);
      CHECK(read64(reattached.native_state.data + 152) == 0);
      CHECK(read64(reattached.native_state.data + 180) == UINT64_C(135));
      CHECK(read32(reattached.native_state.data + 288) == 10);
      CHECK(read32(reattached.native_state.data + 292) == (recovery ? 1U : 4U));
      CHECK(read64(reattached.native_state.data + 300) == (recovery ? 3U : 7U));
      CHECK(read32(reattached.native_state.data + 220) == (recovery ? 1U : 0U));
      REQUIRE(reattached.action_count == 2);
      CHECK(reattached.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(reattached.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      auto *roundTrip = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          reattached.native_state.data,
          reattached.native_state.length,
          1,
          3,
          0,
          0);
      CHECK(roundTrip != nullptr);
      if (roundTrip != nullptr) {
        CHECK(irfq_infinite_destroy_v2(roundTrip) == IRFQ_INFINITE_STATUS_OK_V2);
      }
      CHECK(irfq_infinite_destroy_v2(restored) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 enforces fixed and peer-negotiated Logon heartbeat profiles before attachment",
    "[infinite][adapter][v2][stock-smoke][inbound-logon-profile]") {
  for (const auto &variant : std::array<std::pair<std::string, std::uint32_t>, 4>{
           {{"fixed-wrong", 31}, {"peer-min", 20}, {"peer-max", 40}, {"peer-outside", 41}}}) {
    DYNAMIC_SECTION(variant.first) {
      const bool fixed = variant.first == "fixed-wrong";
      const auto config
          = fixed ? otherwiseValidUnavailableProfile() : otherwiseValidUnavailableProfile(1, {}, 2, 0, 20, 40);
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
      const auto fields
          = "98=0\001108=" + std::to_string(variant.second) + "\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
      InboundCall inbound(session, participantFrame('A', 1, fields), 0xa5);
      inbound.request.expected_revision = 0;
      inbound.request.next_original_value = 1;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      const bool accepted = variant.first == "peer-min" || variant.first == "peer-max";
      const auto status = irfq_infinite_prepare_v2(session, &inbound.request, &result);
      REQUIRE(status == IRFQ_INFINITE_STATUS_READY_V2);
      if (accepted) {
        CHECK(read32(result.native_state.data + 188) == variant.second);
        const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
        CHECK(output.find("\001108=" + std::to_string(variant.second) + "\001") != std::string::npos);
        CHECK(output.find("\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001") != std::string::npos);
      } else {
        const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
        CHECK(output.find("\00135=3\001") != std::string::npos);
        CHECK(output.find("\001371=108\001") != std::string::npos);
        CHECK(output.find("\001373=5\001") != std::string::npos);
        CHECK(output.find("\00135=5\001") != std::string::npos);
        CHECK(read64(result.native_state.data + 144) == 1);
        CHECK(read64(result.native_state.data + 152) == 0);
        REQUIRE(result.action_count == 4);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
        CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects each missing or mismatched fixed Logon profile field before attachment",
    "[infinite][adapter][v2][stock-smoke][inbound-logon-profile][required]") {
  const auto config = otherwiseValidUnavailableProfile();
  struct Variant {
    std::string fields;
    std::uint32_t tag;
    std::uint32_t reason;
  };
  const std::array<Variant, 8> variants{{
      {"98=0\001108=30\0011407=299\0011408=INFINITE-RFQ-1.0.0\001", 1137, 18},
      {"98=0\001108=30\0011137=9\0011407=299\0011408=INFINITE-RFQ-1.0.0\001", 1137, 18},
      {"98=0\001108=30\0011137=10\0011408=INFINITE-RFQ-1.0.0\001", 1407, 18},
      {"98=0\001108=30\0011137=10\0011407=300\0011408=INFINITE-RFQ-1.0.0\001", 1407, 18},
      {"98=0\001108=30\0011137=10\0011407=299\001", 1408, 18},
      {"98=0\001108=30\0011137=10\0011407=299\0011408=WRONG\001", 1408, 18},
      {"98=0\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001", 108, 1},
      {"98=0\001108=030\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001", 108, 5},
  }};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION("tag=" << variant.tag << " fields=" << variant.fields) {
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
      InboundCall inbound(session, participantFrame('A', 1, variant.fields), 0xb9);
      inbound.request.expected_revision = 0;
      inbound.request.next_original_value = 1;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=3\001") != std::string::npos);
      CHECK(output.find("\001371=" + std::to_string(variant.tag) + "\001") != std::string::npos);
      CHECK(output.find("\001373=" + std::to_string(variant.reason) + "\001") != std::string::npos);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(output.find("\00135=A\001") == std::string::npos);
      CHECK(read64(result.native_state.data + 144) == 1);
      CHECK(read64(result.native_state.data + 152) == 0);
      CHECK((read64(result.native_state.data + 180) & UINT64_C(6)) == 0);
      REQUIRE(result.action_count == 4);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  const std::array<std::pair<std::string, std::uint32_t>, 2> compounds{{
      {"35=A\00149=EVIL\00156=VENUE\00134=1\00152=20231114-22:13:20.123456\001369=0\001",
       IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2},
      {"35=A\00149=PARTICIPANT\00156=VENUE\00134=1\00152=20200101-00:00:00.000000\001369=0\001",
       IRFQ_INFINITE_REASON_LATENCY_V2},
  }};
  for (const auto &compound : compounds) {
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
    InboundCall inbound(
        session,
        finishFix(compound.first + "98=0\001108=30\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
        0xba);
    inbound.request.expected_revision = 0;
    inbound.request.next_original_value = 1;
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            inbound.request,
            inbound.request.event_identity_sha256));
    PlanBuffers buffers;
    auto result = buffers.response();
    REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
    const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
    CHECK(output.find("\001371=1137\001") != std::string::npos);
    CHECK(output.find("\001373=18\001") != std::string::npos);
    CHECK(output.find("\00135=5\001") != std::string::npos);
    REQUIRE(result.action_count == 4);
    CHECK(result.actions[0].reason_code == compound.second);
    CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
    CHECK(read64(result.native_state.data + 144) == 1);
    CHECK(read64(result.native_state.data + 152) == 0);
    CHECK((read64(result.native_state.data + 180) & UINT64_C(6)) == 0);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke requests one exact at-target application decision",
    "[infinite][adapter][v2][stock-smoke][inbound-application]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto body = quoteResponseBody("RFQ-1");
  InboundCall inbound(session, participantFrame("AJ", 2, body), 0x72);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &result)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);

  REQUIRE(inbound.wire.find(body) != std::string::npos);
  CHECK(result.step == 0);
  CHECK(result.subject_sequence == 2);
  CHECK(result.msg_type_length == 2);
  CHECK(result.msg_type[0] == 'A');
  CHECK(result.msg_type[1] == 'J');
  CHECK(result.input_source == IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2);
  CHECK(result.input_item_index == 0);
  CHECK(result.input_offset == 68 + inbound.wire.find(body));
  CHECK(result.input_length == body.size());
  CHECK(std::any_of(std::begin(result.subject_sha256), std::end(result.subject_sha256), [](auto byte) {
    return byte != 0;
  }));
  CHECK(result.native_state.length == 0);
  CHECK(result.action_count == 0);

  irfq_infinite_abort_request_v2 abort{};
  init(abort);
  abort.prepare_id = result.prepare_id;
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(session, &abort, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects an ineligible nine-byte inbound MsgType without crossing fixed storage",
    "[infinite][adapter][v2][stock-smoke][inbound-application][msg-type-bound]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      finishFix(
          "35=ABCDEFGHI\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\00111=RFQ-BOUND\001"),
      0xa1);
  struct GuardedBuffers {
    std::array<std::uint8_t, 16> before{};
    PlanBuffers buffers;
    std::array<std::uint8_t, 16> after{};
  } guarded;
  guarded.before.fill(0xa5);
  guarded.after.fill(0x5a);
  auto result = guarded.buffers.response();
  CHECK(
      irfq_infinite_prepare_v2(session, &inbound.request, &result)
      != IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  CHECK(result.native_state.length == 0);
  CHECK(result.action_count == 0);
  CHECK(std::all_of(guarded.before.begin(), guarded.before.end(), [](auto byte) { return byte == 0xa5; }));
  CHECK(std::all_of(guarded.after.begin(), guarded.after.end(), [](auto byte) { return byte == 0x5a; }));
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 copies an eligible two-byte MsgType with an exact length and zero tail",
    "[infinite][adapter][v2][stock-smoke][inbound-application][msg-type-bound]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      finishFix(
          "35=AJ\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\001693=Q-1\001694="
          "1\001"),
      0xa2);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &result)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  CHECK(result.msg_type_length == 2);
  CHECK(result.msg_type[0] == 'A');
  CHECK(result.msg_type[1] == 'J');
  CHECK(std::all_of(result.msg_type + 2, result.msg_type + 8, [](auto byte) { return byte == 0; }));
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke resumes allowed application with exact dispatch and pending disposition",
    "[infinite][adapter][v2][stock-smoke][inbound-application][allow]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto body = quoteResponseBody("RFQ-2");
  InboundCall inbound(session, participantFrame("AJ", 2, body), 0x73);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);

  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.step = pending.step;
  resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  resume.subject_sequence = pending.subject_sequence;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2;
  resume.input_source = pending.input_source;
  resume.input_item_index = pending.input_item_index;
  resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  CHECK(result.step == 1);
  CHECK(result.output.length == 0);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 2);
  irfq_infinite_declarative_action_v2 dispatch{};
  dispatch.kind = IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2;
  dispatch.msg_type_length = 2;
  dispatch.msg_type[0] = 'A';
  dispatch.msg_type[1] = 'J';
  dispatch.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
  dispatch.sequence_begin = 2;
  dispatch.sequence_end_exclusive = 3;
  dispatch.input_offset = pending.input_offset;
  dispatch.input_length = body.size();
  const auto bodyDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
      reinterpret_cast<const std::uint8_t *>(body.data()),
      body.size());
  std::copy(bodyDigest.begin(), bodyDigest.end(), dispatch.binding_sha256);
  CHECK(std::memcmp(result.actions, &dispatch, sizeof(dispatch)) == 0);
  irfq_infinite_declarative_action_v2 disposition{};
  disposition.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
  disposition.disposition = IRFQ_INFINITE_DISPOSITION_PENDING_CORE_V2;
  disposition.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
  disposition.sequence_begin = 2;
  disposition.sequence_end_exclusive = 3;
  disposition.input_offset = 68;
  disposition.input_length = inbound.wire.size();
  std::copy_n(pending.subject_sha256, 32, disposition.binding_sha256);
  CHECK(std::memcmp(result.actions + 1, &disposition, sizeof(disposition)) == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke resumes rejected application with ordinary BusinessReject",
    "[infinite][adapter][v2][stock-smoke][inbound-application][reject]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto body = quoteResponseBody("RFQ-REJECT");
  InboundCall inbound(session, participantFrame("AJ", 2, body), 0x79);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  resume.subject_sequence = pending.subject_sequence;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2;
  resume.input_source = pending.input_source;
  resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=j\001") != std::string::npos);
  CHECK(output.find("\00145=2\001") != std::string::npos);
  CHECK(output.find("\001372=AJ\001") != std::string::npos);
  CHECK(output.find("\001380=3\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 3);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 3);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[1].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
  CHECK(result.actions[1].reason_code == IRFQ_INFINITE_REASON_PROTOCOL_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].msg_type[0] == 'j');
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejected application exhausts the final legal sender sequence",
    "[infinite][adapter][v2][stock-smoke][inbound-application][reject][sequence-exhaustion]") {
  const auto config = otherwiseValidUnavailableProfile();
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  auto *session = stockLoggedOnSession(config, lastLegal);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame("AJ", 2, quoteResponseBody("RFQ-LAST")), 0x6d);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  resume.subject_sequence = pending.subject_sequence;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2;
  resume.input_source = pending.input_source;
  resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
  CHECK(read64(result.native_state.data + 132) == 0);
  auto *restored = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      result.native_state.data,
      result.native_state.length,
      1,
      2,
      0,
      0);
  REQUIRE(restored != nullptr);
  CHECK(irfq_infinite_destroy_v2(restored) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 application decision mismatch consumes the volatile plan fail closed",
    "[infinite][adapter][v2][stock-smoke][inbound-application][resume-mismatch]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame("AJ", 2, quoteResponseBody("RFQ-MISMATCH")), 0x7a);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  resume.subject_sequence = pending.subject_sequence;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.subject_sha256[0] ^= 1;
  resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2;
  resume.input_source = pending.input_source;
  resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
  PlanBuffers invalidBuffers;
  auto invalid = invalidBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &invalid) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  resume.subject_sha256[0] ^= 1;
  PlanBuffers staleBuffers;
  auto stale = staleBuffers.response();
  CHECK(irfq_infinite_resume_v2(session, &resume, &stale) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 invalid resume envelopes terminalize the live decision handle",
    "[infinite][adapter][v2][stock-smoke][inbound-application][resume-envelope]") {
  const auto config = otherwiseValidUnavailableProfile();
  for (const std::string variant :
       {"request-null", "request-abi", "response-null", "response-misaligned", "response-abi"}) {
    DYNAMIC_SECTION(variant) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, participantFrame("AJ", 2, quoteResponseBody("RFQ-ENVELOPE")), 0x8d);
      PlanBuffers pendingBuffers;
      auto pending = pendingBuffers.response();
      REQUIRE(
          irfq_infinite_prepare_v2(session, &inbound.request, &pending)
          == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
      irfq_infinite_resume_request_v2 resume{};
      init(resume);
      resume.prepare_id = pending.prepare_id;
      resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
      resume.subject_sequence = pending.subject_sequence;
      std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
      resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2;
      resume.input_source = pending.input_source;
      resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
      PlanBuffers invalidBuffers;
      auto invalid = invalidBuffers.response();
      alignas(irfq_infinite_prepare_response_v2) std::array<std::uint8_t, sizeof(irfq_infinite_prepare_response_v2) + 1>
          misaligned{};
      auto *request = &resume;
      auto *response = &invalid;
      auto expected = IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
      if (variant == "request-null") {
        request = nullptr;
      } else if (variant == "request-abi") {
        resume.header.abi_version = 1;
        expected = IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2;
      } else if (variant == "response-null") {
        response = nullptr;
      } else if (variant == "response-misaligned") {
        response = reinterpret_cast<irfq_infinite_prepare_response_v2 *>(misaligned.data() + 1);
      } else {
        invalid.header.abi_version = 1;
        expected = IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2;
      }
      CHECK(irfq_infinite_resume_v2(session, request, response) == expected);

      resume.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2;
      PlanBuffers retryBuffers;
      auto retry = retryBuffers.response();
      CHECK(irfq_infinite_resume_v2(session, &resume, &retry) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      InboundCall replacement(session, participantFrame("AJ", 2, quoteResponseBody("REPLACEMENT")), 0x8e);
      PlanBuffers replacementBuffers;
      auto replacementResult = replacementBuffers.response();
      CHECK(
          irfq_infinite_prepare_v2(session, &replacement.request, &replacementResult)
          == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 application resume identity step alias and capacity failures consume the plan",
    "[infinite][adapter][v2][stock-smoke][inbound-application][resume-invalid]") {
  const auto config = otherwiseValidUnavailableProfile();
  for (const std::string variant : {"step", "alias", "capacity", "action-capacity"}) {
    DYNAMIC_SECTION(variant) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, participantFrame("AJ", 2, quoteResponseBody("RFQ-INVALID")), 0x89);
      PlanBuffers pendingBuffers;
      auto pending = pendingBuffers.response();
      REQUIRE(
          irfq_infinite_prepare_v2(session, &inbound.request, &pending)
          == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
      irfq_infinite_resume_request_v2 resume{};
      init(resume);
      resume.prepare_id = pending.prepare_id;
      resume.step = pending.step;
      resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
      resume.subject_sequence = pending.subject_sequence;
      std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
      resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2;
      resume.input_source = pending.input_source;
      resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
      PlanBuffers invalidBuffers;
      auto invalid = invalidBuffers.response();
      if (variant == "step") {
        ++resume.step;
      } else if (variant == "alias") {
        invalid.native_state.data = inbound.payload.data();
      } else if (variant == "action-capacity") {
        invalid.action_capacity = 1;
      } else {
        invalid.output.capacity = IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 + 1;
      }
      const auto expected
          = variant == "step" ? IRFQ_INFINITE_STATUS_STALE_PLAN_V2 : IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &invalid) == expected);
      resume.step = pending.step;
      PlanBuffers retryBuffers;
      auto retry = retryBuffers.response();
      CHECK(irfq_infinite_resume_v2(session, &resume, &retry) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      InboundCall replacement(session, participantFrame("AJ", 2, quoteResponseBody("REPLACEMENT")), 0x8c);
      PlanBuffers replacementBuffers;
      auto replacementResult = replacementBuffers.response();
      CHECK(
          irfq_infinite_prepare_v2(session, &replacement.request, &replacementResult)
          == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  auto *left = stockLoggedOnSession(config);
  auto *right = stockLoggedOnSession(config);
  REQUIRE(left != nullptr);
  REQUIRE(right != nullptr);
  InboundCall leftInbound(left, participantFrame("AJ", 2, quoteResponseBody("LEFT")), 0x8a);
  InboundCall rightInbound(right, participantFrame("AJ", 2, quoteResponseBody("RIGHT")), 0x8b);
  PlanBuffers leftPendingBuffers;
  PlanBuffers rightPendingBuffers;
  auto leftPending = leftPendingBuffers.response();
  auto rightPending = rightPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(left, &leftInbound.request, &leftPending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  REQUIRE(
      irfq_infinite_prepare_v2(right, &rightInbound.request, &rightPending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  irfq_infinite_resume_request_v2 cross{};
  init(cross);
  cross.prepare_id = leftPending.prepare_id;
  cross.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  cross.subject_sequence = leftPending.subject_sequence;
  std::copy_n(leftPending.subject_sha256, 32, cross.subject_sha256);
  cross.decision = IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2;
  cross.input_source = leftPending.input_source;
  cross.input_source_bytes = {leftInbound.payload.data(), leftInbound.payload.size()};
  PlanBuffers crossBuffers;
  auto crossResult = crossBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(right, &cross, &crossResult) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  cross.prepare_id = rightPending.prepare_id;
  cross.subject_sequence = rightPending.subject_sequence;
  std::copy_n(rightPending.subject_sha256, 32, cross.subject_sha256);
  cross.input_source_bytes = {rightInbound.payload.data(), rightInbound.payload.size()};
  PlanBuffers retryBuffers;
  auto retry = retryBuffers.response();
  CHECK(irfq_infinite_resume_v2(right, &cross, &retry) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  irfq_infinite_abort_request_v2 abort{};
  init(abort);
  abort.prepare_id = leftPending.prepare_id;
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(left, &abort, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(right) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(left) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke discards too-high application and requests all subsequent",
    "[infinite][adapter][v2][stock-smoke][inbound-too-high-application]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame("AJ", 4, quoteResponseBody("FUTURE")), 0x74);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  const auto resend = finishFix(
      "35=2\00134=2\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\001369=1\0017=2\00116=0\001");
  CHECK(std::string(reinterpret_cast<char *>(result.output.data), result.output.length) == resend);
  CHECK(read64(result.native_state.data + 132) == 3);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 2);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[1].msg_type[0] == '2');
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke queues too-high credential-free admin before disposition and resend",
    "[infinite][adapter][v2][stock-smoke][inbound-too-high-admin]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('0', 4), 0x75);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  REQUIRE(result.action_count == 3);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_QUEUE_INSERT_V2);
  CHECK(result.actions[0].msg_type[0] == '0');
  CHECK(result.actions[0].sequence_begin == 4);
  CHECK(result.actions[0].sequence_end_exclusive == 5);
  CHECK(std::equal(inbound.payload.begin() + 36, inbound.payload.begin() + 68, result.actions[0].binding_sha256));
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[1].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(read64(result.native_state.data + 196) == 2);
  CHECK(read64(result.native_state.data + 204) == 5);
  CHECK(read64(result.native_state.data + 212) == 2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke discards standard-valid stale PossDup without target progress",
    "[infinite][adapter][v2][stock-smoke][inbound-too-low-valid]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('0', 1, "43=Y\001122=20231114-22:13:20.123455\001"), 0x76);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(result.output.length == 0);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 1);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke terminates stale invalid OrigSendingTime variants",
    "[infinite][adapter][v2][stock-smoke][inbound-too-low-orig-invalid]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::string, 2> variants{"43=Y\001", "43=Y\001122=20231114-22:13:20.123457\001"};
  for (const auto &fields : variants) {
    DYNAMIC_SECTION(fields) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, participantFrame('0', 1, fields), 0x80);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=3\001") != std::string::npos);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK((read64(result.native_state.data + 180) & UINT64_C(272)) == UINT64_C(272));
      REQUIRE(result.action_count == 4);
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke terminates invalid stale non-PossDup through ordinary Session",
    "[infinite][adapter][v2][stock-smoke][inbound-too-low-invalid]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('0', 1), 0x77);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string wire(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(wire.find("\00135=5\001") != std::string::npos);
  CHECK(wire.find("MsgSeqNum too low, expecting 2 but received 1") != std::string::npos);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 3);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(result.actions[2].reason_code == IRFQ_INFINITE_REASON_PROTOCOL_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke rejects nonadvancing at-target SequenceReset",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-invalid]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('4', 2, "36=2\001123=Y\001"), 0x86);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=3\001") != std::string::npos);
  CHECK(output.find("\001371=36\001") != std::string::npos);
  CHECK(output.find("\001372=4\001") != std::string::npos);
  CHECK(output.find("\001373=5\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 2);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_PROTOCOL_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects absent or false at-target GapFillFlag before eligibility",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-invalid][gap-fill-flag]") {
  const auto config = otherwiseValidUnavailableProfile();
  for (const auto &variant : {std::pair<std::string, std::uint32_t>{"36=4\001123=N\001", 5}, {"36=4\001", 1}}) {
    DYNAMIC_SECTION("reason=" << variant.second) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, participantFrame('4', 2, variant.first), 0x86);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=3\001") != std::string::npos);
      CHECK(output.find("\001371=123\001") != std::string::npos);
      CHECK(output.find("\001373=" + std::to_string(variant.second) + "\001") != std::string::npos);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(output.find("\00158=Reset mode is not supported\001") != std::string::npos);
      CHECK(read64(result.native_state.data + 132) == 4);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      REQUIRE(result.action_count == 4);
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_PROTOCOL_V2);
      CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(result.actions[1].msg_type[0] == '3');
      CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(result.actions[2].msg_type[0] == '5');
      CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes invalid at-target GapFill NewSeqNo before eligibility",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-invalid][new-seq-bound]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::pair<std::string, bool>, 6> variants{{
      {"123=Y\001", true},
      {"36=NOPE\001123=Y\001", true},
      {"36=0\001123=Y\001", false},
      {"36=9223372036854775807\001123=Y\001", false},
      {"36=18446744073709551615\001123=Y\001", false},
      {"36=0000000000000000000004\001123=Y\001", false},
  }};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION(variant.first) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, participantFrame('4', 2, variant.first), 0x86);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      if (variant.second) {
        CHECK(output.find("\00135=3\001") != std::string::npos);
      }
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      REQUIRE(result.action_count == (variant.second ? 4 : 3));
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_PROTOCOL_V2);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes invalid future and stale GapFill before sequence routing",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-invalid][terminal-position]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::string, 7> variants{{
      "36=4\001123=N\001",
      "36=4\001",
      "123=Y\001",
      "36=NOPE\001123=Y\001",
      "36=0\001123=Y\001",
      "36=9223372036854775807\001123=Y\001",
      "36=18446744073709551615\001123=Y\001",
  }};
  for (const auto sequence : {UINT64_C(1), UINT64_C(4)}) {
    for (const auto &variant : variants) {
      DYNAMIC_SECTION("sequence=" << sequence << " " << variant) {
        auto *session = stockLoggedOnSession(config);
        REQUIRE(session != nullptr);
        const auto duplicateHeader = sequence == 1 ? "43=Y\001122=20231114-22:13:20.123455\001" : "";
        InboundCall inbound(session, participantFrame('4', sequence, duplicateHeader + variant), 0x86);
        PlanBuffers buffers;
        auto result = buffers.response();
        REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
        const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
        CHECK(output.find("\00135=5\001") != std::string::npos);
        CHECK(output.find("\00135=2\001") == std::string::npos);
        CHECK(read64(result.native_state.data + 144) == 2);
        CHECK(read64(result.native_state.data + 152) == 1);
        REQUIRE(result.action_count >= 3);
        CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
        CHECK(std::none_of(result.actions, result.actions + result.action_count, [](const auto &action) {
          return action.kind == IRFQ_INFINITE_ACTION_QUEUE_INSERT_V2;
        }));
        CHECK(std::none_of(result.actions, result.actions + result.action_count, [](const auto &action) {
          return action.kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2 && action.msg_type_length == 1
                 && action.msg_type[0] == '2';
        }));
        CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      }
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes SequenceReset wire MsgSeqNum outside the closed sequence domain",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-invalid][wire-seq-bound]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::uint64_t, 4> sequences{{
      0,
      UINT64_C(9223372036854775807),
      UINT64_C(9223372036854775808),
      UINT64_MAX,
  }};
  for (const auto sequence : sequences) {
    DYNAMIC_SECTION("sequence=" << sequence) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, participantFrame('4', sequence, "36=4\001123=Y\001"), 0xb1);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(output.find("\00135=2\001") == std::string::npos);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      CHECK(std::none_of(result.actions, result.actions + result.action_count, [](const auto &action) {
        return action.kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2
               || action.kind == IRFQ_INFINITE_ACTION_QUEUE_INSERT_V2;
      }));
      REQUIRE(result.action_count >= 2);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
  for (const std::string rawSequence : {
           "-1",
           "",
           "18446744073709551616",
           "0000000000000000000001",
       }) {
    DYNAMIC_SECTION("raw-sequence=" << rawSequence) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      const auto wire = finishFix(
          "35=4\00149=PARTICIPANT\00156=VENUE\00134=" + rawSequence
          + "\00152=20231114-22:13:20.123456\00136=4\001123=Y\001");
      InboundCall inbound(session, wire, 0xb1);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(output.find("\00135=2\001") == std::string::npos);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      CHECK(std::none_of(result.actions, result.actions + result.action_count, [](const auto &action) {
        return action.kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2
               || action.kind == IRFQ_INFINITE_ACTION_QUEUE_INSERT_V2;
      }));
      REQUIRE(result.action_count >= 2);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 preserves identity and time precedence for malformed SequenceReset",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-invalid][compound-validation]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::pair<std::string, std::uint32_t>, 2> variants{{
      {"35=4\00149=EVIL\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\00136=NOPE\001123=Y\001",
       IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2},
      {"35=4\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20200101-00:00:00.000000\001369=1\00136=NOPE\001123=Y\001",
       IRFQ_INFINITE_REASON_LATENCY_V2},
  }};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION("reason=" << variant.second) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, finishFix(variant.first), 0xb2);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      REQUIRE(result.action_count >= 3);
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == variant.second);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 validates SequenceReset identity before custom NewSeqNo handling",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-invalid][terminal-compid]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto wire
      = finishFix("35=4\00149=EVIL\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\00136=2\001123=Y\001");
  InboundCall inbound(session, wire, 0xa4);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\001373=9\001") != std::string::npos);
  CHECK(output.find("\00135=5\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 4);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2);
  CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke terminates stale GapFill without required OrigSendingTime",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-stale-invalid]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('4', 1, "43=Y\00136=4\001123=Y\001"), 0x87);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=5\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK((read64(result.native_state.data + 180) & UINT64_C(272)) == UINT64_C(272));
  REQUIRE(result.action_count == 3);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_PROTOCOL_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke never applies future GapFill NewSeqNo",
    "[infinite][adapter][v2][stock-smoke][inbound-sequence-reset-future]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('4', 4, "36=9\001123=Y\001"), 0x88);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read64(result.native_state.data + 196) == 2);
  CHECK(read64(result.native_state.data + 204) == 5);
  REQUIRE(result.action_count == 3);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_QUEUE_INSERT_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].msg_type[0] == '2');
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 accepts only the epoch's exact due weekly reset boundary",
    "[infinite][adapter][v2][stock-smoke][scheduled-reset-decision][boundary]") {
  constexpr std::int64_t boundary = INT64_C(604799000000000);
  constexpr std::int64_t week = INT64_C(604800000000000);
  const auto weekly = otherwiseValidUnavailableProfile(2, {4, 0, 3, 86399, 4, 0, 3, 86399});
  const auto nonstop = otherwiseValidUnavailableProfile();
  const auto prepare = [&](const std::vector<std::uint8_t> &config, std::int64_t now, std::int64_t proposed) {
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
    std::array<std::uint8_t, 40> payload{};
    std::fill_n(payload.begin(), 32, std::uint8_t{0x81});
    write64(payload.data() + 32, proposed);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_SCHEDULED_RESET_TRIGGER_V2;
    request.expected_epoch = 1;
    request.now_tai_ns = now;
    request.now_utc_ns = now;
    request.payload = {payload.data(), payload.size()};
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256));
    PlanBuffers buffers;
    auto result = buffers.response();
    const auto status = irfq_infinite_prepare_v2(session, &request, &result);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    return status;
  };

  CHECK(prepare(weekly, boundary - 1, boundary) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(prepare(weekly, boundary, boundary) == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  CHECK(prepare(weekly, boundary + 1, boundary) == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  CHECK(prepare(weekly, boundary + week, boundary + week) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(prepare(nonstop, boundary, boundary) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 derives every weekly end and rejects non-reset schedule instants",
    "[infinite][adapter][v2][stock-smoke][scheduled-reset-decision][boundary-table]") {
  constexpr std::int64_t second = INT64_C(1000000000);
  constexpr std::int64_t day = INT64_C(86400) * second;
  constexpr std::int64_t week = INT64_C(604800) * second;
  const auto prepare = [&](const std::array<std::uint32_t, 8> &schedule,
                           std::int64_t creation,
                           std::int64_t now,
                           std::int64_t proposed) {
    const auto config = otherwiseValidUnavailableProfile(2, schedule);
    auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
        config.data(),
        config.size(),
        nullptr,
        0,
        1,
        0,
        creation,
        creation);
    REQUIRE(session != nullptr);
    std::array<std::uint8_t, 40> payload{};
    std::fill_n(payload.begin(), 32, std::uint8_t{0x81});
    write64(payload.data() + 32, proposed);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_SCHEDULED_RESET_TRIGGER_V2;
    request.expected_epoch = 1;
    request.now_tai_ns = now;
    request.now_utc_ns = now;
    request.payload = {payload.data(), payload.size()};
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256));
    PlanBuffers buffers;
    auto result = buffers.response();
    const auto status = irfq_infinite_prepare_v2(session, &request, &result);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    return status;
  };

  const std::array<std::int64_t, 7> firstEnds{{3 * day, 4 * day, 5 * day, 6 * day, week, day, 2 * day}};
  for (std::uint32_t endDay = 0; endDay < firstEnds.size(); ++endDay) {
    DYNAMIC_SECTION("end-day=" << endDay) {
      const std::array<std::uint32_t, 8> schedule{{endDay, 0, endDay, 0, endDay, 0, endDay, 0}};
      CHECK(
          prepare(schedule, 1, firstEnds[endDay], firstEnds[endDay])
          == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
    }
  }

  const std::array<std::uint32_t, 8> friday{{5, 0, 5, 0, 5, 0, 5, 0}};
  constexpr std::int64_t fridayBoundary = day;
  CHECK(
      prepare(friday, fridayBoundary - 1, fridayBoundary, fridayBoundary)
      == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  CHECK(
      prepare(friday, fridayBoundary, fridayBoundary + week, fridayBoundary + week)
      == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  CHECK(
      prepare(friday, fridayBoundary + 1, fridayBoundary + week, fridayBoundary + week)
      == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  CHECK(prepare(friday, 1, fridayBoundary + 1, fridayBoundary + 1) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

  const auto fridayConfig = otherwiseValidUnavailableProfile(2, friday);
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      fridayConfig.data(),
      fridayConfig.size(),
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
  auto *restored = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      fridayConfig.data(),
      fridayConfig.size(),
      closed.native_state.data,
      closed.native_state.length,
      1,
      1,
      0,
      0);
  REQUIRE(restored != nullptr);
  std::array<std::uint8_t, 40> restoredPayload{};
  std::fill_n(restoredPayload.begin(), 32, std::uint8_t{0x81});
  write64(restoredPayload.data() + 32, fridayBoundary);
  irfq_infinite_prepare_request_v2 restoredRequest{};
  init(restoredRequest);
  restoredRequest.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  restoredRequest.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  restoredRequest.event = IRFQ_INFINITE_EVENT_SCHEDULED_RESET_TRIGGER_V2;
  restoredRequest.expected_epoch = 1;
  restoredRequest.expected_revision = 1;
  restoredRequest.now_tai_ns = fridayBoundary;
  restoredRequest.now_utc_ns = fridayBoundary;
  restoredRequest.payload = {restoredPayload.data(), restoredPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          restored,
          restoredRequest,
          restoredRequest.event_identity_sha256));
  PlanBuffers restoredBuffers;
  auto restoredResult = restoredBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(restored, &restoredRequest, &restoredResult)
      == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  CHECK(irfq_infinite_destroy_v2(restored) == IRFQ_INFINITE_STATUS_OK_V2);

  const std::array<std::uint32_t, 8> distinct{{4, 0, 6, 0, 4, 1, 5, 0}};
  constexpr std::int64_t saturdayBoundary = 2 * day;
  CHECK(prepare(distinct, 1, week, week) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(prepare(distinct, 1, saturdayBoundary, second) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(prepare(distinct, 1, saturdayBoundary, day) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke requests credential-safe scheduled reset decision",
    "[infinite][adapter][v2][stock-smoke][scheduled-reset-decision]") {
  constexpr std::int64_t boundary = INT64_C(604799000000000);
  const auto config = otherwiseValidUnavailableProfile(2, {4, 0, 3, 86399, 4, 0, 3, 86399});
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  std::array<std::uint8_t, 40> payload{};
  std::fill_n(payload.begin(), 32, std::uint8_t{0x81});
  write64(payload.data() + 32, boundary);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_SCHEDULED_RESET_TRIGGER_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = INT64_C(1700000000123456001);
  request.now_utc_ns = INT64_C(1700000000123456001);
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);

  CHECK(result.step == 0);
  CHECK(std::equal(payload.begin(), payload.begin() + 32, result.subject_sha256));
  CHECK(result.input_source == IRFQ_INFINITE_INPUT_NONE_V2);
  CHECK(result.input_item_index == 0);
  CHECK(result.input_offset == 0);
  CHECK(result.input_length == 0);
  CHECK(result.native_state.length == 0);
  irfq_infinite_abort_request_v2 abort{};
  init(abort);
  abort.prepare_id = result.prepare_id;
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(session, &abort, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke closes both scheduled reset decisions without epoch mutation",
    "[infinite][adapter][v2][stock-smoke][scheduled-reset-decision][resume]") {
  constexpr std::int64_t boundary = INT64_C(604799000000000);
  const auto config = otherwiseValidUnavailableProfile(2, {4, 0, 3, 86399, 4, 0, 3, 86399});
  for (const auto decision :
       {IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2, IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2}) {
    DYNAMIC_SECTION("decision=" << decision) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      std::array<std::uint8_t, 40> payload{};
      std::fill_n(payload.begin(), 32, std::uint8_t{0x82});
      write64(payload.data() + 32, boundary);
      irfq_infinite_prepare_request_v2 request{};
      init(request);
      request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
      request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
      request.event = IRFQ_INFINITE_EVENT_SCHEDULED_RESET_TRIGGER_V2;
      request.expected_epoch = 1;
      request.expected_revision = 1;
      request.now_tai_ns = INT64_C(1700000000123456001);
      request.now_utc_ns = INT64_C(1700000000123456001);
      request.payload = {payload.data(), payload.size()};
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              request,
              request.event_identity_sha256));
      PlanBuffers pendingBuffers;
      auto pending = pendingBuffers.response();
      REQUIRE(
          irfq_infinite_prepare_v2(session, &request, &pending) == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
      irfq_infinite_resume_request_v2 resume{};
      init(resume);
      resume.prepare_id = pending.prepare_id;
      resume.step = pending.step;
      resume.kind = IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2;
      std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
      resume.decision = decision;
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);

      CHECK(result.step == 1);
      CHECK(result.result_epoch == 1);
      CHECK(result.result_revision == 2);
      CHECK(read64(result.native_state.data + 132) == 2);
      CHECK(read64(result.native_state.data + 144) == 2);
      if (decision == IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2) {
        REQUIRE(result.action_count == 1);
        irfq_infinite_declarative_action_v2 expected{};
        expected.kind = IRFQ_INFINITE_ACTION_RESET_TRIGGER_V2;
        expected.disposition = 2;
        std::copy_n(payload.data(), 32, expected.binding_sha256);
        CHECK(std::memcmp(result.actions, &expected, sizeof(expected)) == 0);
      } else {
        CHECK(result.action_count == 0);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke finalizes held reset Logon through ordinary Session",
    "[infinite][adapter][v2][stock-smoke][reset-final-logon]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  const auto held
      = participantFrame('A', 1, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001");
  std::vector<std::uint8_t> payload(128 + held.size());
  std::fill_n(payload.begin(), 32, std::uint8_t{0x83});
  std::fill_n(payload.begin() + 32, 32, std::uint8_t{0x91});
  write32(payload.data() + 64, 1);
  write64(payload.data() + 68, 2);
  write64(payload.data() + 76, UINT64_C(1700000000123456000));
  write64(payload.data() + 84, UINT64_C(1700000000123456000));
  write32(payload.data() + 92, held.size());
  const auto heldDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
      reinterpret_cast<const std::uint8_t *>(held.data()),
      held.size());
  std::copy(heldDigest.begin(), heldDigest.end(), payload.begin() + 96);
  std::copy(held.begin(), held.end(), payload.begin() + 128);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_RESET_FINAL_V2;
  request.event = IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = INT64_C(1700000000123456001);
  request.now_utc_ns = INT64_C(1700000000123456001);
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);

  const auto expected = finishFix(
      "35=A\00134=1\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\001369=1\00198=0\001"
      "108=30\001141=Y\001789=2\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001");
  CHECK(std::string(reinterpret_cast<char *>(result.output.data), result.output.length) == expected);
  CHECK(result.result_epoch == 2);
  CHECK(result.result_revision == 1);
  CHECK(read64(result.native_state.data + 132) == 2);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read64(result.native_state.data + 152) == 1);
  CHECK(read64(result.native_state.data + 180) == UINT64_C(231));
  REQUIRE(result.action_count == 1);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[0].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
  CHECK(result.actions[0].msg_type[0] == 'A');
  CHECK(result.actions[0].sequence_begin == 1);
  CHECK(result.actions[0].sequence_end_exclusive == 2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke requests credential-safe inbound reset Logon decision",
    "[infinite][adapter][v2][stock-smoke][inbound-reset-decision]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      participantFrame('A', 2, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x84);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &result)
      == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  CHECK(result.step == 0);
  CHECK(std::equal(inbound.payload.begin(), inbound.payload.begin() + 32, result.subject_sha256));
  CHECK(result.input_source == IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2);
  CHECK(result.input_item_index == 0);
  CHECK(result.input_offset == 0);
  CHECK(result.input_length == inbound.payload.size());
  CHECK(result.native_state.length == 0);
  irfq_infinite_abort_request_v2 abort{};
  init(abort);
  abort.prepare_id = result.prepare_id;
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(session, &abort, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 fresh transport reset Logon starts the reset decision before rendering",
    "[infinite][adapter][v2][stock-smoke][inbound-reset-decision][fresh]") {
  const auto config = otherwiseValidUnavailableProfile();
  FIX::DataDictionaryProvider dictionaries;
  dictionaries.addTransportDataDictionary(FIX::BeginString("FIXT.1.1"), FIX::TestSettings::pathForSpec("FIXT11"));
  dictionaries.addApplicationDataDictionary(FIX::ApplVerID("10"), FIX::TestSettings::pathForSpec("FIX50SP2"));
  FIX::InfiniteSessionStaticProfile profile{};
  profile.defaultCustomApplicationVersion = "INFINITE-RFQ-1.0.0";
  profile.scheduleMode = 1;
  profile.heartbeatMode = 1;
  profile.configuredHeartbeat = 30;
  profile.minimumHeartbeat = 30;
  profile.maximumHeartbeat = 30;
  profile.timestampPrecision = 6;
  profile.maximumLatency = 120;
  profile.checkCompId = true;
  profile.checkLatency = true;
  profile.persistMessages = true;
  profile.validateLengthAndChecksum = true;
  profile.sendNextExpectedMsgSeqNum = true;
  const auto resetWire = participantFrame(
      'A',
      1,
      "369=0\00198=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001");
  const auto classified = FIX::InfiniteSessionPlanner::inbound(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      1,
      1,
      INT64_C(1700000000123456001),
      INT64_C(1700000000123456000),
      INT64_C(1700000000123456000),
      UINT64_C(1),
      0,
      0,
      resetWire,
      dictionaries,
      profile);
  CHECK(classified.resetLogon);
  CHECK(classified.outputs.empty());
  CHECK(classified.nextSenderSequence == 1);
  CHECK(classified.nextTargetSequence == 1);

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
  InboundCall inbound(session, resetWire, 0xbb);
  inbound.request.expected_revision = 0;
  inbound.request.next_original_value = 1;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          inbound.request,
          inbound.request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &result)
      == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  CHECK(result.native_state.length == 0);
  CHECK(result.output.length == 0);
  CHECK(result.action_count == 0);
  irfq_infinite_abort_request_v2 abort{};
  init(abort);
  abort.prepare_id = result.prepare_id;
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  REQUIRE(irfq_infinite_abort_v2(session, &abort, &operation) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes invalid reset Logon before the reset decision",
    "[infinite][adapter][v2][stock-smoke][inbound-reset-decision][validation]") {
  const auto config = otherwiseValidUnavailableProfile();
  struct Variant {
    std::string wire;
    std::uint32_t reason;
    std::uint32_t refTag;
    std::uint32_t rejectReason;
  };
  const std::array<Variant, 4> variants{{
      {"35=A\00149=EVIL\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\001"
       "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001",
       IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2,
       0,
       0},
      {"35=A\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20200101-00:00:00.000000\001369=1\001"
       "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001",
       IRFQ_INFINITE_REASON_LATENCY_V2,
       0,
       0},
      {"35=A\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\001"
       "98=0\001108=30\001141=Y\0011407=299\0011408=INFINITE-RFQ-1.0.0\001",
       IRFQ_INFINITE_REASON_PROTOCOL_V2,
       1137,
       18},
      {"35=A\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\001"
       "108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001",
       IRFQ_INFINITE_REASON_PROTOCOL_V2,
       98,
       1},
  }};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION("reason=" << variant.reason << " refTag=" << variant.refTag) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(session, finishFix(variant.wire), 0xbc);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
      CHECK(output.find("\00135=3\001") != std::string::npos);
      CHECK(output.find("\00135=5\001") != std::string::npos);
      CHECK(output.find("\00135=A\001") == std::string::npos);
      if (variant.refTag != 0) {
        CHECK(output.find("\001371=" + std::to_string(variant.refTag) + "\001") != std::string::npos);
        CHECK(output.find("\001373=" + std::to_string(variant.rejectReason) + "\001") != std::string::npos);
      }
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read64(result.native_state.data + 152) == 1);
      REQUIRE(result.action_count == 4);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == variant.reason);
      CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 stock smoke closes both inbound reset Logon decisions",
    "[infinite][adapter][v2][stock-smoke][inbound-reset-decision][resume]") {
  const auto config = otherwiseValidUnavailableProfile();
  for (const auto decision :
       {IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2, IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2}) {
    DYNAMIC_SECTION("decision=" << decision) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(
          session,
          participantFrame('A', 2, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x85);
      PlanBuffers pendingBuffers;
      auto pending = pendingBuffers.response();
      REQUIRE(
          irfq_infinite_prepare_v2(session, &inbound.request, &pending)
          == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
      irfq_infinite_resume_request_v2 resume{};
      init(resume);
      resume.prepare_id = pending.prepare_id;
      resume.step = pending.step;
      resume.kind = IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2;
      std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
      resume.decision = decision;
      resume.input_source = pending.input_source;
      resume.input_item_index = pending.input_item_index;
      resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);

      CHECK(result.result_epoch == 1);
      CHECK(result.result_revision == 2);
      CHECK(read64(result.native_state.data + 144) == 2);
      if (decision == IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2) {
        CHECK(result.output.length == 0);
        REQUIRE(result.action_count == 2);
        CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_PENDING_RESET_LOGON_V2);
        CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_RESET_TRIGGER_V2);
        CHECK(result.actions[1].disposition == 1);
      } else {
        const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
        CHECK(output.find("\00135=5\001") != std::string::npos);
        CHECK(output.find("\00158=Reset rejected\001") != std::string::npos);
        CHECK(read64(result.native_state.data + 132) == 3);
        REQUIRE(result.action_count == 3);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
        CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_RESET_REJECTED_V2);
        CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
        CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejected reset exhausts the final legal sender sequence",
    "[infinite][adapter][v2][stock-smoke][inbound-reset-decision][reject][sequence-exhaustion]") {
  const auto config = otherwiseValidUnavailableProfile();
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  auto *session = stockLoggedOnSession(config, lastLegal);
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      participantFrame('A', 2, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x6e);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.decision = IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2;
  resume.input_source = pending.input_source;
  resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
  CHECK(read64(result.native_state.data + 132) == 0);
  auto *restored = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      result.native_state.data,
      result.native_state.length,
      1,
      2,
      0,
      0);
  REQUIRE(restored != nullptr);
  CHECK(irfq_infinite_destroy_v2(restored) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 inbound reset resume mismatch consumes the volatile plan fail closed",
    "[infinite][adapter][v2][stock-smoke][inbound-reset-decision][resume-invalid]") {
  const auto config = otherwiseValidUnavailableProfile();
  for (const std::string variant : {"subject", "reborrow"}) {
    DYNAMIC_SECTION(variant) {
      auto *session = stockLoggedOnSession(config);
      REQUIRE(session != nullptr);
      InboundCall inbound(
          session,
          participantFrame('A', 2, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x9a);
      PlanBuffers pendingBuffers;
      auto pending = pendingBuffers.response();
      REQUIRE(
          irfq_infinite_prepare_v2(session, &inbound.request, &pending)
          == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
      auto reborrow = inbound.payload;
      irfq_infinite_resume_request_v2 resume{};
      init(resume);
      resume.prepare_id = pending.prepare_id;
      resume.step = pending.step;
      resume.kind = IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2;
      std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
      resume.decision = IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2;
      resume.input_source = pending.input_source;
      resume.input_source_bytes = {reborrow.data(), reborrow.size()};
      const auto expected
          = variant == "subject" ? IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2 : IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2;
      if (variant == "subject") {
        resume.subject_sha256[0] ^= 1;
      } else {
        reborrow.back() ^= 1;
      }
      PlanBuffers invalidBuffers;
      auto invalid = invalidBuffers.response();
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &invalid) == expected);
      std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
      resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
      PlanBuffers retryBuffers;
      auto retry = retryBuffers.response();
      CHECK(irfq_infinite_resume_v2(session, &resume, &retry) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}
