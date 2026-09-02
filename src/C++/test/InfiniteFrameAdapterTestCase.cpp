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

#include "DataDictionary.h"
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
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
void forceInfiniteFrameAdapterStockNonconformanceSmokeNextPlanOverflow(irfq_infinite_session_v2 *session) noexcept;
void resetInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations() noexcept;
void stopInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations() noexcept;
std::array<std::uint32_t, 4> infiniteFrameAdapterStockNonconformanceSmokeScrubObservations() noexcept;
bool infiniteFrameAdapterStockNonconformanceSmokePendingPlanRetainsBusinessRejectRefId() noexcept;
void scrubInfiniteFrameAdapterStockNonconformanceSmokePendingBusinessRejectRefId(
    irfq_infinite_session_v2 *session) noexcept;
void resetInfiniteCompleteFrameBeginStringVisits() noexcept;
std::size_t stopInfiniteCompleteFrameBeginStringVisits() noexcept;
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
static_assert(sizeof(irfq_infinite_resume_request_v2) == 160);
static_assert(sizeof(irfq_infinite_apply_committed_request_v2) == 72);
static_assert(sizeof(irfq_infinite_abort_request_v2) == 32);
static_assert(sizeof(irfq_infinite_operation_response_v2) == 24);
static_assert(offsetof(irfq_infinite_session_create_request_v2, canonical_session_create_config) == 24);
static_assert(offsetof(irfq_infinite_session_create_request_v2, creation_utc_ns) == 64);
static_assert(offsetof(irfq_infinite_prepare_request_v2, event_identity_sha256) == 32);
static_assert(offsetof(irfq_infinite_prepare_request_v2, now_utc_ns) == 88);
static_assert(offsetof(irfq_infinite_prepare_response_v2, result_epoch) == 200);
static_assert(offsetof(irfq_infinite_prepare_response_v2, actions) == 296);
static_assert(offsetof(irfq_infinite_resume_request_v2, gateway_inbound_disposition_id) == 144);

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

FIX::DataDictionaryProvider applicationBlockDictionaries() {
  FIX::DataDictionaryProvider dictionaries;
  dictionaries.addTransportDataDictionary(FIX::BeginString("FIXT.1.1"), FIX::TestSettings::pathForSpec("FIXT11"));
  auto application = std::make_shared<FIX::DataDictionary>(FIX::TestSettings::pathForSpec("FIX50SP2"));
  application->addField(20003);
  application->addFieldName(20003, "InfiniteOutcomeRef");
  application->addFieldType(20003, FIX::TYPE::String);
  application->addField(20006);
  application->addFieldName(20006, "InfiniteAH0ResultCount");
  application->addFieldType(20006, FIX::TYPE::Int);
  application->addMsgType("UAH0");
  for (const auto field : {644, 20003, 20006}) {
    application->addMsgField("UAH0", field);
    application->addRequiredField("UAH0", field);
  }
  dictionaries.addApplicationDataDictionary(FIX::ApplVerID("10"), std::move(application));
  return dictionaries;
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

void cborDigest(std::vector<std::uint8_t> &bytes, const std::array<std::uint8_t, 32> &digest) {
  cborArgument(bytes, 2, digest.size());
  bytes.insert(bytes.end(), digest.begin(), digest.end());
}

void cborBoolean(std::vector<std::uint8_t> &bytes, bool value) { bytes.push_back(value ? 0xf5 : 0xf4); }

struct TestDictionaryTuple {
  std::string transportId{"unavailable-transport-dictionary"};
  std::array<std::uint8_t, 32> transportSha256 = [] {
    std::array<std::uint8_t, 32> value{};
    value.fill(0x22);
    return value;
  }();
  std::string applicationId{"unavailable-application-dictionary"};
  std::array<std::uint8_t, 32> applicationSha256 = [] {
    std::array<std::uint8_t, 32> value{};
    value.fill(0x33);
    return value;
  }();
};

TestDictionaryTuple governedDictionaryTuple() {
  return {
      "INFINITE-FIXT11",
      {0x75, 0xec, 0xae, 0x39, 0x57, 0xf5, 0xf5, 0xb0, 0xcc, 0x86, 0x13, 0xac, 0x89, 0x76, 0xbb, 0x33,
       0xdf, 0xe3, 0xe2, 0xed, 0xf0, 0x12, 0xcd, 0xf3, 0x60, 0x87, 0xb3, 0x49, 0xad, 0x5f, 0x85, 0xe5},
      "INFINITE-RFQ-1.0.0-EP299",
      {0xd9, 0xce, 0x75, 0xd2, 0x06, 0x57, 0x3a, 0x39, 0x1d, 0xbc, 0xb8, 0x3a, 0x61, 0x66, 0x5f, 0x38,
       0x44, 0x91, 0x6c, 0xfd, 0x63, 0x00, 0x6d, 0x6a, 0xb9, 0x9d, 0x64, 0x5b, 0xac, 0x6d, 0x25, 0x51}};
}

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
    const std::string &qualifier = "",
    const std::string &defaultCustomApplicationVersion = "INFINITE-RFQ-1.0.0",
    const TestDictionaryTuple &dictionaries = {}) {
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
  cborBytes(bytes, defaultCustomApplicationVersion);
  cborBytes(bytes, dictionaries.transportId);
  cborDigest(bytes, dictionaries.transportSha256);
  cborBytes(bytes, dictionaries.applicationId);
  cborDigest(bytes, dictionaries.applicationSha256);
  return bytes;
}

std::vector<std::uint8_t> profileWithDictionaries(
    const TestDictionaryTuple &dictionaries,
    const std::string &defaultCustomApplicationVersion = "INFINITE-RFQ-1.0.0") {
  return otherwiseValidUnavailableProfile(
      1,
      {},
      1,
      30,
      30,
      30,
      "",
      "",
      "",
      "",
      "",
      defaultCustomApplicationVersion,
      dictionaries);
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

void append32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  const auto offset = bytes.size();
  bytes.resize(offset + 4);
  write32(bytes.data() + offset, value);
}

void append64(std::vector<std::uint8_t> &bytes, std::uint64_t value) {
  const auto offset = bytes.size();
  bytes.resize(offset + 8);
  write64(bytes.data() + offset, value);
}

void appendApplicationUnit(
    std::vector<std::uint8_t> &bytes,
    std::uint32_t index,
    const std::string &msgType,
    const std::string &body) {
  append32(bytes, index);
  append32(bytes, msgType.size());
  append32(bytes, body.size());
  const auto digest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
      reinterpret_cast<const std::uint8_t *>(body.data()),
      body.size());
  bytes.insert(bytes.end(), digest.begin(), digest.end());
  bytes.insert(bytes.end(), msgType.begin(), msgType.end());
  bytes.insert(bytes.end(), body.begin(), body.end());
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

irfq_infinite_prepare_response_v2 poisonedPlanResponse(PlanBuffers &buffers) {
  irfq_infinite_prepare_response_v2 value;
  std::memset(&value, 0xa5, sizeof(value));
  value.header.structure_size = sizeof(value);
  value.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2;
  value.header.reserved = 0;
  value.native_state = {buffers.state.data(), buffers.state.size(), 0};
  value.output = {buffers.output.data(), buffers.output.size(), 0};
  value.actions = buffers.actions.data();
  value.action_capacity = buffers.actions.size();
  return value;
}

void checkZeroPlanPayload(const irfq_infinite_prepare_response_v2 &response) {
  const std::array<std::uint8_t, sizeof(response) - sizeof(response.header)> zero{};
  CHECK(
      std::memcmp(reinterpret_cast<const std::uint8_t *>(&response) + sizeof(response.header), zero.data(), zero.size())
      == 0);
}

void checkNeedOutputPlanPayload(
    const irfq_infinite_prepare_response_v2 &response,
    const irfq_infinite_prepare_response_v2 &identity,
    std::uint64_t requiredOutputCapacity) {
  irfq_infinite_prepare_response_v2 expected{};
  expected.prepare_id = identity.prepare_id;
  expected.step = identity.step;
  expected.kind = identity.kind;
  expected.stage = identity.stage;
  expected.event = identity.event;
  std::copy_n(identity.event_identity_sha256, 32, expected.event_identity_sha256);
  expected.base_epoch = identity.base_epoch;
  expected.base_revision = identity.base_revision;
  expected.required_output_capacity = requiredOutputCapacity;
  CHECK(
      std::memcmp(
          reinterpret_cast<const std::uint8_t *>(&response) + sizeof(response.header),
          reinterpret_cast<const std::uint8_t *>(&expected) + sizeof(expected.header),
          sizeof(response) - sizeof(response.header))
      == 0);
}

irfq_infinite_session_v2 *stockLoggedOnSession(
    const std::vector<std::uint8_t> &config,
    std::uint64_t senderSequence = 2,
    const FIX::DataDictionaryProvider *dictionaries = nullptr,
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> *restoredState = nullptr) {
  const auto create = [&](const std::uint8_t *state,
                          std::size_t stateLength,
                          std::uint64_t revision,
                          std::int64_t creationTai,
                          std::int64_t creationUtc) {
    return dictionaries == nullptr ? FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
                                         config.data(),
                                         config.size(),
                                         state,
                                         stateLength,
                                         1,
                                         revision,
                                         creationTai,
                                         creationUtc)
                                   : FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSessionWithDataDictionaries(
                                         config.data(),
                                         config.size(),
                                         state,
                                         stateLength,
                                         1,
                                         revision,
                                         creationTai,
                                         creationUtc,
                                         *dictionaries);
  };
  auto *session = create(nullptr, 0, 0, 1, 1);
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
  if (restoredState != nullptr) {
    std::copy_n(result.native_state.data, result.native_state.length, restoredState->begin());
  }
  return create(result.native_state.data, result.native_state.length, 1, 0, 0);
}

struct InboundCall {
  std::string wire;
  std::vector<std::uint8_t> payload;
  irfq_infinite_prepare_request_v2 request{};

  InboundCall(
      irfq_infinite_session_v2 *session,
      std::string value,
      std::uint8_t subject = 0x71,
      std::uint64_t expectedRevision = 1)
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
    request.expected_revision = expectedRevision;
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

#ifdef IRFQ_INFINITE_EMBED_DICTIONARIES
irfq_infinite_session_v2 *governedProductionLoggedOnSession() {
  const auto config = profileWithDictionaries(governedDictionaryTuple());
  irfq_infinite_session_create_request_v2 create{};
  init(create);
  create.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
  create.canonical_session_create_config = {config.data(), config.size()};
  create.session_epoch = 1;
  create.creation_tai_ns = INT64_C(1700000000123456000);
  create.creation_utc_ns = INT64_C(1700000000123456000);
  irfq_infinite_session_create_response_v2 created{};
  init(created);
  const auto createStatus = irfq_infinite_session_create_v2(&create, &created);
  REQUIRE(createStatus == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(created.session != nullptr);

  auto *session = created.session;
  InboundCall logon(
      session,
      participantFrame('A', 1, "98=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x52);
  logon.request.expected_revision = 0;
  logon.request.next_original_value = 1;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          logon.request,
          logon.request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  const auto prepareStatus = irfq_infinite_prepare_v2(session, &logon.request, &result);
  REQUIRE(prepareStatus == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
  REQUIRE(output.find("\00135=A\001") != std::string::npos);
  REQUIRE(output.find("\00135=3\001") == std::string::npos);
  REQUIRE(output.find("\00135=5\001") == std::string::npos);
  REQUIRE(result.action_count == 2);
  REQUIRE(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);

  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = result.prepare_id;
  apply.result_revision = result.result_revision;
  std::copy_n(result.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 applied{};
  init(applied);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(applied.cache_revision == 1);

  std::array<std::uint8_t, 56> casPayload{};
  std::copy_n(result.actions[0].binding_sha256, 32, casPayload.begin());
  write32(casPayload.data() + 32, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 36, 1);
  write32(casPayload.data() + 44, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 48, 2);
  irfq_infinite_prepare_request_v2 cas{};
  init(cas);
  cas.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  cas.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  cas.event = IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2;
  cas.expected_epoch = 1;
  cas.expected_revision = 1;
  cas.now_tai_ns = logon.request.now_tai_ns;
  cas.now_utc_ns = logon.request.now_utc_ns;
  cas.payload = {casPayload.data(), casPayload.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, cas, cas.event_identity_sha256));
  PlanBuffers casBuffers;
  auto casResult = casBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &cas, &casResult) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(casResult.action_count == 1);
  REQUIRE(casResult.actions[0].kind == IRFQ_INFINITE_ACTION_TARGET_ADVANCE_V2);

  init(apply);
  apply.prepare_id = casResult.prepare_id;
  apply.result_revision = casResult.result_revision;
  std::copy_n(casResult.native_state_sha256, 32, apply.native_state_sha256);
  init(applied);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(applied.cache_revision == 2);

  std::array<std::uint8_t, 80> frontierPayload{};
  std::copy_n(result.actions[0].binding_sha256, 32, frontierPayload.begin());
  write64(frontierPayload.data() + 32, 0);
  write64(frontierPayload.data() + 40, 1);
  std::copy_n(logon.payload.data() + 36, 32, frontierPayload.begin() + 48);
  irfq_infinite_prepare_request_v2 frontier{};
  init(frontier);
  frontier.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  frontier.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  frontier.event = IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2;
  frontier.expected_epoch = 1;
  frontier.expected_revision = 2;
  frontier.now_tai_ns = logon.request.now_tai_ns;
  frontier.now_utc_ns = logon.request.now_utc_ns;
  frontier.payload = {frontierPayload.data(), frontierPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          frontier,
          frontier.event_identity_sha256));
  PlanBuffers frontierBuffers;
  auto frontierResult = frontierBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &frontier, &frontierResult) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(frontierResult.action_count == 0);

  init(apply);
  apply.prepare_id = frontierResult.prepare_id;
  apply.result_revision = frontierResult.result_revision;
  std::copy_n(frontierResult.native_state_sha256, 32, apply.native_state_sha256);
  init(applied);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(applied.cache_revision == 3);
  return session;
}
#endif

struct ApplicationCall {
  std::vector<std::uint8_t> payload;
  irfq_infinite_prepare_request_v2 request{};

  ApplicationCall(
      irfq_infinite_session_v2 *session,
      irfq_infinite_prepare_kind_v2 kind,
      irfq_infinite_stage_v2 stage,
      irfq_infinite_event_v2 event,
      irfq_infinite_application_block_mode_v2 mode,
      const std::string &msgType,
      const std::string &body)
      : payload(32, 0x91) {
    if (event == IRFQ_INFINITE_EVENT_APPLICATION_REPLAY_BEGIN_V2) {
      payload.insert(payload.end(), 32, 0x92);
    } else if (event == IRFQ_INFINITE_EVENT_READ_RESULT_BEGIN_V2) {
      payload.insert(payload.end(), 32, 0x93);
      payload.insert(payload.end(), 32, 0x94);
    }
    append32(payload, 1);
    append32(payload, 1);
    append32(payload, 1);
    append32(payload, 0);
    append32(payload, 1);
    appendApplicationUnit(payload, 0, msgType, body);
    init(request);
    request.kind = kind;
    request.stage = stage;
    request.event = event;
    request.application_block_mode = mode;
    request.expected_epoch = 1;
    request.expected_revision = 1;
    request.now_tai_ns = INT64_C(1700000000123456001);
    request.now_utc_ns = INT64_C(1700000000123456001);
    request.payload = {payload.data(), payload.size()};
    if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256)) {
      throw std::logic_error("Application identity");
    }
  }
};

struct StoredRetransmitCall {
  std::string wire;
  std::vector<std::uint8_t> payload;
  irfq_infinite_prepare_request_v2 request{};

  StoredRetransmitCall(
      irfq_infinite_session_v2 *session,
      std::uint32_t storeClass,
      std::string value,
      std::uint64_t revision = 1,
      std::uint8_t subject = 0xc1)
      : wire(std::move(value)),
        payload(72 + wire.size()) {
    std::fill_n(payload.begin(), 32, subject);
    write32(payload.data() + 32, storeClass);
    write32(payload.data() + 36, wire.size());
    const auto digest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
        reinterpret_cast<const std::uint8_t *>(wire.data()),
        wire.size());
    std::copy(digest.begin(), digest.end(), payload.begin() + 40);
    std::copy(wire.begin(), wire.end(), payload.begin() + 72);
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_STORED_FRAME_RETRANSMIT_V2;
    request.expected_epoch = 1;
    request.expected_revision = revision;
    request.now_tai_ns = INT64_C(1700000000123456001) + static_cast<std::int64_t>(revision - 1);
    request.now_utc_ns = INT64_C(1700000000123456001) + static_cast<std::int64_t>(revision - 1);
    request.payload = {payload.data(), payload.size()};
    if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256)) {
      throw std::logic_error("Stored retransmit identity");
    }
  }
};

struct ContinueResendCall {
  std::vector<std::uint8_t> payload;
  irfq_infinite_prepare_request_v2 request{};

  ContinueResendCall(
      irfq_infinite_session_v2 *session,
      std::uint64_t begin,
      std::uint64_t end,
      std::uint64_t cursor,
      std::uint64_t revision = 1,
      std::uint64_t original = 6)
      : payload(56, 0xd1) {
    write64(payload.data() + 32, begin);
    write64(payload.data() + 40, end);
    write64(payload.data() + 48, cursor);
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_CONTINUE_RESEND_V2;
    request.expected_epoch = 1;
    request.expected_revision = revision;
    request.now_tai_ns = INT64_C(1700000000123456001) + static_cast<std::int64_t>(revision - 1);
    request.now_utc_ns = INT64_C(1700000000123456001) + static_cast<std::int64_t>(revision - 1);
    request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
    request.next_original_value = original;
    request.payload = {payload.data(), payload.size()};
    if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256)) {
      throw std::logic_error("Continue resend identity");
    }
  }
};

irfq_infinite_store_row_v2 retainedRow(
    std::uint64_t sequence,
    std::uint32_t storeClass,
    const std::string &msgType,
    const std::string &body,
    const std::string &wire) {
  irfq_infinite_store_row_v2 row{};
  row.sequence = sequence;
  row.store_class = storeClass;
  row.msg_type_length = msgType.size();
  row.frame_length = wire.size();
  std::copy(msgType.begin(), msgType.end(), row.msg_type);
  if (storeClass != IRFQ_INFINITE_STORE_CLASS_PROVEN_GAP_V2) {
    const auto frameDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
        reinterpret_cast<const std::uint8_t *>(wire.data()),
        wire.size());
    std::copy(frameDigest.begin(), frameDigest.end(), row.frame_sha256);
    const auto bodyDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
        reinterpret_cast<const std::uint8_t *>(body.data()),
        body.size());
    std::copy(bodyDigest.begin(), bodyDigest.end(), row.body_sha256);
  }
  row.frame = slice(wire);
  return row;
}

irfq_infinite_session_v2 *resendRecoverySession(
    const std::vector<std::uint8_t> &config,
    std::uint64_t begin,
    std::uint64_t end,
    std::uint64_t cursor,
    std::uint64_t sender = 6,
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> *restoredState = nullptr) {
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  auto *loggedOn = stockLoggedOnSession(config, sender, nullptr, &state);
  if (loggedOn == nullptr || irfq_infinite_destroy_v2(loggedOn) != IRFQ_INFINITE_STATUS_OK_V2) {
    return nullptr;
  }
  write32(state.data() + 220, 1);
  write32(state.data() + 224, 3);
  write64(state.data() + 228, begin);
  write64(state.data() + 236, end);
  write64(state.data() + 244, cursor);
  std::fill_n(state.data() + 252, 32, std::uint8_t{0xd1});
  write32(state.data() + 292, 1);
  write64(state.data() + 300, cursor);
  if (restoredState != nullptr) {
    *restoredState = state;
  }
  return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      1,
      0,
      0);
}

irfq_infinite_session_v2 *detachedResendRecoverySession(
    const std::vector<std::uint8_t> &config,
    std::uint64_t begin,
    std::uint64_t end,
    std::uint64_t cursor,
    std::uint64_t sender,
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> *restoredState = nullptr) {
  if (cursor != begin || end <= begin) {
    return nullptr;
  }
  auto *attached = stockLoggedOnSession(config, sender);
  if (attached == nullptr) {
    return nullptr;
  }
  InboundCall resend(
      attached,
      participantFrame('2', 2, "7=" + std::to_string(begin) + "\00116=" + std::to_string(end - 1) + "\001"),
      0xd8);
  resend.request.next_original_value = begin;
  if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          attached,
          resend.request,
          resend.request.event_identity_sha256)) {
    irfq_infinite_destroy_v2(attached);
    return nullptr;
  }
  PlanBuffers resendBuffers;
  auto resendResult = resendBuffers.response();
  if (irfq_infinite_prepare_v2(attached, &resend.request, &resendResult) != IRFQ_INFINITE_STATUS_READY_V2
      || resendResult.action_count != 1) {
    irfq_infinite_destroy_v2(attached);
    return nullptr;
  }
  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = resendResult.prepare_id;
  apply.result_revision = resendResult.result_revision;
  std::copy_n(resendResult.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 operation{};
  init(operation);
  if (irfq_infinite_apply_committed_v2(attached, &apply, &operation) != IRFQ_INFINITE_STATUS_OK_V2) {
    irfq_infinite_destroy_v2(attached);
    return nullptr;
  }

  std::array<std::uint8_t, 56> casPayload{};
  std::copy_n(resendResult.actions[0].binding_sha256, 32, casPayload.begin());
  write32(casPayload.data() + 32, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 36, 2);
  write32(casPayload.data() + 44, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(casPayload.data() + 48, 3);
  irfq_infinite_prepare_request_v2 cas{};
  init(cas);
  cas.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  cas.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  cas.event = IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2;
  cas.expected_epoch = 1;
  cas.expected_revision = 2;
  cas.now_tai_ns = INT64_C(1700000000123456002);
  cas.now_utc_ns = INT64_C(1700000000123456002);
  cas.payload = {casPayload.data(), casPayload.size()};
  if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(attached, cas, cas.event_identity_sha256)) {
    irfq_infinite_destroy_v2(attached);
    return nullptr;
  }
  PlanBuffers casBuffers;
  auto casResult = casBuffers.response();
  if (irfq_infinite_prepare_v2(attached, &cas, &casResult) != IRFQ_INFINITE_STATUS_READY_V2) {
    irfq_infinite_destroy_v2(attached);
    return nullptr;
  }
  init(apply);
  apply.prepare_id = casResult.prepare_id;
  apply.result_revision = casResult.result_revision;
  std::copy_n(casResult.native_state_sha256, 32, apply.native_state_sha256);
  init(operation);
  if (irfq_infinite_apply_committed_v2(attached, &apply, &operation) != IRFQ_INFINITE_STATUS_OK_V2) {
    irfq_infinite_destroy_v2(attached);
    return nullptr;
  }

  std::array<std::uint8_t, 32> payload{};
  payload.fill(0xd9);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = 1;
  close.expected_revision = 3;
  close.now_tai_ns = INT64_C(1700000000123456003);
  close.now_utc_ns = INT64_C(1700000000123456003);
  close.payload = {payload.data(), payload.size()};
  if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(attached, close, close.event_identity_sha256)) {
    irfq_infinite_destroy_v2(attached);
    return nullptr;
  }
  PlanBuffers buffers;
  auto result = buffers.response();
  if (irfq_infinite_prepare_v2(attached, &close, &result) != IRFQ_INFINITE_STATUS_READY_V2
      || irfq_infinite_destroy_v2(attached) != IRFQ_INFINITE_STATUS_OK_V2) {
    return nullptr;
  }
  if (restoredState != nullptr) {
    std::copy_n(result.native_state.data, result.native_state.length, restoredState->begin());
  }
  return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      result.native_state.data,
      result.native_state.length,
      1,
      4,
      0,
      0);
}

irfq_infinite_session_v2 *logonRecoverySession(const std::vector<std::uint8_t> &config) {
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  auto *loggedOn = stockLoggedOnSession(config, 7, nullptr, &state);
  if (loggedOn == nullptr || irfq_infinite_destroy_v2(loggedOn) != IRFQ_INFINITE_STATUS_OK_V2) {
    return nullptr;
  }
  write32(state.data() + 168, 1);
  write64(state.data() + 172, 5);
  write64(state.data() + 180, 1);
  write32(state.data() + 220, 2);
  write32(state.data() + 224, 1);
  write64(state.data() + 228, 2);
  write64(state.data() + 236, 5);
  write64(state.data() + 244, 2);
  std::fill_n(state.data() + 252, 32, std::uint8_t{0xd3});
  write32(state.data() + 288, 0);
  write32(state.data() + 292, 1);
  write64(state.data() + 300, 2);
  return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      1,
      0,
      0);
}

irfq_infinite_session_v2 *logonResponseRecoverySession(
    const std::vector<std::uint8_t> &config,
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> *restoredState = nullptr) {
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  auto *loggedOn = stockLoggedOnSession(config, 8, nullptr, &state);
  if (loggedOn == nullptr || irfq_infinite_destroy_v2(loggedOn) != IRFQ_INFINITE_STATUS_OK_V2) {
    return nullptr;
  }
  write32(state.data() + 168, 1);
  write64(state.data() + 172, 5);
  write32(state.data() + 220, 2);
  write32(state.data() + 224, 2);
  write64(state.data() + 228, 7);
  write64(state.data() + 236, 8);
  write64(state.data() + 244, 7);
  std::fill_n(state.data() + 252, 32, std::uint8_t{0xd3});
  write32(state.data() + 292, 0);
  write64(state.data() + 300, 0);
  if (restoredState != nullptr) {
    *restoredState = state;
  }
  return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      1,
      0,
      0);
}

irfq_infinite_session_v2 *detachedSenderSession(
    const std::vector<std::uint8_t> &config,
    std::uint64_t senderSequence,
    std::uint32_t heartbeatSeconds = 30,
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> *restoredState = nullptr) {
  auto *fresh = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      1,
      1);
  if (fresh == nullptr) {
    return nullptr;
  }
  InboundCall logon(
      fresh,
      participantFrame(
          'A',
          1,
          "98=0\001108=" + std::to_string(heartbeatSeconds) + "\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0xe0);
  logon.request.expected_revision = 0;
  logon.request.next_original_value = 1;
  if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          fresh,
          logon.request,
          logon.request.event_identity_sha256)) {
    irfq_infinite_destroy_v2(fresh);
    return nullptr;
  }
  PlanBuffers logonBuffers;
  auto loggedOn = logonBuffers.response();
  if (irfq_infinite_prepare_v2(fresh, &logon.request, &loggedOn) != IRFQ_INFINITE_STATUS_READY_V2
      || irfq_infinite_destroy_v2(fresh) != IRFQ_INFINITE_STATUS_OK_V2) {
    return nullptr;
  }
  write32(loggedOn.native_state.data + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(loggedOn.native_state.data + 132, senderSequence);
  write64(loggedOn.native_state.data + 144, 2);
  write64(loggedOn.native_state.data + 152, 1);
  auto *attached = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      loggedOn.native_state.data,
      loggedOn.native_state.length,
      1,
      1,
      0,
      0);
  if (attached == nullptr) {
    return nullptr;
  }
  std::array<std::uint8_t, 32> payload{};
  payload.fill(0xe2);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  request.expected_epoch = 1;
  request.expected_revision = 1;
  request.now_tai_ns = INT64_C(1700000000123456001);
  request.now_utc_ns = INT64_C(1700000000123456001);
  request.payload = {payload.data(), payload.size()};
  if (!FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          attached,
          request,
          request.event_identity_sha256)) {
    irfq_infinite_destroy_v2(attached);
    return nullptr;
  }
  PlanBuffers buffers;
  auto result = buffers.response();
  if (irfq_infinite_prepare_v2(attached, &request, &result) != IRFQ_INFINITE_STATUS_READY_V2
      || irfq_infinite_destroy_v2(attached) != IRFQ_INFINITE_STATUS_OK_V2) {
    return nullptr;
  }
  if (restoredState != nullptr) {
    std::copy_n(result.native_state.data, result.native_state.length, restoredState->begin());
  }
  return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      result.native_state.data,
      result.native_state.length,
      1,
      2,
      0,
      0);
}

std::string canonicalBody(const std::string &wire) {
  FIX::DataDictionary sessionDictionary(FIX::TestSettings::pathForSpec("FIXT11"));
  FIX::DataDictionary applicationDictionary(FIX::TestSettings::pathForSpec("FIX50SP2"));
  FIX::Message parsed(wire, sessionDictionary, applicationDictionary, true);
  std::string body;
  parsed.calculateString(body);
  return body;
}

#if defined(__unix__) || defined(__APPLE__)
enum class CallKind {
  Prepare,
  Resume
};

int runGuardedHeaderCall(CallKind kind) {
  const auto child = fork();
  if (child == 0) {
    const auto pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
      _exit(255);
    }
    auto *pages = static_cast<std::uint8_t *>(mmap(
        nullptr,
        static_cast<std::size_t>(pageSize) * 2,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0));
    if (pages == MAP_FAILED || mprotect(pages + pageSize, pageSize, PROT_NONE) != 0) {
      _exit(255);
    }
    auto *header
        = reinterpret_cast<irfq_infinite_output_header_v2 *>(pages + pageSize - sizeof(irfq_infinite_output_header_v2));
    *header = {};
    header->structure_size = sizeof(irfq_infinite_output_header_v2);
    header->abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2;
    auto *response = reinterpret_cast<irfq_infinite_prepare_response_v2 *>(header);
    irfq_infinite_prepare_request_v2 prepare{};
    irfq_infinite_resume_request_v2 resume{};
    init(prepare);
    init(resume);
    const auto status = kind == CallKind::Prepare ? irfq_infinite_prepare_v2(nullptr, &prepare, response)
                                                  : irfq_infinite_resume_v2(nullptr, &resume, response);
    const bool expected = status == IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2;
    munmap(pages, static_cast<std::size_t>(pageSize) * 2);
    _exit(expected ? static_cast<int>(status) : 254);
  }
  if (child < 0) {
    return -1;
  }
  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
    return -1;
  }
  return WEXITSTATUS(status);
}
#endif
} // namespace

#if defined(__unix__) || defined(__APPLE__)
TEST_CASE(
    "InfiniteFrameAdapterV2 validates output headers before reading response tails",
    "[infinite][adapter][v2][abi-validation]") {
  CHECK(runGuardedHeaderCall(CallKind::Prepare) == IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2);
  CHECK(runGuardedHeaderCall(CallKind::Resume) == IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2);
}
#endif

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects misaligned typed buffers before plan access",
    "[infinite][adapter][v2][abi-validation]") {
  const auto config = otherwiseValidUnavailableProfile();

  SECTION("prepare actions") {
    auto *session = stockLoggedOnSession(config);
    REQUIRE(session != nullptr);
    InboundCall inbound(session, participantFrame("AJ", 2, quoteResponseBody("MISALIGNED-ACTIONS")), 0xa5);
    PlanBuffers invalidBuffers;
    auto invalid = invalidBuffers.response();
    alignas(irfq_infinite_declarative_action_v2)
        std::array<std::uint8_t, sizeof(irfq_infinite_declarative_action_v2) + 1>
            storage{};
    invalid.actions = reinterpret_cast<irfq_infinite_declarative_action_v2 *>(storage.data() + 1);
    invalid.action_capacity = 1;
    CHECK(irfq_infinite_prepare_v2(session, &inbound.request, &invalid) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

    PlanBuffers retryBuffers;
    auto retry = retryBuffers.response();
    CHECK(
        irfq_infinite_prepare_v2(session, &inbound.request, &retry)
        == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }

  SECTION("resume store rows") {
    auto *session = resendRecoverySession(config, 2, 3, 2);
    REQUIRE(session != nullptr);
    ContinueResendCall call(session, 2, 3, 2);
    PlanBuffers pendingBuffers;
    auto pending = pendingBuffers.response();
    REQUIRE(irfq_infinite_prepare_v2(session, &call.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);

    const auto wire = participantFrame('0', 2);
    const auto row = retainedRow(2, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, "0", "", wire);
    alignas(irfq_infinite_store_row_v2) std::array<std::uint8_t, sizeof(irfq_infinite_store_row_v2) + 1> storage{};
    std::memcpy(storage.data() + 1, &row, sizeof(row));
    irfq_infinite_resume_request_v2 resume{};
    init(resume);
    resume.prepare_id = pending.prepare_id;
    resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
    resume.store_range_begin = 2;
    resume.store_range_end_exclusive = 3;
    resume.store_rows = reinterpret_cast<const irfq_infinite_store_row_v2 *>(storage.data() + 1);
    resume.store_row_count = 1;
    PlanBuffers resultBuffers;
    auto result = resultBuffers.response();
    CHECK(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

    resume.store_rows = &row;
    PlanBuffers retryBuffers;
    auto retry = retryBuffers.response();
    CHECK(irfq_infinite_resume_v2(session, &resume, &retry) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 preserves original application body bytes for stored replay",
    "[infinite][adapter][v2][task2e][stored-retransmit][body-bytes]") {
  const auto dictionaries = applicationBlockDictionaries();
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
  const std::string body = "70=LIST-1\00171=0\001626=1\00154=1\00153=1\00175=20231114\001";
  const auto wire
      = finishFix("35=J\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + body);

  const auto rendered = FIX::InfiniteSessionPlanner::storedFrame(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      6,
      2,
      INT64_C(1700000000123456001),
      1,
      wire,
      dictionaries,
      profile);

  CHECK(rendered.body == body);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 retransmits one exact retained application frame without advancing the live sender",
    "[infinite][adapter][v2][task2d][stored-retransmit]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config, 5);
  REQUIRE(session != nullptr);
  StoredRetransmitCall call(
      session,
      IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2,
      finishFix(
          "35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001"
          "693=TASK-2D\001694=1\001"));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &call.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(result.action_count == 1);
  CHECK(result.output_frame_count == 1);
  CHECK(result.has_more == IRFQ_INFINITE_NO_V2);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[0].output_class == IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2);
  CHECK(result.actions[0].sequence_begin == 2);
  CHECK(result.actions[0].sequence_end_exclusive == 3);
  CHECK(result.actions[0].msg_type_length == 2);
  CHECK(std::equal(result.actions[0].msg_type, result.actions[0].msg_type + 2, "AJ"));
  const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
  INFO(output);
  CHECK(output.find("\00134=2\001") != std::string::npos);
  CHECK(output.find("\00143=Y\001") != std::string::npos);
  CHECK(output.find("\001122=20260828-12:00:00.000000\001") != std::string::npos);
  CHECK(output.find("\00152=20231114-22:13:20.123456\001") != std::string::npos);
  CHECK(output.find("\001369=1\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 5);
  CHECK(read64(result.native_state.data + 96) == static_cast<std::uint64_t>(call.request.now_tai_ns));
  CHECK(read64(result.native_state.data + 104) == static_cast<std::uint64_t>(call.request.now_utc_ns));
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 continues direct resend with retransmit and one complete interior-gap run",
    "[infinite][adapter][v2][task2d][resend][gap-fill]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = resendRecoverySession(config, 2, 5, 2);
  REQUIRE(session != nullptr);
  ContinueResendCall call(session, 2, 5, 2);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &call.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  CHECK(pending.store_range_begin == 2);
  CHECK(pending.store_range_end_exclusive == 5);

  const auto body = quoteResponseBody("DIRECT");
  const std::vector<std::string> frames{
      finishFix("35=AJ\00134=2\00149=VENUE\00152=20260828-11:59:59.000000\00156=PARTICIPANT\001369=1\001" + body),
      finishFix("35=0\00134=3\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001"),
      ""};
  std::vector<irfq_infinite_store_row_v2> rows{
      retainedRow(2, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", body, frames[0]),
      retainedRow(3, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, "0", "", frames[1]),
      retainedRow(4, IRFQ_INFINITE_STORE_CLASS_PROVEN_GAP_V2, "", "", frames[2])};
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  resume.store_range_begin = 2;
  resume.store_range_end_exclusive = 5;
  resume.store_rows = rows.data();
  resume.store_row_count = rows.size();
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(result.action_count == 2);
  CHECK(result.output_frame_count == 2);
  CHECK(result.actions[0].output_class == IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2);
  CHECK(result.actions[0].sequence_begin == 2);
  CHECK(result.actions[0].sequence_end_exclusive == 3);
  CHECK(result.actions[1].output_class == IRFQ_INFINITE_OUTPUT_GAP_FILL_V2);
  CHECK(result.actions[1].sequence_begin == 3);
  CHECK(result.actions[1].sequence_end_exclusive == 5);
  const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
  INFO(output);
  CHECK(output.find("\00134=2\001") != std::string::npos);
  CHECK(output.find("\00134=3\001") != std::string::npos);
  CHECK(output.find("\00136=5\001") != std::string::npos);
  CHECK(output.find("\001122=20260828-12:00:00.000000\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 6);
  CHECK(read32(result.native_state.data + 220) == 0);
  CHECK(read32(result.native_state.data + 292) == 0);
  CHECK(result.has_more == IRFQ_INFINITE_NO_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 treats leading and interior PROVEN_GAP rows as the same complete GapFill run",
    "[infinite][adapter][v2][recovery][resend][gap-fill]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::string empty;
  const auto run = [&](bool leading) {
    auto *session = resendRecoverySession(config, 2, 4, 2);
    REQUIRE(session != nullptr);
    ContinueResendCall call(session, 2, 4, 2);
    PlanBuffers pendingBuffers;
    auto pending = pendingBuffers.response();
    REQUIRE(irfq_infinite_prepare_v2(session, &call.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
    const auto admin = finishFix(
        "35=0\00134=" + std::to_string(leading ? 3 : 2)
        + "\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001");
    std::array rows{
        leading ? retainedRow(2, IRFQ_INFINITE_STORE_CLASS_PROVEN_GAP_V2, "", "", empty)
                : retainedRow(2, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, "0", "", admin),
        leading ? retainedRow(3, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, "0", "", admin)
                : retainedRow(3, IRFQ_INFINITE_STORE_CLASS_PROVEN_GAP_V2, "", "", empty)};
    irfq_infinite_resume_request_v2 resume{};
    init(resume);
    resume.prepare_id = pending.prepare_id;
    resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
    resume.store_range_begin = 2;
    resume.store_range_end_exclusive = 4;
    resume.store_rows = rows.data();
    resume.store_row_count = rows.size();
    PlanBuffers buffers;
    auto result = buffers.response();
    REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
    REQUIRE(result.action_count == 1);
    const auto &action = result.actions[0];
    CHECK(result.output.length != 0);
    CHECK(result.output_frame_count == 1);
    CHECK(action.kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
    CHECK(action.output_class == IRFQ_INFINITE_OUTPUT_GAP_FILL_V2);
    CHECK(action.msg_type_length == 1);
    CHECK(action.msg_type[0] == '4');
    CHECK(action.sequence_begin == 2);
    CHECK(action.sequence_end_exclusive == 4);
    const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
    CHECK(output.find("\00135=4\001") != std::string::npos);
    CHECK(output.find("\00134=2\001") != std::string::npos);
    CHECK(output.find("\00136=4\001") != std::string::npos);
    CHECK(output.find("\001123=Y\001") != std::string::npos);
    CHECK(read32(result.native_state.data + 220) == 0);
    CHECK(read64(result.native_state.data + 228) == 0);
    CHECK(read64(result.native_state.data + 236) == 0);
    CHECK(read64(result.native_state.data + 244) == 0);
    CHECK(read32(result.native_state.data + 292) == 0);
    CHECK(read64(result.native_state.data + 300) == 0);
    CHECK(result.has_more == IRFQ_INFINITE_NO_V2);
    const std::array<std::uint64_t, 9> outcome{
        result.output_frame_count,
        action.kind,
        action.output_class,
        action.sequence_begin,
        action.sequence_end_exclusive,
        read32(result.native_state.data + 220),
        read64(result.native_state.data + 244),
        read32(result.native_state.data + 292),
        read64(result.native_state.data + 300)};
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    return std::pair{output, outcome};
  };

  CHECK(run(true) == run(false));
}

TEST_CASE(
    "InfiniteFrameAdapterV2 normalizes direct finite open empty and held-above-original resend ranges",
    "[infinite][adapter][v2][task2d][resend][direct-range]") {
  struct Variant {
    std::uint64_t begin;
    std::uint64_t inclusiveEnd;
    std::uint64_t original;
    std::uint64_t normalizedEnd;
    bool active;
  };
  const std::array variants{
      Variant{2, 4, 2, 5, true},
      Variant{3, 0, 3, 6, true},
      Variant{6, 0, 6, 6, false},
      Variant{4, 5, 2, 6, true}};
  const auto config = otherwiseValidUnavailableProfile();
  for (const auto &variant : variants) {
    DYNAMIC_SECTION("begin=" << variant.begin << " end=" << variant.inclusiveEnd << " original=" << variant.original) {
      auto *session = stockLoggedOnSession(config, 6);
      REQUIRE(session != nullptr);
      InboundCall inbound(
          session,
          participantFrame(
              '2',
              2,
              "7=" + std::to_string(variant.begin) + "\00116=" + std::to_string(variant.inclusiveEnd) + "\001"),
          0xd2);
      inbound.request.next_original_value = variant.original;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(result.output.length == 0);
      REQUIRE(result.action_count == 1);
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
      CHECK(read64(result.native_state.data + 144) == 2);
      CHECK(read32(result.native_state.data + 220) == (variant.active ? 1U : 0U));
      CHECK(read32(result.native_state.data + 224) == (variant.active ? 3U : 0U));
      CHECK(read64(result.native_state.data + 228) == (variant.active ? variant.begin : 0));
      CHECK(read64(result.native_state.data + 236) == (variant.active ? variant.normalizedEnd : 0));
      CHECK(read64(result.native_state.data + 244) == (variant.active ? variant.begin : 0));
      CHECK(read32(result.native_state.data + 292) == (variant.active ? 1U : 0U));
      CHECK(read64(result.native_state.data + 300) == (variant.active ? variant.begin : 0));
      if (variant.begin > variant.original) {
        irfq_infinite_apply_committed_request_v2 apply{};
        init(apply);
        apply.prepare_id = result.prepare_id;
        apply.result_revision = result.result_revision;
        std::copy_n(result.native_state_sha256, 32, apply.native_state_sha256);
        irfq_infinite_operation_response_v2 applied{};
        init(applied);
        REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);

        ContinueResendCall held(session, variant.begin, variant.normalizedEnd, variant.begin, 2, variant.original);
        PlanBuffers heldBuffers;
        auto heldResult = heldBuffers.response();
        CHECK(
            irfq_infinite_prepare_v2(session, &held.request, &heldResult) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        ContinueResendCall released(session, variant.begin, variant.normalizedEnd, variant.begin, 2, variant.begin);
        PlanBuffers releasedBuffers;
        auto releasedResult = releasedBuffers.response();
        CHECK(
            irfq_infinite_prepare_v2(session, &released.request, &releasedResult)
            == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  auto *paged = resendRecoverySession(config, 2, 5, 3);
  REQUIRE(paged != nullptr);
  ContinueResendCall overtaking(paged, 2, 5, 3, 1, 2);
  PlanBuffers overtakingBuffers;
  auto overtakingResult = overtakingBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(paged, &overtaking.request, &overtakingResult)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  ContinueResendCall closedPrefix(paged, 2, 5, 3, 1, 3);
  PlanBuffers closedPrefixBuffers;
  auto closedPrefixResult = closedPrefixBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(paged, &closedPrefix.request, &closedPrefixResult)
      == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  CHECK(irfq_infinite_destroy_v2(paged) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 holds direct recovery behind the exact Logon response handoff",
    "[infinite][adapter][v2][task2d][resend][direct-response-barrier][handoff]") {
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  struct Variant {
    const char *name;
    std::uint64_t sender;
    bool peerPresent;
  };
  const std::array variants{
      Variant{"value-peer-absent", 7, false},
      Variant{"value-peer-equal", 7, true},
      Variant{"bound-peer-absent", lastLegal, false},
      Variant{"bound-peer-equal", lastLegal, true}};
  const auto config = otherwiseValidUnavailableProfile();
  for (const auto &variant : variants) {
    DYNAMIC_SECTION(variant.name) {
      std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> detachedState{};
      auto *session = detachedResendRecoverySession(config, 2, 5, 2, variant.sender, &detachedState);
      REQUIRE(session != nullptr);
      auto fields = std::string("98=0\001108=30\001");
      if (variant.peerPresent) {
        fields += "789=" + std::to_string(variant.sender) + "\001";
      }
      fields += "1137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
      InboundCall reattach(session, participantFrame('A', 3, fields), 0x31);
      reattach.request.expected_revision = 4;
      reattach.request.next_original_value = variant.sender;
      reattach.request.now_tai_ns = INT64_C(1700000000123456004);
      reattach.request.now_utc_ns = INT64_C(1700000000123456004);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              reattach.request,
              reattach.request.event_identity_sha256));
      PlanBuffers responseBuffers;
      auto response = responseBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &reattach.request, &response) == IRFQ_INFINITE_STATUS_READY_V2);
      REQUIRE(response.action_count == 2);
      CHECK(response.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(response.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
      CHECK(response.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(response.actions[1].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
      CHECK(response.actions[1].msg_type_length == 1);
      CHECK(response.actions[1].msg_type[0] == 'A');
      CHECK(response.actions[1].sequence_begin == variant.sender);
      CHECK(response.actions[1].sequence_end_exclusive == variant.sender + 1);
      const std::string responseWire(reinterpret_cast<const char *>(response.output.data), response.output.length);
      std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> targetExhaustedBarrier{};
      std::copy_n(response.native_state.data, response.native_state.length, targetExhaustedBarrier.begin());
      write32(targetExhaustedBarrier.data() + 140, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
      write64(targetExhaustedBarrier.data() + 144, 0);
      auto *retry = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          targetExhaustedBarrier.data(),
          targetExhaustedBarrier.size(),
          1,
          5,
          0,
          0);
      REQUIRE(retry != nullptr);
      StoredRetransmitCall retained(retry, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, responseWire, 5, 0x33);
      PlanBuffers retainedBuffers;
      auto retainedResult = retainedBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(retry, &retained.request, &retainedResult) == IRFQ_INFINITE_STATUS_READY_V2);
      REQUIRE(retainedResult.action_count == 1);
      CHECK(retainedResult.actions[0].output_class == IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2);
      CHECK(retainedResult.actions[0].sequence_begin == variant.sender);
      CHECK(read32(retainedResult.native_state.data + 128) == read32(targetExhaustedBarrier.data() + 128));
      CHECK(read64(retainedResult.native_state.data + 132) == read64(targetExhaustedBarrier.data() + 132));
      CHECK(read32(retainedResult.native_state.data + 140) == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
      CHECK(read64(retainedResult.native_state.data + 144) == 0);
      CHECK(read32(retainedResult.native_state.data + 220) == 1);
      CHECK(read32(retainedResult.native_state.data + 292) == 0);
      CHECK(irfq_infinite_destroy_v2(retry) == IRFQ_INFINITE_STATUS_OK_V2);
      CHECK(responseWire.find("\00135=A\001") != std::string::npos);
      CHECK(responseWire.find("\00134=" + std::to_string(variant.sender) + "\001") != std::string::npos);
      const auto exhausted = variant.sender == lastLegal;
      CHECK(
          read32(response.native_state.data + 128)
          == (exhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2));
      CHECK(read64(response.native_state.data + 132) == (exhausted ? 0 : variant.sender + 1));
      CHECK(read64(response.native_state.data + 180) == UINT64_C(135));
      CHECK(read32(response.native_state.data + 188) == 30);
      CHECK(read32(response.native_state.data + 220) == 1);
      CHECK(read32(response.native_state.data + 224) == 3);
      CHECK(read64(response.native_state.data + 228) == 2);
      CHECK(read64(response.native_state.data + 236) == 5);
      CHECK(read64(response.native_state.data + 244) == 2);
      CHECK(std::equal(detachedState.begin() + 252, detachedState.begin() + 284, response.native_state.data + 252));
      CHECK(read32(response.native_state.data + 288) == 10);
      CHECK(read32(response.native_state.data + 292) == 0);
      CHECK(read64(response.native_state.data + 300) == 0);
      CHECK(response.has_more == IRFQ_INFINITE_NO_V2);

      irfq_infinite_apply_committed_request_v2 apply{};
      init(apply);
      apply.prepare_id = response.prepare_id;
      apply.result_revision = response.result_revision;
      std::copy_n(response.native_state_sha256, 32, apply.native_state_sha256);
      irfq_infinite_operation_response_v2 operation{};
      init(operation);
      REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

      InboundCall heartbeat(session, participantFrame('0', 3), 0x32);
      heartbeat.request.expected_revision = 5;
      heartbeat.request.next_original_state
          = exhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2;
      heartbeat.request.next_original_value = exhausted ? 0 : variant.sender + 1;
      heartbeat.request.now_tai_ns = INT64_C(1700000000123456005);
      heartbeat.request.now_utc_ns = INT64_C(1700000000123456005);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              heartbeat.request,
              heartbeat.request.event_identity_sha256));
      PlanBuffers heartbeatBuffers;
      auto heartbeatResult = heartbeatBuffers.response();
      CHECK(
          irfq_infinite_prepare_v2(session, &heartbeat.request, &heartbeatResult)
          == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

      ContinueResendCall oldCursor(session, 2, 5, 2, 5, variant.sender);
      PlanBuffers oldBuffers;
      auto old = oldBuffers.response();
      CHECK(irfq_infinite_prepare_v2(session, &oldCursor.request, &old) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

      ContinueResendCall handedOff(session, 2, 5, 2, 5, exhausted ? 1 : variant.sender + 1);
      handedOff.request.next_original_state
          = exhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2;
      handedOff.request.next_original_value = exhausted ? 0 : variant.sender + 1;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              handedOff.request,
              handedOff.request.event_identity_sha256));
      PlanBuffers handoffBuffers;
      auto handoff = handoffBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &handedOff.request, &handoff) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(handoff.output.length == 0);
      CHECK(handoff.output_frame_count == 0);
      CHECK(handoff.action_count == 0);
      CHECK(read32(handoff.native_state.data + 128) == read32(response.native_state.data + 128));
      CHECK(read64(handoff.native_state.data + 132) == read64(response.native_state.data + 132));
      CHECK(read64(handoff.native_state.data + 180) == UINT64_C(135));
      CHECK(read32(handoff.native_state.data + 220) == 1);
      CHECK(read32(handoff.native_state.data + 224) == 3);
      CHECK(read64(handoff.native_state.data + 228) == 2);
      CHECK(read64(handoff.native_state.data + 236) == 5);
      CHECK(read64(handoff.native_state.data + 244) == 2);
      CHECK(
          std::equal(
              response.native_state.data + 252,
              response.native_state.data + 284,
              handoff.native_state.data + 252));
      CHECK(read32(handoff.native_state.data + 292) == 1);
      CHECK(read64(handoff.native_state.data + 300) == 2);
      const auto drainTargetExhausted = [&](bool gapFill) {
        std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
        std::copy_n(handoff.native_state.data, handoff.native_state.length, state.begin());
        write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
        write64(state.data() + 144, 0);
        auto *drain = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
            config.data(),
            config.size(),
            state.data(),
            state.size(),
            1,
            6,
            0,
            0);
        REQUIRE(drain != nullptr);
        ContinueResendCall page(drain, 2, 5, 2, 6, exhausted ? 1 : variant.sender + 1);
        page.request.next_original_state
            = exhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2;
        page.request.next_original_value = exhausted ? 0 : variant.sender + 1;
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                drain,
                page.request,
                page.request.event_identity_sha256));
        PlanBuffers pendingBuffers;
        auto pending = pendingBuffers.response();
        REQUIRE(irfq_infinite_prepare_v2(drain, &page.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
        std::vector<std::string> bodies;
        std::vector<std::string> wires;
        std::vector<irfq_infinite_store_row_v2> rows;
        bodies.reserve(3);
        wires.reserve(3);
        rows.reserve(3);
        for (std::uint64_t sequence = 2; sequence < 5; ++sequence) {
          bodies.push_back(quoteResponseBody("TARGET-EXHAUSTED-" + std::to_string(sequence)));
          wires.push_back(finishFix(
              "35=AJ\00134=" + std::to_string(sequence)
              + "\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + bodies.back()));
          rows.push_back(retainedRow(
              sequence,
              gapFill ? IRFQ_INFINITE_STORE_CLASS_REVOCABLE_SUPPRESSED_V2
                      : IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2,
              "AJ",
              bodies.back(),
              wires.back()));
        }
        irfq_infinite_resume_request_v2 resume{};
        init(resume);
        resume.prepare_id = pending.prepare_id;
        resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
        resume.store_range_begin = 2;
        resume.store_range_end_exclusive = 5;
        resume.store_rows = rows.data();
        resume.store_row_count = rows.size();
        PlanBuffers resumedBuffers;
        auto resumed = resumedBuffers.response();
        REQUIRE(irfq_infinite_resume_v2(drain, &resume, &resumed) == IRFQ_INFINITE_STATUS_READY_V2);
        CHECK(resumed.output_frame_count == (gapFill ? 1 : 3));
        CHECK(resumed.action_count == (gapFill ? 1 : 3));
        CHECK(read32(resumed.native_state.data + 128) == read32(state.data() + 128));
        CHECK(read64(resumed.native_state.data + 132) == read64(state.data() + 132));
        CHECK(read32(resumed.native_state.data + 140) == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
        CHECK(read64(resumed.native_state.data + 144) == 0);
        CHECK(read32(resumed.native_state.data + 220) == 0);
        CHECK(read32(resumed.native_state.data + 292) == 0);
        CHECK(irfq_infinite_destroy_v2(drain) == IRFQ_INFINITE_STATUS_OK_V2);
      };
      drainTargetExhausted(false);
      drainTargetExhausted(true);
      auto *roundTrip = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          handoff.native_state.data,
          handoff.native_state.length,
          1,
          6,
          0,
          0);
      REQUIRE(roundTrip != nullptr);
      if (exhausted) {
        ContinueResendCall page(roundTrip, 2, 5, 2, 6, 1);
        page.request.next_original_state = IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2;
        page.request.next_original_value = 0;
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                roundTrip,
                page.request,
                page.request.event_identity_sha256));
        PlanBuffers pendingBuffers;
        auto pending = pendingBuffers.response();
        REQUIRE(
            irfq_infinite_prepare_v2(roundTrip, &page.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
        std::vector<std::string> bodies;
        std::vector<std::string> wires;
        std::vector<irfq_infinite_store_row_v2> rows;
        bodies.reserve(3);
        wires.reserve(3);
        rows.reserve(3);
        for (std::uint64_t sequence = 2; sequence < 5; ++sequence) {
          bodies.push_back(quoteResponseBody("EXHAUSTED-" + std::to_string(sequence)));
          wires.push_back(finishFix(
              "35=AJ\00134=" + std::to_string(sequence)
              + "\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + bodies.back()));
          rows.push_back(retainedRow(
              sequence,
              IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2,
              "AJ",
              bodies.back(),
              wires.back()));
        }
        irfq_infinite_resume_request_v2 resume{};
        init(resume);
        resume.prepare_id = pending.prepare_id;
        resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
        resume.store_range_begin = 2;
        resume.store_range_end_exclusive = 5;
        resume.store_rows = rows.data();
        resume.store_row_count = rows.size();
        PlanBuffers resumedBuffers;
        auto resumed = resumedBuffers.response();
        REQUIRE(irfq_infinite_resume_v2(roundTrip, &resume, &resumed) == IRFQ_INFINITE_STATUS_READY_V2);
        CHECK(resumed.output_frame_count == 3);
        CHECK(resumed.action_count == 3);
        CHECK(read32(resumed.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
        CHECK(read64(resumed.native_state.data + 132) == 0);
        CHECK(read32(resumed.native_state.data + 220) == 0);
        CHECK(read32(resumed.native_state.data + 292) == 0);
      }
      CHECK(irfq_infinite_destroy_v2(roundTrip) == IRFQ_INFINITE_STATUS_OK_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 round-trips the negotiated heartbeat in a direct response barrier",
    "[infinite][adapter][v2][task2d][resend][direct-response-barrier][heartbeat]") {
  const auto config = otherwiseValidUnavailableProfile(1, {}, 2, 0, 20, 40);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  auto *detached = detachedSenderSession(config, 7, 35, &state);
  REQUIRE(detached != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(detached) == IRFQ_INFINITE_STATUS_OK_V2);
  write32(state.data() + 220, 1);
  write32(state.data() + 224, 3);
  write64(state.data() + 228, 2);
  write64(state.data() + 236, 5);
  write64(state.data() + 244, 2);
  std::fill_n(state.data() + 252, 32, std::uint8_t{0x71});
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
  InboundCall reattach(
      session,
      participantFrame('A', 2, "98=0\001108=35\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x64);
  reattach.request.expected_revision = 2;
  reattach.request.next_original_value = 7;
  reattach.request.now_tai_ns = INT64_C(1700000000123456002);
  reattach.request.now_utc_ns = INT64_C(1700000000123456002);
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          reattach.request,
          reattach.request.event_identity_sha256));
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &reattach.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(result.native_state.data + 180) == UINT64_C(135));
  CHECK(read32(result.native_state.data + 188) == 35);
  CHECK(read32(result.native_state.data + 220) == 1);
  CHECK(read32(result.native_state.data + 292) == 0);
  REQUIRE(result.action_count == 2);
  const std::string responseWire(reinterpret_cast<const char *>(result.output.data), result.output.length);
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
  write64(casPayload.data() + 48, 3);
  irfq_infinite_prepare_request_v2 cas{};
  init(cas);
  cas.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  cas.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
  cas.event = IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2;
  cas.expected_epoch = 1;
  cas.expected_revision = 3;
  cas.now_tai_ns = INT64_C(1700000000123456003);
  cas.now_utc_ns = INT64_C(1700000000123456003);
  cas.payload = {casPayload.data(), casPayload.size()};
  REQUIRE(FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, cas, cas.event_identity_sha256));
  PlanBuffers casBuffers;
  auto casResult = casBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &cas, &casResult) == IRFQ_INFINITE_STATUS_READY_V2);
  init(apply);
  apply.prepare_id = casResult.prepare_id;
  apply.result_revision = casResult.result_revision;
  std::copy_n(casResult.native_state_sha256, 32, apply.native_state_sha256);
  init(operation);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &operation) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x65);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = 1;
  close.expected_revision = 4;
  close.now_tai_ns = INT64_C(1700000000123456004);
  close.now_utc_ns = INT64_C(1700000000123456004);
  close.payload = {closePayload.data(), closePayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, close, close.event_identity_sha256));
  PlanBuffers closeBuffers;
  auto closed = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read32(closed.native_state.data + 188) == 0);
  REQUIRE(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      closed.native_state.data,
      closed.native_state.length,
      1,
      5,
      0,
      0);
  REQUIRE(session != nullptr);

  InboundCall lost(
      session,
      participantFrame('A', 3, "98=0\001108=35\001789=7\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x66);
  lost.request.expected_revision = 5;
  lost.request.next_original_value = 7;
  lost.request.now_tai_ns = INT64_C(1700000000123456005);
  lost.request.now_utc_ns = INT64_C(1700000000123456005);
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          lost.request,
          lost.request.event_identity_sha256));
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &lost.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  const auto row
      = retainedRow(7, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, "A", canonicalBody(responseWire), responseWire);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  resume.store_range_begin = 7;
  resume.store_range_end_exclusive = 8;
  resume.store_rows = &row;
  resume.store_row_count = 1;
  PlanBuffers retryBuffers;
  auto retry = retryBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &retry) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(retry.action_count == 2);
  CHECK(retry.actions[1].output_class == IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2);
  CHECK(read32(retry.native_state.data + 188) == 35);
  auto *roundTrip = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      retry.native_state.data,
      retry.native_state.length,
      1,
      6,
      0,
      0);
  REQUIRE(roundTrip != nullptr);
  CHECK(irfq_infinite_destroy_v2(roundTrip) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 reborrows one lost direct-recovery Logon response across repeated fresh transports",
    "[infinite][adapter][v2][task2d][resend][direct-response-barrier][lost-response]") {
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  const auto config = otherwiseValidUnavailableProfile();
  for (const auto sender : {UINT64_C(7), lastLegal}) {
    DYNAMIC_SECTION("response-sequence=" << sender) {
      std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> detachedState{};
      auto *session = detachedResendRecoverySession(config, 2, 5, 2, sender, &detachedState);
      REQUIRE(session != nullptr);
      const auto commit = [](irfq_infinite_session_v2 *target, const irfq_infinite_prepare_response_v2 &result) {
        irfq_infinite_apply_committed_request_v2 request{};
        init(request);
        request.prepare_id = result.prepare_id;
        request.result_revision = result.result_revision;
        std::copy_n(result.native_state_sha256, 32, request.native_state_sha256);
        irfq_infinite_operation_response_v2 response{};
        init(response);
        return irfq_infinite_apply_committed_v2(target, &request, &response);
      };
      InboundCall initial(
          session,
          participantFrame('A', 3, "98=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x41);
      initial.request.expected_revision = 4;
      initial.request.next_original_value = sender;
      initial.request.now_tai_ns = INT64_C(1700000000123456004);
      initial.request.now_utc_ns = INT64_C(1700000000123456004);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              initial.request,
              initial.request.event_identity_sha256));
      PlanBuffers initialBuffers;
      auto initialResult = initialBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &initial.request, &initialResult) == IRFQ_INFINITE_STATUS_READY_V2);
      REQUIRE(initialResult.action_count == 2);
      const std::string responseWire(
          reinterpret_cast<const char *>(initialResult.output.data),
          initialResult.output.length);
      std::array<std::uint8_t, 32> dispositionBinding{};
      std::copy_n(initialResult.actions[0].binding_sha256, dispositionBinding.size(), dispositionBinding.begin());
      REQUIRE(commit(session, initialResult) == IRFQ_INFINITE_STATUS_OK_V2);

      const auto rejectStored = [&](const std::string &wire, std::uint8_t subject) {
        StoredRetransmitCall call(session, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, wire, 5, subject);
        PlanBuffers buffers;
        auto result = buffers.response();
        CHECK(irfq_infinite_prepare_v2(session, &call.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      };
      rejectStored(
          finishFix(
              "35=0\00134=" + std::to_string(sender)
              + "\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\001369=1\001"),
          0x42);
      rejectStored(
          FIX::InfiniteSessionPlanner::logon(
              "FIXT.1.1",
              "VENUE",
              "PARTICIPANT",
              30,
              sender - 1,
              3,
              INT64_C(1700000000123456004),
              1)
              .output,
          0x43);

      StoredRetransmitCall exactRetry(session, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, responseWire, 5, 0x44);
      PlanBuffers exactBuffers;
      auto exact = exactBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &exactRetry.request, &exact) == IRFQ_INFINITE_STATUS_READY_V2);
      REQUIRE(exact.action_count == 1);
      CHECK(exact.actions[0].output_class == IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2);
      CHECK(exact.actions[0].msg_type[0] == 'A');
      CHECK(exact.actions[0].sequence_begin == sender);
      CHECK(read32(exact.native_state.data + 128) == read32(initialResult.native_state.data + 128));
      CHECK(read64(exact.native_state.data + 132) == read64(initialResult.native_state.data + 132));
      CHECK(read32(exact.native_state.data + 292) == 0);
      REQUIRE(commit(session, exact) == IRFQ_INFINITE_STATUS_OK_V2);

      std::uint64_t revision = 6;
      std::uint64_t targetSequence = 3;
      for (unsigned cycle = 0; cycle < 2; ++cycle) {
        INFO("cycle=" << cycle);
        std::array<std::uint8_t, 56> casPayload{};
        std::copy(dispositionBinding.begin(), dispositionBinding.end(), casPayload.begin());
        write32(casPayload.data() + 32, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
        write64(casPayload.data() + 36, targetSequence);
        write32(casPayload.data() + 44, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
        write64(casPayload.data() + 48, targetSequence + 1);
        irfq_infinite_prepare_request_v2 cas{};
        init(cas);
        cas.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
        cas.stage = IRFQ_INFINITE_STAGE_TARGET_CAS_V2;
        cas.event = IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2;
        cas.expected_epoch = 1;
        cas.expected_revision = revision;
        cas.now_tai_ns = INT64_C(1700000000123456010) + cycle * 4;
        cas.now_utc_ns = cas.now_tai_ns;
        cas.payload = {casPayload.data(), casPayload.size()};
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, cas, cas.event_identity_sha256));
        PlanBuffers casBuffers;
        auto casResult = casBuffers.response();
        REQUIRE(irfq_infinite_prepare_v2(session, &cas, &casResult) == IRFQ_INFINITE_STATUS_READY_V2);
        REQUIRE(commit(session, casResult) == IRFQ_INFINITE_STATUS_OK_V2);
        ++revision;
        ++targetSequence;

        std::array<std::uint8_t, 32> closePayload{};
        closePayload.fill(static_cast<std::uint8_t>(0x45 + cycle));
        irfq_infinite_prepare_request_v2 close{};
        init(close);
        close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
        close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
        close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
        close.expected_epoch = 1;
        close.expected_revision = revision;
        close.now_tai_ns = cas.now_tai_ns + 1;
        close.now_utc_ns = cas.now_utc_ns + 1;
        close.payload = {closePayload.data(), closePayload.size()};
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                session,
                close,
                close.event_identity_sha256));
        PlanBuffers closeBuffers;
        auto closed = closeBuffers.response();
        REQUIRE(irfq_infinite_prepare_v2(session, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
        ++revision;
        REQUIRE(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
        session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
            config.data(),
            config.size(),
            closed.native_state.data,
            closed.native_state.length,
            1,
            revision,
            0,
            0);
        REQUIRE(session != nullptr);

        InboundCall reattach(
            session,
            participantFrame(
                'A',
                targetSequence,
                "98=0\001108=30\001789=" + std::to_string(sender)
                    + "\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
            static_cast<std::uint8_t>(0x47 + cycle));
        reattach.request.expected_revision = revision;
        reattach.request.next_original_value = sender;
        reattach.request.now_tai_ns = close.now_tai_ns + 1;
        reattach.request.now_utc_ns = close.now_utc_ns + 1;
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                session,
                reattach.request,
                reattach.request.event_identity_sha256));
        PlanBuffers pendingBuffers;
        auto pending = pendingBuffers.response();
        const auto pendingStatus = irfq_infinite_prepare_v2(session, &reattach.request, &pending);
        if (sender == lastLegal) {
          CHECK(pendingStatus == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
          CHECK(pending.prepare_id.low == 0);
          CHECK(pending.native_state.length == 0);
          CHECK(pending.output.length == 0);
          CHECK(pending.action_count == 0);
          break;
        }
        REQUIRE(pendingStatus == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
        CHECK(pending.store_range_begin == sender);
        CHECK(pending.store_range_end_exclusive == sender + 1);
        const auto row = retainedRow(
            sender,
            IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2,
            "A",
            canonicalBody(responseWire),
            responseWire);
        irfq_infinite_resume_request_v2 resume{};
        init(resume);
        resume.prepare_id = pending.prepare_id;
        resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
        resume.store_range_begin = sender;
        resume.store_range_end_exclusive = sender + 1;
        resume.store_rows = &row;
        resume.store_row_count = 1;
        PlanBuffers retryBuffers;
        auto retry = retryBuffers.response();
        REQUIRE(irfq_infinite_resume_v2(session, &resume, &retry) == IRFQ_INFINITE_STATUS_READY_V2);
        REQUIRE(retry.action_count == 2);
        CHECK(retry.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
        CHECK(retry.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
        CHECK(retry.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
        CHECK(retry.actions[1].output_class == IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2);
        CHECK(retry.actions[1].sequence_begin == sender);
        CHECK(read32(retry.native_state.data + 128) == read32(initialResult.native_state.data + 128));
        CHECK(read64(retry.native_state.data + 132) == read64(initialResult.native_state.data + 132));
        CHECK(read64(retry.native_state.data + 180) == UINT64_C(135));
        CHECK(read32(retry.native_state.data + 220) == 1);
        CHECK(read32(retry.native_state.data + 224) == 3);
        CHECK(read64(retry.native_state.data + 228) == 2);
        CHECK(read64(retry.native_state.data + 236) == 5);
        CHECK(read64(retry.native_state.data + 244) == 2);
        CHECK(std::equal(detachedState.begin() + 252, detachedState.begin() + 284, retry.native_state.data + 252));
        CHECK(read32(retry.native_state.data + 292) == 0);
        CHECK(read64(retry.native_state.data + 300) == 0);
        std::copy_n(retry.actions[0].binding_sha256, dispositionBinding.size(), dispositionBinding.begin());
        REQUIRE(commit(session, retry) == IRFQ_INFINITE_STATUS_OK_V2);
        ++revision;
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 fail-closes invalid direct-recovery and exhausted ordinary Logon cursors",
    "[infinite][adapter][v2][task2d][resend][direct-response-barrier][fail-closed]") {
  struct DirectVariant {
    const char *name;
    std::uint32_t originalState;
    std::uint64_t original;
    bool peerPresent;
    std::uint64_t peer;
  };
  const std::array directVariants{
      DirectVariant{"equal-original-conflicting-peer", IRFQ_INFINITE_SEQUENCE_VALUE_V2, 7, true, 6},
      DirectVariant{"predecessor-without-peer", IRFQ_INFINITE_SEQUENCE_VALUE_V2, 6, false, 0},
      DirectVariant{"predecessor-conflicting-peer", IRFQ_INFINITE_SEQUENCE_VALUE_V2, 6, true, 7},
      DirectVariant{"future-original", IRFQ_INFINITE_SEQUENCE_VALUE_V2, 8, false, 0},
      DirectVariant{"exhausted-original", IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2, 0, false, 0}};
  const auto config = otherwiseValidUnavailableProfile();
  for (const auto &variant : directVariants) {
    DYNAMIC_SECTION(variant.name) {
      std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> detachedState{};
      auto *session = detachedResendRecoverySession(config, 2, 5, 2, 7, &detachedState);
      REQUIRE(session != nullptr);
      auto fields = std::string("98=0\001108=30\001");
      if (variant.peerPresent) {
        fields += "789=" + std::to_string(variant.peer) + "\001";
      }
      fields += "1137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
      InboundCall reattach(session, participantFrame('A', 3, fields), 0x51);
      reattach.request.expected_revision = 4;
      reattach.request.next_original_state = variant.originalState;
      reattach.request.next_original_value = variant.original;
      reattach.request.now_tai_ns = INT64_C(1700000000123456004);
      reattach.request.now_utc_ns = INT64_C(1700000000123456004);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              reattach.request,
              reattach.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &reattach.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(result.output.length == 0);
      CHECK(result.output_frame_count == 0);
      REQUIRE(result.action_count == 2);
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(result.actions[1].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_VALUE_V2);
      CHECK(read64(result.native_state.data + 132) == 7);
      CHECK(read64(result.native_state.data + 180) == UINT64_C(1));
      CHECK(read32(result.native_state.data + 168) == 0);
      CHECK(read64(result.native_state.data + 172) == 0);
      CHECK(read32(result.native_state.data + 220) == 1);
      CHECK(read32(result.native_state.data + 224) == 3);
      CHECK(read64(result.native_state.data + 228) == 2);
      CHECK(read64(result.native_state.data + 236) == 5);
      CHECK(read64(result.native_state.data + 244) == 2);
      CHECK(std::equal(detachedState.begin() + 252, detachedState.begin() + 284, result.native_state.data + 252));
      CHECK(read32(result.native_state.data + 288) == 0);
      CHECK(read32(result.native_state.data + 292) == 0);
      CHECK(read64(result.native_state.data + 300) == 0);
      CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_NONE_V2);
      auto *roundTrip = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          result.native_state.data,
          result.native_state.length,
          1,
          5,
          0,
          0);
      REQUIRE(roundTrip != nullptr);
      CHECK(irfq_infinite_destroy_v2(roundTrip) == IRFQ_INFINITE_STATUS_OK_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> exhaustedRecovery{};
  auto *bounded = detachedResendRecoverySession(config, 2, 5, 2, lastLegal, &exhaustedRecovery);
  REQUIRE(bounded != nullptr);
  CHECK(irfq_infinite_destroy_v2(bounded) == IRFQ_INFINITE_STATUS_OK_V2);
  write32(exhaustedRecovery.data() + 128, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
  write64(exhaustedRecovery.data() + 132, 0);
  auto *exhausted = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      exhaustedRecovery.data(),
      exhaustedRecovery.size(),
      1,
      4,
      0,
      0);
  REQUIRE(exhausted != nullptr);
  InboundCall exhaustedReattach(
      exhausted,
      participantFrame('A', 3, "98=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x52);
  exhaustedReattach.request.expected_revision = 4;
  exhaustedReattach.request.next_original_state = IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2;
  exhaustedReattach.request.next_original_value = 0;
  exhaustedReattach.request.now_tai_ns = INT64_C(1700000000123456004);
  exhaustedReattach.request.now_utc_ns = INT64_C(1700000000123456004);
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          exhausted,
          exhaustedReattach.request,
          exhaustedReattach.request.event_identity_sha256));
  PlanBuffers exhaustedBuffers;
  auto exhaustedResult = exhaustedBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(exhausted, &exhaustedReattach.request, &exhaustedResult)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(exhaustedResult.prepare_id.low == 0);
  CHECK(exhaustedResult.native_state.length == 0);
  CHECK(exhaustedResult.output.length == 0);
  CHECK(exhaustedResult.action_count == 0);
  CHECK(irfq_infinite_destroy_v2(exhausted) == IRFQ_INFINITE_STATUS_OK_V2);

  for (const bool nativeExhausted : {false, true}) {
    DYNAMIC_SECTION("recovery-none-native-exhausted=" << nativeExhausted) {
      std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
      auto *ordinary = detachedSenderSession(config, 7, 30, &state);
      REQUIRE(ordinary != nullptr);
      REQUIRE(irfq_infinite_destroy_v2(ordinary) == IRFQ_INFINITE_STATUS_OK_V2);
      if (nativeExhausted) {
        write32(state.data() + 128, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
        write64(state.data() + 132, 0);
      }
      ordinary = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          state.data(),
          state.size(),
          1,
          2,
          0,
          0);
      REQUIRE(ordinary != nullptr);
      InboundCall logon(
          ordinary,
          participantFrame('A', 2, "98=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x53);
      logon.request.expected_revision = 2;
      logon.request.next_original_state
          = nativeExhausted ? IRFQ_INFINITE_SEQUENCE_VALUE_V2 : IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2;
      logon.request.next_original_value = nativeExhausted ? 7 : 0;
      logon.request.now_tai_ns = INT64_C(1700000000123456002);
      logon.request.now_utc_ns = INT64_C(1700000000123456002);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              ordinary,
              logon.request,
              logon.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      if (nativeExhausted) {
        CHECK(irfq_infinite_prepare_v2(ordinary, &logon.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        CHECK(result.prepare_id.low == 0);
        CHECK(result.native_state.length == 0);
        CHECK(result.output.length == 0);
        CHECK(result.action_count == 0);
        CHECK(irfq_infinite_destroy_v2(ordinary) == IRFQ_INFINITE_STATUS_OK_V2);
        continue;
      }
      REQUIRE(irfq_infinite_prepare_v2(ordinary, &logon.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(result.output.length == 0);
      REQUIRE(result.action_count == 2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(result.actions[1].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      CHECK(read32(result.native_state.data + 128) == read32(state.data() + 128));
      CHECK(read64(result.native_state.data + 132) == read64(state.data() + 132));
      CHECK(read64(result.native_state.data + 180) == (UINT64_C(1) | UINT64_C(256)));
      CHECK(read32(result.native_state.data + 168) == 0);
      CHECK(read64(result.native_state.data + 172) == 0);
      CHECK(read32(result.native_state.data + 220) == 0);
      CHECK(read32(result.native_state.data + 288) == 0);
      CHECK(read32(result.native_state.data + 292) == 0);
      CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      auto *roundTrip = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          result.native_state.data,
          result.native_state.length,
          1,
          3,
          0,
          0);
      REQUIRE(roundTrip != nullptr);
      CHECK(irfq_infinite_destroy_v2(roundTrip) == IRFQ_INFINITE_STATUS_OK_V2);
      CHECK(irfq_infinite_destroy_v2(ordinary) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  auto impossibleBarrier = exhaustedRecovery;
  write32(impossibleBarrier.data() + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(impossibleBarrier.data() + 132, 1);
  write64(impossibleBarrier.data() + 160, 0);
  write64(impossibleBarrier.data() + 180, UINT64_C(135));
  write32(impossibleBarrier.data() + 288, 10);
  write32(impossibleBarrier.data() + 292, 0);
  write64(impossibleBarrier.data() + 300, 0);
  CHECK(
      FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          impossibleBarrier.data(),
          impossibleBarrier.size(),
          1,
          4,
          0,
          0)
      == nullptr);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 preserves valid reset-Logon decisions across detached exhausted and recovery states",
    "[infinite][adapter][v2][task2d][reset-logon-order]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto expectDecision = [&](const char *name,
                                  const std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> &baselineState,
                                  std::uint64_t revision,
                                  std::uint64_t target,
                                  std::uint32_t originalState,
                                  std::uint64_t original,
                                  std::int64_t now) {
    DYNAMIC_SECTION(name) {
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          baselineState.data(),
          baselineState.size(),
          1,
          revision,
          0,
          0);
      REQUIRE(session != nullptr);
      InboundCall reset(
          session,
          participantFrame(
              'A',
              target,
              "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x67);
      reset.request.expected_revision = revision;
      reset.request.next_original_state = originalState;
      reset.request.next_original_value = original;
      reset.request.now_tai_ns = now;
      reset.request.now_utc_ns = now;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              reset.request,
              reset.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(
          irfq_infinite_prepare_v2(session, &reset.request, &result)
          == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
      CHECK(result.input_source == IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2);
      CHECK(result.input_offset == 0);
      CHECK(result.input_length == reset.payload.size());
      CHECK(
          std::any_of(result.subject_sha256, result.subject_sha256 + 32, [](std::uint8_t byte) { return byte != 0; }));

      irfq_infinite_resume_request_v2 resume{};
      init(resume);
      resume.prepare_id = result.prepare_id;
      resume.step = result.step;
      resume.kind = IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2;
      std::copy_n(result.subject_sha256, 32, resume.subject_sha256);
      resume.decision = IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2;
      resume.input_source = result.input_source;
      resume.input_item_index = result.input_item_index;
      resume.input_source_bytes = {reset.payload.data(), reset.payload.size()};
      PlanBuffers resumedBuffers;
      auto resumed = resumedBuffers.response();
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &resumed) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(resumed.result_epoch == 1);
      CHECK(resumed.result_revision == revision + 1);
      CHECK(resumed.output.length == 0);
      REQUIRE(resumed.action_count == 2);
      CHECK(resumed.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(resumed.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_PENDING_RESET_LOGON_V2);
      CHECK(resumed.actions[1].kind == IRFQ_INFINITE_ACTION_RESET_TRIGGER_V2);
      CHECK(resumed.actions[1].disposition == 1);
      REQUIRE(resumed.native_state.length == baselineState.size());
      CHECK(std::equal(baselineState.begin(), baselineState.begin() + 56, resumed.native_state.data));
      CHECK(std::equal(baselineState.begin() + 64, baselineState.begin() + 80, resumed.native_state.data + 64));
      CHECK(std::equal(baselineState.begin() + 96, baselineState.begin() + 112, resumed.native_state.data + 96));
      CHECK(std::equal(baselineState.begin() + 128, baselineState.end(), resumed.native_state.data + 128));
      CHECK(read64(resumed.native_state.data + 56) == revision + 1);
      CHECK(read64(resumed.native_state.data + 80) == static_cast<std::uint64_t>(reset.request.now_tai_ns));
      CHECK(read64(resumed.native_state.data + 88) == static_cast<std::uint64_t>(reset.request.now_utc_ns));
      CHECK(read64(resumed.native_state.data + 112) == static_cast<std::uint64_t>(reset.request.now_tai_ns));
      CHECK(read64(resumed.native_state.data + 120) == static_cast<std::uint64_t>(reset.request.now_utc_ns));
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  };

  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> exhaustedState{};
  auto *ordinary = detachedSenderSession(config, 7, 30, &exhaustedState);
  REQUIRE(ordinary != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(ordinary) == IRFQ_INFINITE_STATUS_OK_V2);
  write32(exhaustedState.data() + 128, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
  write64(exhaustedState.data() + 132, 0);
  expectDecision(
      "recovery-none-sender-exhausted",
      exhaustedState,
      2,
      2,
      IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2,
      0,
      INT64_C(1700000000123456002));

  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> recoveryState{};
  auto *directSource = detachedResendRecoverySession(config, 2, 5, 2, 7, &recoveryState);
  REQUIRE(directSource != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(directSource) == IRFQ_INFINITE_STATUS_OK_V2);
  expectDecision(
      "detached-direct-recovery",
      recoveryState,
      4,
      3,
      IRFQ_INFINITE_SEQUENCE_VALUE_V2,
      7,
      INT64_C(1700000000123456004));

  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> attachedNone{};
  auto *source = stockLoggedOnSession(config, 7, nullptr, &attachedNone);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> attachedDirect{};
  source = resendRecoverySession(config, 2, 5, 2, 7, &attachedDirect);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  auto attachedBarrier = attachedDirect;
  write64(attachedBarrier.data() + 180, UINT64_C(135));
  write32(attachedBarrier.data() + 288, 10);
  write32(attachedBarrier.data() + 292, 0);
  write64(attachedBarrier.data() + 300, 0);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> detachedNone{};
  source = detachedSenderSession(config, 7, 30, &detachedNone);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> detachedDirect{};
  source = detachedResendRecoverySession(config, 2, 5, 2, 7, &detachedDirect);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);

  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  const auto expectAdmission = [&](const char *connection,
                                   const std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> &baseline,
                                   bool decision) {
    for (const bool senderExhausted : {false, true}) {
      for (const bool targetExhausted : {false, true}) {
        DYNAMIC_SECTION(
            connection << "-sender-" << (senderExhausted ? "E" : "V") << "-target-" << (targetExhausted ? "E" : "V")) {
          auto state = baseline;
          write64(state.data() + 56, 1);
          write32(
              state.data() + 128,
              senderExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2);
          write64(state.data() + 132, senderExhausted ? 0 : 7);
          write32(
              state.data() + 140,
              targetExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2);
          write64(state.data() + 144, targetExhausted ? 0 : 3);
          write64(state.data() + 152, 1);
          write64(state.data() + 160, 0);
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
          InboundCall reset(
              session,
              participantFrame(
                  'A',
                  targetExhausted ? lastLegal : 3,
                  "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
              0x68);
          reset.request.next_original_state
              = senderExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2;
          reset.request.next_original_value = senderExhausted ? 0 : 7;
          reset.request.now_tai_ns = INT64_C(1700000000123457000);
          reset.request.now_utc_ns = INT64_C(1700000000123457000);
          REQUIRE(
              FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                  session,
                  reset.request,
                  reset.request.event_identity_sha256));
          PlanBuffers resetBuffers;
          auto result = resetBuffers.response();
          const auto status = irfq_infinite_prepare_v2(session, &reset.request, &result);
          CHECK(
              status
              == (decision ? IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2
                           : IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2));
          if (!decision) {
            CHECK(result.prepare_id.high == 0);
            CHECK(result.prepare_id.low == 0);
            CHECK(result.native_state.length == 0);
            CHECK(result.output.length == 0);
            CHECK(result.action_count == 0);

            std::array<std::uint8_t, 80> payload{};
            std::fill_n(payload.begin(), 32, std::uint8_t{0x69});
            write64(payload.data() + 32, 1);
            write64(payload.data() + 40, 2);
            std::fill_n(payload.begin() + 48, 32, std::uint8_t{0x6a});
            irfq_infinite_prepare_request_v2 frontier{};
            init(frontier);
            frontier.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
            frontier.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
            frontier.event = IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2;
            frontier.expected_epoch = 1;
            frontier.expected_revision = 1;
            frontier.now_tai_ns = reset.request.now_tai_ns + 1;
            frontier.now_utc_ns = reset.request.now_utc_ns + 1;
            frontier.payload = {payload.data(), payload.size()};
            REQUIRE(
                FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                    session,
                    frontier,
                    frontier.event_identity_sha256));
            PlanBuffers frontierBuffers;
            auto frontierResult = frontierBuffers.response();
            CHECK(irfq_infinite_prepare_v2(session, &frontier, &frontierResult) == IRFQ_INFINITE_STATUS_READY_V2);
            CHECK(frontierResult.prepare_id.low == 1);
            CHECK(frontierResult.output.length == 0);
            CHECK(frontierResult.action_count == 0);
          }
          CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
        }
      }
    }
  };
  expectAdmission("attached-none", attachedNone, false);
  expectAdmission("attached-direct-resend", attachedDirect, false);
  expectAdmission("attached-direct-response-barrier", attachedBarrier, false);
  expectAdmission("detached-none", detachedNone, true);
  expectAdmission("detached-direct", detachedDirect, true);

  const auto logonPhaseState = [&](std::uint32_t phase, bool attachedPhase) {
    auto state = attachedNone;
    write64(state.data() + 56, 1);
    write32(state.data() + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
    write64(state.data() + 132, 8);
    write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
    write64(state.data() + 144, 3);
    write64(state.data() + 152, 1);
    write64(state.data() + 160, 0);
    write32(state.data() + 168, 1);
    write64(state.data() + 172, 5);
    write32(state.data() + 220, 2);
    write32(state.data() + 224, phase);
    std::fill_n(state.data() + 252, 32, std::uint8_t{0x6b});
    write32(state.data() + 296, 0);
    if (phase == 1) {
      write64(state.data() + 228, 2);
      write64(state.data() + 236, 5);
      write64(state.data() + 244, 2);
    } else if (phase == 2) {
      write64(state.data() + 228, 7);
      write64(state.data() + 236, 8);
      write64(state.data() + 244, 7);
    } else if (phase == 3) {
      write64(state.data() + 228, 5);
      write64(state.data() + 236, 7);
      write64(state.data() + 244, 5);
    } else {
      write64(state.data() + 228, 7);
      write64(state.data() + 236, 8);
      write64(state.data() + 244, 7);
    }
    if (attachedPhase) {
      write64(state.data() + 180, phase == 1 ? UINT64_C(1) : UINT64_C(135));
      write32(state.data() + 288, phase == 1 ? 0 : 10);
      write32(state.data() + 292, phase == 2 ? 0 : 1);
      write64(state.data() + 300, phase == 2 ? 0 : read64(state.data() + 244));
    } else {
      write64(state.data() + 180, UINT64_C(1));
      write32(state.data() + 288, 0);
      write32(state.data() + 292, 0);
      write64(state.data() + 300, 0);
    }
    return state;
  };
  const auto expectInvalidLogonRecoveryReset = [&](const char *connection, std::uint32_t phase, bool attachedPhase) {
    DYNAMIC_SECTION(connection << "-phase-" << phase) {
      const auto state = logonPhaseState(phase, attachedPhase);
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
      InboundCall reset(
          session,
          participantFrame('A', 3, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x6c);
      reset.request.next_original_value = 8;
      reset.request.now_tai_ns = INT64_C(1700000000123457000);
      reset.request.now_utc_ns = INT64_C(1700000000123457000);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              reset.request,
              reset.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      CHECK(irfq_infinite_prepare_v2(session, &reset.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      CHECK(result.prepare_id.low == 0);
      CHECK(result.native_state.length == 0);
      CHECK(result.output.length == 0);
      CHECK(result.action_count == 0);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

      auto exhausted = state;
      write32(exhausted.data() + 140, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
      write64(exhausted.data() + 144, 0);
      auto *invalid = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          exhausted.data(),
          exhausted.size(),
          1,
          1,
          0,
          0);
      CHECK(invalid == nullptr);
      if (invalid != nullptr) {
        CHECK(irfq_infinite_destroy_v2(invalid) == IRFQ_INFINITE_STATUS_OK_V2);
      }
    }
  };
  for (std::uint32_t phase = 1; phase <= 4; ++phase) {
    expectInvalidLogonRecoveryReset("attached-logon-789", phase, true);
    expectInvalidLogonRecoveryReset("detached-logon-789", phase, false);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 closes the detached reset-resume sender target and recovery cross product",
    "[infinite][adapter][v2][task2d][reset-resume-matrix]") {
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  const auto fixedConfig = otherwiseValidUnavailableProfile();
  const auto peerConfig = otherwiseValidUnavailableProfile(1, {}, 2, 0, 20, 40);
  struct HeartbeatVariant {
    const std::vector<std::uint8_t> *config;
    std::uint32_t heartbeat;
    bool peerHeartbeat;
  };
  const std::array heartbeatVariants{
      HeartbeatVariant{&fixedConfig, 30, false},
      HeartbeatVariant{&peerConfig, 35, true}};
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> directTemplate{};
  auto *source = detachedResendRecoverySession(fixedConfig, 2, 5, 2, 7, &directTemplate);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);

  for (const auto &heartbeatVariant : heartbeatVariants) {
    const auto &config = *heartbeatVariant.config;
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> noneState{};
    source = detachedSenderSession(config, 7, heartbeatVariant.heartbeat, &noneState);
    REQUIRE(source != nullptr);
    REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
    auto directState = heartbeatVariant.peerHeartbeat ? noneState : directTemplate;
    if (heartbeatVariant.peerHeartbeat) {
      std::copy(directTemplate.begin() + 220, directTemplate.begin() + 284, directState.begin() + 220);
    }

    for (const bool direct : {false, true}) {
      for (const bool senderExhausted : {false, true}) {
        for (const bool targetExhausted : {false, true}) {
          for (const auto decision :
               {IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2,
                IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2}) {
            DYNAMIC_SECTION(
                (heartbeatVariant.peerHeartbeat ? "heartbeat-range-" : "")
                << (direct ? "direct" : "none") << "-sender-" << (senderExhausted ? "E" : "V") << "-target-"
                << (targetExhausted ? "E" : "V") << "-decision-" << decision) {
              auto baseline = direct ? directState : noneState;
              write64(baseline.data() + 56, 1);
              write32(
                  baseline.data() + 128,
                  senderExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2);
              write64(baseline.data() + 132, senderExhausted ? 0 : 7);
              write32(
                  baseline.data() + 140,
                  targetExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2);
              write64(baseline.data() + 144, targetExhausted ? 0 : 3);
              write64(baseline.data() + 152, 1);
              write64(baseline.data() + 160, 0);
              auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
                  config.data(),
                  config.size(),
                  baseline.data(),
                  baseline.size(),
                  1,
                  1,
                  0,
                  0);
              REQUIRE(session != nullptr);
              InboundCall reset(
                  session,
                  participantFrame(
                      'A',
                      targetExhausted ? lastLegal : 3,
                      "98=0\001108=" + std::to_string(heartbeatVariant.heartbeat)
                          + "\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
                  0x6d);
              reset.request.next_original_state
                  = senderExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2;
              reset.request.next_original_value = senderExhausted ? 0 : 7;
              reset.request.now_tai_ns = INT64_C(1700000000123457000);
              reset.request.now_utc_ns = INT64_C(1700000000123457000);
              REQUIRE(
                  FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                      session,
                      reset.request,
                      reset.request.event_identity_sha256));
              PlanBuffers pendingBuffers;
              auto pending = pendingBuffers.response();
              REQUIRE(
                  irfq_infinite_prepare_v2(session, &reset.request, &pending)
                  == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);

              irfq_infinite_resume_request_v2 resume{};
              init(resume);
              resume.prepare_id = pending.prepare_id;
              resume.kind = IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2;
              std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
              resume.decision = decision;
              resume.input_source = pending.input_source;
              resume.input_item_index = pending.input_item_index;
              resume.input_source_bytes = {reset.payload.data(), reset.payload.size()};
              PlanBuffers buffers;
              auto result = buffers.response();
              REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
              CHECK(result.result_epoch == 1);
              CHECK(result.result_revision == 2);
              CHECK(read32(result.native_state.data + 140) == read32(baseline.data() + 140));
              CHECK(read64(result.native_state.data + 144) == read64(baseline.data() + 144));
              CHECK(read64(result.native_state.data + 152) == read64(baseline.data() + 152));
              CHECK(read64(result.native_state.data + 160) == read64(baseline.data() + 160));
              if (heartbeatVariant.peerHeartbeat) {
                CHECK(read32(result.native_state.data + 188) == 0);
              }

              if (decision == IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2) {
                CHECK(result.output.length == 0);
                CHECK(result.output_frame_count == 0);
                REQUIRE(result.action_count == 2);
                CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
                CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_PENDING_RESET_LOGON_V2);
                CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_NONE_V2);
                CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_RESET_TRIGGER_V2);
                CHECK(result.actions[1].disposition == 1);
                for (std::size_t index = 0; index < baseline.size(); ++index) {
                  const bool changed
                      = (index >= 56 && index < 64) || (index >= 80 && index < 96) || (index >= 112 && index < 128);
                  if (!changed) {
                    CHECK(result.native_state.data[index] == baseline[index]);
                  }
                }
              } else {
                REQUIRE(result.action_count == (senderExhausted ? 2 : 3));
                CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
                CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
                CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_RESET_REJECTED_V2);
                CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
                CHECK(result.actions[result.action_count - 1].reason_code == IRFQ_INFINITE_REASON_RESET_REJECTED_V2);
                if (senderExhausted) {
                  CHECK(result.output.length == 0);
                  CHECK(result.output_frame_count == 0);
                  CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
                  CHECK(read64(result.native_state.data + 132) == 0);
                  CHECK(read64(result.native_state.data + 96) == read64(baseline.data() + 96));
                  CHECK(read64(result.native_state.data + 104) == read64(baseline.data() + 104));
                } else {
                  REQUIRE(result.output_frame_count == 1);
                  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
                  CHECK(result.actions[1].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
                  CHECK(result.actions[1].msg_type_length == 1);
                  CHECK(result.actions[1].msg_type[0] == '5');
                  CHECK(result.actions[1].sequence_begin == 7);
                  CHECK(result.actions[1].sequence_end_exclusive == 8);
                  const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
                  FIX::Message logout(output, true);
                  FIX::MsgType msgType;
                  FIX::MsgSeqNum sequence;
                  FIX::Text text;
                  logout.getHeader().getField(msgType);
                  logout.getHeader().getField(sequence);
                  logout.getField(text);
                  CHECK(msgType == FIX::MsgType_Logout);
                  CHECK(sequence == 7);
                  CHECK(text == "Reset rejected");
                  CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_VALUE_V2);
                  CHECK(read64(result.native_state.data + 132) == 8);
                }
                if (direct) {
                  CHECK(read64(result.native_state.data + 180) == UINT64_C(1));
                  CHECK(read32(result.native_state.data + 188) == read32(baseline.data() + 188));
                  CHECK(std::equal(baseline.begin() + 220, baseline.begin() + 284, result.native_state.data + 220));
                  CHECK(read32(result.native_state.data + 288) == 0);
                  CHECK(read32(result.native_state.data + 292) == 0);
                  CHECK(read32(result.native_state.data + 296) == 0);
                  CHECK(read64(result.native_state.data + 300) == 0);
                  CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_NONE_V2);
                } else {
                  CHECK(read64(result.native_state.data + 180) == (UINT64_C(1) | UINT64_C(256)));
                  CHECK(read32(result.native_state.data + 220) == 0);
                  CHECK(read32(result.native_state.data + 292) == 0);
                  CHECK(read64(result.native_state.data + 300) == 0);
                  CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_RESET_REJECTED_V2);
                }
              }
              CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
            }
          }
        }
      }
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 pages an exact 257-row resend through fresh committed plans",
    "[infinite][adapter][v2][task2d][resend][paging][max-rows]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = resendRecoverySession(config, 2, 259, 2, 300);
  REQUIRE(session != nullptr);
  ContinueResendCall firstCall(session, 2, 259, 2);
  firstCall.request.next_original_value = 300;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          firstCall.request,
          firstCall.request.event_identity_sha256));
  PlanBuffers firstPendingBuffers;
  auto firstPending = firstPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &firstCall.request, &firstPending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  CHECK(firstPending.store_range_begin == 2);
  CHECK(firstPending.store_range_end_exclusive == 258);

  std::vector<std::string> bodies;
  std::vector<std::string> frames;
  std::vector<irfq_infinite_store_row_v2> rows;
  bodies.reserve(256);
  frames.reserve(256);
  rows.reserve(256);
  for (std::uint64_t sequence = 2; sequence < 258; ++sequence) {
    bodies.push_back(quoteResponseBody(std::to_string(sequence)));
    frames.push_back(finishFix(
        "35=AJ\00134=" + std::to_string(sequence)
        + "\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + bodies.back()));
    rows.push_back(
        retainedRow(sequence, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", bodies.back(), frames.back()));
  }
  irfq_infinite_resume_request_v2 firstResume{};
  init(firstResume);
  firstResume.prepare_id = firstPending.prepare_id;
  firstResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  firstResume.store_range_begin = 2;
  firstResume.store_range_end_exclusive = 258;
  firstResume.store_rows = rows.data();
  firstResume.store_row_count = rows.size();
  PlanBuffers firstBuffers;
  auto first = firstBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &firstResume, &first) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(first.output_frame_count == 256);
  CHECK(first.action_count == 256);
  CHECK(first.has_more == IRFQ_INFINITE_YES_V2);
  CHECK(read64(first.native_state.data + 244) == 258);
  CHECK(read64(first.native_state.data + 300) == 258);
  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = first.prepare_id;
  apply.result_revision = first.result_revision;
  std::copy_n(first.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 applied{};
  init(applied);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);

  ContinueResendCall secondCall(session, 2, 259, 258, 2);
  secondCall.request.next_original_value = 300;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          secondCall.request,
          secondCall.request.event_identity_sha256));
  PlanBuffers secondPendingBuffers;
  auto secondPending = secondPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &secondCall.request, &secondPending)
      == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  CHECK(secondPending.prepare_id.low != firstPending.prepare_id.low);
  CHECK(secondPending.store_range_begin == 258);
  CHECK(secondPending.store_range_end_exclusive == 259);
  const auto lastBody = quoteResponseBody("258");
  const auto lastFrame = finishFix(
      "35=AJ\00134=258\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + lastBody);
  const auto lastRow = retainedRow(258, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", lastBody, lastFrame);
  irfq_infinite_resume_request_v2 secondResume{};
  init(secondResume);
  secondResume.prepare_id = secondPending.prepare_id;
  secondResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  secondResume.store_range_begin = 258;
  secondResume.store_range_end_exclusive = 259;
  secondResume.store_rows = &lastRow;
  secondResume.store_row_count = 1;
  PlanBuffers secondBuffers;
  auto second = secondBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &secondResume, &second) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(second.output_frame_count == 1);
  CHECK(second.has_more == IRFQ_INFINITE_NO_V2);
  CHECK(read32(second.native_state.data + 220) == 0);
  CHECK(read32(second.native_state.data + 292) == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 drives FNR1 through response handoff stored recovery and final GapFill",
    "[infinite][adapter][v2][task2d][resend][logon-789][fnr1]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = logonRecoverySession(config);
  REQUIRE(session != nullptr);
  const auto apply = [&](const irfq_infinite_prepare_response_v2 &result) {
    irfq_infinite_apply_committed_request_v2 request{};
    init(request);
    request.prepare_id = result.prepare_id;
    request.result_revision = result.result_revision;
    std::copy_n(result.native_state_sha256, 32, request.native_state_sha256);
    irfq_infinite_operation_response_v2 response{};
    init(response);
    return irfq_infinite_apply_committed_v2(session, &request, &response);
  };

  ContinueResendCall prefix(session, 2, 5, 2, 1, 4);
  PlanBuffers prefixBuffers;
  auto prefixResult = prefixBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &prefix.request, &prefixResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(prefixResult.output.length == 0);
  CHECK(prefixResult.action_count == 0);
  CHECK(read32(prefixResult.native_state.data + 224) == 1);
  CHECK(read64(prefixResult.native_state.data + 244) == 4);
  CHECK(read64(prefixResult.native_state.data + 300) == 4);
  CHECK(read64(prefixResult.native_state.data + 132) == 7);
  REQUIRE(apply(prefixResult) == IRFQ_INFINITE_STATUS_OK_V2);

  ContinueResendCall terminal(session, 2, 5, 4, 2, 5);
  PlanBuffers terminalBuffers;
  auto terminalResult = terminalBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &terminal.request, &terminalResult) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(terminalResult.action_count == 1);
  CHECK(terminalResult.actions[0].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(terminalResult.actions[0].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
  CHECK(terminalResult.actions[0].sequence_begin == 7);
  CHECK(terminalResult.actions[0].sequence_end_exclusive == 8);
  const std::string responseFrame(
      reinterpret_cast<const char *>(terminalResult.output.data),
      terminalResult.output.length);
  CHECK(responseFrame.find("\00135=A\001") != std::string::npos);
  CHECK(responseFrame.find("\00134=7\001") != std::string::npos);
  CHECK(read64(terminalResult.native_state.data + 132) == 8);
  CHECK(read64(terminalResult.native_state.data + 180) == UINT64_C(135));
  CHECK(read32(terminalResult.native_state.data + 224) == 2);
  CHECK(read64(terminalResult.native_state.data + 228) == 7);
  CHECK(read64(terminalResult.native_state.data + 236) == 8);
  CHECK(read32(terminalResult.native_state.data + 292) == 0);
  REQUIRE(apply(terminalResult) == IRFQ_INFINITE_STATUS_OK_V2);

  ContinueResendCall handoff(session, 7, 8, 7, 3, 8);
  PlanBuffers storedPendingBuffers;
  auto storedPending = storedPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &handoff.request, &storedPending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  CHECK(storedPending.store_range_begin == 5);
  CHECK(storedPending.store_range_end_exclusive == 7);
  const auto body = quoteResponseBody("LOGON-789");
  const std::vector<std::string> frames{
      finishFix("35=AJ\00134=5\00149=VENUE\00152=20260828-11:59:59.000000\00156=PARTICIPANT\001369=1\001" + body),
      finishFix("35=0\00134=6\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001")};
  std::vector<irfq_infinite_store_row_v2> rows{
      retainedRow(5, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", body, frames[0]),
      retainedRow(6, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, "0", "", frames[1])};
  irfq_infinite_resume_request_v2 storedResume{};
  init(storedResume);
  storedResume.prepare_id = storedPending.prepare_id;
  storedResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  storedResume.store_range_begin = 5;
  storedResume.store_range_end_exclusive = 7;
  storedResume.store_rows = rows.data();
  storedResume.store_row_count = rows.size();
  PlanBuffers storedBuffers;
  auto stored = storedBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &storedResume, &stored) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(stored.output_frame_count == 2);
  CHECK(read32(stored.native_state.data + 224) == 4);
  CHECK(read64(stored.native_state.data + 228) == 7);
  CHECK(read64(stored.native_state.data + 236) == 8);
  CHECK(read64(stored.native_state.data + 244) == 7);
  REQUIRE(apply(stored) == IRFQ_INFINITE_STATUS_OK_V2);

  ContinueResendCall finalCall(session, 7, 8, 7, 4, 8);
  PlanBuffers finalPendingBuffers;
  auto finalPending = finalPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &finalCall.request, &finalPending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  const auto responseRow
      = retainedRow(7, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, "A", canonicalBody(responseFrame), responseFrame);
  irfq_infinite_resume_request_v2 finalResume{};
  init(finalResume);
  finalResume.prepare_id = finalPending.prepare_id;
  finalResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  finalResume.store_range_begin = 7;
  finalResume.store_range_end_exclusive = 8;
  finalResume.store_rows = &responseRow;
  finalResume.store_row_count = 1;
  PlanBuffers finalBuffers;
  auto final = finalBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &finalResume, &final) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(final.action_count == 1);
  CHECK(final.actions[0].output_class == IRFQ_INFINITE_OUTPUT_GAP_FILL_V2);
  CHECK(final.actions[0].sequence_begin == 7);
  CHECK(final.actions[0].sequence_end_exclusive == 8);
  const std::string finalFrame(reinterpret_cast<const char *>(final.output.data), final.output.length);
  CHECK(finalFrame.find("\00134=7\001") != std::string::npos);
  CHECK(finalFrame.find("\00136=8\001") != std::string::npos);
  CHECK(finalFrame.find("\001122=20231114-22:13:20.123456\001") != std::string::npos);
  CHECK(read32(final.native_state.data + 220) == 0);
  CHECK(read32(final.native_state.data + 292) == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects out-of-order LOGON_789 phases handoffs and changed retained rows",
    "[infinite][adapter][v2][task2d][resend][logon-789][negative-logon-789]") {
  const auto config = otherwiseValidUnavailableProfile();
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  auto *session = logonResponseRecoverySession(config, &state);
  REQUIRE(session != nullptr);

  auto outOfOrder = state;
  write32(outOfOrder.data() + 224, 4);
  CHECK(
      FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          outOfOrder.data(),
          outOfOrder.size(),
          1,
          1,
          0,
          0)
      == nullptr);
  auto impossibleFence = state;
  write64(impossibleFence.data() + 180, read64(impossibleFence.data() + 180) | UINT64_C(256));
  write32(impossibleFence.data() + 308, IRFQ_INFINITE_REASON_INTEGRITY_V2);
  auto *fenced = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      impossibleFence.data(),
      impossibleFence.size(),
      1,
      1,
      0,
      0);
  CHECK(fenced == nullptr);
  if (fenced != nullptr) {
    CHECK(irfq_infinite_destroy_v2(fenced) == IRFQ_INFINITE_STATUS_OK_V2);
  }

  ContinueResendCall wrongHandoff(session, 7, 8, 7, 1, 7);
  PlanBuffers wrongBuffers;
  auto wrong = wrongBuffers.response();
  CHECK(irfq_infinite_prepare_v2(session, &wrongHandoff.request, &wrong) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

  const auto body = quoteResponseBody("LOGON-NEGATIVE");
  const std::vector<std::string> frames{
      finishFix("35=AJ\00134=5\00149=VENUE\00152=20260828-11:59:59.000000\00156=PARTICIPANT\001369=1\001" + body),
      finishFix("35=0\00134=6\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001")};
  const std::vector<irfq_infinite_store_row_v2> rows{
      retainedRow(5, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", body, frames[0]),
      retainedRow(6, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, "0", "", frames[1])};
  const auto rejectRows = [&](std::vector<irfq_infinite_store_row_v2> changed) {
    auto *rowSession = logonResponseRecoverySession(config);
    REQUIRE(rowSession != nullptr);
    ContinueResendCall handoff(rowSession, 7, 8, 7, 1, 8);
    PlanBuffers pendingBuffers;
    auto pending = pendingBuffers.response();
    REQUIRE(
        irfq_infinite_prepare_v2(rowSession, &handoff.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
    irfq_infinite_resume_request_v2 resume{};
    init(resume);
    resume.prepare_id = pending.prepare_id;
    resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
    resume.store_range_begin = 5;
    resume.store_range_end_exclusive = 7;
    resume.store_rows = changed.data();
    resume.store_row_count = changed.size();
    PlanBuffers resultBuffers;
    auto result = resultBuffers.response();
    CHECK(irfq_infinite_resume_v2(rowSession, &resume, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    CHECK(irfq_infinite_destroy_v2(rowSession) == IRFQ_INFINITE_STATUS_OK_V2);
  };
  auto reordered = rows;
  std::swap(reordered[0], reordered[1]);
  rejectRows(std::move(reordered));
  const auto changedFrame = participantFrame("AJ", 5, body);
  auto changed = rows;
  changed[0] = retainedRow(5, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", body, changedFrame);
  rejectRows(std::move(changed));
}

TEST_CASE(
    "InfiniteFrameAdapterV2 restores lower-789 only with response and final-GapFill headroom",
    "[infinite][adapter][v2][task2d][resend][logon-789][state-bound]") {
  const auto config = otherwiseValidUnavailableProfile();
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  auto *loggedOn = stockLoggedOnSession(config, lastLegal - 1, nullptr, &state);
  REQUIRE(loggedOn != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(loggedOn) == IRFQ_INFINITE_STATUS_OK_V2);
  write32(state.data() + 168, 1);
  write64(state.data() + 172, lastLegal - 2);
  write64(state.data() + 180, 1);
  write32(state.data() + 220, 2);
  write32(state.data() + 224, 1);
  write64(state.data() + 228, 2);
  write64(state.data() + 236, lastLegal - 2);
  write64(state.data() + 244, 2);
  std::fill_n(state.data() + 252, 32, std::uint8_t{0xd7});
  write32(state.data() + 288, 0);
  write32(state.data() + 292, 1);
  write64(state.data() + 300, 2);

  auto *valid = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      1,
      0,
      0);
  REQUIRE(valid != nullptr);
  CHECK(irfq_infinite_destroy_v2(valid) == IRFQ_INFINITE_STATUS_OK_V2);

  write64(state.data() + 132, lastLegal);
  write64(state.data() + 172, lastLegal - 1);
  write64(state.data() + 236, lastLegal - 1);
  auto *invalid = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      state.data(),
      state.size(),
      1,
      1,
      0,
      0);
  CHECK(invalid == nullptr);
  if (invalid != nullptr) {
    CHECK(irfq_infinite_destroy_v2(invalid) == IRFQ_INFINITE_STATUS_OK_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 enters real lower-789 peer-prefix recovery and reattaches it byte-identically",
    "[infinite][adapter][v2][task2d][resend][logon-789][real-ingress][peer-prefix]") {
  for (const bool peerHeartbeat : {false, true}) {
    DYNAMIC_SECTION("peer-heartbeat=" << peerHeartbeat) {
      const auto config
          = peerHeartbeat ? otherwiseValidUnavailableProfile(1, {}, 2, 0, 20, 40) : otherwiseValidUnavailableProfile();
      auto *session = detachedSenderSession(config, 7, peerHeartbeat ? 35 : 30);
      REQUIRE(session != nullptr);
      InboundCall inbound(
          session,
          participantFrame(
              'A',
              2,
              "98=0\001108=" + std::to_string(peerHeartbeat ? 35 : 30)
                  + "\001789=5\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0xe3);
      inbound.request.expected_revision = 2;
      inbound.request.next_original_value = 2;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers firstBuffers;
      auto first = firstBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &first) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(first.output.length == 0);
      REQUIRE(first.action_count == 1);
      CHECK(first.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(first.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
      CHECK(read64(first.native_state.data + 132) == 7);
      CHECK(read32(first.native_state.data + 168) == 1);
      CHECK(read64(first.native_state.data + 172) == 5);
      CHECK(read64(first.native_state.data + 180) == UINT64_C(1));
      CHECK(read32(first.native_state.data + 188) == (peerHeartbeat ? 35U : 30U));
      CHECK(read32(first.native_state.data + 220) == 2);
      CHECK(read32(first.native_state.data + 224) == 1);
      CHECK(read64(first.native_state.data + 228) == 2);
      CHECK(read64(first.native_state.data + 236) == 5);
      CHECK(read64(first.native_state.data + 244) == 2);
      CHECK(read32(first.native_state.data + 288) == 0);
      CHECK(read32(first.native_state.data + 292) == 1);
      CHECK(read64(first.native_state.data + 300) == 2);
      REQUIRE(std::any_of(first.native_state.data + 252, first.native_state.data + 284, [](std::uint8_t byte) {
        return byte != 0;
      }));
      std::array<std::uint8_t, 32> recoveryDigest{};
      std::copy_n(first.native_state.data + 252, recoveryDigest.size(), recoveryDigest.begin());
      CHECK(std::equal(inbound.payload.begin(), inbound.payload.begin() + 32, recoveryDigest.begin()));

      irfq_infinite_apply_committed_request_v2 apply{};
      init(apply);
      apply.prepare_id = first.prepare_id;
      apply.result_revision = first.result_revision;
      std::copy_n(first.native_state_sha256, 32, apply.native_state_sha256);
      irfq_infinite_operation_response_v2 applied{};
      init(applied);
      REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);

      InboundCall secondInbound(session, inbound.wire, 0xe4);
      secondInbound.request.expected_revision = 3;
      secondInbound.request.next_original_value = 2;
      secondInbound.request.now_tai_ns += 1;
      secondInbound.request.now_utc_ns += 1;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              secondInbound.request,
              secondInbound.request.event_identity_sha256));
      PlanBuffers secondInboundBuffers;
      auto secondInboundResult = secondInboundBuffers.response();
      CHECK(
          irfq_infinite_prepare_v2(session, &secondInbound.request, &secondInboundResult)
          == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

      std::array<std::uint8_t, 32> closePayload{};
      closePayload.fill(0xe5);
      irfq_infinite_prepare_request_v2 close{};
      init(close);
      close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
      close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
      close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
      close.expected_epoch = 1;
      close.expected_revision = 3;
      close.now_tai_ns = inbound.request.now_tai_ns + 2;
      close.now_utc_ns = inbound.request.now_utc_ns + 2;
      close.payload = {closePayload.data(), closePayload.size()};
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              close,
              close.event_identity_sha256));
      PlanBuffers closedBuffers;
      auto closed = closedBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(read64(closed.native_state.data + 180) == UINT64_C(1));
      CHECK(read32(closed.native_state.data + 188) == (peerHeartbeat ? 0U : 30U));
      CHECK(read32(closed.native_state.data + 292) == 0);
      CHECK(read64(closed.native_state.data + 300) == 0);
      CHECK(std::equal(recoveryDigest.begin(), recoveryDigest.end(), closed.native_state.data + 252));
      REQUIRE(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

      auto *resetTrigger = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          closed.native_state.data,
          closed.native_state.length,
          1,
          4,
          0,
          0);
      REQUIRE(resetTrigger != nullptr);
      InboundCall resetLogon(
          resetTrigger,
          participantFrame(
              'A',
              2,
              "98=0\001108=" + std::to_string(peerHeartbeat ? 35 : 30)
                  + "\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0xf8);
      resetLogon.request.expected_revision = 4;
      resetLogon.request.next_original_value = 2;
      resetLogon.request.now_tai_ns = close.now_tai_ns + 1;
      resetLogon.request.now_utc_ns = close.now_utc_ns + 1;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              resetTrigger,
              resetLogon.request,
              resetLogon.request.event_identity_sha256));
      PlanBuffers resetBuffers;
      auto resetResult = resetBuffers.response();
      CHECK(
          irfq_infinite_prepare_v2(resetTrigger, &resetLogon.request, &resetResult)
          == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      CHECK(resetResult.prepare_id.low == 0);
      CHECK(resetResult.native_state.length == 0);
      CHECK(resetResult.output.length == 0);
      CHECK(resetResult.action_count == 0);
      CHECK(irfq_infinite_destroy_v2(resetTrigger) == IRFQ_INFINITE_STATUS_OK_V2);

      session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          closed.native_state.data,
          closed.native_state.length,
          1,
          4,
          0,
          0);
      REQUIRE(session != nullptr);
      // This cache-level oracle deliberately leaves the separately governed TARGET_CAS pending.
      InboundCall reattach(session, inbound.wire, 0xe6);
      reattach.request.expected_revision = 4;
      reattach.request.next_original_value = 2;
      reattach.request.now_tai_ns = close.now_tai_ns + 1;
      reattach.request.now_utc_ns = close.now_utc_ns + 1;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              reattach.request,
              reattach.request.event_identity_sha256));
      PlanBuffers reattachedBuffers;
      auto reattached = reattachedBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &reattach.request, &reattached) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(reattached.output.length == 0);
      REQUIRE(reattached.action_count == 1);
      CHECK(reattached.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(read64(reattached.native_state.data + 132) == 7);
      CHECK(read64(reattached.native_state.data + 180) == UINT64_C(1));
      CHECK(read32(reattached.native_state.data + 188) == (peerHeartbeat ? 35U : 30U));
      CHECK(read32(reattached.native_state.data + 224) == 1);
      CHECK(read64(reattached.native_state.data + 244) == 2);
      CHECK(read32(reattached.native_state.data + 288) == 0);
      CHECK(read32(reattached.native_state.data + 292) == 1);
      CHECK(read64(reattached.native_state.data + 300) == 2);
      CHECK(std::equal(recoveryDigest.begin(), recoveryDigest.end(), reattached.native_state.data + 252));
      auto *roundTrip = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          reattached.native_state.data,
          reattached.native_state.length,
          1,
          5,
          0,
          0);
      REQUIRE(roundTrip != nullptr);
      CHECK(irfq_infinite_destroy_v2(roundTrip) == IRFQ_INFINITE_STATUS_OK_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  const auto peerConfig = otherwiseValidUnavailableProfile(1, {}, 2, 0, 20, 40);
  auto *peerSession = detachedSenderSession(peerConfig, 7, 35);
  REQUIRE(peerSession != nullptr);
  InboundCall peerReconciliation(
      peerSession,
      participantFrame('A', 2, "98=0\001108=35\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0xfa);
  peerReconciliation.request.expected_revision = 2;
  peerReconciliation.request.next_original_value = 6;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          peerSession,
          peerReconciliation.request,
          peerReconciliation.request.event_identity_sha256));
  PlanBuffers peerBuffers;
  auto peerResult = peerBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(peerSession, &peerReconciliation.request, &peerResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read64(peerResult.native_state.data + 180) == (UINT64_C(1) | UINT64_C(256)));
  CHECK(read32(peerResult.native_state.data + 188) == 0);
  auto *peerRoundTrip = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      peerConfig.data(),
      peerConfig.size(),
      peerResult.native_state.data,
      peerResult.native_state.length,
      1,
      3,
      0,
      0);
  REQUIRE(peerRoundTrip != nullptr);
  CHECK(irfq_infinite_destroy_v2(peerRoundTrip) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(peerSession) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 reattaches real lower-789 response-barrier recovery without a second response",
    "[infinite][adapter][v2][task2d][resend][logon-789][real-ingress][response-barrier]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = detachedSenderSession(config, 7);
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      participantFrame('A', 2, "98=0\001108=30\001789=5\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0xe7);
  inbound.request.expected_revision = 2;
  inbound.request.next_original_value = 5;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          inbound.request,
          inbound.request.event_identity_sha256));
  PlanBuffers firstBuffers;
  auto first = firstBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &first) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(first.output_frame_count == 1);
  REQUIRE(first.action_count == 2);
  const std::string responseFrame(reinterpret_cast<const char *>(first.output.data), first.output.length);
  CHECK(responseFrame.find("\00135=A\001") != std::string::npos);
  CHECK(responseFrame.find("\00134=7\001") != std::string::npos);
  CHECK(read64(first.native_state.data + 132) == 8);
  CHECK(read64(first.native_state.data + 180) == UINT64_C(135));
  CHECK(read32(first.native_state.data + 224) == 2);
  CHECK(read64(first.native_state.data + 228) == 7);
  CHECK(read64(first.native_state.data + 236) == 8);
  CHECK(read64(first.native_state.data + 244) == 7);
  CHECK(read32(first.native_state.data + 292) == 0);

  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = first.prepare_id;
  apply.result_revision = first.result_revision;
  std::copy_n(first.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 applied{};
  init(applied);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);

  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0xe8);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = 1;
  close.expected_revision = 3;
  close.now_tai_ns = inbound.request.now_tai_ns + 1;
  close.now_utc_ns = inbound.request.now_utc_ns + 1;
  close.payload = {closePayload.data(), closePayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(session, close, close.event_identity_sha256));
  PlanBuffers closedBuffers;
  auto closed = closedBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

  session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      closed.native_state.data,
      closed.native_state.length,
      1,
      4,
      0,
      0);
  REQUIRE(session != nullptr);
  // This cache-level oracle deliberately leaves the separately governed TARGET_CAS pending.
  InboundCall reattach(session, inbound.wire, 0xe9);
  reattach.request.expected_revision = 4;
  reattach.request.next_original_value = 7;
  reattach.request.now_tai_ns = close.now_tai_ns + 1;
  reattach.request.now_utc_ns = close.now_utc_ns + 1;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          reattach.request,
          reattach.request.event_identity_sha256));
  PlanBuffers reattachedBuffers;
  auto reattached = reattachedBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &reattach.request, &reattached) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(reattached.output.length == 0);
  REQUIRE(reattached.action_count == 1);
  CHECK(reattached.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(read64(reattached.native_state.data + 132) == 8);
  CHECK(read64(reattached.native_state.data + 180) == UINT64_C(135));
  CHECK(read32(reattached.native_state.data + 224) == 2);
  CHECK(read64(reattached.native_state.data + 244) == 7);
  CHECK(read32(reattached.native_state.data + 292) == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

  session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      closed.native_state.data,
      closed.native_state.length,
      1,
      4,
      0,
      0);
  REQUIRE(session != nullptr);
  InboundCall handedOff(session, inbound.wire, 0xf7);
  handedOff.request.expected_revision = 4;
  handedOff.request.next_original_value = 8;
  handedOff.request.now_tai_ns = close.now_tai_ns + 1;
  handedOff.request.now_utc_ns = close.now_utc_ns + 1;
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          handedOff.request,
          handedOff.request.event_identity_sha256));
  PlanBuffers handedOffBuffers;
  auto handedOffResult = handedOffBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &handedOff.request, &handedOffResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(handedOffResult.output.length == 0);
  CHECK(read64(handedOffResult.native_state.data + 132) == 8);
  CHECK(read32(handedOffResult.native_state.data + 224) == 2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 applies the real ordinary-Logon C peer-789 and sender matrix",
    "[infinite][adapter][v2][task2d][resend][logon-789][real-ingress][matrix]") {
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  struct Variant {
    const char *name;
    std::uint64_t sender;
    std::uint64_t original;
    std::uint64_t peer;
    bool peerPresent;
    bool reconciliation;
  };
  const std::array variants{
      Variant{"omitted-equal", 7, 7, 0, false, false},
      Variant{"omitted-not-equal", 7, 6, 0, false, true},
      Variant{"peer-above-sender", 7, 7, 8, true, true},
      Variant{"peer-equal", 7, 7, 7, true, false},
      Variant{"lower-at-sender-bound", lastLegal, lastLegal - 1, lastLegal - 1, true, true}};
  const auto config = otherwiseValidUnavailableProfile();
  for (const auto &variant : variants) {
    DYNAMIC_SECTION(variant.name) {
      auto *session = detachedSenderSession(config, variant.sender);
      REQUIRE(session != nullptr);
      auto fields = std::string("98=0\001108=30\001");
      if (variant.peerPresent) {
        fields += "789=" + std::to_string(variant.peer) + "\001";
      }
      fields += "1137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
      InboundCall inbound(session, participantFrame('A', 2, fields), 0xea);
      inbound.request.expected_revision = 2;
      inbound.request.next_original_value = variant.original;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
      CHECK(output.find(variant.reconciliation ? "\00135=5\001" : "\00135=A\001") != std::string::npos);
      CHECK(read32(result.native_state.data + 220) == 0);
      if (variant.reconciliation) {
        REQUIRE(result.action_count == 3);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
        CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
        CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
        CHECK(result.actions[2].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      } else {
        REQUIRE(result.action_count == 2);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
        CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 intercepts eligible reset before final-target terminalization",
    "[infinite][adapter][v2][task2d][final-target-matrix][reset]") {
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  const auto config = otherwiseValidUnavailableProfile();
  for (const bool direct : {false, true}) {
    DYNAMIC_SECTION("recovery-" << (direct ? "direct" : "none")) {
      std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
      auto *source = direct ? detachedResendRecoverySession(config, 2, 5, 2, 7, &state)
                            : detachedSenderSession(config, 7, 30, &state);
      REQUIRE(source != nullptr);
      REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
      write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
      write64(state.data() + 144, lastLegal);
      write64(state.data() + 152, lastLegal - 1);
      const auto revision = direct ? UINT64_C(4) : UINT64_C(2);
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          state.data(),
          state.size(),
          1,
          revision,
          0,
          0);
      REQUIRE(session != nullptr);
      InboundCall reset(
          session,
          participantFrame(
              'A',
              lastLegal,
              "369=" + std::to_string(lastLegal - 1)
                  + "\00198=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0xbc);
      reset.request.expected_revision = revision;
      reset.request.next_original_value = 7;
      reset.request.now_tai_ns = INT64_C(1700000000123456000) + static_cast<std::int64_t>(revision);
      reset.request.now_utc_ns = reset.request.now_tai_ns;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              reset.request,
              reset.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      CHECK(
          irfq_infinite_prepare_v2(session, &reset.request, &result)
          == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
      CHECK(result.output.length == 0);
      CHECK(result.action_count == 0);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 permits only phase-derived work while lower-789 recovery is active",
    "[infinite][adapter][v2][task2d][resend][logon-789][recovery-gates]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto prepareStatus = [](irfq_infinite_session_v2 *session, const irfq_infinite_prepare_request_v2 &request) {
    PlanBuffers buffers;
    auto result = buffers.response();
    return irfq_infinite_prepare_v2(session, &request, &result);
  };
  const auto expectInvalid = [&](const auto &run) {
    auto *session = logonResponseRecoverySession(config);
    REQUIRE(session != nullptr);
    CHECK(run(session) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  };

  expectInvalid([&](irfq_infinite_session_v2 *session) {
    ApplicationCall application(
        session,
        IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2,
        IRFQ_INFINITE_STAGE_EVENT_V2,
        IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2,
        IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2,
        "AJ",
        quoteResponseBody("RECOVERY-BLOCK"));
    return prepareStatus(session, application.request);
  });
  expectInvalid([&](irfq_infinite_session_v2 *session) {
    static std::array<std::uint8_t, 36> payload{};
    payload.fill(0xeb);
    write32(payload.data() + 32, 0);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_ADMIN_HEARTBEAT_V2;
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
    return prepareStatus(session, request);
  });
  expectInvalid([&](irfq_infinite_session_v2 *session) {
    static std::array<std::uint8_t, 32> payload{};
    payload.fill(0xec);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_TIMER_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_TIMER_TICK_V2;
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
    return prepareStatus(session, request);
  });
  expectInvalid([&](irfq_infinite_session_v2 *session) {
    InboundCall heartbeat(session, participantFrame('0', 2), 0xed);
    return prepareStatus(session, heartbeat.request);
  });
  expectInvalid([&](irfq_infinite_session_v2 *session) {
    InboundCall logon(
        session,
        participantFrame('A', 2, "98=0\001108=30\001789=5\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
        0xee);
    logon.request.next_original_value = 7;
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            logon.request,
            logon.request.event_identity_sha256));
    return prepareStatus(session, logon.request);
  });
  expectInvalid([&](irfq_infinite_session_v2 *session) {
    std::array<std::uint8_t, 32> payload{};
    payload.fill(0xf1);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2;
    request.expected_epoch = 1;
    request.expected_revision = 1;
    request.now_tai_ns = INT64_C(1700000000123456001);
    request.now_utc_ns = INT64_C(1700000000123456001);
    request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
    request.next_original_value = 8;
    request.payload = {payload.data(), payload.size()};
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256));
    return prepareStatus(session, request);
  });
  expectInvalid([&](irfq_infinite_session_v2 *session) {
    const auto held
        = participantFrame('A', 1, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001");
    std::vector<std::uint8_t> payload(128 + held.size());
    std::fill_n(payload.begin(), 32, std::uint8_t{0xf3});
    std::fill_n(payload.begin() + 32, 32, std::uint8_t{0xf4});
    write32(payload.data() + 64, 1);
    write64(payload.data() + 68, 2);
    write64(payload.data() + 76, INT64_C(1700000000123456000));
    write64(payload.data() + 84, INT64_C(1700000000123456000));
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
    return prepareStatus(session, request);
  });

  const auto response = FIX::InfiniteSessionPlanner::logon(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      7,
      2,
      INT64_C(1700000000123456000),
      1);
  auto *exactSession = logonResponseRecoverySession(config);
  REQUIRE(exactSession != nullptr);
  StoredRetransmitCall exact(exactSession, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, response.output, 1, 0xef);
  PlanBuffers exactBuffers;
  auto exactResult = exactBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(exactSession, &exact.request, &exactResult) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(exactResult.action_count == 1);
  CHECK(exactResult.actions[0].output_class == IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2);
  CHECK(exactResult.actions[0].msg_type[0] == 'A');
  CHECK(exactResult.actions[0].sequence_begin == 7);
  CHECK(read32(exactResult.native_state.data + 224) == 2);
  CHECK(irfq_infinite_destroy_v2(exactSession) == IRFQ_INFINITE_STATUS_OK_V2);

  auto *wrongSession = logonResponseRecoverySession(config);
  REQUIRE(wrongSession != nullptr);
  const auto body = quoteResponseBody("WRONG-RECOVERY-ROW");
  StoredRetransmitCall wrong(
      wrongSession,
      IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2,
      finishFix("35=AJ\00134=5\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + body),
      1,
      0xf0);
  PlanBuffers wrongBuffers;
  auto wrongResult = wrongBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(wrongSession, &wrong.request, &wrongResult) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(irfq_infinite_destroy_v2(wrongSession) == IRFQ_INFINITE_STATUS_OK_V2);

  const auto rejectAdmin = [&](const std::string &wire, std::uint8_t subject) {
    auto *session = logonResponseRecoverySession(config);
    REQUIRE(session != nullptr);
    StoredRetransmitCall call(session, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2, wire, 1, subject);
    PlanBuffers buffers;
    auto result = buffers.response();
    CHECK(irfq_infinite_prepare_v2(session, &call.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  };
  rejectAdmin(
      finishFix("35=0\00134=7\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001"),
      0xf5);
  rejectAdmin(
      FIX::InfiniteSessionPlanner::logon("FIXT.1.1", "VENUE", "PARTICIPANT", 30, 6, 2, INT64_C(1700000000123456000), 1)
          .output,
      0xf6);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects every attached recovery inbound before creating a plan",
    "[infinite][adapter][v2][task2d][resend][recovery-inbound-gate]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto expectBlocked
      = [&](const char *name, const auto &createSession, std::uint64_t sender, std::uint64_t target) {
          DYNAMIC_SECTION(name) {
            auto *session = createSession();
            REQUIRE(session != nullptr);
            InboundCall heartbeat(session, participantFrame('0', target), 0x61);
            heartbeat.request.next_original_value = sender;
            REQUIRE(
                FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                    session,
                    heartbeat.request,
                    heartbeat.request.event_identity_sha256));
            PlanBuffers heartbeatBuffers;
            auto heartbeatResult = heartbeatBuffers.response();
            CHECK(
                irfq_infinite_prepare_v2(session, &heartbeat.request, &heartbeatResult)
                == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
            CHECK(heartbeatResult.prepare_id.low == 0);
            CHECK(heartbeatResult.native_state.length == 0);
            CHECK(heartbeatResult.output.length == 0);
            CHECK(heartbeatResult.action_count == 0);

            InboundCall resetLogon(
                session,
                participantFrame(
                    'A',
                    target,
                    "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
                0x62);
            resetLogon.request.next_original_value = sender;
            REQUIRE(
                FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                    session,
                    resetLogon.request,
                    resetLogon.request.event_identity_sha256));
            PlanBuffers resetBuffers;
            auto resetResult = resetBuffers.response();
            CHECK(
                irfq_infinite_prepare_v2(session, &resetLogon.request, &resetResult)
                == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
            CHECK(resetResult.prepare_id.low == 0);
            CHECK(resetResult.native_state.length == 0);
            CHECK(resetResult.output.length == 0);
            CHECK(resetResult.action_count == 0);

            std::array<std::uint8_t, 32> closePayload{};
            closePayload.fill(0x63);
            irfq_infinite_prepare_request_v2 close{};
            init(close);
            close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
            close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
            close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
            close.expected_epoch = 1;
            close.expected_revision = 1;
            close.now_tai_ns = INT64_C(1700000000123456002);
            close.now_utc_ns = INT64_C(1700000000123456002);
            close.payload = {closePayload.data(), closePayload.size()};
            REQUIRE(
                FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                    session,
                    close,
                    close.event_identity_sha256));
            PlanBuffers closeBuffers;
            auto closed = closeBuffers.response();
            REQUIRE(irfq_infinite_prepare_v2(session, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
            CHECK(closed.prepare_id.low == 1);
            CHECK(closed.output.length == 0);
            CHECK(closed.action_count == 0);
            CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
          }
        };

  expectBlocked("direct-stored-range", [&] { return resendRecoverySession(config, 2, 5, 2, 7); }, 7, 2);
  expectBlocked("logon-peer-prefix", [&] { return logonRecoverySession(config); }, 7, 2);
  expectBlocked("logon-response", [&] { return logonResponseRecoverySession(config); }, 8, 2);

  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  auto *base = logonResponseRecoverySession(config, &state);
  REQUIRE(base != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(base) == IRFQ_INFINITE_STATUS_OK_V2);
  write32(state.data() + 224, 3);
  write64(state.data() + 228, 5);
  write64(state.data() + 236, 7);
  write64(state.data() + 244, 5);
  write32(state.data() + 292, 1);
  write64(state.data() + 300, 5);
  const auto storedRangeState = state;
  expectBlocked(
      "logon-stored-range",
      [&] {
        return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
            config.data(),
            config.size(),
            storedRangeState.data(),
            storedRangeState.size(),
            1,
            1,
            0,
            0);
      },
      8,
      2);

  write32(state.data() + 224, 4);
  write64(state.data() + 228, 7);
  write64(state.data() + 236, 8);
  write64(state.data() + 244, 7);
  write64(state.data() + 300, 7);
  const auto finalGapFillState = state;
  expectBlocked(
      "logon-final-gap-fill",
      [&] {
        return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
            config.data(),
            config.size(),
            finalGapFillState.data(),
            finalGapFillState.size(),
            1,
            1,
            0,
            0);
      },
      8,
      2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 reconstructs byte-identical resend after result loss and failed cache apply",
    "[infinite][adapter][v2][task2d][resend][reconstruction][apply-loss]") {
  const auto config = otherwiseValidUnavailableProfile();
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> baseState{};
  auto *failed = resendRecoverySession(config, 2, 4, 2, 6, &baseState);
  REQUIRE(failed != nullptr);
  auto *lost = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      baseState.data(),
      baseState.size(),
      1,
      1,
      0,
      0);
  REQUIRE(lost != nullptr);
  ContinueResendCall failedCall(failed, 2, 4, 2);
  ContinueResendCall lostCall(lost, 2, 4, 2);
  PlanBuffers failedPendingBuffers;
  PlanBuffers lostPendingBuffers;
  auto failedPending = failedPendingBuffers.response();
  auto lostPending = lostPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(failed, &failedCall.request, &failedPending)
      == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  REQUIRE(irfq_infinite_prepare_v2(lost, &lostCall.request, &lostPending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  CHECK(failedPending.prepare_id.high != lostPending.prepare_id.high);
  const std::vector<std::string> bodies{
      quoteResponseBody(std::string(39000, 'A')),
      quoteResponseBody(std::string(39000, 'B'))};
  const std::vector<std::string> frames{
      finishFix("35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + bodies[0]),
      finishFix("35=AJ\00134=3\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + bodies[1])};
  std::vector<irfq_infinite_store_row_v2> rows{
      retainedRow(2, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", bodies[0], frames[0]),
      retainedRow(3, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", bodies[1], frames[1])};
  const auto resume = [&](const irfq_infinite_prepare_response_v2 &pending) {
    irfq_infinite_resume_request_v2 request{};
    init(request);
    request.prepare_id = pending.prepare_id;
    request.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
    request.store_range_begin = 2;
    request.store_range_end_exclusive = 4;
    request.store_rows = rows.data();
    request.store_row_count = rows.size();
    return request;
  };
  auto failedResume = resume(failedPending);
  auto lostResume = resume(lostPending);
  PlanBuffers failedBuffers;
  PlanBuffers lostBuffers;
  auto failedResult = failedBuffers.response();
  auto lostResult = lostBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(failed, &failedResume, &failedResult) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_resume_v2(lost, &lostResume, &lostResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(failedResult.output_frame_count == 1);
  CHECK(read64(failedResult.native_state.data + 244) == 3);
  CHECK(
      std::equal(
          failedResult.output.data,
          failedResult.output.data + failedResult.output.length,
          lostResult.output.data));
  CHECK(
      std::equal(
          failedResult.native_state.data,
          failedResult.native_state.data + failedResult.native_state.length,
          lostResult.native_state.data));
  CHECK(std::memcmp(failedResult.actions, lostResult.actions, sizeof(*failedResult.actions)) == 0);

  irfq_infinite_apply_committed_request_v2 badApply{};
  init(badApply);
  badApply.prepare_id = failedResult.prepare_id;
  badApply.result_revision = failedResult.result_revision;
  std::copy_n(failedResult.native_state_sha256, 32, badApply.native_state_sha256);
  badApply.native_state_sha256[0] ^= 1;
  irfq_infinite_operation_response_v2 badApplyResult{};
  init(badApplyResult);
  CHECK(
      irfq_infinite_apply_committed_v2(failed, &badApply, &badApplyResult) == IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
  REQUIRE(irfq_infinite_destroy_v2(failed) == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(irfq_infinite_destroy_v2(lost) == IRFQ_INFINITE_STATUS_OK_V2);

  auto *restored = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      failedResult.native_state.data,
      failedResult.native_state.length,
      1,
      2,
      0,
      0);
  auto *pristine = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      failedResult.native_state.data,
      failedResult.native_state.length,
      1,
      2,
      0,
      0);
  REQUIRE(restored != nullptr);
  REQUIRE(pristine != nullptr);
  ContinueResendCall restoredCall(restored, 2, 4, 3, 2);
  ContinueResendCall pristineCall(pristine, 2, 4, 3, 2);
  PlanBuffers restoredPendingBuffers;
  PlanBuffers pristinePendingBuffers;
  auto restoredPending = restoredPendingBuffers.response();
  auto pristinePending = pristinePendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(restored, &restoredCall.request, &restoredPending)
      == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  REQUIRE(
      irfq_infinite_prepare_v2(pristine, &pristineCall.request, &pristinePending)
      == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  CHECK(restoredPending.prepare_id.high != failedPending.prepare_id.high);
  auto lastRow = rows[1];
  irfq_infinite_resume_request_v2 restoredResume{};
  init(restoredResume);
  restoredResume.prepare_id = restoredPending.prepare_id;
  restoredResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  restoredResume.store_range_begin = 3;
  restoredResume.store_range_end_exclusive = 4;
  restoredResume.store_rows = &lastRow;
  restoredResume.store_row_count = 1;
  auto pristineResume = restoredResume;
  pristineResume.prepare_id = pristinePending.prepare_id;
  PlanBuffers restoredBuffers;
  PlanBuffers pristineBuffers;
  auto restoredResult = restoredBuffers.response();
  auto pristineResult = pristineBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(restored, &restoredResume, &restoredResult) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(irfq_infinite_resume_v2(pristine, &pristineResume, &pristineResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(restoredResult.output.length == pristineResult.output.length);
  CHECK(
      std::equal(
          restoredResult.output.data,
          restoredResult.output.data + restoredResult.output.length,
          pristineResult.output.data));
  CHECK(
      std::equal(
          restoredResult.native_state.data,
          restoredResult.native_state.data + restoredResult.native_state.length,
          pristineResult.native_state.data));
  CHECK(irfq_infinite_destroy_v2(restored) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(irfq_infinite_destroy_v2(pristine) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 fail-closes changed resend rows ranges directions classes and digests",
    "[infinite][adapter][v2][task2d][resend][negative-matrix]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto body = quoteResponseBody("NEGATIVE");
  const auto outbound
      = finishFix("35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + body);
  const auto inbound = participantFrame("AJ", 2, body);
  for (const std::string variant :
       {"range",
        "sequence",
        "class",
        "direction",
        "frame-digest",
        "body-digest",
        "msg-type",
        "possdup",
        "orig-sending-time",
        "possresend"}) {
    DYNAMIC_SECTION(variant) {
      auto *session = resendRecoverySession(config, 2, 3, 2);
      REQUIRE(session != nullptr);
      ContinueResendCall call(session, 2, 3, 2);
      PlanBuffers pendingBuffers;
      auto pending = pendingBuffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &call.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
      auto supplied = outbound;
      if (variant == "possdup") {
        supplied = finishFix(
            "35=AJ\00134=2\00143=Y\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001"
            "122=20260828-11:59:59.000000\001"
            + body);
      } else if (variant == "orig-sending-time") {
        supplied = finishFix(
            "35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001"
            "122=20260828-11:59:59.000000\001"
            + body);
      } else if (variant == "possresend") {
        supplied = finishFix(
            "35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\00197=Y\001" + body);
      }
      auto row = retainedRow(2, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", body, supplied);
      if (variant == "sequence") {
        row.sequence = 3;
      } else if (variant == "class") {
        row.store_class = IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2;
      } else if (variant == "direction") {
        row = retainedRow(2, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", body, inbound);
      } else if (variant == "frame-digest") {
        row.frame_sha256[0] ^= 1;
      } else if (variant == "body-digest") {
        row.body_sha256[0] ^= 1;
      } else if (variant == "msg-type") {
        row.msg_type[1] = 'K';
      }
      irfq_infinite_resume_request_v2 resume{};
      init(resume);
      resume.prepare_id = pending.prepare_id;
      resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
      resume.store_range_begin = 2;
      resume.store_range_end_exclusive = variant == "range" ? 4 : 3;
      resume.store_rows = &row;
      resume.store_row_count = 1;
      PlanBuffers buffers;
      auto result = buffers.response();
      const auto expected = variant == "frame-digest" || variant == "body-digest"
                                ? IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2
                                : IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == expected);
      auto retry = buffers.response();
      CHECK(irfq_infinite_resume_v2(session, &resume, &retry) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  auto *cursorSession = resendRecoverySession(config, 2, 3, 2);
  REQUIRE(cursorSession != nullptr);
  ContinueResendCall changedCursor(cursorSession, 2, 3, 3);
  PlanBuffers cursorBuffers;
  auto cursorResult = cursorBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(cursorSession, &changedCursor.request, &cursorResult)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(irfq_infinite_destroy_v2(cursorSession) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 accepts 240 application units and rejects 241",
    "[infinite][adapter][v2][task2d][bounds][application-units]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto dictionaries = applicationBlockDictionaries();
  const auto prepare = [&](std::uint32_t count) {
    auto *session = stockLoggedOnSession(config, 2, &dictionaries);
    REQUIRE(session != nullptr);
    std::vector<std::uint8_t> payload(32, 0xd4);
    append32(payload, 2);
    append32(payload, 1);
    append32(payload, count);
    append32(payload, 0);
    append32(payload, count);
    for (std::uint32_t index = 0; index < count; ++index) {
      if (index + 1 == count) {
        appendApplicationUnit(payload, index, "UAH0", "644=REQ\00120003=OUT\00120006=1\001");
      } else {
        appendApplicationUnit(payload, index, "R", "58=Q\001131=REQ\001");
      }
    }
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2;
    request.application_block_mode = IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2;
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
    const auto status = irfq_infinite_prepare_v2(session, &request, &result);
    if (count == IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2) {
      CHECK(status == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(result.output_frame_count == IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2);
      CHECK(result.action_count == IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2);
    } else {
      CHECK(status == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  };
  prepare(IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2);
  prepare(IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2 + 1);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 closes output frame payload and action-capacity exact and one-over bounds",
    "[infinite][adapter][v2][task2d][bounds][byte-limits]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto dictionaries = applicationBlockDictionaries();
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
  const auto wireFor = [](std::size_t filler) {
    return finishFix(
        "35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001693="
        + std::string(filler, 'X') + "\001694=1\001");
  };
  std::size_t filler = 65000;
  auto wire = wireFor(filler);
  auto rendered = FIX::InfiniteSessionPlanner::storedFrame(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      6,
      2,
      INT64_C(1700000000123456001),
      1,
      wire,
      dictionaries,
      profile);
  REQUIRE(rendered.output.size() < IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2);
  filler += IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 - rendered.output.size();
  wire = wireFor(filler);
  rendered = FIX::InfiniteSessionPlanner::storedFrame(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      6,
      2,
      INT64_C(1700000000123456001),
      1,
      wire,
      dictionaries,
      profile);
  REQUIRE(rendered.output.size() == IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2);
  REQUIRE(wire.size() <= IRFQ_INFINITE_MAX_FRAME_BYTES_V2);
  auto *session = stockLoggedOnSession(config, 6);
  REQUIRE(session != nullptr);
  StoredRetransmitCall exactOutput(session, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, wire);
  PlanBuffers exactBuffers;
  auto exact = exactBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &exactOutput.request, &exact) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(exact.output.length == IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2);
  CHECK(exact.action_count == 1);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

  session = stockLoggedOnSession(config, 6);
  REQUIRE(session != nullptr);
  StoredRetransmitCall overOutput(session, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, wireFor(filler + 1));
  auto overOutputResult = exactBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(session, &overOutput.request, &overOutputResult)
      == IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

  std::size_t frameFiller = 65000;
  auto exactFrame = wireFor(frameFiller);
  frameFiller += IRFQ_INFINITE_MAX_FRAME_BYTES_V2 - exactFrame.size();
  exactFrame = wireFor(frameFiller);
  REQUIRE(exactFrame.size() == IRFQ_INFINITE_MAX_FRAME_BYTES_V2);
  session = stockLoggedOnSession(config, 6);
  REQUIRE(session != nullptr);
  StoredRetransmitCall exactFrameCall(session, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, exactFrame);
  auto exactFrameResult = exactBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(session, &exactFrameCall.request, &exactFrameResult)
      == IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  const auto overFrame = wireFor(frameFiller + 1);
  REQUIRE(overFrame.size() == IRFQ_INFINITE_MAX_FRAME_BYTES_V2 + 1);
  session = stockLoggedOnSession(config, 6);
  REQUIRE(session != nullptr);
  StoredRetransmitCall overFrameCall(session, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, overFrame);
  auto overFrameResult = exactBuffers.response();
  CHECK(
      irfq_infinite_prepare_v2(session, &overFrameCall.request, &overFrameResult)
      == IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

  session = stockLoggedOnSession(config, 6);
  REQUIRE(session != nullptr);
  StoredRetransmitCall capacityCall(
      session,
      IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2,
      finishFix(
          "35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001"
          "693=CAPACITY\001694=1\001"));
  std::array<irfq_infinite_declarative_action_v2, IRFQ_INFINITE_MAX_ACTIONS_V2 + 1> tooManyActions{};
  auto capacityResult = exactBuffers.response();
  capacityResult.actions = tooManyActions.data();
  capacityResult.action_capacity = tooManyActions.size();
  CHECK(
      irfq_infinite_prepare_v2(session, &capacityCall.request, &capacityResult)
      == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  capacityResult = exactBuffers.response();
  CHECK(irfq_infinite_prepare_v2(session, &capacityCall.request, &capacityResult) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(capacityResult.action_capacity == IRFQ_INFINITE_MAX_ACTIONS_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

  for (const std::uint64_t length :
       {IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2, IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2 + 1}) {
    session = stockLoggedOnSession(config, 6);
    REQUIRE(session != nullptr);
    std::vector<std::uint8_t> payload(length, 0xd5);
    write32(payload.data() + 32, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2);
    write32(payload.data() + 36, 0);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_STORED_FRAME_RETRANSMIT_V2;
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
    auto result = exactBuffers.response();
    const auto expected = length == IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2
                              ? IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2
                              : IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2;
    CHECK(irfq_infinite_prepare_v2(session, &request, &result) == expected);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 closes direct continuation at exactly 64 KiB and rematerializes after short output",
    "[infinite][adapter][v2][task2d][bounds][byte-limits][continue-resend]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto dictionaries = applicationBlockDictionaries();
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
  const auto wireFor = [](std::size_t filler) {
    return finishFix(
        "35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001693="
        + std::string(filler, 'X') + "\001694=1\001");
  };
  std::size_t filler = 65000;
  auto wire = wireFor(filler);
  auto rendered = FIX::InfiniteSessionPlanner::storedFrame(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      6,
      2,
      INT64_C(1700000000123456001),
      1,
      wire,
      dictionaries,
      profile);
  filler += IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 - rendered.output.size();
  wire = wireFor(filler);
  rendered = FIX::InfiniteSessionPlanner::storedFrame(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      6,
      2,
      INT64_C(1700000000123456001),
      1,
      wire,
      dictionaries,
      profile);
  REQUIRE(rendered.output.size() == IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2);
  const auto body = canonicalBody(wire);
  const auto exactRow = retainedRow(2, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", body, wire);

  auto *exactSession = resendRecoverySession(config, 2, 3, 2);
  REQUIRE(exactSession != nullptr);
  ContinueResendCall exactCall(exactSession, 2, 3, 2);
  PlanBuffers exactPendingBuffers;
  auto exactPending = exactPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(exactSession, &exactCall.request, &exactPending)
      == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  irfq_infinite_resume_request_v2 exactResume{};
  init(exactResume);
  exactResume.prepare_id = exactPending.prepare_id;
  exactResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  exactResume.store_range_begin = 2;
  exactResume.store_range_end_exclusive = 3;
  exactResume.store_rows = &exactRow;
  exactResume.store_row_count = 1;
  PlanBuffers exactBuffers;
  auto exact = exactBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(exactSession, &exactResume, &exact) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(exact.output.length == IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2);
  CHECK(read32(exact.native_state.data + 220) == 0);
  CHECK(read32(exact.native_state.data + 292) == 0);
  CHECK(irfq_infinite_destroy_v2(exactSession) == IRFQ_INFINITE_STATUS_OK_V2);

  auto *overSession = resendRecoverySession(config, 2, 3, 2);
  REQUIRE(overSession != nullptr);
  ContinueResendCall overCall(overSession, 2, 3, 2);
  PlanBuffers overPendingBuffers;
  auto overPending = overPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(overSession, &overCall.request, &overPending)
      == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  const auto overWire = wireFor(filler + 1);
  const auto overRow
      = retainedRow(2, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", canonicalBody(overWire), overWire);
  irfq_infinite_resume_request_v2 overResume{};
  init(overResume);
  overResume.prepare_id = overPending.prepare_id;
  overResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  overResume.store_range_begin = 2;
  overResume.store_range_end_exclusive = 3;
  overResume.store_rows = &overRow;
  overResume.store_row_count = 1;
  PlanBuffers overBuffers;
  auto over = overBuffers.response();
  CHECK(irfq_infinite_resume_v2(overSession, &overResume, &over) == IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
  CHECK(irfq_infinite_destroy_v2(overSession) == IRFQ_INFINITE_STATUS_OK_V2);

  auto *shortSession = resendRecoverySession(config, 2, 3, 2);
  REQUIRE(shortSession != nullptr);
  ContinueResendCall shortCall(shortSession, 2, 3, 2);
  PlanBuffers shortPendingBuffers;
  auto shortPending = shortPendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(shortSession, &shortCall.request, &shortPending)
      == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  irfq_infinite_resume_request_v2 shortResume{};
  init(shortResume);
  shortResume.prepare_id = shortPending.prepare_id;
  shortResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  shortResume.store_range_begin = 2;
  shortResume.store_range_end_exclusive = 3;
  shortResume.store_rows = &exactRow;
  shortResume.store_row_count = 1;
  PlanBuffers shortBuffers;
  auto shortOutput = shortBuffers.response();
  shortOutput.output.capacity = IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 - 1;
  REQUIRE(irfq_infinite_resume_v2(shortSession, &shortResume, &shortOutput) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  CHECK(shortOutput.required_output_capacity == IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2);
  CHECK(shortOutput.native_state.length == 0);
  irfq_infinite_resume_request_v2 rematerialize{};
  init(rematerialize);
  rematerialize.prepare_id = shortOutput.prepare_id;
  rematerialize.step = shortOutput.step;
  rematerialize.kind = IRFQ_INFINITE_RESUME_OUTPUT_V2;
  PlanBuffers rematerializedBuffers;
  auto rematerialized = rematerializedBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(shortSession, &rematerialize, &rematerialized) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(rematerialized.output.length == exact.output.length);
  CHECK(std::equal(exact.output.data, exact.output.data + exact.output.length, rematerialized.output.data));
  CHECK(
      std::equal(
          exact.native_state.data,
          exact.native_state.data + exact.native_state.length,
          rematerialized.native_state.data));
  CHECK(read32(rematerialized.native_state.data + 220) == 0);
  CHECK(read32(rematerialized.native_state.data + 292) == 0);
  CHECK(irfq_infinite_destroy_v2(shortSession) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 accepts an exact 16 MiB 256-row retained range",
    "[infinite][adapter][v2][task2d][bounds][store-bytes]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = resendRecoverySession(config, 2, 258, 2, 300);
  REQUIRE(session != nullptr);
  ContinueResendCall call(session, 2, 258, 2, 1, 300);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &call.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  std::vector<std::string> bodies;
  std::vector<std::string> frames;
  std::vector<irfq_infinite_store_row_v2> rows;
  bodies.reserve(IRFQ_INFINITE_MAX_STORE_ITEMS_V2);
  frames.reserve(IRFQ_INFINITE_MAX_STORE_ITEMS_V2);
  rows.reserve(IRFQ_INFINITE_MAX_STORE_ITEMS_V2);
  for (std::uint64_t sequence = 2; sequence < 258; ++sequence) {
    std::size_t filler = 65000;
    auto body = quoteResponseBody(std::string(filler, 'S'));
    auto retained = finishFix(
        "35=AJ\00134=" + std::to_string(sequence)
        + "\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + body);
    filler += IRFQ_INFINITE_MAX_FRAME_BYTES_V2 - retained.size();
    body = quoteResponseBody(std::string(filler, 'S'));
    retained = finishFix(
        "35=AJ\00134=" + std::to_string(sequence)
        + "\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + body);
    REQUIRE(retained.size() == IRFQ_INFINITE_MAX_FRAME_BYTES_V2);
    bodies.push_back(std::move(body));
    frames.push_back(std::move(retained));
    rows.push_back(
        retainedRow(sequence, IRFQ_INFINITE_STORE_CLASS_REVOCABLE_SUPPRESSED_V2, "AJ", bodies.back(), frames.back()));
  }
  const auto totalBytes
      = std::accumulate(rows.begin(), rows.end(), std::uint64_t{0}, [](std::uint64_t sum, const auto &row) {
          return sum + row.frame_length;
        });
  REQUIRE(totalBytes == IRFQ_INFINITE_MAX_STORE_RANGE_BYTES_V2);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  resume.store_range_begin = 2;
  resume.store_range_end_exclusive = 258;
  resume.store_rows = rows.data();
  resume.store_row_count = rows.size();
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(result.output_frame_count == 1);
  CHECK(result.action_count == 1);
  CHECK(result.actions[0].output_class == IRFQ_INFINITE_OUTPUT_GAP_FILL_V2);
  CHECK(result.actions[0].sequence_begin == 2);
  CHECK(result.actions[0].sequence_end_exclusive == 258);
  CHECK(read32(result.native_state.data + 220) == 0);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);

  session = resendRecoverySession(config, 2, 259, 2, 300);
  REQUIRE(session != nullptr);
  ContinueResendCall overCall(session, 2, 259, 2, 1, 300);
  auto overPending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &overCall.request, &overPending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  std::vector<irfq_infinite_store_row_v2> tooManyRows(rows);
  tooManyRows.push_back(rows.back());
  irfq_infinite_resume_request_v2 overResume{};
  init(overResume);
  overResume.prepare_id = overPending.prepare_id;
  overResume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  overResume.store_range_begin = 2;
  overResume.store_range_end_exclusive = 258;
  overResume.store_rows = tooManyRows.data();
  overResume.store_row_count = tooManyRows.size();
  auto overResult = buffers.response();
  CHECK(irfq_infinite_resume_v2(session, &overResume, &overResult) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 accepts 15,728,640 application wire bytes and rejects one over",
    "[infinite][adapter][v2][task2d][bounds][application-wire]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto dictionaries = applicationBlockDictionaries();
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
  const auto tune = [&](std::uint64_t sequence, const std::string &msgType) {
    const auto bodyFor = [&](std::size_t filler) {
      return msgType == "R" ? "58=" + std::string(filler, 'W') + "\001131=REQ\001"
                            : "644=REQ\00120003=" + std::string(filler, 'W') + "\00120006=1\001";
    };
    std::size_t filler = 65000;
    auto body = bodyFor(filler);
    auto plan = FIX::InfiniteSessionPlanner::application(
        "FIXT.1.1",
        "VENUE",
        "PARTICIPANT",
        30,
        sequence,
        2,
        INT64_C(1700000000123456001),
        msgType,
        body,
        FIX::InfiniteApplicationRenderMode::Original,
        1,
        dictionaries,
        profile);
    REQUIRE(plan.maximumWireSize < IRFQ_INFINITE_MAX_FRAME_BYTES_V2);
    filler += IRFQ_INFINITE_MAX_FRAME_BYTES_V2 - plan.maximumWireSize;
    body = bodyFor(filler);
    plan = FIX::InfiniteSessionPlanner::application(
        "FIXT.1.1",
        "VENUE",
        "PARTICIPANT",
        30,
        sequence,
        2,
        INT64_C(1700000000123456001),
        msgType,
        body,
        FIX::InfiniteApplicationRenderMode::Original,
        1,
        dictionaries,
        profile);
    REQUIRE(plan.maximumWireSize == IRFQ_INFINITE_MAX_FRAME_BYTES_V2);
    return body;
  };
  const auto oneDigit = tune(2, "R");
  const auto twoDigits = tune(10, "R");
  const auto threeDigits = tune(100, "R");
  const auto completion = tune(241, "UAH0");
  std::vector<std::string> bodies;
  bodies.reserve(IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2);
  for (std::uint32_t index = 0; index < IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2; ++index) {
    const auto sequence = 2 + index;
    bodies.push_back(
        index + 1 == IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2 ? completion
        : sequence < 10                                     ? oneDigit
        : sequence < 100                                    ? twoDigits
                                                            : threeDigits);
  }
  const auto prepare = [&](const std::vector<std::string> &selectedBodies) {
    auto *session = stockLoggedOnSession(config, 2, &dictionaries);
    REQUIRE(session != nullptr);
    std::vector<std::uint8_t> payload(32, 0xd6);
    append32(payload, 2);
    append32(payload, 1);
    append32(payload, IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2);
    append32(payload, 0);
    append32(payload, IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2);
    for (std::uint32_t index = 0; index < selectedBodies.size(); ++index) {
      appendApplicationUnit(payload, index, index + 1 == selectedBodies.size() ? "UAH0" : "R", selectedBodies[index]);
    }
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2;
    request.application_block_mode = IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2;
    request.expected_epoch = 1;
    request.expected_revision = 1;
    request.now_tai_ns = INT64_C(1700000000123456001);
    request.now_utc_ns = INT64_C(1700000000123456001);
    request.payload = {payload.data(), payload.size()};
    REQUIRE(payload.size() <= IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2);
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
  REQUIRE(
      static_cast<std::uint64_t>(IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2) * IRFQ_INFINITE_MAX_FRAME_BYTES_V2
      == IRFQ_INFINITE_MAX_APPLICATION_WIRE_BYTES_V2);
  CHECK(prepare(bodies) == IRFQ_INFINITE_STATUS_READY_V2);
  auto over = bodies;
  const auto split = over[0].find("\001131=");
  REQUIRE(split != std::string::npos);
  over[0].insert(split, 1, 'W');
  CHECK(prepare(over) == IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 moves one recovery handle sequentially across prepare resume and apply threads",
    "[infinite][adapter][v2][task2d][resend][thread-mobility]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = resendRecoverySession(config, 2, 3, 2);
  REQUIRE(session != nullptr);
  ContinueResendCall call(session, 2, 3, 2);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &call.request, &pending) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  const auto body = quoteResponseBody("THREAD");
  const auto frame
      = finishFix("35=AJ\00134=2\00149=VENUE\00152=20260828-12:00:00.000000\00156=PARTICIPANT\001369=1\001" + body);
  const auto row = retainedRow(2, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2, "AJ", body, frame);
  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
  resume.store_range_begin = 2;
  resume.store_range_end_exclusive = 3;
  resume.store_rows = &row;
  resume.store_row_count = 1;
  PlanBuffers resultBuffers;
  auto result = resultBuffers.response();
  irfq_infinite_status_v2 resumeStatus = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2;
  std::thread resumeOwner([&] { resumeStatus = irfq_infinite_resume_v2(session, &resume, &result); });
  resumeOwner.join();
  REQUIRE(resumeStatus == IRFQ_INFINITE_STATUS_READY_V2);
  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = result.prepare_id;
  apply.result_revision = result.result_revision;
  std::copy_n(result.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 applied{};
  init(applied);
  irfq_infinite_status_v2 applyStatus = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2;
  std::thread applyOwner([&] { applyStatus = irfq_infinite_apply_committed_v2(session, &apply, &applied); });
  applyOwner.join();
  CHECK(applyStatus == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(applied.cache_revision == 2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 renders authorized original replay and read application batches",
    "[infinite][adapter][v2][task2c][application]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto body = quoteResponseBody("TASK-2C");
  const auto expectedOriginal
      = finishFix("35=AJ\00134=2\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\001369=1\001" + body);
  const auto expectedReplay = finishFix(
      "35=AJ\00134=2\00149=VENUE\00152=20231114-22:13:20.123456\00156=PARTICIPANT\00197=Y\001369=1\001" + body);
  const std::array variants{
      std::tuple{
          IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2,
          IRFQ_INFINITE_STAGE_EVENT_V2,
          IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2,
          IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2,
          IRFQ_INFINITE_OUTPUT_ORIGINAL_APPLICATION_V2,
          expectedOriginal},
      std::tuple{
          IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2,
          IRFQ_INFINITE_STAGE_EVENT_V2,
          IRFQ_INFINITE_EVENT_APPLICATION_REPLAY_BEGIN_V2,
          IRFQ_INFINITE_APPLICATION_BLOCK_SEMANTIC_REPLAY_V2,
          IRFQ_INFINITE_OUTPUT_SEMANTIC_REPLAY_V2,
          expectedReplay},
      std::tuple{
          IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2,
          IRFQ_INFINITE_STAGE_READ_R2_V2,
          IRFQ_INFINITE_EVENT_READ_RESULT_BEGIN_V2,
          IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2,
          IRFQ_INFINITE_OUTPUT_ORIGINAL_APPLICATION_V2,
          expectedOriginal}};
  for (const auto &[kind, stage, event, mode, outputClass, expected] : variants) {
    INFO(event);
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> baseState{};
    auto *session = stockLoggedOnSession(config, 2, nullptr, &baseState);
    REQUIRE(session != nullptr);
    ApplicationCall call(session, kind, stage, event, mode, "AJ", body);
    PlanBuffers buffers;
    auto result = buffers.response();
    REQUIRE(irfq_infinite_prepare_v2(session, &call.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
    CHECK(result.output.length == expected.size());
    CHECK(std::string(reinterpret_cast<const char *>(result.output.data), result.output.length) == expected);
    REQUIRE(result.action_count == 1);
    CHECK(result.output_frame_count == 1);
    CHECK(result.has_more == IRFQ_INFINITE_NO_V2);
    CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
    CHECK(result.actions[0].output_class == outputClass);
    CHECK(result.actions[0].sequence_begin == 2);
    CHECK(result.actions[0].sequence_end_exclusive == 3);
    irfq_infinite_declarative_action_v2 expectedAction{};
    expectedAction.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
    expectedAction.output_class = outputClass;
    expectedAction.msg_type_length = 2;
    expectedAction.msg_type[0] = 'A';
    expectedAction.msg_type[1] = 'J';
    expectedAction.sequence_begin = 2;
    expectedAction.sequence_end_exclusive = 3;
    expectedAction.output_length = expected.size();
    const auto expectedDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
        reinterpret_cast<const std::uint8_t *>(expected.data()),
        expected.size());
    std::copy(expectedDigest.begin(), expectedDigest.end(), expectedAction.binding_sha256);
    CHECK(std::memcmp(&result.actions[0], &expectedAction, sizeof(expectedAction)) == 0);
    CHECK(read64(result.native_state.data + 132) == 3);
    CHECK(read32(result.native_state.data + 292) == 0);
    CHECK(read32(result.native_state.data + 296) == 0);
    CHECK(read64(result.native_state.data + 300) == 0);
    auto expectedState = baseState;
    write64(expectedState.data() + 56, 2);
    write64(expectedState.data() + 80, call.request.now_tai_ns);
    write64(expectedState.data() + 88, call.request.now_utc_ns);
    write64(expectedState.data() + 96, call.request.now_tai_ns);
    write64(expectedState.data() + 104, call.request.now_utc_ns);
    write64(expectedState.data() + 132, 3);
    write32(expectedState.data() + 292, 0);
    write32(expectedState.data() + 296, 0);
    write64(expectedState.data() + 300, 0);
    REQUIRE(result.native_state.length == expectedState.size());
    CHECK(std::equal(expectedState.begin(), expectedState.end(), result.native_state.data));
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 releases one typed queued admin row through allow or reject",
    "[infinite][adapter][v2][task2c][queued]") {
  for (const std::string variant : {
           "allow",
           "delayed-allow",
           "weekly-delayed-allow",
           "logout-allow",
           "reject",
           "logout-reject",
           "reject-disposition",
           "reject-output-disposition",
           "bad-store-sequence",
           "bad-store-class",
           "bad-store-direction",
           "bad-store-dictionary",
           "bad-store-type",
           "bad-frame-digest",
           "bad-body-digest",
           "bad-store-reserved",
           "bad-store-tail",
           "bad-store-alias",
           "bad-store-count",
           "bad-store-disposition",
       }) {
    INFO(variant);
    const auto logout = variant.find("logout") != std::string::npos;
    const auto delayed = variant.find("delayed") != std::string::npos;
    const auto weekly = variant.find("weekly") != std::string::npos;
    const auto config = weekly ? otherwiseValidUnavailableProfile(2, {2, 72000, 3, 3600, 2, 72000, 3, 3600})
                               : otherwiseValidUnavailableProfile();
    const auto decision = variant.find("reject") != std::string::npos ? IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2
                                                                      : IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2;
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> activeWeeklyState{};
    auto *session = stockLoggedOnSession(config, 2, nullptr, weekly ? &activeWeeklyState : nullptr);
    REQUIRE(session != nullptr);
    if (weekly) {
      REQUIRE(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      write64(activeWeeklyState.data() + 64, INT64_C(1699992000000000000));
      write64(activeWeeklyState.data() + 72, INT64_C(1699992000000000000));
      session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          activeWeeklyState.data(),
          activeWeeklyState.size(),
          1,
          1,
          0,
          0);
      REQUIRE(session != nullptr);
    }
    InboundCall future(session, participantFrame('0', 3), 0xa1);
    PlanBuffers queueBuffers;
    auto queued = queueBuffers.response();
    REQUIRE(irfq_infinite_prepare_v2(session, &future.request, &queued) == IRFQ_INFINITE_STATUS_READY_V2);
    REQUIRE(read32(queued.native_state.data + 292) == 2);
    CHECK(read64(queued.native_state.data + 300) == 2);
    irfq_infinite_apply_committed_request_v2 apply{};
    init(apply);
    apply.prepare_id = queued.prepare_id;
    apply.result_revision = queued.result_revision;
    std::copy_n(queued.native_state_sha256, 32, apply.native_state_sha256);
    irfq_infinite_operation_response_v2 applied{};
    init(applied);
    REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);

    std::vector<std::uint8_t> payload(56, 0xa2);
    write64(payload.data() + 32, 2);
    write64(payload.data() + 40, 4);
    write64(payload.data() + 48, 2);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_CONTINUE_QUEUED_INBOUND_V2;
    request.expected_epoch = 1;
    request.expected_revision = 2;
    const auto continuationNow = weekly    ? INT64_C(1700010000000000001)
                                 : delayed ? INT64_C(1700000121123456002)
                                           : INT64_C(1700000000123456002);
    request.now_tai_ns = continuationNow;
    request.now_utc_ns = continuationNow;
    request.payload = {payload.data(), payload.size()};
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            request,
            request.event_identity_sha256));
    PlanBuffers storeBuffers;
    auto store = storeBuffers.response();
    REQUIRE(irfq_infinite_prepare_v2(session, &request, &store) == IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
    CHECK(store.store_range_begin == 2);
    CHECK(store.store_range_end_exclusive == 3);

    auto wire = participantFrame(logout ? '5' : '0', 2);
    if (variant == "bad-store-direction") {
      wire = finishFix("35=0\00149=VENUE\00156=PARTICIPANT\00134=2\00152=20231114-22:13:20.123456\001369=1\001");
    } else if (variant == "bad-store-dictionary") {
      wire = participantFrame('1', 2);
    }
    irfq_infinite_store_row_v2 row{};
    row.sequence = 2;
    row.store_class = IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2;
    row.msg_type_length = 1;
    row.msg_type[0] = variant == "bad-store-dictionary" ? '1' : logout ? '5' : '0';
    row.frame_length = wire.size();
    row.frame = slice(wire);
    const auto frameDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
        reinterpret_cast<const std::uint8_t *>(wire.data()),
        wire.size());
    const auto bodyDigest = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(nullptr, 0);
    std::copy(frameDigest.begin(), frameDigest.end(), row.frame_sha256);
    std::copy(bodyDigest.begin(), bodyDigest.end(), row.body_sha256);
    if (variant == "bad-store-sequence") {
      row.sequence = 3;
    } else if (variant == "bad-store-class") {
      row.store_class = IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2;
    } else if (variant == "bad-store-type") {
      row.msg_type[0] = '1';
    } else if (variant == "bad-frame-digest") {
      row.frame_sha256[0] ^= 1;
    } else if (variant == "bad-body-digest") {
      row.body_sha256[0] ^= 1;
    } else if (variant == "bad-store-reserved") {
      row.reserved = 1;
    } else if (variant == "bad-store-tail") {
      row.msg_type[7] = 1;
    } else if (variant == "bad-store-alias") {
      REQUIRE(wire.size() <= sizeof(row));
      row.frame = {reinterpret_cast<const std::uint8_t *>(&row), wire.size()};
    }
    irfq_infinite_resume_request_v2 resume{};
    init(resume);
    resume.prepare_id = store.prepare_id;
    resume.step = store.step;
    resume.kind = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
    resume.store_range_begin = 2;
    resume.store_range_end_exclusive = 3;
    resume.store_rows = &row;
    resume.store_row_count = variant == "bad-store-count" ? 2 : 1;
    const std::string forbiddenDisposition = "GID.NOT-APPLICATION";
    if (variant == "bad-store-disposition") {
      resume.gateway_inbound_disposition_id = slice(forbiddenDisposition);
    }
    PlanBuffers decisionBuffers;
    auto pending = decisionBuffers.response();
    const auto storeStatus = irfq_infinite_resume_v2(session, &resume, &pending);
    const auto invalidStore
        = variant.rfind("bad-store-", 0) == 0 || variant == "bad-frame-digest" || variant == "bad-body-digest";
    if (invalidStore) {
      const auto expectedStatus = variant == "bad-frame-digest" || variant == "bad-body-digest"
                                      ? IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2
                                      : IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
      REQUIRE(storeStatus == expectedStatus);
      CHECK(irfq_infinite_resume_v2(session, &resume, &pending) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      continue;
    }
    REQUIRE(storeStatus == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
    CHECK(pending.step == 1);
    CHECK(pending.subject_sequence == 2);
    CHECK(pending.input_source == IRFQ_INFINITE_INPUT_STORE_ROW_V2);
    CHECK(pending.msg_type_length == 1);
    CHECK(pending.msg_type[0] == (logout ? '5' : '0'));

    init(resume);
    resume.prepare_id = pending.prepare_id;
    resume.step = pending.step;
    resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
    resume.subject_sequence = pending.subject_sequence;
    std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
    resume.decision = decision;
    resume.input_source = pending.input_source;
    resume.input_item_index = pending.input_item_index;
    resume.input_source_bytes = slice(wire);
    if (variant == "reject-disposition") {
      resume.gateway_inbound_disposition_id = slice(forbiddenDisposition);
    }
    PlanBuffers resultBuffers;
    auto result = resultBuffers.response();
    if (decision == IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2) {
      if (variant == "reject-disposition") {
        REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
        continue;
      }
      result.output.capacity = 0;
      result.output.data = nullptr;
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
      CHECK(result.step == 2);
      CHECK(result.required_output_capacity > 0);
      const auto pendingId = result.prepare_id;
      const auto pendingStep = result.step;
      const auto requiredCapacity = result.required_output_capacity;
      init(resume);
      resume.prepare_id = pendingId;
      resume.step = pendingStep;
      resume.kind = IRFQ_INFINITE_RESUME_OUTPUT_V2;
      result = resultBuffers.response();
      if (variant == "reject-output-disposition") {
        resume.gateway_inbound_disposition_id = slice(forbiddenDisposition);
        REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
        continue;
      }
      result.output.capacity = requiredCapacity - 1;
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
      CHECK(result.step == pendingStep);
      CHECK(result.required_output_capacity > requiredCapacity - 1);
      const auto exactCapacity = result.required_output_capacity;
      result = resultBuffers.response();
      result.output.capacity = exactCapacity;
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK(result.step == 3);
    } else {
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
    }
    const auto hasDetachedOutput = delayed || decision == IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2;
    REQUIRE(
        result.action_count
        == (logout && decision == IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2 ? 4
            : hasDetachedOutput                                               ? 3
                                                                              : 2));
    CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
    CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
    CHECK(
        result.actions[0].reason_code
        == (decision == IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2 ? IRFQ_INFINITE_REASON_NONE_V2
                                                                    : IRFQ_INFINITE_REASON_PROTOCOL_V2));
    CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_QUEUE_ERASE_RANGE_V2);
    CHECK(read64(result.native_state.data + 144) == 2);
    CHECK(read64(result.native_state.data + 212) == 3);
    CHECK(read32(result.native_state.data + 292) == 2);
    if (decision == IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2) {
      CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(result.actions[2].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
      CHECK(result.actions[2].msg_type[0] == '3');
      CHECK(
          std::string(reinterpret_cast<const char *>(result.output.data), result.output.length).find("\00135=3\001")
          != std::string::npos);
    } else if (logout) {
      CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(result.actions[2].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
      CHECK(result.actions[2].msg_type[0] == '5');
      CHECK(result.actions[3].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(result.actions[3].reason_code == IRFQ_INFINITE_REASON_PROTOCOL_V2);
      CHECK(read64(result.native_state.data + 180) == UINT64_C(415));
      CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_PROTOCOL_V2);
    } else if (weekly) {
      CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(result.actions[2].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
      CHECK(result.actions[2].msg_type[0] == '5');
      CHECK(
          std::string(reinterpret_cast<const char *>(result.output.data), result.output.length).find("\00135=5\001")
          != std::string::npos);
      CHECK((read64(result.native_state.data + 180) & UINT64_C(16)) != 0);
      CHECK((read64(result.native_state.data + 180) & UINT64_C(256)) == 0);
      CHECK(read32(result.native_state.data + 192) == 0);
      CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_NONE_V2);
    } else if (delayed) {
      CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
      CHECK(result.actions[2].output_class == IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
      CHECK(result.actions[2].msg_type[0] == '0');
      CHECK(
          std::string(reinterpret_cast<const char *>(result.output.data), result.output.length).find("\00135=0\001")
          != std::string::npos);
    } else {
      CHECK(result.output.length == 0);
    }

    irfq_infinite_declarative_action_v2 expectedDisposition{};
    expectedDisposition.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
    expectedDisposition.disposition = IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2;
    expectedDisposition.input_source = IRFQ_INFINITE_INPUT_STORE_ROW_V2;
    expectedDisposition.sequence_begin = 2;
    expectedDisposition.sequence_end_exclusive = 3;
    expectedDisposition.input_length = wire.size();
    std::copy_n(pending.subject_sha256, 32, expectedDisposition.binding_sha256);
    expectedDisposition.reason_code = decision == IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2
                                          ? IRFQ_INFINITE_REASON_NONE_V2
                                          : IRFQ_INFINITE_REASON_PROTOCOL_V2;
    CHECK(std::memcmp(&result.actions[0], &expectedDisposition, sizeof(expectedDisposition)) == 0);

    std::vector<std::uint8_t> queueRowPreimage;
    const std::string queueRowDomain = "IRFQ-FIX-ABI-V2-QUEUE-ROW-V1";
    queueRowPreimage.insert(queueRowPreimage.end(), queueRowDomain.begin(), queueRowDomain.end());
    queueRowPreimage.push_back(0);
    queueRowPreimage.insert(queueRowPreimage.end(), 32, 0x11);
    append64(queueRowPreimage, 1);
    append64(queueRowPreimage, 2);
    queueRowPreimage.insert(queueRowPreimage.end(), 32, 0xa2);
    append64(queueRowPreimage, 2);
    append32(queueRowPreimage, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2);
    append32(queueRowPreimage, 1);
    queueRowPreimage.push_back(logout ? '5' : '0');
    append32(queueRowPreimage, wire.size());
    queueRowPreimage.insert(queueRowPreimage.end(), frameDigest.begin(), frameDigest.end());
    queueRowPreimage.insert(queueRowPreimage.end(), bodyDigest.begin(), bodyDigest.end());
    const auto queueRowSubject = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
        queueRowPreimage.data(),
        queueRowPreimage.size());
    std::vector<std::uint8_t> erasePreimage;
    const std::string eraseDomain = "IRFQ-FIX-ABI-V2-QUEUE-ERASE-V1";
    erasePreimage.insert(erasePreimage.end(), eraseDomain.begin(), eraseDomain.end());
    erasePreimage.push_back(0);
    erasePreimage.insert(erasePreimage.end(), 32, 0x11);
    append64(erasePreimage, 1);
    append64(erasePreimage, 2);
    erasePreimage.insert(erasePreimage.end(), 32, 0xa2);
    append64(erasePreimage, 2);
    append64(erasePreimage, 3);
    append32(erasePreimage, 1);
    erasePreimage.insert(erasePreimage.end(), queueRowSubject.begin(), queueRowSubject.end());
    append32(erasePreimage, decision);
    const auto eraseDigest
        = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(erasePreimage.data(), erasePreimage.size());
    irfq_infinite_declarative_action_v2 expectedErase{};
    expectedErase.kind = IRFQ_INFINITE_ACTION_QUEUE_ERASE_RANGE_V2;
    expectedErase.sequence_begin = 2;
    expectedErase.sequence_end_exclusive = 3;
    std::copy(eraseDigest.begin(), eraseDigest.end(), expectedErase.binding_sha256);
    CHECK(std::memcmp(&result.actions[1], &expectedErase, sizeof(expectedErase)) == 0);

    if (hasDetachedOutput) {
      irfq_infinite_declarative_action_v2 expectedOutput{};
      expectedOutput.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
      expectedOutput.output_class = IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2;
      expectedOutput.msg_type_length = 1;
      expectedOutput.msg_type[0] = decision == IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2 ? '3' : weekly ? '5' : '0';
      expectedOutput.sequence_begin = read64(queued.native_state.data + 132);
      expectedOutput.sequence_end_exclusive = expectedOutput.sequence_begin + 1;
      expectedOutput.output_length = result.output.length;
      const auto outputDigest
          = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(result.output.data, result.output.length);
      std::copy(outputDigest.begin(), outputDigest.end(), expectedOutput.binding_sha256);
      CHECK(std::memcmp(&result.actions[2], &expectedOutput, sizeof(expectedOutput)) == 0);
    }
    if (logout && decision == IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2) {
      irfq_infinite_declarative_action_v2 expectedOutput{};
      expectedOutput.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
      expectedOutput.output_class = IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2;
      expectedOutput.msg_type_length = 1;
      expectedOutput.msg_type[0] = '5';
      expectedOutput.sequence_begin = read64(queued.native_state.data + 132);
      expectedOutput.sequence_end_exclusive = expectedOutput.sequence_begin + 1;
      expectedOutput.output_length = result.output.length;
      const auto outputDigest
          = FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(result.output.data, result.output.length);
      std::copy(outputDigest.begin(), outputDigest.end(), expectedOutput.binding_sha256);
      CHECK(std::memcmp(&result.actions[2], &expectedOutput, sizeof(expectedOutput)) == 0);
      irfq_infinite_declarative_action_v2 expectedDisconnect{};
      expectedDisconnect.kind = IRFQ_INFINITE_ACTION_DISCONNECT_V2;
      expectedDisconnect.reason_code = IRFQ_INFINITE_REASON_PROTOCOL_V2;
      CHECK(std::memcmp(&result.actions[3], &expectedDisconnect, sizeof(expectedDisconnect)) == 0);
    }

    auto expectedState = queueBuffers.state;
    write64(expectedState.data() + 56, 3);
    write64(expectedState.data() + 80, request.now_tai_ns);
    write64(expectedState.data() + 88, request.now_utc_ns);
    write64(expectedState.data() + 112, request.now_tai_ns);
    write64(expectedState.data() + 120, request.now_utc_ns);
    write64(expectedState.data() + 212, 3);
    write64(expectedState.data() + 300, 3);
    if (hasDetachedOutput || (logout && decision == IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2)) {
      write64(expectedState.data() + 96, request.now_tai_ns);
      write64(expectedState.data() + 104, request.now_utc_ns);
      write64(expectedState.data() + 132, read64(queued.native_state.data + 132) + 1);
    }
    if (logout && decision == IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2) {
      write64(expectedState.data() + 180, UINT64_C(415));
      write32(expectedState.data() + 308, IRFQ_INFINITE_REASON_PROTOCOL_V2);
    } else if (weekly) {
      write64(expectedState.data() + 180, read64(expectedState.data() + 180) | UINT64_C(16));
    }
    REQUIRE(result.native_state.length == expectedState.size());
    CHECK(std::equal(expectedState.begin(), expectedState.end(), result.native_state.data));
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 paginates and restores an original AH0 application block",
    "[infinite][adapter][v2][task2c][application][pagination]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto dictionaries = applicationBlockDictionaries();
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> baseState{};
  auto *session = stockLoggedOnSession(config, 2, &dictionaries, &baseState);
  REQUIRE(session != nullptr);
  const std::string quoteBody = "58=" + std::string(65311, 'Q') + "\001131=REQ\001";
  const std::string completionBody = "644=REQ\00120003=OUT\00120006=1\001";
  std::vector<std::uint8_t> payload(32, 0xb1);
  append32(payload, 2);
  append32(payload, 1);
  append32(payload, 2);
  append32(payload, 0);
  append32(payload, 2);
  appendApplicationUnit(payload, 0, "R", quoteBody);
  appendApplicationUnit(payload, 1, "UAH0", completionBody);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2;
  request.application_block_mode = IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2;
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
  PlanBuffers firstBuffers;
  auto first = firstBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &first) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(first.action_count == 1);
  CHECK(first.actions[0].msg_type[0] == 'R');
  CHECK(first.has_more == IRFQ_INFINITE_YES_V2);
  CHECK(read32(first.native_state.data + 292) == 3);
  CHECK(read32(first.native_state.data + 296) == IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2);
  CHECK(read64(first.native_state.data + 300) == 1);
  auto expectedFirstState = baseState;
  write64(expectedFirstState.data() + 56, 2);
  write64(expectedFirstState.data() + 80, request.now_tai_ns);
  write64(expectedFirstState.data() + 88, request.now_utc_ns);
  write64(expectedFirstState.data() + 96, request.now_tai_ns);
  write64(expectedFirstState.data() + 104, request.now_utc_ns);
  write64(expectedFirstState.data() + 132, 3);
  write32(expectedFirstState.data() + 292, 3);
  write32(expectedFirstState.data() + 296, IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2);
  write64(expectedFirstState.data() + 300, 1);
  REQUIRE(first.native_state.length == expectedFirstState.size());
  CHECK(std::equal(expectedFirstState.begin(), expectedFirstState.end(), first.native_state.data));
  irfq_infinite_apply_committed_request_v2 apply{};
  init(apply);
  apply.prepare_id = first.prepare_id;
  apply.result_revision = first.result_revision;
  std::copy_n(first.native_state_sha256, 32, apply.native_state_sha256);
  irfq_infinite_operation_response_v2 applied{};
  init(applied);
  REQUIRE(irfq_infinite_apply_committed_v2(session, &apply, &applied) == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSessionWithDataDictionaries(
      config.data(),
      config.size(),
      first.native_state.data,
      first.native_state.length,
      1,
      2,
      0,
      0,
      dictionaries);
  REQUIRE(session != nullptr);

  payload.assign(32, 0xb1);
  append32(payload, 2);
  append32(payload, 1);
  append32(payload, 2);
  append32(payload, 1);
  append32(payload, 1);
  appendApplicationUnit(payload, 1, "UAH0", completionBody);
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_CONTINUE_APPLICATION_BLOCK_V2;
  request.application_block_mode = IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2;
  request.expected_epoch = 1;
  request.expected_revision = 2;
  request.now_tai_ns = INT64_C(1700000000123456002);
  request.now_utc_ns = INT64_C(1700000000123456002);
  request.payload = {payload.data(), payload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          request,
          request.event_identity_sha256));
  PlanBuffers secondBuffers;
  auto second = secondBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &second) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(second.action_count == 1);
  CHECK(second.actions[0].msg_type_length == 4);
  CHECK(std::equal(second.actions[0].msg_type, second.actions[0].msg_type + 4, "UAH0"));
  CHECK(second.actions[0].sequence_begin == 3);
  CHECK(second.has_more == IRFQ_INFINITE_NO_V2);
  CHECK(read32(second.native_state.data + 292) == 0);
  CHECK(read64(second.native_state.data + 132) == 4);
  auto expectedSecondState = expectedFirstState;
  write64(expectedSecondState.data() + 56, 3);
  write64(expectedSecondState.data() + 80, request.now_tai_ns);
  write64(expectedSecondState.data() + 88, request.now_utc_ns);
  write64(expectedSecondState.data() + 96, request.now_tai_ns);
  write64(expectedSecondState.data() + 104, request.now_utc_ns);
  write64(expectedSecondState.data() + 132, 4);
  write32(expectedSecondState.data() + 292, 0);
  write32(expectedSecondState.data() + 296, 0);
  write64(expectedSecondState.data() + 300, 0);
  REQUIRE(second.native_state.length == expectedSecondState.size());
  CHECK(std::equal(expectedSecondState.begin(), expectedSecondState.end(), second.native_state.data));
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects altered application mode body digest and canonical order before planning",
    "[infinite][adapter][v2][task2c][application][invalid]") {
  const auto config = otherwiseValidUnavailableProfile();
  for (const std::string variant : {"mode", "digest", "order", "missing-required", "unknown-field"}) {
    INFO(variant);
    auto *session = stockLoggedOnSession(config);
    REQUIRE(session != nullptr);
    const auto body = variant == "order"              ? std::string("694=1\001693=INVALID\001")
                      : variant == "missing-required" ? std::string("693=INVALID\001")
                      : variant == "unknown-field"    ? quoteResponseBody("INVALID") + "9999=UNKNOWN\001"
                                                      : quoteResponseBody("INVALID");
    ApplicationCall call(
        session,
        IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2,
        IRFQ_INFINITE_STAGE_EVENT_V2,
        IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2,
        IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2,
        "AJ",
        body);
    if (variant == "mode") {
      call.request.application_block_mode = IRFQ_INFINITE_APPLICATION_BLOCK_SEMANTIC_REPLAY_V2;
    } else if (variant == "digest") {
      call.payload[64] ^= 1;
      call.request.payload = {call.payload.data(), call.payload.size()};
    }
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            call.request,
            call.request.event_identity_sha256));
    PlanBuffers buffers;
    auto result = buffers.response();
    CHECK(irfq_infinite_prepare_v2(session, &call.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects fresh application and read plans while Task 2C continuation work is active",
    "[infinite][adapter][v2][task2c][application][continuation-guard]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *base = stockLoggedOnSession(config);
  REQUIRE(base != nullptr);
  ApplicationCall completed(
      base,
      IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2,
      IRFQ_INFINITE_STAGE_EVENT_V2,
      IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2,
      IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2,
      "AJ",
      quoteResponseBody("CONTINUATION-BASE"));
  PlanBuffers completedBuffers;
  auto completedResult = completedBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(base, &completed.request, &completedResult) == IRFQ_INFINITE_STATUS_READY_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> baseState{};
  std::copy_n(completedResult.native_state.data, completedResult.native_state.length, baseState.begin());
  REQUIRE(irfq_infinite_destroy_v2(base) == IRFQ_INFINITE_STATUS_OK_V2);

  const std::array continuations{
      std::pair{UINT32_C(2), IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2},
      std::pair{UINT32_C(3), IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2},
      std::pair{UINT32_C(4), IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2},
  };
  const std::array freshEvents{
      std::tuple{
          IRFQ_INFINITE_STAGE_EVENT_V2,
          IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2,
          IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2},
      std::tuple{
          IRFQ_INFINITE_STAGE_EVENT_V2,
          IRFQ_INFINITE_EVENT_APPLICATION_REPLAY_BEGIN_V2,
          IRFQ_INFINITE_APPLICATION_BLOCK_SEMANTIC_REPLAY_V2},
      std::tuple{
          IRFQ_INFINITE_STAGE_READ_R2_V2,
          IRFQ_INFINITE_EVENT_READ_RESULT_BEGIN_V2,
          IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2},
  };
  for (const auto &[continuation, continuationMode] : continuations) {
    for (const auto &[stage, event, mode] : freshEvents) {
      INFO("continuation=" << continuation << " event=" << event);
      auto state = baseState;
      write32(state.data() + 292, continuation);
      write32(state.data() + 296, continuationMode);
      write64(state.data() + 300, continuation == 2 ? 2 : 1);
      if (continuation == 2) {
        write64(state.data() + 196, 2);
        write64(state.data() + 204, 3);
        write64(state.data() + 212, 2);
      }
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
      ApplicationCall call(
          session,
          IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2,
          stage,
          event,
          mode,
          "AJ",
          quoteResponseBody("FRESH-BLOCKED"));
      call.request.expected_revision = 2;
      call.request.now_tai_ns = INT64_C(1700000000123456002);
      call.request.now_utc_ns = INT64_C(1700000000123456002);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              call.request,
              call.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      CHECK(irfq_infinite_prepare_v2(session, &call.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 binds and validates application and read continuation schemas",
    "[infinite][adapter][v2][task2c][application][continuation-schema]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto dictionaries = applicationBlockDictionaries();
  auto *base = stockLoggedOnSession(config, 2, &dictionaries);
  REQUIRE(base != nullptr);
  ApplicationCall completed(
      base,
      IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2,
      IRFQ_INFINITE_STAGE_EVENT_V2,
      IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2,
      IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2,
      "AJ",
      quoteResponseBody("CONTINUATION-SCHEMA"));
  PlanBuffers completedBuffers;
  auto completedResult = completedBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(base, &completed.request, &completedResult) == IRFQ_INFINITE_STATUS_READY_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> baseState{};
  std::copy_n(completedResult.native_state.data, completedResult.native_state.length, baseState.begin());
  REQUIRE(irfq_infinite_destroy_v2(base) == IRFQ_INFINITE_STATUS_OK_V2);

  const std::string completionBody = "644=REQ\00120003=OUT\00120006=1\001";
  for (const auto read : {false, true}) {
    const std::vector<std::string> variants
        = read ? std::vector<std::string>{"valid", "subject", "cut", "result", "block", "total", "cursor"}
               : std::vector<std::string>{"valid", "subject", "block", "total", "cursor"};
    for (const auto &variant : variants) {
      INFO("read=" << read << " variant=" << variant);
      auto state = baseState;
      write32(state.data() + 292, read ? UINT32_C(4) : UINT32_C(3));
      write32(
          state.data() + 296,
          read ? IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2 : IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2);
      write64(state.data() + 300, 1);
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSessionWithDataDictionaries(
          config.data(),
          config.size(),
          state.data(),
          state.size(),
          1,
          2,
          0,
          0,
          dictionaries);
      REQUIRE(session != nullptr);

      std::vector<std::uint8_t> payload(32, 0xc1);
      if (read) {
        payload.insert(payload.end(), 32, 0xc2);
        payload.insert(payload.end(), 32, 0xc3);
      }
      const auto blockOffset = payload.size();
      append32(payload, 2);
      append32(payload, 1);
      append32(payload, 2);
      append32(payload, 1);
      append32(payload, 1);
      appendApplicationUnit(payload, 1, "UAH0", completionBody);
      irfq_infinite_prepare_request_v2 request{};
      init(request);
      request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
      request.stage = read ? IRFQ_INFINITE_STAGE_READ_R2_V2 : IRFQ_INFINITE_STAGE_EVENT_V2;
      request.event
          = read ? IRFQ_INFINITE_EVENT_CONTINUE_READ_RESULT_V2 : IRFQ_INFINITE_EVENT_CONTINUE_APPLICATION_BLOCK_V2;
      request.application_block_mode
          = read ? IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2 : IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2;
      request.expected_epoch = 1;
      request.expected_revision = 2;
      request.now_tai_ns = INT64_C(1700000000123456002);
      request.now_utc_ns = INT64_C(1700000000123456002);
      request.payload = {payload.data(), payload.size()};
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              request,
              request.event_identity_sha256));
      const auto rebind = variant == "block" || variant == "total" || variant == "cursor";
      if (variant == "subject") {
        payload[0] ^= 1;
      } else if (variant == "cut") {
        payload[32] ^= 1;
      } else if (variant == "result") {
        payload[64] ^= 1;
      } else if (variant == "block") {
        write32(payload.data() + blockOffset, 1);
      } else if (variant == "total") {
        write32(payload.data() + blockOffset + 8, 3);
      } else if (variant == "cursor") {
        write32(payload.data() + blockOffset + 12, 0);
      }
      if (rebind) {
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                session,
                request,
                request.event_identity_sha256));
      }
      PlanBuffers buffers;
      auto result = buffers.response();
      const auto status = irfq_infinite_prepare_v2(session, &request, &result);
      if (variant == "valid") {
        REQUIRE(status == IRFQ_INFINITE_STATUS_READY_V2);
        REQUIRE(result.action_count == 1);
        CHECK(result.actions[0].msg_type_length == 4);
        CHECK(std::equal(result.actions[0].msg_type, result.actions[0].msg_type + 4, "UAH0"));
        CHECK(result.actions[0].sequence_begin == 3);
        CHECK(result.has_more == IRFQ_INFINITE_NO_V2);
        CHECK(read32(result.native_state.data + 292) == 0);
        auto expectedState = state;
        write64(expectedState.data() + 56, 3);
        write64(expectedState.data() + 80, request.now_tai_ns);
        write64(expectedState.data() + 88, request.now_utc_ns);
        write64(expectedState.data() + 96, request.now_tai_ns);
        write64(expectedState.data() + 104, request.now_utc_ns);
        write64(expectedState.data() + 132, 4);
        write32(expectedState.data() + 292, 0);
        write32(expectedState.data() + 296, 0);
        write64(expectedState.data() + 300, 0);
        REQUIRE(result.native_state.length == expectedState.size());
        CHECK(std::equal(expectedState.begin(), expectedState.end(), result.native_state.data));
      } else {
        CHECK(status == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

namespace {
struct AbiFixtureRow {
  std::string kind;
  std::string name;
  std::string value;

  bool operator==(const AbiFixtureRow &other) const {
    return kind == other.kind && name == other.name && value == other.value;
  }
};

void appendAbiText(std::vector<AbiFixtureRow> &rows, const char *kind, const char *name, const char *value) {
  rows.push_back({kind, name, value});
}

template <typename T>
void appendAbiNumber(std::vector<AbiFixtureRow> &rows, const char *kind, const char *name, T value) {
  rows.push_back({kind, name, std::to_string(value)});
}

std::vector<AbiFixtureRow> expectedAbiFixtureRows() {
  std::vector<AbiFixtureRow> rows;
  rows.reserve(317);

#define ABI_TEXT(kind, name, value) appendAbiText(rows, #kind, #name, value)
#define ABI_NUMBER(kind, name, value) appendAbiNumber(rows, #kind, #name, value)
#define ABI_LAYOUT(type)                                                                                               \
  ABI_NUMBER(size, type, sizeof(type));                                                                                \
  ABI_NUMBER(align, type, alignof(type))
#define ABI_OFFSET(type, field) ABI_NUMBER(offset, type.field, offsetof(type, field))

  ABI_TEXT(kind, name, "value");
  ABI_TEXT(contract, byte_order, "little_endian");
  ABI_TEXT(contract, semantic_calls, "scan>session_create>prepare>resume>apply_committed|abort>destroy");
  ABI_TEXT(contract, handle_ownership, "exclusive_nonoverlap_join_before_destroy");
  ABI_TEXT(contract, borrowed_input_lifetime, "synchronous_call_only");
  ABI_TEXT(contract, output_ownership, "caller_owned_bounded");
  ABI_TEXT(contract, pending_plan, "one_per_session_one_stage");
  ABI_TEXT(contract, native_state_digest, "SHA-256(\"IRFQ-FIX-NATIVE-STATE-V1\"||0x00||native_state_bstr)");

  ABI_NUMBER(constant, abi_version, IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2);
  ABI_NUMBER(constant, snapshot_codec_version, IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2);
  ABI_NUMBER(constant, native_state_schema_version, IRFQ_INFINITE_NATIVE_STATE_SCHEMA_VERSION_V2);
  ABI_NUMBER(constant, native_state_bytes, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2);
  ABI_NUMBER(constant, max_scan_bytes, IRFQ_INFINITE_MAX_SCAN_BYTES_V2);
  ABI_NUMBER(constant, max_frame_bytes, IRFQ_INFINITE_MAX_FRAME_BYTES_V2);
  ABI_NUMBER(constant, max_native_state_bytes, IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2);
  ABI_NUMBER(constant, max_prepare_payload_bytes, IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2);
  ABI_NUMBER(constant, max_store_range_bytes, IRFQ_INFINITE_MAX_STORE_RANGE_BYTES_V2);
  ABI_NUMBER(constant, max_application_wire_bytes, IRFQ_INFINITE_MAX_APPLICATION_WIRE_BYTES_V2);
  ABI_NUMBER(constant, max_output_bytes, IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2);
  ABI_NUMBER(constant, max_store_items, IRFQ_INFINITE_MAX_STORE_ITEMS_V2);
  ABI_NUMBER(constant, max_application_units, IRFQ_INFINITE_MAX_APPLICATION_UNITS_V2);
  ABI_NUMBER(constant, max_output_frames, IRFQ_INFINITE_MAX_OUTPUT_FRAMES_V2);
  ABI_NUMBER(constant, max_actions, IRFQ_INFINITE_MAX_ACTIONS_V2);
  ABI_NUMBER(constant, max_resume_steps, IRFQ_INFINITE_MAX_RESUME_STEPS_V2);
  ABI_NUMBER(constant, max_message_type_bytes, IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2);
  ABI_NUMBER(constant, max_test_request_id_bytes, IRFQ_INFINITE_MAX_TEST_REQUEST_ID_BYTES_V2);
  ABI_NUMBER(
      constant,
      max_gateway_inbound_disposition_id_bytes,
      IRFQ_INFINITE_MAX_GATEWAY_INBOUND_DISPOSITION_ID_BYTES_V2);
  ABI_NUMBER(constant, fix_sequence_bound, IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2);

  ABI_NUMBER(status, OK, IRFQ_INFINITE_STATUS_OK_V2);
  ABI_NUMBER(status, INVALID_ARGUMENT, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  ABI_NUMBER(status, ABI_MISMATCH, IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2);
  ABI_NUMBER(status, NEED_MORE, IRFQ_INFINITE_STATUS_NEED_MORE_V2);
  ABI_NUMBER(status, FRAME_READY, IRFQ_INFINITE_STATUS_FRAME_READY_V2);
  ABI_NUMBER(status, NEED_STORE_RANGE, IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2);
  ABI_NUMBER(status, NEED_APPLICATION_DECISION, IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  ABI_NUMBER(status, NEED_EPOCH_RESET_DECISION, IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2);
  ABI_NUMBER(status, NEED_OUTPUT, IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  ABI_NUMBER(status, READY, IRFQ_INFINITE_STATUS_READY_V2);
  ABI_NUMBER(status, PLAN_PENDING, IRFQ_INFINITE_STATUS_PLAN_PENDING_V2);
  ABI_NUMBER(status, STALE_PLAN, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  ABI_NUMBER(status, REVISION_MISMATCH, IRFQ_INFINITE_STATUS_REVISION_MISMATCH_V2);
  ABI_NUMBER(status, DIGEST_MISMATCH, IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
  ABI_NUMBER(status, INTERNAL_ERROR, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  ABI_NUMBER(status, PROFILE_UNAVAILABLE, IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  ABI_NUMBER(status, LIMIT_EXCEEDED, IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);

  ABI_NUMBER(boolean, NO, IRFQ_INFINITE_NO_V2);
  ABI_NUMBER(boolean, YES, IRFQ_INFINITE_YES_V2);
  ABI_NUMBER(scan, BEGIN_STRING, IRFQ_INFINITE_SCAN_BEGIN_STRING_V2);
  ABI_NUMBER(scan, BODY_LENGTH_PREFIX, IRFQ_INFINITE_SCAN_BODY_LENGTH_PREFIX_V2);
  ABI_NUMBER(scan, BODY_LENGTH, IRFQ_INFINITE_SCAN_BODY_LENGTH_V2);
  ABI_NUMBER(scan, BODY, IRFQ_INFINITE_SCAN_BODY_V2);
  ABI_NUMBER(prepare, REGISTERED_INBOUND, IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2);
  ABI_NUMBER(prepare, AUTHORIZED_APPLICATION, IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2);
  ABI_NUMBER(prepare, RUST_TIMER, IRFQ_INFINITE_PREPARE_RUST_TIMER_V2);
  ABI_NUMBER(prepare, RUST_SESSION_CONTROL, IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2);
  ABI_NUMBER(stage, HEAD, IRFQ_INFINITE_STAGE_HEAD_V2);
  ABI_NUMBER(stage, READ_R2, IRFQ_INFINITE_STAGE_READ_R2_V2);
  ABI_NUMBER(stage, TARGET_CAS, IRFQ_INFINITE_STAGE_TARGET_CAS_V2);
  ABI_NUMBER(stage, RESET_FINAL, IRFQ_INFINITE_STAGE_RESET_FINAL_V2);
  ABI_NUMBER(stage, EVENT, IRFQ_INFINITE_STAGE_EVENT_V2);
  ABI_NUMBER(event, INBOUND_FRAME, IRFQ_INFINITE_EVENT_INBOUND_FRAME_V2);
  ABI_NUMBER(event, ORIGINAL_APPLICATION, IRFQ_INFINITE_EVENT_ORIGINAL_APPLICATION_V2);
  ABI_NUMBER(event, STORED_FRAME_RETRANSMIT, IRFQ_INFINITE_EVENT_STORED_FRAME_RETRANSMIT_V2);
  ABI_NUMBER(event, APPLICATION_REPLAY_BEGIN, IRFQ_INFINITE_EVENT_APPLICATION_REPLAY_BEGIN_V2);
  ABI_NUMBER(event, READ_RESULT_BEGIN, IRFQ_INFINITE_EVENT_READ_RESULT_BEGIN_V2);
  ABI_NUMBER(event, TIMER_TICK, IRFQ_INFINITE_EVENT_TIMER_TICK_V2);
  ABI_NUMBER(event, SCHEDULED_RESET_TRIGGER, IRFQ_INFINITE_EVENT_SCHEDULED_RESET_TRIGGER_V2);
  ABI_NUMBER(event, ADMIN_LOGON, IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2);
  ABI_NUMBER(event, ADMIN_LOGOUT, IRFQ_INFINITE_EVENT_ADMIN_LOGOUT_V2);
  ABI_NUMBER(event, ADMIN_REJECT, IRFQ_INFINITE_EVENT_ADMIN_REJECT_V2);
  ABI_NUMBER(event, ADMIN_RESEND_REQUEST, IRFQ_INFINITE_EVENT_ADMIN_RESEND_REQUEST_V2);
  ABI_NUMBER(event, ADMIN_HEARTBEAT, IRFQ_INFINITE_EVENT_ADMIN_HEARTBEAT_V2);
  ABI_NUMBER(event, ADMIN_TEST_REQUEST, IRFQ_INFINITE_EVENT_ADMIN_TEST_REQUEST_V2);
  ABI_NUMBER(event, CONTINUE_RESEND, IRFQ_INFINITE_EVENT_CONTINUE_RESEND_V2);
  ABI_NUMBER(event, CONTINUE_QUEUED_INBOUND, IRFQ_INFINITE_EVENT_CONTINUE_QUEUED_INBOUND_V2);
  ABI_NUMBER(event, CONTINUE_APPLICATION_BLOCK, IRFQ_INFINITE_EVENT_CONTINUE_APPLICATION_BLOCK_V2);
  ABI_NUMBER(event, CONTINUE_READ_RESULT, IRFQ_INFINITE_EVENT_CONTINUE_READ_RESULT_V2);
  ABI_NUMBER(event, ADVANCE_TARGET, IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2);
  ABI_NUMBER(event, FINALIZE_RESET, IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2);
  ABI_NUMBER(event, TRANSPORT_CLOSED, IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2);
  ABI_NUMBER(event, ADVANCE_PROCESSING_FRONTIER, IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2);
  ABI_NUMBER(resume, STORE_RANGE, IRFQ_INFINITE_RESUME_STORE_RANGE_V2);
  ABI_NUMBER(resume, APPLICATION_DECISION, IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2);
  ABI_NUMBER(resume, EPOCH_RESET_DECISION, IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2);
  ABI_NUMBER(resume, OUTPUT, IRFQ_INFINITE_RESUME_OUTPUT_V2);
  ABI_NUMBER(application_decision, ALLOW, IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2);
  ABI_NUMBER(application_decision, REJECT, IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2);
  ABI_NUMBER(epoch_reset_decision, START_SAGA, IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2);
  ABI_NUMBER(epoch_reset_decision, REJECT_TRIGGER, IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2);
  ABI_NUMBER(sequence_state, ABSENT, IRFQ_INFINITE_SEQUENCE_ABSENT_V2);
  ABI_NUMBER(sequence_state, VALUE, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  ABI_NUMBER(sequence_state, EXHAUSTED, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
  ABI_NUMBER(application_block, NONE, IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2);
  ABI_NUMBER(application_block, ORIGINAL, IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2);
  ABI_NUMBER(application_block, SEMANTIC_REPLAY, IRFQ_INFINITE_APPLICATION_BLOCK_SEMANTIC_REPLAY_V2);
  ABI_NUMBER(input_source, NONE, IRFQ_INFINITE_INPUT_NONE_V2);
  ABI_NUMBER(input_source, PREPARE_PAYLOAD, IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2);
  ABI_NUMBER(input_source, STORE_ROW, IRFQ_INFINITE_INPUT_STORE_ROW_V2);
  ABI_NUMBER(store_class, MANDATORY_APPLICATION, IRFQ_INFINITE_STORE_CLASS_MANDATORY_APPLICATION_V2);
  ABI_NUMBER(store_class, REVOCABLE_SUPPRESSED, IRFQ_INFINITE_STORE_CLASS_REVOCABLE_SUPPRESSED_V2);
  ABI_NUMBER(store_class, AH0_RESULT_BLOCK, IRFQ_INFINITE_STORE_CLASS_AH0_RESULT_BLOCK_V2);
  ABI_NUMBER(store_class, SESSION_ADMIN, IRFQ_INFINITE_STORE_CLASS_SESSION_ADMIN_V2);
  ABI_NUMBER(store_class, PROVEN_GAP, IRFQ_INFINITE_STORE_CLASS_PROVEN_GAP_V2);
  ABI_NUMBER(action, APPLICATION_DISPATCH, IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2);
  ABI_NUMBER(action, INBOUND_PROTOCOL_DISPOSITION, IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  ABI_NUMBER(action, QUEUE_INSERT, IRFQ_INFINITE_ACTION_QUEUE_INSERT_V2);
  ABI_NUMBER(action, QUEUE_ERASE_RANGE, IRFQ_INFINITE_ACTION_QUEUE_ERASE_RANGE_V2);
  ABI_NUMBER(action, OUTPUT_FRAME, IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  ABI_NUMBER(action, DISCONNECT, IRFQ_INFINITE_ACTION_DISCONNECT_V2);
  ABI_NUMBER(action, RESET_TRIGGER, IRFQ_INFINITE_ACTION_RESET_TRIGGER_V2);
  ABI_NUMBER(action, TARGET_ADVANCE, IRFQ_INFINITE_ACTION_TARGET_ADVANCE_V2);
  ABI_NUMBER(output_class, NONE, IRFQ_INFINITE_OUTPUT_NONE_V2);
  ABI_NUMBER(output_class, ORIGINAL_APPLICATION, IRFQ_INFINITE_OUTPUT_ORIGINAL_APPLICATION_V2);
  ABI_NUMBER(output_class, SESSION_RETRANSMIT, IRFQ_INFINITE_OUTPUT_SESSION_RETRANSMIT_V2);
  ABI_NUMBER(output_class, SEMANTIC_REPLAY, IRFQ_INFINITE_OUTPUT_SEMANTIC_REPLAY_V2);
  ABI_NUMBER(output_class, SESSION_ADMIN, IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2);
  ABI_NUMBER(output_class, GAP_FILL, IRFQ_INFINITE_OUTPUT_GAP_FILL_V2);
  ABI_NUMBER(disposition, DURABLE_CONSUME, IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
  ABI_NUMBER(disposition, DURABLE_NO_CONSUME, IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
  ABI_NUMBER(disposition, PENDING_CORE, IRFQ_INFINITE_DISPOSITION_PENDING_CORE_V2);
  ABI_NUMBER(disposition, PENDING_READ, IRFQ_INFINITE_DISPOSITION_PENDING_READ_V2);
  ABI_NUMBER(disposition, PENDING_RESET_LOGON, IRFQ_INFINITE_DISPOSITION_PENDING_RESET_LOGON_V2);
  ABI_NUMBER(reason, NONE, IRFQ_INFINITE_REASON_NONE_V2);
  ABI_NUMBER(reason, IDENTITY_MISMATCH, IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2);
  ABI_NUMBER(reason, SESSION_TIME, IRFQ_INFINITE_REASON_SESSION_TIME_V2);
  ABI_NUMBER(reason, LATENCY, IRFQ_INFINITE_REASON_LATENCY_V2);
  ABI_NUMBER(reason, SEQUENCE, IRFQ_INFINITE_REASON_SEQUENCE_V2);
  ABI_NUMBER(reason, DICTIONARY, IRFQ_INFINITE_REASON_DICTIONARY_V2);
  ABI_NUMBER(reason, RESET_REJECTED, IRFQ_INFINITE_REASON_RESET_REJECTED_V2);
  ABI_NUMBER(reason, HEARTBEAT_TIMEOUT, IRFQ_INFINITE_REASON_HEARTBEAT_TIMEOUT_V2);
  ABI_NUMBER(reason, PROTOCOL, IRFQ_INFINITE_REASON_PROTOCOL_V2);
  ABI_NUMBER(reason, INTEGRITY, IRFQ_INFINITE_REASON_INTEGRITY_V2);

  ABI_LAYOUT(irfq_infinite_input_header_v2);
  ABI_OFFSET(irfq_infinite_input_header_v2, structure_size);
  ABI_OFFSET(irfq_infinite_input_header_v2, abi_version);
  ABI_OFFSET(irfq_infinite_input_header_v2, reserved);
  ABI_LAYOUT(irfq_infinite_output_header_v2);
  ABI_OFFSET(irfq_infinite_output_header_v2, structure_size);
  ABI_OFFSET(irfq_infinite_output_header_v2, abi_version);
  ABI_OFFSET(irfq_infinite_output_header_v2, status);
  ABI_OFFSET(irfq_infinite_output_header_v2, reserved);
  ABI_LAYOUT(irfq_infinite_slice_v2);
  ABI_OFFSET(irfq_infinite_slice_v2, data);
  ABI_OFFSET(irfq_infinite_slice_v2, length);
  ABI_LAYOUT(irfq_infinite_buffer_v2);
  ABI_OFFSET(irfq_infinite_buffer_v2, data);
  ABI_OFFSET(irfq_infinite_buffer_v2, capacity);
  ABI_OFFSET(irfq_infinite_buffer_v2, length);
  ABI_LAYOUT(irfq_infinite_scan_cursor_v2);
  ABI_OFFSET(irfq_infinite_scan_cursor_v2, scan_offset);
  ABI_OFFSET(irfq_infinite_scan_cursor_v2, body_length);
  ABI_OFFSET(irfq_infinite_scan_cursor_v2, checksum_begin);
  ABI_OFFSET(irfq_infinite_scan_cursor_v2, stage);
  ABI_OFFSET(irfq_infinite_scan_cursor_v2, body_length_has_digit);
  ABI_LAYOUT(irfq_infinite_scan_request_v2);
  ABI_OFFSET(irfq_infinite_scan_request_v2, header);
  ABI_OFFSET(irfq_infinite_scan_request_v2, input);
  ABI_OFFSET(irfq_infinite_scan_request_v2, cursor);
  ABI_LAYOUT(irfq_infinite_scan_response_v2);
  ABI_OFFSET(irfq_infinite_scan_response_v2, header);
  ABI_OFFSET(irfq_infinite_scan_response_v2, cursor);
  ABI_OFFSET(irfq_infinite_scan_response_v2, complete_prefix_length);
  ABI_LAYOUT(irfq_infinite_prepare_id_v2);
  ABI_OFFSET(irfq_infinite_prepare_id_v2, high);
  ABI_OFFSET(irfq_infinite_prepare_id_v2, low);
  ABI_LAYOUT(irfq_infinite_session_create_request_v2);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, header);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, snapshot_codec_version);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, reserved);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, canonical_session_create_config);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, session_epoch);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, cache_revision);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, creation_tai_ns);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, creation_utc_ns);
  ABI_OFFSET(irfq_infinite_session_create_request_v2, native_state);
  ABI_LAYOUT(irfq_infinite_session_create_response_v2);
  ABI_OFFSET(irfq_infinite_session_create_response_v2, header);
  ABI_OFFSET(irfq_infinite_session_create_response_v2, session);
  ABI_OFFSET(irfq_infinite_session_create_response_v2, cache_epoch);
  ABI_OFFSET(irfq_infinite_session_create_response_v2, cache_revision);
  ABI_LAYOUT(irfq_infinite_prepare_request_v2);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, header);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, kind);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, stage);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, event);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, application_block_mode);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, event_identity_sha256);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, expected_epoch);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, expected_revision);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, now_tai_ns);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, now_utc_ns);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, next_original_state);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, reserved);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, next_original_value);
  ABI_OFFSET(irfq_infinite_prepare_request_v2, payload);
  ABI_LAYOUT(irfq_infinite_declarative_action_v2);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, kind);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, output_class);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, disposition);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, msg_type_length);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, msg_type);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, input_source);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, input_item_index);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, sequence_begin);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, sequence_end_exclusive);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, input_offset);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, input_length);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, output_offset);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, output_length);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, binding_sha256);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, reason_code);
  ABI_OFFSET(irfq_infinite_declarative_action_v2, reserved);
  ABI_LAYOUT(irfq_infinite_prepare_response_v2);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, header);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, prepare_id);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, step);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, kind);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, stage);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, event);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, event_identity_sha256);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, base_epoch);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, base_revision);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, store_range_begin);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, store_range_end_exclusive);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, subject_sequence);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, subject_sha256);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, msg_type);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, msg_type_length);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, input_source);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, input_item_index);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, reserved);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, input_offset);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, input_length);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, required_output_capacity);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, result_epoch);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, result_revision);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, native_state);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, output);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, native_state_sha256);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, actions);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, action_capacity);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, action_count);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, output_frame_count);
  ABI_OFFSET(irfq_infinite_prepare_response_v2, has_more);
  ABI_LAYOUT(irfq_infinite_store_row_v2);
  ABI_OFFSET(irfq_infinite_store_row_v2, sequence);
  ABI_OFFSET(irfq_infinite_store_row_v2, store_class);
  ABI_OFFSET(irfq_infinite_store_row_v2, msg_type_length);
  ABI_OFFSET(irfq_infinite_store_row_v2, frame_length);
  ABI_OFFSET(irfq_infinite_store_row_v2, reserved);
  ABI_OFFSET(irfq_infinite_store_row_v2, frame_sha256);
  ABI_OFFSET(irfq_infinite_store_row_v2, body_sha256);
  ABI_OFFSET(irfq_infinite_store_row_v2, msg_type);
  ABI_OFFSET(irfq_infinite_store_row_v2, frame);
  ABI_LAYOUT(irfq_infinite_resume_request_v2);
  ABI_OFFSET(irfq_infinite_resume_request_v2, header);
  ABI_OFFSET(irfq_infinite_resume_request_v2, prepare_id);
  ABI_OFFSET(irfq_infinite_resume_request_v2, step);
  ABI_OFFSET(irfq_infinite_resume_request_v2, kind);
  ABI_OFFSET(irfq_infinite_resume_request_v2, subject_sequence);
  ABI_OFFSET(irfq_infinite_resume_request_v2, subject_sha256);
  ABI_OFFSET(irfq_infinite_resume_request_v2, decision);
  ABI_OFFSET(irfq_infinite_resume_request_v2, input_source);
  ABI_OFFSET(irfq_infinite_resume_request_v2, input_item_index);
  ABI_OFFSET(irfq_infinite_resume_request_v2, reserved);
  ABI_OFFSET(irfq_infinite_resume_request_v2, input_source_bytes);
  ABI_OFFSET(irfq_infinite_resume_request_v2, store_range_begin);
  ABI_OFFSET(irfq_infinite_resume_request_v2, store_range_end_exclusive);
  ABI_OFFSET(irfq_infinite_resume_request_v2, store_rows);
  ABI_OFFSET(irfq_infinite_resume_request_v2, store_row_count);
  ABI_OFFSET(irfq_infinite_resume_request_v2, reserved2);
  ABI_OFFSET(irfq_infinite_resume_request_v2, gateway_inbound_disposition_id);
  ABI_LAYOUT(irfq_infinite_apply_committed_request_v2);
  ABI_OFFSET(irfq_infinite_apply_committed_request_v2, header);
  ABI_OFFSET(irfq_infinite_apply_committed_request_v2, prepare_id);
  ABI_OFFSET(irfq_infinite_apply_committed_request_v2, result_revision);
  ABI_OFFSET(irfq_infinite_apply_committed_request_v2, native_state_sha256);
  ABI_LAYOUT(irfq_infinite_abort_request_v2);
  ABI_OFFSET(irfq_infinite_abort_request_v2, header);
  ABI_OFFSET(irfq_infinite_abort_request_v2, prepare_id);
  ABI_LAYOUT(irfq_infinite_operation_response_v2);
  ABI_OFFSET(irfq_infinite_operation_response_v2, header);
  ABI_OFFSET(irfq_infinite_operation_response_v2, cache_revision);

  ABI_NUMBER(reserved_zero, irfq_infinite_input_header_v2.reserved, 0);
  ABI_NUMBER(reserved_zero, irfq_infinite_output_header_v2.reserved, 0);
  ABI_NUMBER(reserved_zero, irfq_infinite_session_create_request_v2.reserved, 0);
  ABI_NUMBER(reserved_zero, irfq_infinite_prepare_request_v2.reserved, 0);
  ABI_NUMBER(reserved_zero, irfq_infinite_declarative_action_v2.reserved, 0);
  ABI_NUMBER(reserved_zero, irfq_infinite_prepare_response_v2.reserved, 0);
  ABI_NUMBER(reserved_zero, irfq_infinite_store_row_v2.reserved, 0);
  ABI_NUMBER(reserved_zero, irfq_infinite_resume_request_v2.reserved, 0);
  ABI_NUMBER(reserved_zero, irfq_infinite_resume_request_v2.reserved2, 0);
  ABI_TEXT(symbol, irfq_infinite_scan_v2, "function");
  ABI_TEXT(symbol, irfq_infinite_session_create_v2, "function");
  ABI_TEXT(symbol, irfq_infinite_prepare_v2, "function");
  ABI_TEXT(symbol, irfq_infinite_resume_v2, "function");
  ABI_TEXT(symbol, irfq_infinite_apply_committed_v2, "function");
  ABI_TEXT(symbol, irfq_infinite_abort_v2, "function");
  ABI_TEXT(symbol, irfq_infinite_destroy_v2, "function");

#undef ABI_OFFSET
#undef ABI_LAYOUT
#undef ABI_NUMBER
#undef ABI_TEXT

  return rows;
}

bool validAbiFixture(const std::string &bytes) {
  if (bytes.empty() || bytes.back() != '\n' || bytes.find('\r') != std::string::npos) {
    return false;
  }

  std::vector<AbiFixtureRow> rows;
  for (std::size_t begin = 0; begin < bytes.size();) {
    const auto end = bytes.find('\n', begin);
    if (end == std::string::npos || end == begin) {
      return false;
    }
    const auto firstTab = bytes.find('\t', begin);
    if (firstTab == std::string::npos || firstTab >= end || firstTab == begin) {
      return false;
    }
    const auto secondTab = bytes.find('\t', firstTab + 1);
    if (secondTab == std::string::npos || secondTab >= end || secondTab == firstTab + 1
        || bytes.find('\t', secondTab + 1) < end || secondTab + 1 == end) {
      return false;
    }
    rows.push_back(
        {bytes.substr(begin, firstTab - begin),
         bytes.substr(firstTab + 1, secondTab - firstTab - 1),
         bytes.substr(secondTab + 1, end - secondTab - 1)});
    begin = end + 1;
  }
  const auto expected = expectedAbiFixtureRows();
  return expected.size() == 317 && rows == expected;
}

void replaceAbiFixtureText(std::string &bytes, const std::string &from, const std::string &to) {
  const auto position = bytes.find(from);
  REQUIRE(position != std::string::npos);
  bytes.replace(position, from.size(), to);
}
} // namespace

TEST_CASE("InfiniteFrameAdapterV2 fixture pins the exact compiled C ABI", "[infinite][adapter][v2][abi]") {
  std::ifstream stream(INFINITE_FRAME_ADAPTER_ABI_FIXTURE_PATH, std::ios::binary);
  REQUIRE(stream.good());
  const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  REQUIRE(validAbiFixture(bytes));

  auto crlf = bytes;
  replaceAbiFixtureText(crlf, "\n", "\r\n");
  CHECK_FALSE(validAbiFixture(crlf));

  auto duplicate = bytes;
  replaceAbiFixtureText(
      duplicate,
      "constant\tabi_version\t131072\n",
      "constant\tabi_version\t131072\nconstant\tabi_version\t131072\n");
  CHECK_FALSE(validAbiFixture(duplicate));

  auto unknown = bytes;
  replaceAbiFixtureText(unknown, "constant\tabi_version\t", "constant\tunknown_version\t");
  CHECK_FALSE(validAbiFixture(unknown));

  auto missing = bytes;
  replaceAbiFixtureText(missing, "boolean\tYES\t1\n", "");
  CHECK_FALSE(validAbiFixture(missing));

  auto reordered = bytes;
  replaceAbiFixtureText(reordered, "boolean\tNO\t0\nboolean\tYES\t1\n", "boolean\tYES\t1\nboolean\tNO\t0\n");
  CHECK_FALSE(validAbiFixture(reordered));

  auto wrongValue = bytes;
  replaceAbiFixtureText(
      wrongValue,
      "constant\tmax_native_state_bytes\t312\n",
      "constant\tmax_native_state_bytes\t313\n");
  CHECK_FALSE(validAbiFixture(wrongValue));

  auto trailing = bytes;
  trailing += "symbol\tirfq_infinite_destroy_v2\tfunction\n";
  CHECK_FALSE(validAbiFixture(trailing));

  auto noFinalLf = bytes;
  noFinalLf.pop_back();
  CHECK_FALSE(validAbiFixture(noFinalLf));
}

TEST_CASE("InfiniteFrameAdapterV2 exposes the frozen v2 numeric domains", "[infinite][adapter][v2][abi]") {
  CHECK(IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2 == 2);
  CHECK(IRFQ_INFINITE_MAX_SCAN_BYTES_V2 == 65536);
  CHECK(IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2 == 312);
  CHECK(IRFQ_INFINITE_MAX_RESUME_STEPS_V2 == 3);
  CHECK(IRFQ_INFINITE_MAX_ACTIONS_V2 == 258);
  CHECK(IRFQ_INFINITE_MAX_GATEWAY_INBOUND_DISPOSITION_ID_BYTES_V2 == 64);
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

  for (std::size_t length = 0; length < first.size(); ++length) {
    INFO("fragment length=" << length << " scan offset=" << cursor.scan_offset << " stage=" << cursor.stage);
    std::string relocated(combined.data(), length);
    irfq_infinite_scan_request_v2 request{};
    init(request);
    request.input = slice(relocated);
    request.cursor = cursor;
    irfq_infinite_scan_response_v2 response{};
    init(response);
    REQUIRE(irfq_infinite_scan_v2(&request, &response) == IRFQ_INFINITE_STATUS_NEED_MORE_V2);
    CHECK(response.cursor.scan_offset <= request.input.length);
    CHECK(response.cursor.checksum_begin <= request.input.length);
    if (length <= 2) {
      CHECK(response.cursor.scan_offset == length);
      CHECK(response.cursor.body_length == 0);
      CHECK(response.cursor.checksum_begin == 0);
      CHECK(response.cursor.stage == IRFQ_INFINITE_SCAN_BEGIN_STRING_V2);
      CHECK(response.cursor.body_length_has_digit == IRFQ_INFINITE_NO_V2);
    }
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
    "InfiniteFrameAdapterV2 scan bounds BeginString and preserves BodyLength status across every split",
    "[infinite][adapter][v2][scan][bounds]") {
  const auto scan = [](const std::string &input, const irfq_infinite_scan_cursor_v2 &cursor) {
    irfq_infinite_scan_request_v2 request{};
    init(request);
    request.input = slice(input);
    request.cursor = cursor;
    irfq_infinite_scan_response_v2 response{};
    init(response);
    response.complete_prefix_length = UINT64_MAX;
    response.cursor = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT32_MAX, UINT32_MAX};
    const auto status = irfq_infinite_scan_v2(&request, &response);
    return std::pair{status, response};
  };
  const auto checkZeroResult = [](const irfq_infinite_scan_response_v2 &response) {
    CHECK(response.complete_prefix_length == 0);
    CHECK(response.cursor.scan_offset == 0);
    CHECK(response.cursor.body_length == 0);
    CHECK(response.cursor.checksum_begin == 0);
    CHECK(response.cursor.stage == IRFQ_INFINITE_SCAN_BEGIN_STRING_V2);
    CHECK(response.cursor.body_length_has_digit == IRFQ_INFINITE_NO_V2);
  };

  const std::string oneByteBegin = "8=X\0019=0\00110=000\001";
  CHECK(scan(oneByteBegin, {}).first == IRFQ_INFINITE_STATUS_FRAME_READY_V2);
  const std::string maximumBegin = "8=1234567890123456\0019=0\00110=000\001";
  CHECK(scan(maximumBegin, {}).first == IRFQ_INFINITE_STATUS_FRAME_READY_V2);

  const std::string maximumPrefix = "8=1234567890123456";
  const auto maximumPartial = scan(maximumPrefix, {});
  REQUIRE(maximumPartial.first == IRFQ_INFINITE_STATUS_NEED_MORE_V2);
  const auto overlong = scan(maximumPrefix + "7", maximumPartial.second.cursor);
  CHECK(overlong.first == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  checkZeroResult(overlong.second);

  FIX::resetInfiniteCompleteFrameBeginStringVisits();
  std::string cumulative = "8=";
  irfq_infinite_scan_cursor_v2 cursor{};
  for (std::size_t valueLength = 1; valueLength <= 17; ++valueLength) {
    cumulative.push_back('X');
    const auto result = scan(cumulative, cursor);
    if (valueLength <= 16) {
      REQUIRE(result.first == IRFQ_INFINITE_STATUS_NEED_MORE_V2);
      cursor = result.second.cursor;
    } else {
      CHECK(result.first == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      checkZeroResult(result.second);
    }
  }
  const auto visits = FIX::stopInfiniteCompleteFrameBeginStringVisits();
  CHECK(visits >= 17);
  CHECK(visits <= 17 * 17);

  const auto emptyBegin = scan("8=\0019=0\00110=000\001", {});
  CHECK(emptyBegin.first == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  checkZeroResult(emptyBegin.second);

  const std::string fragmentedFixt = "8=FIXT.1.1\0019=0\00110=000\001";
  const auto fixtPrefix = scan(fragmentedFixt.substr(0, 7), {});
  REQUIRE(fixtPrefix.first == IRFQ_INFINITE_STATUS_NEED_MORE_V2);
  CHECK(scan(fragmentedFixt, fixtPrefix.second.cursor).first == IRFQ_INFINITE_STATUS_FRAME_READY_V2);

  const std::string oversizedLength = "8=FIXT.1.1\0019=65537\001";
  for (std::size_t split = 0; split <= oversizedLength.size(); ++split) {
    INFO("split=" << split);
    const auto first = scan(oversizedLength.substr(0, split), {});
    if (split == oversizedLength.size()) {
      CHECK(first.first == IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
      checkZeroResult(first.second);
      continue;
    }
    REQUIRE(first.first == IRFQ_INFINITE_STATUS_NEED_MORE_V2);
    const auto completed = scan(oversizedLength, first.second.cursor);
    CHECK(completed.first == IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
    checkZeroResult(completed.second);
  }

  for (const std::string malformed : {
           "8=FIXT.1.1\0019=6553x\001",
           "8=FIXT.1.1\0019=999999999999999999999\001",
       }) {
    const auto result = scan(malformed, {});
    CHECK(result.first == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    checkZeroResult(result.second);
  }
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
      {1, 0, 0, IRFQ_INFINITE_SCAN_BODY_LENGTH_PREFIX_V2, IRFQ_INFINITE_NO_V2},
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
    "InfiniteFrameAdapterV2 production session accepts only the governed dictionary tuple",
    "[infinite][adapter][v2][profile][dictionaries]") {
  const auto create = [](const std::vector<std::uint8_t> &config, irfq_infinite_session_create_response_v2 &response) {
    irfq_infinite_session_create_request_v2 request{};
    init(request);
    request.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
    request.canonical_session_create_config = {config.data(), config.size()};
    request.session_epoch = 1;
    request.creation_tai_ns = 1;
    request.creation_utc_ns = 1;
    init(response);
    return irfq_infinite_session_create_v2(&request, &response);
  };

  const auto governed = governedDictionaryTuple();
  const auto governedProfile = profileWithDictionaries(governed);
  irfq_infinite_session_create_response_v2 response{};
#ifdef IRFQ_INFINITE_EMBED_DICTIONARIES
  REQUIRE(create(governedProfile, response) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(response.header.status == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(response.session != nullptr);
  CHECK(response.cache_epoch == 1);
  CHECK(response.cache_revision == 0);
  std::array<std::uint8_t, 32> closePayload{};
  closePayload.fill(0x51);
  irfq_infinite_prepare_request_v2 close{};
  init(close);
  close.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  close.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  close.event = IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2;
  close.expected_epoch = response.cache_epoch;
  close.expected_revision = response.cache_revision;
  close.now_tai_ns = 2;
  close.now_utc_ns = 2;
  close.payload = {closePayload.data(), closePayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          response.session,
          close,
          close.event_identity_sha256));
  PlanBuffers closeBuffers;
  auto closed = closeBuffers.response();
  REQUIRE(irfq_infinite_prepare_v2(response.session, &close, &closed) == IRFQ_INFINITE_STATUS_READY_V2);
  REQUIRE(closed.native_state.length == IRFQ_INFINITE_NATIVE_STATE_BYTES_V2);
  CHECK(irfq_infinite_destroy_v2(response.session) == IRFQ_INFINITE_STATUS_OK_V2);

  irfq_infinite_session_create_request_v2 restore{};
  init(restore);
  restore.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
  restore.canonical_session_create_config = {governedProfile.data(), governedProfile.size()};
  restore.session_epoch = closed.result_epoch;
  restore.cache_revision = closed.result_revision;
  restore.native_state = {closed.native_state.data, closed.native_state.length};
  init(response);
  REQUIRE(irfq_infinite_session_create_v2(&restore, &response) == IRFQ_INFINITE_STATUS_OK_V2);
  CHECK(response.header.status == IRFQ_INFINITE_STATUS_OK_V2);
  REQUIRE(response.session != nullptr);
  CHECK(response.cache_epoch == restore.session_epoch);
  CHECK(response.cache_revision == restore.cache_revision);
  CHECK(irfq_infinite_destroy_v2(response.session) == IRFQ_INFINITE_STATUS_OK_V2);
#else
  REQUIRE(create(governedProfile, response) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  CHECK(response.header.status == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  CHECK(response.session == nullptr);
  CHECK(response.cache_epoch == 0);
  CHECK(response.cache_revision == 0);
#endif

  std::array<TestDictionaryTuple, 5> rejected{};
  rejected.fill(governed);
  rejected[0].transportId += "-wrong";
  rejected[1].transportSha256[0] ^= 1;
  rejected[2].applicationId += "-wrong";
  rejected[3].applicationSha256[0] ^= 1;
  std::swap(rejected[4].transportId, rejected[4].applicationId);
  std::swap(rejected[4].transportSha256, rejected[4].applicationSha256);
  const std::array<const char *, 5> names{{
      "transport ID",
      "transport SHA-256",
      "application ID",
      "application SHA-256",
      "swapped tuple",
  }};
  for (std::size_t index = 0; index < rejected.size(); ++index) {
    DYNAMIC_SECTION(names[index]) {
      CHECK(create(profileWithDictionaries(rejected[index]), response) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
      CHECK(response.header.status == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
      CHECK(response.session == nullptr);
      CHECK(response.cache_epoch == 0);
      CHECK(response.cache_revision == 0);
    }
  }

  CHECK(
      create(profileWithDictionaries(governed, "INFINITE-RFQ-1.0.0-wrong"), response)
      == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  CHECK(response.header.status == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  CHECK(response.session == nullptr);
  CHECK(response.cache_epoch == 0);
  CHECK(response.cache_revision == 0);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 governed dictionaries enforce the production application boundary",
    "[infinite][adapter][v2][governed-dictionaries]") {
#ifdef IRFQ_INFINITE_EMBED_DICTIONARIES
  SECTION("transport Logon and admitted AJ reach their governed paths") {
    auto *session = governedProductionLoggedOnSession();
    REQUIRE(session != nullptr);
    const auto body = quoteResponseBody("RFQ-GOVERNED");
    InboundCall inbound(session, participantFrame("AJ", 2, body), 0x53, 3);
    PlanBuffers pendingBuffers;
    auto pending = pendingBuffers.response();
    const auto status = irfq_infinite_prepare_v2(session, &inbound.request, &pending);
    const std::string diagnostic(reinterpret_cast<const char *>(pending.output.data), pending.output.length);
    CAPTURE(status, diagnostic, pending.action_count);
    REQUIRE(status == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
    REQUIRE(pending.msg_type_length == 2);
    CHECK(std::equal(pending.msg_type, pending.msg_type + 2, "AJ"));

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
    PlanBuffers resultBuffers;
    auto result = resultBuffers.response();
    REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
    REQUIRE(result.action_count == 2);
    CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2);
    CHECK(result.actions[0].msg_type_length == 2);
    CHECK(std::equal(result.actions[0].msg_type, result.actions[0].msg_type + 2, "AJ"));
    CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
    CHECK(result.actions[1].disposition == IRFQ_INFINITE_DISPOSITION_PENDING_CORE_V2);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }

  SECTION("recognized future surfaces reach the Rust decision and explicit rejection") {
    FIX::DataDictionary sessionDictionary(FIX::TestSettings::pathForSpec("infinite-rfq-1.0.0/INFINITE-FIXT11"));
    FIX::DataDictionary applicationDictionary(
        FIX::TestSettings::pathForSpec("infinite-rfq-1.0.0/INFINITE-RFQ-1.0.0-EP299"));
    struct Recognized {
      const char *msgType;
      const char *body;
      const char *businessReference;
    };
    const std::array<Recognized, 4> recognized{{
        {"CW", "117=QUOTE-1\0011166=QUOTE-MSG-1\001131=REQUEST-1\0011865=0\001", "QUOTE-MSG-1"},
        {"J", "70=ALLOC-1\00171=0\001626=1\00154=1\00153=1\00175=20260830\001", "ALLOC-1"},
        {"AS", "755=REPORT-1\00171=0\001794=2\00187=0\00154=1\00153=1\0016=1\00175=20260830\001", "REPORT-1"},
        {"T", "777=SETTL-1\001160=0\00160=20231114-22:13:20.123456\001", "SETTL-1"},
    }};
    for (const auto &variant : recognized) {
      DYNAMIC_SECTION(variant.msgType) {
        auto *session = governedProductionLoggedOnSession();
        REQUIRE(session != nullptr);
        InboundCall inbound(session, participantFrame(variant.msgType, 2, variant.body), 0x54, 3);
        PlanBuffers pendingBuffers;
        auto pending = pendingBuffers.response();
        const auto status = irfq_infinite_prepare_v2(session, &inbound.request, &pending);
        const std::string diagnostic(reinterpret_cast<const char *>(pending.output.data), pending.output.length);
        CAPTURE(status, diagnostic, pending.action_count);
        REQUIRE(status == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
        REQUIRE(pending.msg_type_length == std::strlen(variant.msgType));
        CHECK(std::equal(pending.msg_type, pending.msg_type + pending.msg_type_length, variant.msgType));
        REQUIRE(pending.input_offset <= inbound.payload.size());
        REQUIRE(pending.input_length <= inbound.payload.size() - pending.input_offset);
        CHECK(pending.input_length == std::strlen(variant.body));
        CHECK(
            std::equal(
                inbound.payload.begin() + pending.input_offset,
                inbound.payload.begin() + pending.input_offset + pending.input_length,
                variant.body));

        irfq_infinite_resume_request_v2 resume{};
        init(resume);
        resume.prepare_id = pending.prepare_id;
        resume.step = pending.step;
        resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
        resume.subject_sequence = pending.subject_sequence;
        std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
        resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2;
        resume.input_source = pending.input_source;
        resume.input_item_index = pending.input_item_index;
        resume.input_source_bytes = {inbound.payload.data(), inbound.payload.size()};
        const std::string dispositionId = "GID.UNSUPPORTED." + std::string(variant.msgType);
        resume.gateway_inbound_disposition_id = slice(dispositionId);
        PlanBuffers resultBuffers;
        auto result = resultBuffers.response();
        REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
        const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
        CHECK(output.find("\00135=j\001") != std::string::npos);
        CHECK(output.find("\00145=") == std::string::npos);
        CHECK(output.find("\0011137=") == std::string::npos);
        CHECK(output.find("\001372=" + std::string(variant.msgType) + "\001") != std::string::npos);
        CHECK(output.find("\001379=" + std::string(variant.businessReference) + "\001") != std::string::npos);
        CHECK(output.find("\001380=3\001") != std::string::npos);
        CHECK(output.find("\00158=Application message is unsupported.\001") != std::string::npos);
        CHECK(output.find("\00120003=" + dispositionId + "\001") != std::string::npos);
        CHECK(output.find("\00120004=INF-1002\001") != std::string::npos);
        FIX::Message rendered(output, sessionDictionary, applicationDictionary, true);
        CHECK_NOTHROW(FIX::DataDictionary::validate(rendered, &sessionDictionary, &applicationDictionary));
        REQUIRE(result.action_count == 3);
        CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2);
        CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
        CHECK(result.actions[1].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
        CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
        CHECK(result.actions[2].output_class == IRFQ_INFINITE_OUTPUT_ORIGINAL_APPLICATION_V2);
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      }
    }
  }

  SECTION("validated application messages derive the closed BusinessReject reference table") {
    FIX::DataDictionaryProvider dictionaries;
    dictionaries.addTransportDataDictionary(
        FIX::BeginString("FIXT.1.1"),
        FIX::TestSettings::pathForSpec("infinite-rfq-1.0.0/INFINITE-FIXT11"));
    dictionaries.addApplicationDataDictionary(
        FIX::ApplVerID("10"),
        FIX::TestSettings::pathForSpec("infinite-rfq-1.0.0/INFINITE-RFQ-1.0.0-EP299"));
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
    struct ReferenceCase {
      const char *name;
      const char *msgType;
      const char *body;
      const char *expected;
    };
    const std::array<ReferenceCase, 34> cases{{
        {"j", "j", "372=AJ\001379=J-REF\001380=3\001", "J-REF"},
        {"j-none", "j", "372=AJ\001380=3\001", ""},
        {"AH", "AH", "644=AH-REF\001146=1\00155=[N/A]\00148=BTC/USD\00122=100\001", "AH-REF"},
        {"UAH0", "UAH0", "644=UAH-REF\00120003=OUT\00120006=1\001", "UAH-REF"},
        {"R", "R", "131=R-REF\001146=1\00155=BTC/USD\001", "R-REF"},
        {"S", "S", "117=QUOTE\0011166=S-REF\001", "S-REF"},
        {"S-none", "S", "117=QUOTE\001", ""},
        {"Z", "Z", "117=QUOTE\0011166=Z-REF\001298=5\001", "Z-REF"},
        {"Z-none", "Z", "117=QUOTE\001298=5\001", ""},
        {"AJ", "AJ", "693=RESP\001694=1\00111=AJ-REF\001", "AJ-REF"},
        {"AJ-none", "AJ", "693=RESP\001694=1\001", ""},
        {"EC", "EC", "2965=EC-REF\001263=0\00160=20231114-22:13:20.123456\001", "EC-REF"},
        {"AI-649", "AI", "649=AI-649\001131=AI-131\001117=AI-117\0011166=AI-1166\001693=AI-693\001", "AI-649"},
        {"AI-1166", "AI", "131=AI-131\001117=AI-117\0011166=AI-1166\001693=AI-693\001", "AI-1166"},
        {"AI-117", "AI", "131=AI-131\001117=AI-117\001693=AI-693\001", "AI-117"},
        {"AI-131", "AI", "131=AI-131\001693=AI-693\001", "AI-131"},
        {"AI-693", "AI", "693=AI-693\001", "AI-693"},
        {"AI-none", "AI", "58=NOREF\001", ""},
        {"AG", "AG", "131=AG-REF\001658=1\001146=1\00155=BTC/USD\001", "AG-REF"},
        {"8",
         "8",
         "37=ORDER\00111=CL\0011166=QUOTE-MSG\001693=RESP\00117=EXEC-REF\001150=F\00139=2\00154=1\001151=0\00114=1\001",
         "EXEC-REF"},
        {"AE-571", "AE", "571=AE-571\0011003=AE-1003\00117=AE-17\001552=1\00154=1\001", "AE-571"},
        {"AE-1003", "AE", "1003=AE-1003\00117=AE-17\001552=1\00154=1\001", "AE-1003"},
        {"AE-17", "AE", "17=AE-17\001552=1\00154=1\001", "AE-17"},
        {"AE-none", "AE", "552=1\00154=1\001", ""},
        {"AK",
         "AK",
         "664=AK-REF\001666=0\001773=2\001665=1\00160=20231114-22:13:20.123456\00175=20231114\00180=1\00154=1\00179="
         "ACC\0016=1\001381=1\001118=1\001862=1\001528=P\001",
         "AK-REF"},
        {"ED", "ED", "2965=ED-REF\0012966=2\001", "ED-REF"},
        {"EE",
         "EE",
         "2967=EE-REF\0012965=REQ\0012968=SETT/INFI/PEND\00160=20231114-22:13:20.123456\001664=CONF\001",
         "EE-REF"},
        {"CW-1166", "CW", "117=CW-117\0011166=CW-1166\001131=CW-131\0011865=0\001", "CW-1166"},
        {"CW-117", "CW", "117=CW-117\001131=CW-131\0011865=0\001", "CW-117"},
        {"CW-131", "CW", "131=CW-131\0011865=0\001", "CW-131"},
        {"CW-none", "CW", "1865=0\001", ""},
        {"J", "J", "70=J-REF\00171=0\001626=1\00154=1\00153=1\00175=20231114\001", "J-REF"},
        {"AS", "AS", "755=AS-REF\00171=0\001794=2\00187=0\00154=1\00153=1\0016=1\00175=20231114\001", "AS-REF"},
        {"T", "T", "777=T-REF\001160=0\00160=20231114-22:13:20.123456\001", "T-REF"},
    }};
    for (const auto &variant : cases) {
      DYNAMIC_SECTION(variant.name) {
        const auto classified = FIX::InfiniteSessionPlanner::inbound(
            "FIXT.1.1",
            "VENUE",
            "PARTICIPANT",
            30,
            2,
            2,
            INT64_C(1700000000123456000),
            INT64_C(1700000000123456001),
            INT64_C(1700000000123456000),
            INT64_C(1700000000123456000),
            UINT64_C(135),
            0,
            1,
            participantFrame(variant.msgType, 2, variant.body),
            dictionaries,
            profile);
        REQUIRE(classified.dictionaryValid);
        REQUIRE(classified.application);
        CHECK(classified.businessRejectRefId == variant.expected);
      }
    }
  }

  SECTION("unknown messages and reserved future tags fail dictionary validation") {
    const std::array<std::pair<const char *, const char *>, 2> unknown{{
        {"EF", "2967=REPORT-1\0012973=1\001"},
        {"EG", "2988=RISK-1\001"},
    }};
    for (const auto &variant : unknown) {
      DYNAMIC_SECTION(variant.first) {
        auto *session = governedProductionLoggedOnSession();
        REQUIRE(session != nullptr);
        InboundCall inbound(session, participantFrame(variant.first, 2, variant.second), 0x55, 3);
        PlanBuffers buffers;
        auto result = buffers.response();
        REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
        const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
        INFO(output);
        CHECK(output.find("\00135=3\001") != std::string::npos);
        CHECK(output.find("\001372=" + std::string(variant.first) + "\001") != std::string::npos);
        CHECK(output.find("\001373=11\001") != std::string::npos);
        CHECK(std::none_of(result.actions, result.actions + result.action_count, [](const auto &action) {
          return action.kind == IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2;
        }));
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      }
    }

    for (const auto tag : {20043, 20044, 20045}) {
      DYNAMIC_SECTION(tag) {
        auto *session = governedProductionLoggedOnSession();
        REQUIRE(session != nullptr);
        const auto body = quoteResponseBody("RFQ-RESERVED") + std::to_string(tag) + "=X\001";
        InboundCall inbound(session, participantFrame("AJ", 2, body), 0x56, 3);
        PlanBuffers buffers;
        auto result = buffers.response();
        REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
        const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
        CHECK(output.find("\00135=3\001") != std::string::npos);
        CHECK(output.find("\001371=" + std::to_string(tag) + "\001") != std::string::npos);
        CHECK(output.find("\001372=AJ\001") != std::string::npos);
        CHECK(output.find("\001373=0\001") != std::string::npos);
        CHECK(std::none_of(result.actions, result.actions + result.action_count, [](const auto &action) {
          return action.kind == IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2;
        }));
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      }
    }
  }
#else
  SUCCEED("governed dictionaries are intentionally unavailable in ordinary builds");
#endif
}

#ifdef IRFQ_INFINITE_EMBED_DICTIONARIES
TEST_CASE(
    "InfiniteFrameAdapterV2 reparses BusinessRejectRefID from the verified relocated source",
    "[infinite][adapter][v2][borrow]") {
  auto *session = governedProductionLoggedOnSession();
  REQUIRE(session != nullptr);
  const std::string participantReference = "PARTICIPANT-REJECT-REF";
  InboundCall inbound(
      session,
      participantFrame("j", 2, "372=AJ\001379=" + participantReference + "\001380=3\001"),
      0x57,
      3);
  PlanBuffers pendingBuffers;
  auto pending = pendingBuffers.response();
  REQUIRE(
      irfq_infinite_prepare_v2(session, &inbound.request, &pending)
      == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2);
  CHECK_FALSE(FIX::infiniteFrameAdapterStockNonconformanceSmokePendingPlanRetainsBusinessRejectRefId());

  auto relocated = inbound.payload;
  REQUIRE(relocated.data() != inbound.payload.data());
  FIX::scrubInfiniteFrameAdapterStockNonconformanceSmokePendingBusinessRejectRefId(session);
  std::fill(inbound.payload.begin(), inbound.payload.end(), std::uint8_t{0xa5});

  irfq_infinite_resume_request_v2 resume{};
  init(resume);
  resume.prepare_id = pending.prepare_id;
  resume.step = pending.step;
  resume.kind = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
  resume.subject_sequence = pending.subject_sequence;
  std::copy_n(pending.subject_sha256, 32, resume.subject_sha256);
  resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2;
  resume.input_source = pending.input_source;
  resume.input_item_index = pending.input_item_index;
  resume.input_source_bytes = {relocated.data(), relocated.size()};
  const std::string dispositionId = "GID.REBORROW";
  resume.gateway_inbound_disposition_id = slice(dispositionId);
  PlanBuffers resultBuffers;
  auto result = resultBuffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=j\001") != std::string::npos);
  CHECK(output.find("\001379=" + participantReference + "\001") != std::string::npos);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}
#endif

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
  const auto fullWeek = otherwiseValidUnavailableProfile(2, {2, 82800, 2, 82800, 2, 82800, 2, 82800});
  CHECK(createStatus(fullWeek) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  const auto fullWeekSession = otherwiseValidUnavailableProfile(2, {2, 82800, 2, 82800, 3, 0, 4, 0});
  CHECK(createStatus(fullWeekSession) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  const auto fullWeekLogon = otherwiseValidUnavailableProfile(2, {1, 32400, 5, 61200, 2, 82800, 2, 82800});
  CHECK(createStatus(fullWeekLogon) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);

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

  const auto sameDayConfig = otherwiseValidUnavailableProfile(2, {2, 72000, 2, 82800, 2, 72000, 2, 80000});
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> timerBaseline{};
  auto *timerSource = stockLoggedOnSession(sameDayConfig, 2, nullptr, &timerBaseline);
  REQUIRE(timerSource != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(timerSource) == IRFQ_INFINITE_STATUS_OK_V2);
  struct TimerResult {
    std::string output;
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state;
    std::vector<irfq_infinite_declarative_action_v2> actions;
  };
  const auto prepareTimer
      = [&timerBaseline, &sameDayConfig](
            std::int64_t now,
            std::uint32_t testRequestCount,
            const std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> *restored = nullptr) {
          constexpr std::int64_t epochCreation = INT64_C(1699902000000000000);
          auto state = restored == nullptr ? timerBaseline : *restored;
          if (restored == nullptr) {
            write64(state.data() + 64, epochCreation);
            write64(state.data() + 72, epochCreation);
            write64(state.data() + 96, now - INT64_C(30000000000));
            write64(state.data() + 104, now - INT64_C(30000000000));
            write64(state.data() + 112, now);
            write64(state.data() + 120, now);
          }
          write64(state.data() + 80, now);
          write64(state.data() + 88, now);
          write32(state.data() + 192, testRequestCount);
          const auto epoch = read64(state.data() + 48);
          const auto revision = read64(state.data() + 56);
          auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
              sameDayConfig.data(),
              sameDayConfig.size(),
              state.data(),
              state.size(),
              epoch,
              revision,
              0,
              0);
          REQUIRE(session != nullptr);
          std::array<std::uint8_t, 32> payload{};
          payload.fill(0x78);
          irfq_infinite_prepare_request_v2 request{};
          init(request);
          request.kind = IRFQ_INFINITE_PREPARE_RUST_TIMER_V2;
          request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
          request.event = IRFQ_INFINITE_EVENT_TIMER_TICK_V2;
          request.expected_epoch = epoch;
          request.expected_revision = revision;
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
          REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
          TimerResult prepared{
              std::string(reinterpret_cast<const char *>(result.output.data), result.output.length),
              {},
              {result.actions, result.actions + result.action_count}};
          std::copy_n(result.native_state.data, result.native_state.length, prepared.state.begin());
          CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
          return prepared;
        };

  const auto exactLogonEnd = prepareTimer(INT64_C(1700000000000000000), 0);
  CHECK(exactLogonEnd.output.find("\00135=0\001") != std::string::npos);
  CHECK(exactLogonEnd.actions.size() == 1);
  if (!exactLogonEnd.actions.empty()) {
    CHECK(exactLogonEnd.actions[0].msg_type[0] == '0');
  }

  const auto afterLogonEnd = prepareTimer(INT64_C(1700000000000000001), 1);
  CHECK(afterLogonEnd.output.find("\00135=5\001") != std::string::npos);
  CHECK(afterLogonEnd.output.find("\00135=0\001") == std::string::npos);
  CHECK(afterLogonEnd.output.find("\00135=1\001") == std::string::npos);
  CHECK(afterLogonEnd.actions.size() == 1);
  if (!afterLogonEnd.actions.empty()) {
    CHECK(afterLogonEnd.actions[0].msg_type[0] == '5');
  }
  CHECK((read64(afterLogonEnd.state.data() + 180) & UINT64_C(16)) != 0);
  CHECK((read64(afterLogonEnd.state.data() + 180) & UINT64_C(256)) == 0);
  CHECK(read32(afterLogonEnd.state.data() + 192) == 0);
  CHECK(read32(afterLogonEnd.state.data() + 308) == IRFQ_INFINITE_REASON_NONE_V2);

  const auto afterSentLogout = prepareTimer(INT64_C(1700000000000000002), 0, &afterLogonEnd.state);
  CHECK(afterSentLogout.output.empty());
  CHECK(afterSentLogout.actions.empty());
  CHECK(read64(afterSentLogout.state.data() + 132) == 3);
  CHECK((read64(afterSentLogout.state.data() + 180) & UINT64_C(16)) != 0);

  const auto exactSessionEnd = prepareTimer(INT64_C(1700002800000000000), 0);
  CHECK(exactSessionEnd.output.find("\00135=5\001") != std::string::npos);
  CHECK(read64(exactSessionEnd.state.data() + 132) == 3);

  const auto afterSessionEnd = prepareTimer(INT64_C(1700002800000000001), 0);
  CHECK(afterSessionEnd.output.empty());
  CHECK(afterSessionEnd.actions.empty());
  CHECK(read64(afterSessionEnd.state.data() + 132) == 2);
  CHECK(read64(afterSessionEnd.state.data() + 144) == 2);

  const auto laterOccurrence = prepareTimer(INT64_C(1700604000000000000), 0);
  CHECK(laterOccurrence.output.empty());
  CHECK(laterOccurrence.actions.empty());
  CHECK(read64(laterOccurrence.state.data() + 132) == 2);
  CHECK(read64(laterOccurrence.state.data() + 144) == 2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects ordinary Logon outside its contained weekly UTC window",
    "[infinite][adapter][v2][eligibility]") {
  struct Variant {
    const char *name;
    std::array<std::uint32_t, 8> schedule;
  };
  for (const auto &variant : std::array{
           Variant{"cross-weekday", {1, 72000, 2, 82800, 1, 72000, 2, 79200}},
           Variant{"same-weekday", {2, 72000, 2, 82800, 2, 72000, 2, 79200}},
           Variant{"fraction-after-end", {2, 72000, 2, 80000, 2, 72000, 2, 80000}}}) {
    DYNAMIC_SECTION(variant.name) {
      const auto config = otherwiseValidUnavailableProfile(2, variant.schedule);
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          nullptr,
          0,
          1,
          0,
          INT64_C(1700000000123456000),
          INT64_C(1700000000123456000));
      REQUIRE(session != nullptr);

      InboundCall inbound(
          session,
          participantFrame('A', 1, "98=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0xee);
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
      CHECK(result.output.length == 0);
      REQUIRE(result.action_count == 2);
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
      CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(result.actions[1].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
      CHECK(read64(result.native_state.data + 180) == (UINT64_C(1) | UINT64_C(256)));
      CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  SECTION("canonical occurrence and attached non-Logon") {
    struct EligibilityResult {
      std::string output;
      std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state;
      std::vector<irfq_infinite_declarative_action_v2> actions;
    };
    const auto prepareLogon = [&](const std::array<std::uint32_t, 8> &schedule, std::int64_t now) {
      const auto config = otherwiseValidUnavailableProfile(2, schedule);
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          nullptr,
          0,
          1,
          0,
          INT64_C(1699902000000000000),
          INT64_C(1699902000000000000));
      REQUIRE(session != nullptr);
      InboundCall inbound(
          session,
          participantFrame('A', 1, "98=0\001108=30\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0xed);
      inbound.request.expected_revision = 0;
      inbound.request.next_original_value = 1;
      inbound.request.now_tai_ns = now;
      inbound.request.now_utc_ns = now;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      EligibilityResult prepared{
          std::string(reinterpret_cast<const char *>(result.output.data), result.output.length),
          {},
          {result.actions, result.actions + result.action_count}};
      std::copy_n(result.native_state.data, result.native_state.length, prepared.state.begin());
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      return prepared;
    };

    const auto fullWeek = prepareLogon({2, 82800, 2, 82800, 2, 82800, 2, 82800}, INT64_C(1700000000123456001));
    CHECK(fullWeek.output.find("\00135=A\001") != std::string::npos);
    const auto exactLogonEnd = prepareLogon({2, 72000, 2, 82800, 2, 72000, 2, 80000}, INT64_C(1700000000000000000));
    CHECK(exactLogonEnd.output.find("\00135=A\001") != std::string::npos);
    const auto afterLogonEnd = prepareLogon({2, 72000, 2, 82800, 2, 72000, 2, 80000}, INT64_C(1700000000000000001));
    CHECK(afterLogonEnd.output.empty());
    REQUIRE(afterLogonEnd.actions.size() == 2);
    CHECK(afterLogonEnd.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
    CHECK(afterLogonEnd.actions[0].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
    const auto distinctOutside = prepareLogon({2, 72000, 2, 82800, 2, 72000, 2, 80000}, INT64_C(1700000001000000000));
    CHECK(distinctOutside.output.empty());
    REQUIRE(distinctOutside.actions.size() == 2);
    CHECK(distinctOutside.actions[0].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);

    const std::array<std::uint32_t, 8> attachedSchedule{{2, 72000, 2, 82800, 2, 72000, 2, 80000}};
    const auto attachedConfig = otherwiseValidUnavailableProfile(2, attachedSchedule);
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> attachedBaseline{};
    auto *attachedSource = stockLoggedOnSession(attachedConfig, 2, nullptr, &attachedBaseline);
    REQUIRE(attachedSource != nullptr);
    REQUIRE(irfq_infinite_destroy_v2(attachedSource) == IRFQ_INFINITE_STATUS_OK_V2);
    const auto prepareAttached = [&](std::int64_t now,
                                     const std::string &sendingTime,
                                     std::uint32_t testRequestCount,
                                     std::uint64_t sequence = 2) {
      constexpr std::int64_t epochCreation = INT64_C(1699902000000000000);
      auto state = attachedBaseline;
      write64(state.data() + 64, epochCreation);
      write64(state.data() + 72, epochCreation);
      write64(state.data() + 80, now);
      write64(state.data() + 88, now);
      write64(state.data() + 96, epochCreation);
      write64(state.data() + 104, epochCreation);
      write64(state.data() + 112, epochCreation);
      write64(state.data() + 120, epochCreation);
      write32(state.data() + 192, testRequestCount);
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          attachedConfig.data(),
          attachedConfig.size(),
          state.data(),
          state.size(),
          1,
          1,
          0,
          0);
      REQUIRE(session != nullptr);
      InboundCall inbound(
          session,
          finishFix(
              "35=0\00149=PARTICIPANT\00156=VENUE\00134=" + std::to_string(sequence) + "\00152=" + sendingTime
              + "\001369=1\001"),
          0xec);
      inbound.request.now_tai_ns = now;
      inbound.request.now_utc_ns = now;
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      EligibilityResult prepared{
          std::string(reinterpret_cast<const char *>(result.output.data), result.output.length),
          {},
          {result.actions, result.actions + result.action_count}};
      std::copy_n(result.native_state.data, result.native_state.length, prepared.state.begin());
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      return prepared;
    };

    const auto exactSessionEnd = prepareAttached(INT64_C(1700002800000000000), "20231114-23:00:00.000000", 0);
    REQUIRE_FALSE(exactSessionEnd.actions.empty());
    CHECK(exactSessionEnd.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);

    const auto crossDayConfig = otherwiseValidUnavailableProfile(2, {2, 72000, 3, 10800, 2, 72000, 3, 3600});
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> crossDayState{};
    auto *crossDaySource = stockLoggedOnSession(crossDayConfig, 2, nullptr, &crossDayState);
    REQUIRE(crossDaySource != nullptr);
    REQUIRE(irfq_infinite_destroy_v2(crossDaySource) == IRFQ_INFINITE_STATUS_OK_V2);
    write64(crossDayState.data() + 64, INT64_C(1699992000000000000));
    write64(crossDayState.data() + 72, INT64_C(1699992000000000000));
    write32(crossDayState.data() + 192, 1);
    auto *crossDaySession = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
        crossDayConfig.data(),
        crossDayConfig.size(),
        crossDayState.data(),
        crossDayState.size(),
        1,
        1,
        0,
        0);
    REQUIRE(crossDaySession != nullptr);
    InboundCall afterLogon(
        crossDaySession,
        finishFix("35=0\00149=PARTICIPANT\00156=VENUE\00134=2\00152=20231115-01:00:00.000001\001369=1\001"),
        0xeb);
    afterLogon.request.now_tai_ns = INT64_C(1700010000000001000);
    afterLogon.request.now_utc_ns = INT64_C(1700010000000001000);
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            crossDaySession,
            afterLogon.request,
            afterLogon.request.event_identity_sha256));
    PlanBuffers afterLogonBuffers;
    auto afterLogonResult = afterLogonBuffers.response();
    REQUIRE(
        irfq_infinite_prepare_v2(crossDaySession, &afterLogon.request, &afterLogonResult)
        == IRFQ_INFINITE_STATUS_READY_V2);
    CHECK(
        std::string(reinterpret_cast<const char *>(afterLogonResult.output.data), afterLogonResult.output.length)
            .find("\00135=5\001")
        != std::string::npos);
    REQUIRE(afterLogonResult.action_count == 2);
    CHECK(afterLogonResult.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
    CHECK(afterLogonResult.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
    CHECK(afterLogonResult.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
    CHECK(afterLogonResult.actions[1].msg_type[0] == '5');
    CHECK((read64(afterLogonResult.native_state.data + 180) & UINT64_C(16)) != 0);
    CHECK((read64(afterLogonResult.native_state.data + 180) & UINT64_C(256)) == 0);
    CHECK(read32(afterLogonResult.native_state.data + 192) == 0);
    CHECK(read32(afterLogonResult.native_state.data + 308) == IRFQ_INFINITE_REASON_NONE_V2);
    CHECK(irfq_infinite_destroy_v2(crossDaySession) == IRFQ_INFINITE_STATUS_OK_V2);

    const auto beforeSession = prepareAttached(INT64_C(1699991940000000000), "20231114-19:59:00.000000", 1);
    CHECK(beforeSession.output.empty());
    CHECK(beforeSession.actions.size() == 2);
    if (beforeSession.actions.size() == 2) {
      CHECK(beforeSession.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(beforeSession.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(beforeSession.actions[0].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
      CHECK(beforeSession.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(beforeSession.actions[1].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
    }
    CHECK(read64(beforeSession.state.data() + 132) == 2);
    CHECK(read64(beforeSession.state.data() + 144) == 2);
    CHECK(read64(beforeSession.state.data() + 152) == 1);
    CHECK(read64(beforeSession.state.data() + 160) == 0);
    CHECK(read64(beforeSession.state.data() + 180) == (UINT64_C(135) | UINT64_C(256)));
    CHECK(read32(beforeSession.state.data() + 188) == 30);
    CHECK(read32(beforeSession.state.data() + 192) == 0);
    CHECK(read32(beforeSession.state.data() + 220) == 0);
    CHECK(read32(beforeSession.state.data() + 284) == 10);
    CHECK(read32(beforeSession.state.data() + 288) == 10);
    CHECK(read32(beforeSession.state.data() + 308) == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
    CHECK(read64(beforeSession.state.data() + 96) == UINT64_C(1699902000000000000));
    CHECK(read64(beforeSession.state.data() + 104) == UINT64_C(1699902000000000000));
    CHECK(read64(beforeSession.state.data() + 112) == UINT64_C(1699991940000000000));
    CHECK(read64(beforeSession.state.data() + 120) == UINT64_C(1699991940000000000));

    for (const auto sequence : {UINT64_C(1), UINT64_C(3)}) {
      INFO("MsgSeqNum=" << sequence);
      const auto displaced = prepareAttached(INT64_C(1699991940000000000), "20231114-19:59:00.000000", 0, sequence);
      CHECK(displaced.output.empty());
      CHECK(displaced.actions.size() == 2);
      if (displaced.actions.size() == 2) {
        CHECK(displaced.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
        CHECK(displaced.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
        CHECK(displaced.actions[0].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
        CHECK(displaced.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
        CHECK(displaced.actions[1].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
      }
    }

    const auto laterOccurrence = prepareAttached(INT64_C(1700604000000000000), "20231121-22:00:00.000000", 0);
    CHECK(laterOccurrence.output.empty());
    CHECK(laterOccurrence.actions.size() == 2);
    if (laterOccurrence.actions.size() == 2) {
      CHECK(laterOccurrence.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(laterOccurrence.actions[0].reason_code == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
      CHECK(laterOccurrence.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
    }
    CHECK(read64(laterOccurrence.state.data() + 144) == 2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects contradictory canonical native-state templates",
    "[infinite][adapter][v2][native-state]") {
  const auto config = otherwiseValidUnavailableProfile();
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> attached{};
  auto *source = stockLoggedOnSession(config, 8, nullptr, &attached);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> detached{};
  source = detachedSenderSession(config, 8, 30, &detached);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);

  const auto restoreStatus = [&](const auto &state) {
    irfq_infinite_session_create_request_v2 request{};
    init(request);
    request.snapshot_codec_version = IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2;
    request.canonical_session_create_config = {config.data(), config.size()};
    request.session_epoch = read64(state.data() + 48);
    request.cache_revision = read64(state.data() + 56);
    request.native_state = {state.data(), state.size()};
    irfq_infinite_session_create_response_v2 response{};
    init(response);
    const auto status = irfq_infinite_session_create_v2(&request, &response);
    if (response.session != nullptr) {
      CHECK(irfq_infinite_destroy_v2(response.session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
    return status;
  };
  REQUIRE(restoreStatus(attached) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  REQUIRE(restoreStatus(detached) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);

  struct Mutation {
    const char *name;
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> bytes;
  };
  std::vector<Mutation> mutations;

  auto receivedLogonOnly = attached;
  write64(receivedLogonOnly.data() + 180, UINT64_C(1) | UINT64_C(2));
  write32(receivedLogonOnly.data() + 288, 0);
  mutations.push_back({"acceptor received Logon without sent Logon", receivedLogonOnly});

  auto unnegotiatedTargetVersion = detached;
  write32(unnegotiatedTargetVersion.data() + 288, 10);
  mutations.push_back({"unnegotiated target application version", unnegotiatedTargetVersion});

  auto sentAfterEvaluated = attached;
  write64(sentAfterEvaluated.data() + 96, read64(sentAfterEvaluated.data() + 80) + 1);
  write64(sentAfterEvaluated.data() + 104, read64(sentAfterEvaluated.data() + 88) + 1);
  mutations.push_back({"last sent after last evaluated", sentAfterEvaluated});

  auto detachedApplicationBlock = attached;
  write64(detachedApplicationBlock.data() + 180, UINT64_C(1));
  write32(detachedApplicationBlock.data() + 292, 3);
  write32(detachedApplicationBlock.data() + 296, IRFQ_INFINITE_APPLICATION_BLOCK_ORIGINAL_V2);
  write64(detachedApplicationBlock.data() + 300, 1);
  mutations.push_back({"detached application block with negotiated target version", detachedApplicationBlock});

  auto peerWithoutRecovery = attached;
  write32(peerWithoutRecovery.data() + 168, 1);
  write64(peerWithoutRecovery.data() + 172, 5);
  mutations.push_back({"peer 789 with recovery NONE", peerWithoutRecovery});

  auto oversizedTestRequestCount = attached;
  write32(oversizedTestRequestCount.data() + 192, static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1U);
  mutations.push_back({"TestRequest count above INT_MAX", oversizedTestRequestCount});

  auto direct = attached;
  write32(direct.data() + 220, 1);
  write32(direct.data() + 224, 3);
  write64(direct.data() + 228, 2);
  write64(direct.data() + 236, 5);
  write64(direct.data() + 244, 2);
  std::fill_n(direct.data() + 252, 32, std::uint8_t{0xd1});
  write32(direct.data() + 292, 1);
  write64(direct.data() + 300, 2);
  REQUIRE(restoreStatus(direct) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  auto detachedDirectWithContinuation = direct;
  write64(detachedDirectWithContinuation.data() + 180, UINT64_C(1));
  write32(detachedDirectWithContinuation.data() + 288, 0);
  mutations.push_back({"detached direct recovery with RESEND continuation", detachedDirectWithContinuation});

  auto directResponseBarrier = direct;
  write32(directResponseBarrier.data() + 292, 0);
  write64(directResponseBarrier.data() + 300, 0);
  REQUIRE(restoreStatus(directResponseBarrier) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  write64(directResponseBarrier.data() + 132, 1);
  mutations.push_back({"direct response barrier before a Logon response sequence", directResponseBarrier});

  auto peerPrefix = attached;
  write32(peerPrefix.data() + 168, 1);
  write64(peerPrefix.data() + 172, 5);
  write64(peerPrefix.data() + 180, UINT64_C(1));
  write32(peerPrefix.data() + 220, 2);
  write32(peerPrefix.data() + 224, 1);
  write64(peerPrefix.data() + 228, 2);
  write64(peerPrefix.data() + 236, 5);
  write64(peerPrefix.data() + 244, 2);
  std::fill_n(peerPrefix.data() + 252, 32, std::uint8_t{0xd2});
  write32(peerPrefix.data() + 288, 0);
  write32(peerPrefix.data() + 292, 1);
  write64(peerPrefix.data() + 300, 2);
  REQUIRE(restoreStatus(peerPrefix) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  auto unknownRecoveryKind = peerPrefix;
  write32(unknownRecoveryKind.data() + 220, 3);
  mutations.push_back({"unknown recovery kind", unknownRecoveryKind});
  auto attachedPeerPrefix = peerPrefix;
  write64(attachedPeerPrefix.data() + 180, UINT64_C(135));
  write32(attachedPeerPrefix.data() + 288, 10);
  mutations.push_back({"peer-prefix with negotiated flags", attachedPeerPrefix});

  auto logonResponse = attached;
  write32(logonResponse.data() + 168, 1);
  write64(logonResponse.data() + 172, 5);
  write32(logonResponse.data() + 220, 2);
  write32(logonResponse.data() + 224, 2);
  write64(logonResponse.data() + 228, 7);
  write64(logonResponse.data() + 236, 8);
  write64(logonResponse.data() + 244, 7);
  std::fill_n(logonResponse.data() + 252, 32, std::uint8_t{0xd3});
  REQUIRE(restoreStatus(logonResponse) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  auto logonResponseWithContinuation = logonResponse;
  write32(logonResponseWithContinuation.data() + 292, 1);
  write64(logonResponseWithContinuation.data() + 300, 7);
  mutations.push_back({"Logon-response barrier with continuation", logonResponseWithContinuation});

  auto logonStoredRange = attached;
  write32(logonStoredRange.data() + 168, 1);
  write64(logonStoredRange.data() + 172, 5);
  write32(logonStoredRange.data() + 220, 2);
  write32(logonStoredRange.data() + 224, 3);
  write64(logonStoredRange.data() + 228, 5);
  write64(logonStoredRange.data() + 236, 7);
  write64(logonStoredRange.data() + 244, 5);
  std::fill_n(logonStoredRange.data() + 252, 32, std::uint8_t{0xd4});
  write32(logonStoredRange.data() + 292, 1);
  write64(logonStoredRange.data() + 300, 5);
  REQUIRE(restoreStatus(logonStoredRange) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  write64(logonStoredRange.data() + 228, 6);
  write64(logonStoredRange.data() + 244, 6);
  write64(logonStoredRange.data() + 300, 6);
  mutations.push_back({"Logon stored range not beginning at peer 789", logonStoredRange});

  auto finalGapFill = attached;
  write32(finalGapFill.data() + 168, 1);
  write64(finalGapFill.data() + 172, 5);
  write32(finalGapFill.data() + 220, 2);
  write32(finalGapFill.data() + 224, 4);
  write64(finalGapFill.data() + 228, 7);
  write64(finalGapFill.data() + 236, 8);
  write64(finalGapFill.data() + 244, 7);
  std::fill_n(finalGapFill.data() + 252, 32, std::uint8_t{0xd5});
  write32(finalGapFill.data() + 292, 1);
  write64(finalGapFill.data() + 300, 7);
  REQUIRE(restoreStatus(finalGapFill) == IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  auto finalGapFillAtPeer = finalGapFill;
  write64(finalGapFillAtPeer.data() + 172, 7);
  mutations.push_back({"final GapFill with peer 789 equal to frozen sender", finalGapFillAtPeer});
  write64(finalGapFill.data() + 172, 8);
  mutations.push_back({"final GapFill with peer 789 beyond frozen sender", finalGapFill});

  for (const auto &mutation : mutations) {
    CAPTURE(mutation.name);
    CHECK(restoreStatus(mutation.bytes) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 bounds the native TestRequest counter before integer narrowing and increment",
    "[infinite][adapter][v2][native-state]") {
  constexpr auto maximum = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
  constexpr auto oversized = maximum + 1U;
  const auto config = otherwiseValidUnavailableProfile();
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> baseline{};
  auto *source = stockLoggedOnSession(config, 2, nullptr, &baseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);

  const auto prepareTestRequest = [&](std::uint32_t count, PlanBuffers &buffers) {
    auto state = baseline;
    write32(state.data() + 192, count);
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
    payload.fill(0xef);
    irfq_infinite_prepare_request_v2 request{};
    init(request);
    request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
    request.event = IRFQ_INFINITE_EVENT_ADMIN_TEST_REQUEST_V2;
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
    auto result = buffers.response();
    const auto status = irfq_infinite_prepare_v2(session, &request, &result);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    return std::pair{status, result};
  };

  PlanBuffers lastBuffers;
  const auto [lastStatus, last] = prepareTestRequest(maximum - 1U, lastBuffers);
  REQUIRE(lastStatus == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(read32(last.native_state.data + 192) == maximum);

  PlanBuffers exhaustedBuffers;
  const auto [exhaustedStatus, exhausted] = prepareTestRequest(maximum, exhaustedBuffers);
  CHECK(exhaustedStatus == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(exhausted.prepare_id.low == 0);

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
  CHECK_THROWS_AS(
      FIX::InfiniteSessionPlanner::inbound(
          "FIXT.1.1",
          "VENUE",
          "PARTICIPANT",
          30,
          2,
          2,
          INT64_C(1700000000123456000),
          INT64_C(1700000000123456001),
          INT64_C(1700000000123456000),
          INT64_C(1700000000123456000),
          UINT64_C(135),
          oversized,
          1,
          participantFrame('0', 2),
          dictionaries,
          profile),
      std::invalid_argument);
  CHECK_THROWS_AS(
      FIX::InfiniteSessionPlanner::timer(
          "FIXT.1.1",
          "VENUE",
          "PARTICIPANT",
          30,
          2,
          2,
          INT64_C(1700000000123456001),
          INT64_C(1700000000123456001),
          INT64_C(1700000000123456000),
          INT64_C(1700000000123456000),
          UINT64_C(135),
          oversized,
          10,
          10,
          1,
          &dictionaries,
          &profile),
      std::invalid_argument);

  auto terminalState = baseline;
  write32(terminalState.data() + 192, 1);
  auto *logoutSession = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      terminalState.data(),
      terminalState.size(),
      1,
      1,
      0,
      0);
  REQUIRE(logoutSession != nullptr);
  std::array<std::uint8_t, 36> logoutPayload{};
  std::fill_n(logoutPayload.begin(), 32, std::uint8_t{0xf0});
  write32(logoutPayload.data() + 32, IRFQ_INFINITE_REASON_SESSION_TIME_V2);
  irfq_infinite_prepare_request_v2 logoutRequest{};
  init(logoutRequest);
  logoutRequest.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  logoutRequest.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  logoutRequest.event = IRFQ_INFINITE_EVENT_ADMIN_LOGOUT_V2;
  logoutRequest.expected_epoch = 1;
  logoutRequest.expected_revision = 1;
  logoutRequest.now_tai_ns = INT64_C(1700000000123456001);
  logoutRequest.now_utc_ns = INT64_C(1700000000123456001);
  logoutRequest.payload = {logoutPayload.data(), logoutPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          logoutSession,
          logoutRequest,
          logoutRequest.event_identity_sha256));
  PlanBuffers logoutBuffers;
  auto logoutResult = logoutBuffers.response();
  const auto logoutStatus = irfq_infinite_prepare_v2(logoutSession, &logoutRequest, &logoutResult);
  CHECK(logoutStatus == IRFQ_INFINITE_STATUS_READY_V2);
  if (logoutStatus == IRFQ_INFINITE_STATUS_READY_V2) {
    const std::string logoutOutput(
        reinterpret_cast<const char *>(logoutResult.output.data),
        logoutResult.output.length);
    CHECK(logoutOutput.find("\00135=5\001") != std::string::npos);
    REQUIRE(logoutResult.action_count == 2);
    CHECK(logoutResult.actions[0].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
    CHECK(logoutResult.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
    CHECK((read64(logoutResult.native_state.data + 180) & UINT64_C(16)) != 0);
    CHECK((read64(logoutResult.native_state.data + 180) & UINT64_C(256)) != 0);
    CHECK(read32(logoutResult.native_state.data + 192) == 0);
    CHECK(read32(logoutResult.native_state.data + 308) == IRFQ_INFINITE_REASON_SESSION_TIME_V2);
  }
  CHECK(irfq_infinite_destroy_v2(logoutSession) == IRFQ_INFINITE_STATUS_OK_V2);

  auto *timerSession = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      terminalState.data(),
      terminalState.size(),
      1,
      1,
      0,
      0);
  REQUIRE(timerSession != nullptr);
  std::array<std::uint8_t, 32> timerPayload{};
  timerPayload.fill(0xf1);
  irfq_infinite_prepare_request_v2 timerRequest{};
  init(timerRequest);
  timerRequest.kind = IRFQ_INFINITE_PREPARE_RUST_TIMER_V2;
  timerRequest.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  timerRequest.event = IRFQ_INFINITE_EVENT_TIMER_TICK_V2;
  timerRequest.expected_epoch = 1;
  timerRequest.expected_revision = 1;
  timerRequest.now_tai_ns = INT64_C(1700000100123456000);
  timerRequest.now_utc_ns = INT64_C(1700000100123456000);
  timerRequest.payload = {timerPayload.data(), timerPayload.size()};
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          timerSession,
          timerRequest,
          timerRequest.event_identity_sha256));
  PlanBuffers timerBuffers;
  auto timerResult = timerBuffers.response();
  const auto timerStatus = irfq_infinite_prepare_v2(timerSession, &timerRequest, &timerResult);
  CHECK(timerStatus == IRFQ_INFINITE_STATUS_READY_V2);
  if (timerStatus == IRFQ_INFINITE_STATUS_READY_V2) {
    CHECK(timerResult.output.length == 0);
    REQUIRE(timerResult.action_count == 1);
    CHECK(timerResult.actions[0].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
    CHECK(timerResult.actions[0].reason_code == IRFQ_INFINITE_REASON_HEARTBEAT_TIMEOUT_V2);
    CHECK((read64(timerResult.native_state.data + 180) & UINT64_C(256)) != 0);
    CHECK(read32(timerResult.native_state.data + 192) == 0);
    CHECK(read32(timerResult.native_state.data + 308) == IRFQ_INFINITE_REASON_HEARTBEAT_TIMEOUT_V2);
    CHECK(read64(timerResult.native_state.data + 132) == 2);
    CHECK(read64(timerResult.native_state.data + 144) == 2);
  }
  CHECK(irfq_infinite_destroy_v2(timerSession) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 clears restored TestRequest state for inbound terminal classifications",
    "[infinite][adapter][v2][native-state][inbound-terminal]") {
  const auto config = otherwiseValidUnavailableProfile();
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> baseline{};
  auto *source = stockLoggedOnSession(config, 2, nullptr, &baseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);

  struct Variant {
    const char *name;
    std::string wire;
    std::uint32_t reason;
    bool emitsLogout;
  };
  const std::array variants{
      Variant{
          "missing MsgSeqNum disconnect",
          finishFix("35=0\00149=PARTICIPANT\00156=VENUE\00152=20231114-22:13:20.123456\001"),
          IRFQ_INFINITE_REASON_PROTOCOL_V2,
          false},
      Variant{
          "valid-header identity Logout",
          finishFix("35=0\00149=EVIL\00156=VENUE\00134=2\00152=20231114-22:13:20.123456\001369=1\001"),
          IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2,
          true},
  };

  for (const auto &variant : variants) {
    DYNAMIC_SECTION(variant.name) {
      auto state = baseline;
      write32(state.data() + 192, 1);
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
      InboundCall inbound(session, variant.wire, 0xf2);
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      CHECK((read64(result.native_state.data + 180) & UINT64_C(256)) != 0);
      CHECK(read32(result.native_state.data + 192) == 0);
      CHECK(read32(result.native_state.data + 308) == variant.reason);
      REQUIRE(result.action_count > 0);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(result.actions[result.action_count - 1].reason_code == variant.reason);
      if (variant.emitsLogout) {
        const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
        CHECK(output.find("\00135=5\001") != std::string::npos);
        CHECK((read64(result.native_state.data + 180) & UINT64_C(16)) != 0);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
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
    "InfiniteFrameAdapterV2 scrubs real plan releases and native state before deallocation",
    "[infinite][adapter][v2][scrub]") {
  const auto config = otherwiseValidUnavailableProfile();
  const auto create = [&] {
    return FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
        config.data(),
        config.size(),
        nullptr,
        0,
        1,
        0,
        1,
        1);
  };
  const auto prepare = [](irfq_infinite_session_v2 *session, PlanBuffers &buffers) {
    std::array<std::uint8_t, 32> payload{};
    payload.fill(0x5a);
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
    auto result = buffers.response();
    REQUIRE(irfq_infinite_prepare_v2(session, &request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
    return result;
  };
  const std::array<std::uint32_t, 4> oneZeroPlan{1, 1, 0, 0};
  const std::array<std::uint32_t, 4> oneZeroPlanAndSession{1, 1, 1, 1};

  SECTION("apply") {
    auto *session = create();
    REQUIRE(session != nullptr);
    PlanBuffers buffers;
    const auto pending = prepare(session, buffers);
    irfq_infinite_apply_committed_request_v2 apply{};
    init(apply);
    apply.prepare_id = pending.prepare_id;
    apply.result_revision = pending.result_revision;
    std::copy_n(pending.native_state_sha256, 32, apply.native_state_sha256);
    irfq_infinite_operation_response_v2 response{};
    init(response);
    FIX::resetInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations();
    CHECK(irfq_infinite_apply_committed_v2(session, &apply, &response) == IRFQ_INFINITE_STATUS_OK_V2);
    CHECK(FIX::infiniteFrameAdapterStockNonconformanceSmokeScrubObservations() == oneZeroPlan);
    FIX::stopInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations();
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }

  SECTION("abort") {
    auto *session = create();
    REQUIRE(session != nullptr);
    PlanBuffers buffers;
    const auto pending = prepare(session, buffers);
    irfq_infinite_abort_request_v2 abort{};
    init(abort);
    abort.prepare_id = pending.prepare_id;
    irfq_infinite_operation_response_v2 response{};
    init(response);
    FIX::resetInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations();
    CHECK(irfq_infinite_abort_v2(session, &abort, &response) == IRFQ_INFINITE_STATUS_OK_V2);
    CHECK(FIX::infiniteFrameAdapterStockNonconformanceSmokeScrubObservations() == oneZeroPlan);
    FIX::stopInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations();
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }

  SECTION("terminal resume failure") {
    auto *session = create();
    REQUIRE(session != nullptr);
    PlanBuffers pendingBuffers;
    const auto pending = prepare(session, pendingBuffers);
    irfq_infinite_resume_request_v2 resume{};
    init(resume);
    resume.prepare_id = pending.prepare_id;
    resume.step = pending.step;
    resume.kind = IRFQ_INFINITE_RESUME_OUTPUT_V2;
    PlanBuffers resultBuffers;
    auto result = resultBuffers.response();
    FIX::resetInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations();
    CHECK(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    CHECK(FIX::infiniteFrameAdapterStockNonconformanceSmokeScrubObservations() == oneZeroPlan);
    FIX::stopInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations();
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }

  SECTION("destroy with pending plan") {
    auto *session = create();
    REQUIRE(session != nullptr);
    PlanBuffers buffers;
    prepare(session, buffers);
    FIX::resetInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations();
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    CHECK(FIX::infiniteFrameAdapterStockNonconformanceSmokeScrubObservations() == oneZeroPlanAndSession);
    FIX::stopInfiniteFrameAdapterStockNonconformanceSmokeScrubObservations();
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes a handle after an unexpected prepare exception",
    "[infinite][adapter][v2][task2d][prepare][unexpected-exception]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  FIX::forceInfiniteFrameAdapterStockNonconformanceSmokeNextPlanOverflow(session);

  std::array<std::uint8_t, 36> payload{};
  payload.fill(0xe1);
  write32(payload.data() + 32, 0);
  irfq_infinite_prepare_request_v2 request{};
  init(request);
  request.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  request.stage = IRFQ_INFINITE_STAGE_EVENT_V2;
  request.event = IRFQ_INFINITE_EVENT_ADMIN_HEARTBEAT_V2;
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
  PlanBuffers firstBuffers;
  auto first = firstBuffers.response();
  CHECK(irfq_infinite_prepare_v2(session, &request, &first) == IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  PlanBuffers secondBuffers;
  auto second = secondBuffers.response();
  CHECK(irfq_infinite_prepare_v2(session, &request, &second) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
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
  const auto finalTargetLogon = FIX::InfiniteSessionPlanner::logon(
      "FIXT.1.1",
      "VENUE",
      "PARTICIPANT",
      30,
      1,
      lastLegal,
      INT64_C(1700000000123456000));
  CHECK(finalTargetLogon.output.find("\00135=A\001") != std::string::npos);
  CHECK(finalTargetLogon.nextSenderSequence == 2);
  CHECK(finalTargetLogon.nextTargetSequence == bound);

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
          INT64_C(1700000000123456000),
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
          INT64_C(1700000000123456000),
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
      INT64_C(1700000000123456000),
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
    "InfiniteFrameAdapterV2 terminalizes recovery-none Logon at the final target",
    "[infinite][adapter][v2][stock-smoke][inbound-logon][sequence-boundary][task2d][final-target-matrix]") {
  constexpr auto bound = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2);
  constexpr auto lastLegal = bound - 1;
  const auto fixedConfig = otherwiseValidUnavailableProfile();
  const auto peerConfig = otherwiseValidUnavailableProfile(1, {}, 2, 0, 20, 40);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> fixedBaseline{};
  auto *source = detachedSenderSession(fixedConfig, 7, 30, &fixedBaseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> peerBaseline{};
  source = detachedSenderSession(peerConfig, 7, 35, &peerBaseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);

  struct Variant {
    const char *name;
    bool senderExhausted;
    int peerRelation;
    bool peerHeartbeat;
  };
  const std::array variants{
      Variant{"sender-value-peer-absent", false, 0, false},
      Variant{"sender-value-peer-equal", false, 1, false},
      Variant{"sender-value-peer-lower", false, -1, false},
      Variant{"sender-exhausted-peer-absent", true, 0, false},
      Variant{"sender-exhausted-peer-equal", true, 1, false},
      Variant{"sender-exhausted-peer-lower", true, -1, false},
      Variant{"heartbeat-range-sender-value", false, 0, true},
      Variant{"heartbeat-range-sender-exhausted", true, 0, true}};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION(variant.name) {
      const auto &config = variant.peerHeartbeat ? peerConfig : fixedConfig;
      auto state = variant.peerHeartbeat ? peerBaseline : fixedBaseline;
      write32(
          state.data() + 128,
          variant.senderExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2);
      write64(state.data() + 132, variant.senderExhausted ? 0 : 7);
      write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
      write64(state.data() + 144, lastLegal);
      write64(state.data() + 152, lastLegal - 1);
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
      const auto sender = variant.senderExhausted ? lastLegal : UINT64_C(7);
      auto fields = std::string("369=") + std::to_string(lastLegal - 1)
                    + "\00198=0\001108=" + (variant.peerHeartbeat ? "35\001" : "30\001");
      if (variant.peerRelation != 0) {
        fields += "789=" + std::to_string(sender - (variant.peerRelation < 0 ? 1 : 0)) + "\001";
      }
      fields += "1137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
      InboundCall inbound(session, participantFrame('A', lastLegal, fields), 0xb8);
      inbound.request.expected_revision = 2;
      inbound.request.next_original_state
          = variant.senderExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2;
      inbound.request.next_original_value = variant.senderExhausted ? 0 : 7;
      inbound.request.now_tai_ns = INT64_C(1700000000123456002);
      inbound.request.now_utc_ns = INT64_C(1700000000123456002);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      REQUIRE(result.action_count == (variant.senderExhausted ? 2 : 3));
      CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(result.actions[result.action_count - 1].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      CHECK(read32(result.native_state.data + 140) == IRFQ_INFINITE_SEQUENCE_VALUE_V2);
      CHECK(read64(result.native_state.data + 144) == lastLegal);
      CHECK(read64(result.native_state.data + 152) == lastLegal - 1);
      CHECK(read64(result.native_state.data + 180) == (UINT64_C(1) | UINT64_C(256)));
      if (variant.peerHeartbeat) {
        CHECK(read32(result.native_state.data + 188) == 0);
      }
      CHECK(read32(result.native_state.data + 220) == 0);
      CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      if (variant.senderExhausted) {
        CHECK(result.output.length == 0);
        CHECK(result.output_frame_count == 0);
        CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
        CHECK(read64(result.native_state.data + 132) == 0);
        CHECK(read64(result.native_state.data + 96) == read64(state.data() + 96));
        CHECK(read64(result.native_state.data + 104) == read64(state.data() + 104));
      } else {
        REQUIRE(result.output_frame_count == 1);
        CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
        CHECK(result.actions[1].msg_type_length == 1);
        CHECK(result.actions[1].msg_type[0] == '5');
        const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
        FIX::Message logout(output, true);
        FIX::MsgType msgType;
        FIX::MsgSeqNum sequence;
        logout.getHeader().getField(msgType);
        logout.getHeader().getField(sequence);
        CHECK(msgType == FIX::MsgType_Logout);
        CHECK(sequence == 7);
        CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_VALUE_V2);
        CHECK(read64(result.native_state.data + 132) == 8);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }

  DYNAMIC_SECTION("heartbeat-range-detached-non-logon-invalid") {
    auto state = peerBaseline;
    write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
    write64(state.data() + 144, lastLegal);
    write64(state.data() + 152, lastLegal - 1);
    auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
        peerConfig.data(),
        peerConfig.size(),
        state.data(),
        state.size(),
        1,
        2,
        0,
        0);
    REQUIRE(session != nullptr);
    InboundCall inbound(
        session,
        participantFrame(
            '4',
            lastLegal,
            "369=" + std::to_string(lastLegal - 1) + "\00136=" + std::to_string(lastLegal) + "\001123=N\001"),
        0xbd);
    inbound.request.expected_revision = 2;
    inbound.request.now_tai_ns = INT64_C(1700000000123456002);
    inbound.request.now_utc_ns = INT64_C(1700000000123456002);
    REQUIRE(
        FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
            session,
            inbound.request,
            inbound.request.event_identity_sha256));
    PlanBuffers buffers;
    auto result = buffers.response();
    CHECK(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    CHECK(result.prepare_id.low == 0);
    CHECK(result.native_state.length == 0);
    CHECK(result.output.length == 0);
    CHECK(result.action_count == 0);
    CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes exact direct recovery Logon shapes at the final target",
    "[infinite][adapter][v2][task2d][final-target-matrix][direct]") {
  constexpr auto bound = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2);
  constexpr auto lastLegal = bound - 1;
  const auto fixedConfig = otherwiseValidUnavailableProfile();
  const auto peerConfig = otherwiseValidUnavailableProfile(1, {}, 2, 0, 20, 40);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> fixedBaseline{};
  auto *source = detachedResendRecoverySession(fixedConfig, 2, 5, 2, 7, &fixedBaseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> peerBaseline{};
  source = detachedSenderSession(peerConfig, 7, 35, &peerBaseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  write64(peerBaseline.data() + 56, 4);
  std::copy(fixedBaseline.begin() + 220, fixedBaseline.begin() + 284, peerBaseline.begin() + 220);

  struct Variant {
    const char *name;
    bool senderExhausted;
    bool peerPresent;
    std::uint64_t original;
    std::uint64_t peer;
    bool peerHeartbeat;
  };
  const std::array variants{
      Variant{"initial-peer-absent", false, false, 7, 0, false},
      Variant{"initial-peer-equal", false, true, 7, 7, false},
      Variant{"lost-response", false, true, 6, 6, false},
      Variant{"exhausted-lost-response", true, true, lastLegal, lastLegal, false},
      Variant{"heartbeat-range-sender-value", false, false, 7, 0, true},
      Variant{"heartbeat-range-sender-exhausted", true, true, lastLegal, lastLegal, true}};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION(variant.name) {
      const auto &config = variant.peerHeartbeat ? peerConfig : fixedConfig;
      auto state = variant.peerHeartbeat ? peerBaseline : fixedBaseline;
      write32(
          state.data() + 128,
          variant.senderExhausted ? IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 : IRFQ_INFINITE_SEQUENCE_VALUE_V2);
      write64(state.data() + 132, variant.senderExhausted ? 0 : 7);
      write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
      write64(state.data() + 144, lastLegal);
      write64(state.data() + 152, lastLegal - 1);
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          state.data(),
          state.size(),
          1,
          4,
          0,
          0);
      REQUIRE(session != nullptr);
      auto fields = std::string("369=") + std::to_string(lastLegal - 1)
                    + "\00198=0\001108=" + (variant.peerHeartbeat ? "35\001" : "30\001");
      if (variant.peerPresent) {
        fields += "789=" + std::to_string(variant.peer) + "\001";
      }
      fields += "1137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
      InboundCall inbound(session, participantFrame('A', lastLegal, fields), 0xb9);
      inbound.request.expected_revision = 4;
      inbound.request.next_original_state = IRFQ_INFINITE_SEQUENCE_VALUE_V2;
      inbound.request.next_original_value = variant.original;
      inbound.request.now_tai_ns = INT64_C(1700000000123456004);
      inbound.request.now_utc_ns = INT64_C(1700000000123456004);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
      REQUIRE(result.action_count == (variant.senderExhausted ? 2 : 3));
      CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
      CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      CHECK(result.actions[result.action_count - 1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
      CHECK(result.actions[result.action_count - 1].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
      CHECK(read32(result.native_state.data + 140) == IRFQ_INFINITE_SEQUENCE_VALUE_V2);
      CHECK(read64(result.native_state.data + 144) == lastLegal);
      CHECK(read64(result.native_state.data + 152) == lastLegal - 1);
      CHECK(read64(result.native_state.data + 180) == read64(state.data() + 180));
      CHECK(read32(result.native_state.data + 188) == read32(state.data() + 188));
      if (variant.peerHeartbeat) {
        CHECK(read32(result.native_state.data + 188) == 0);
      }
      CHECK(std::equal(state.begin() + 220, state.begin() + 284, result.native_state.data + 220));
      CHECK(read32(result.native_state.data + 288) == read32(state.data() + 288));
      CHECK(read32(result.native_state.data + 292) == read32(state.data() + 292));
      CHECK(read32(result.native_state.data + 296) == read32(state.data() + 296));
      CHECK(read64(result.native_state.data + 300) == read64(state.data() + 300));
      CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_NONE_V2);
      if (variant.senderExhausted) {
        CHECK(result.output.length == 0);
        CHECK(result.output_frame_count == 0);
        CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
        CHECK(read64(result.native_state.data + 132) == 0);
        CHECK(read64(result.native_state.data + 96) == read64(state.data() + 96));
        CHECK(read64(result.native_state.data + 104) == read64(state.data() + 104));
      } else {
        REQUIRE(result.output_frame_count == 1);
        CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
        CHECK(result.actions[1].msg_type_length == 1);
        CHECK(result.actions[1].msg_type[0] == '5');
        const std::string output(reinterpret_cast<const char *>(result.output.data), result.output.length);
        FIX::Message logout(output, true);
        FIX::MsgType msgType;
        FIX::MsgSeqNum sequence;
        logout.getHeader().getField(msgType);
        logout.getHeader().getField(sequence);
        CHECK(msgType == FIX::MsgType_Logout);
        CHECK(sequence == 7);
        CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_VALUE_V2);
        CHECK(read64(result.native_state.data + 132) == 8);
      }
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 rejects inexact direct recovery identities at the final target",
    "[infinite][adapter][v2][task2d][final-target-matrix][direct][invalid]") {
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  const auto config = otherwiseValidUnavailableProfile();
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> baseline{};
  auto *source = detachedResendRecoverySession(config, 2, 5, 2, 7, &baseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  write32(baseline.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(baseline.data() + 144, lastLegal);
  write64(baseline.data() + 152, lastLegal - 1);

  struct Variant {
    const char *name;
    std::uint64_t original;
    std::uint64_t peer;
    bool wrongCompId;
  };
  const std::array variants{
      Variant{"wrong-original", 8, 0, false},
      Variant{"wrong-789", 7, 6, false},
      Variant{"wrong-comp-id", 7, 0, true}};
  for (const auto &variant : variants) {
    DYNAMIC_SECTION(variant.name) {
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          config.data(),
          config.size(),
          baseline.data(),
          baseline.size(),
          1,
          4,
          0,
          0);
      REQUIRE(session != nullptr);
      auto fields = std::string("369=") + std::to_string(lastLegal - 1) + "\00198=0\001108=30\001";
      if (variant.peer != 0) {
        fields += "789=" + std::to_string(variant.peer) + "\001";
      }
      fields += "1137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
      const auto wire = variant.wrongCompId ? finishFix(
                                                  "35=A\00149=EVIL\00156=VENUE\00134=" + std::to_string(lastLegal)
                                                  + "\00152=20231114-22:13:20.123456\001" + fields)
                                            : participantFrame('A', lastLegal, fields);
      InboundCall inbound(session, wire, 0xbd);
      inbound.request.expected_revision = 4;
      inbound.request.next_original_value = variant.original;
      inbound.request.now_tai_ns = INT64_C(1700000000123456004);
      inbound.request.now_utc_ns = INT64_C(1700000000123456004);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      CHECK(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      CHECK(result.prepare_id.low == 0);
      CHECK(result.native_state.length == 0);
      CHECK(result.output.length == 0);
      CHECK(result.action_count == 0);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes every exact LOGON_789 phase at the final target",
    "[infinite][adapter][v2][task2d][final-target-matrix][logon-789]") {
  constexpr auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  const auto fixedConfig = otherwiseValidUnavailableProfile();
  const auto peerConfig = otherwiseValidUnavailableProfile(1, {}, 2, 0, 20, 40);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> fixedBaseline{};
  auto *source = detachedSenderSession(fixedConfig, 8, 30, &fixedBaseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> peerBaseline{};
  source = detachedSenderSession(peerConfig, 8, 35, &peerBaseline);
  REQUIRE(source != nullptr);
  REQUIRE(irfq_infinite_destroy_v2(source) == IRFQ_INFINITE_STATUS_OK_V2);

  const auto phaseState = [&](const auto &baseline, std::uint32_t phase) {
    auto state = baseline;
    write64(state.data() + 56, 1);
    write32(state.data() + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
    write64(state.data() + 132, 8);
    write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
    write64(state.data() + 144, lastLegal);
    write64(state.data() + 152, lastLegal - 1);
    write64(state.data() + 160, 0);
    write32(state.data() + 168, 1);
    write64(state.data() + 172, 5);
    write64(state.data() + 180, UINT64_C(1));
    write32(state.data() + 220, 2);
    write32(state.data() + 224, phase);
    std::fill_n(state.data() + 252, 32, std::uint8_t{0xba});
    write32(state.data() + 288, 0);
    write32(state.data() + 292, 0);
    write32(state.data() + 296, 0);
    write64(state.data() + 300, 0);
    write32(state.data() + 308, IRFQ_INFINITE_REASON_NONE_V2);
    if (phase == 1) {
      write64(state.data() + 228, 2);
      write64(state.data() + 236, 5);
      write64(state.data() + 244, 2);
    } else if (phase == 2) {
      write64(state.data() + 228, 7);
      write64(state.data() + 236, 8);
      write64(state.data() + 244, 7);
    } else if (phase == 3) {
      write64(state.data() + 228, 5);
      write64(state.data() + 236, 7);
      write64(state.data() + 244, 5);
    } else {
      write64(state.data() + 228, 7);
      write64(state.data() + 236, 8);
      write64(state.data() + 244, 7);
    }
    return state;
  };
  struct HeartbeatVariant {
    const std::vector<std::uint8_t> *config;
    const std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> *baseline;
    std::uint32_t heartbeat;
    bool peerHeartbeat;
  };
  const std::array heartbeatVariants{
      HeartbeatVariant{&fixedConfig, &fixedBaseline, 30, false},
      HeartbeatVariant{&peerConfig, &peerBaseline, 35, true}};
  for (const auto &heartbeatVariant : heartbeatVariants) {
    for (std::uint32_t phase = 1; phase <= 4; ++phase) {
      DYNAMIC_SECTION((heartbeatVariant.peerHeartbeat ? "heartbeat-range-" : "") << "phase-" << phase) {
        const auto state = phaseState(*heartbeatVariant.baseline, phase);
        const auto &config = *heartbeatVariant.config;
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
                "369=" + std::to_string(lastLegal - 1) + "\00198=0\001108=" + std::to_string(heartbeatVariant.heartbeat)
                    + "\001789=5\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
            0xba);
        inbound.request.expected_revision = 1;
        inbound.request.next_original_value = phase == 1 ? 2 : phase == 2 ? 7 : 8;
        inbound.request.now_tai_ns = INT64_C(1700000000123456001);
        inbound.request.now_utc_ns = INT64_C(1700000000123456001);
        REQUIRE(
            FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                session,
                inbound.request,
                inbound.request.event_identity_sha256));
        PlanBuffers buffers;
        auto result = buffers.response();
        REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
        CHECK(result.output.length == 0);
        CHECK(result.output_frame_count == 0);
        REQUIRE(result.action_count == 2);
        CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
        CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
        CHECK(result.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
        CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
        CHECK(result.actions[1].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
        CHECK(read32(result.native_state.data + 128) == IRFQ_INFINITE_SEQUENCE_VALUE_V2);
        CHECK(read64(result.native_state.data + 132) == 8);
        CHECK(read64(result.native_state.data + 96) == read64(state.data() + 96));
        CHECK(read64(result.native_state.data + 104) == read64(state.data() + 104));
        CHECK(read32(result.native_state.data + 140) == IRFQ_INFINITE_SEQUENCE_VALUE_V2);
        CHECK(read64(result.native_state.data + 144) == lastLegal);
        CHECK(read64(result.native_state.data + 152) == lastLegal - 1);
        CHECK(read64(result.native_state.data + 180) == read64(state.data() + 180));
        if (heartbeatVariant.peerHeartbeat) {
          CHECK(read32(result.native_state.data + 188) == 0);
        }
        CHECK(std::equal(state.begin() + 220, state.begin() + 284, result.native_state.data + 220));
        CHECK(read32(result.native_state.data + 288) == 0);
        CHECK(read32(result.native_state.data + 292) == 0);
        CHECK(read32(result.native_state.data + 296) == 0);
        CHECK(read64(result.native_state.data + 300) == 0);
        CHECK(read32(result.native_state.data + 308) == IRFQ_INFINITE_REASON_NONE_V2);
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
      }
    }
  }

  auto invalidPhase = phaseState(fixedBaseline, 1);
  write32(invalidPhase.data() + 224, 0);
  CHECK(
      FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          fixedConfig.data(),
          fixedConfig.size(),
          invalidPhase.data(),
          invalidPhase.size(),
          1,
          1,
          0,
          0)
      == nullptr);

  struct InvalidVariant {
    const char *name;
    std::uint64_t peer;
    std::uint64_t original;
    bool wrongCompId;
    bool reset;
  };
  const std::array invalidVariants{
      InvalidVariant{"wrong-789", 6, 8, false, false},
      InvalidVariant{"wrong-original", 5, 7, false, false},
      InvalidVariant{"wrong-comp-id", 5, 8, true, false},
      InvalidVariant{"reset", 5, 8, false, true}};
  for (const auto &variant : invalidVariants) {
    DYNAMIC_SECTION("invalid-" << variant.name) {
      const auto state = phaseState(fixedBaseline, 3);
      auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
          fixedConfig.data(),
          fixedConfig.size(),
          state.data(),
          state.size(),
          1,
          1,
          0,
          0);
      REQUIRE(session != nullptr);
      const auto fields = std::string("369=") + std::to_string(lastLegal - 1)
                          + "\00198=0\001108=30\001789=" + std::to_string(variant.peer) + "\001"
                          + (variant.reset ? "141=Y\001" : "") + "1137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
      const auto wire = variant.wrongCompId ? finishFix(
                                                  "35=A\00149=EVIL\00156=VENUE\00134=" + std::to_string(lastLegal)
                                                  + "\00152=20231114-22:13:20.123456\001" + fields)
                                            : participantFrame('A', lastLegal, fields);
      InboundCall inbound(session, wire, 0xbb);
      inbound.request.expected_revision = 1;
      inbound.request.next_original_value = variant.original;
      inbound.request.now_tai_ns = INT64_C(1700000000123456001);
      inbound.request.now_utc_ns = INT64_C(1700000000123456001);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
      PlanBuffers buffers;
      auto result = buffers.response();
      CHECK(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      CHECK(result.prepare_id.low == 0);
      CHECK(result.native_state.length == 0);
      CHECK(result.output.length == 0);
      CHECK(result.action_count == 0);
      CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
    }
  }
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
  shortBuffers.state.fill(0x5a);
  shortBuffers.output.fill(0x5a);
  std::memset(shortBuffers.actions.data(), 0x5a, sizeof(shortBuffers.actions));
  auto shortResult = poisonedPlanResponse(shortBuffers);
  const auto shortCapacity = initial.required_output_capacity - 1;
  shortResult.output.capacity = shortCapacity;
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &shortResult) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  CHECK(shortResult.step == 0);
  CHECK(shortResult.required_output_capacity == initial.required_output_capacity);
  checkNeedOutputPlanPayload(shortResult, initial, initial.required_output_capacity);
  CHECK(std::all_of(shortBuffers.state.begin(), shortBuffers.state.end(), [](auto byte) { return byte == 0x5a; }));
  CHECK(std::all_of(shortBuffers.output.begin(), shortBuffers.output.end(), [](auto byte) { return byte == 0x5a; }));
  const auto *shortActionBytes = reinterpret_cast<const std::uint8_t *>(shortBuffers.actions.data());
  CHECK(std::all_of(shortActionBytes, shortActionBytes + sizeof(shortBuffers.actions), [](auto byte) {
    return byte == 0x5a;
  }));

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
  insufficientBuffers.state.fill(0x5a);
  insufficientBuffers.output.fill(0x5a);
  auto insufficient = poisonedPlanResponse(insufficientBuffers);
  insufficient.actions = nullptr;
  insufficient.action_capacity = 0;
  REQUIRE(irfq_infinite_prepare_v2(session, &request, &insufficient) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(insufficient.header.status == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  checkZeroPlanPayload(insufficient);
  CHECK(std::all_of(insufficientBuffers.state.begin(), insufficientBuffers.state.end(), [](auto byte) {
    return byte == 0x5a;
  }));
  CHECK(std::all_of(insufficientBuffers.output.begin(), insufficientBuffers.output.end(), [](auto byte) {
    return byte == 0x5a;
  }));

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
  constexpr std::int64_t scheduledReset = INT64_C(1699487999000000000);
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
  write64(resetPayload.data() + 76, scheduledReset);
  write64(resetPayload.data() + 84, scheduledReset);
  irfq_infinite_prepare_request_v2 reset{};
  init(reset);
  reset.kind = IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
  reset.stage = IRFQ_INFINITE_STAGE_RESET_FINAL_V2;
  reset.event = IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2;
  reset.expected_epoch = 1;
  reset.now_tai_ns = scheduledReset;
  reset.now_utc_ns = scheduledReset;
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
  write64(state.data() + 80, UINT64_C(100000000000));
  write64(state.data() + 88, UINT64_C(1000000000000));
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
    "InfiniteFrameAdapterV2 rejects a gateway disposition ID on a GapFill decision",
    "[infinite][adapter][v2][stock-smoke][inbound-gap-fill][reject][resume-invalid]") {
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
  const std::string dispositionId = "GID.NOT-APPLICATION";
  resume.gateway_inbound_disposition_id = slice(dispositionId);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  CHECK(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
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
    "InfiniteFrameAdapterV2 starts governed recovery for an at-target ResendRequest",
    "[infinite][adapter][v2][stock-smoke][inbound-resend-request]") {
  const auto config = otherwiseValidUnavailableProfile();
  auto *session = stockLoggedOnSession(config);
  REQUIRE(session != nullptr);
  InboundCall inbound(session, participantFrame('2', 2, "7=1\00116=0\001"), 0x7e);
  PlanBuffers buffers;
  auto result = buffers.response();
  REQUIRE(irfq_infinite_prepare_v2(session, &inbound.request, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  CHECK(result.output.length == 0);
  CHECK(read64(result.native_state.data + 144) == 2);
  CHECK(read32(result.native_state.data + 220) == 1);
  CHECK(read64(result.native_state.data + 228) == 1);
  CHECK(read64(result.native_state.data + 236) == 2);
  REQUIRE(result.action_count == 1);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
  CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
}

TEST_CASE(
    "InfiniteFrameAdapterV2 terminalizes ResendRequest ranges outside the closed sequence domain",
    "[infinite][adapter][v2][stock-smoke][inbound-resend-request][sequence-boundary]") {
  const auto config = otherwiseValidUnavailableProfile();
  const std::array<std::string, 6> variants{{
      "7=0\00116=0\001",
      "7=5\00116=4\001",
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
      if (recovery) {
        for (const bool conflictingPeer789 : {false, true}) {
          DYNAMIC_SECTION("invalid direct-recovery reattach peer-789=" << conflictingPeer789) {
            auto *invalid = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
                config.data(),
                config.size(),
                closed.native_state.data,
                closed.native_state.length,
                1,
                2,
                0,
                0);
            REQUIRE(invalid != nullptr);
            const auto fields = std::string("98=0\001108=30\001") + (conflictingPeer789 ? "789=2\001" : "")
                                + "1137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001";
            InboundCall invalidLogon(invalid, participantFrame('A', 1, fields), 0xf9);
            invalidLogon.request.expected_revision = 2;
            invalidLogon.request.next_original_value = conflictingPeer789 ? 1 : 2;
            REQUIRE(
                FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
                    invalid,
                    invalidLogon.request,
                    invalidLogon.request.event_identity_sha256));
            PlanBuffers invalidBuffers;
            auto invalidResult = invalidBuffers.response();
            REQUIRE(
                irfq_infinite_prepare_v2(invalid, &invalidLogon.request, &invalidResult)
                == IRFQ_INFINITE_STATUS_READY_V2);
            CHECK(invalidResult.output.length == 0);
            REQUIRE(invalidResult.action_count == 2);
            CHECK(invalidResult.actions[0].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2);
            CHECK(invalidResult.actions[0].reason_code == IRFQ_INFINITE_REASON_SEQUENCE_V2);
            CHECK(invalidResult.actions[1].kind == IRFQ_INFINITE_ACTION_DISCONNECT_V2);
            CHECK(read32(invalidResult.native_state.data + 220) == 1);
            CHECK(read32(invalidResult.native_state.data + 292) == 0);
            CHECK(irfq_infinite_destroy_v2(invalid) == IRFQ_INFINITE_STATUS_OK_V2);
          }
        }
      }
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
      const std::string response(reinterpret_cast<const char *>(reattached.output.data), reattached.output.length);
      CHECK(response.find("\00135=A\001") != std::string::npos);
      CHECK(read64(reattached.native_state.data + 132) == 2);
      CHECK(read64(reattached.native_state.data + 144) == 1);
      CHECK(read64(reattached.native_state.data + 152) == 0);
      CHECK(read64(reattached.native_state.data + 180) == UINT64_C(135));
      CHECK(read32(reattached.native_state.data + 288) == 10);
      CHECK(read32(reattached.native_state.data + 292) == (recovery ? 0U : 4U));
      CHECK(read64(reattached.native_state.data + 300) == (recovery ? 0U : 7U));
      CHECK(read32(reattached.native_state.data + 220) == (recovery ? 1U : 0U));
      if (recovery) {
        CHECK(read64(reattached.native_state.data + 228) == 2);
        CHECK(read64(reattached.native_state.data + 236) == 5);
        CHECK(read64(reattached.native_state.data + 244) == 3);
        CHECK(
            std::all_of(reattached.native_state.data + 252, reattached.native_state.data + 284, [](std::uint8_t byte) {
              return byte == 0x95;
            }));
      }
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
    "InfiniteFrameAdapterV2 stock smoke resumes rejected application with bounded governed BusinessReject",
    "[infinite][adapter][v2][stock-smoke][inbound-application][reject]") {
  auto dispositionId = GENERATE(std::string("G"), std::string(64, 'G'));
  const auto expectedDispositionId = dispositionId;
  CAPTURE(dispositionId.size());
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
  resume.gateway_inbound_disposition_id = slice(dispositionId);
  PlanBuffers buffers;
  auto result = buffers.response();
  if (dispositionId.size() == IRFQ_INFINITE_MAX_GATEWAY_INBOUND_DISPOSITION_ID_BYTES_V2) {
    result.output = {nullptr, 0, 0};
    REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
    std::fill(dispositionId.begin(), dispositionId.end(), 'X');
    init(resume);
    resume.prepare_id = result.prepare_id;
    resume.step = result.step;
    resume.kind = IRFQ_INFINITE_RESUME_OUTPUT_V2;
    result = buffers.response();
    REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  } else {
    REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_READY_V2);
  }

  const std::string output(reinterpret_cast<char *>(result.output.data), result.output.length);
  CHECK(output.find("\00135=j\001") != std::string::npos);
  CHECK(output.find("\00145=") == std::string::npos);
  CHECK(output.find("\001372=AJ\001") != std::string::npos);
  CHECK(output.find("\001379=") == std::string::npos);
  CHECK(output.find("\001380=3\001") != std::string::npos);
  CHECK(output.find("\00158=Application message is unsupported.\001") != std::string::npos);
  CHECK(output.find("\00120003=" + expectedDispositionId + "\001") != std::string::npos);
  CHECK(output.find("\00120004=INF-1002\001") != std::string::npos);
  CHECK(read64(result.native_state.data + 132) == 3);
  CHECK(read64(result.native_state.data + 144) == 2);
  REQUIRE(result.action_count == 3);
  CHECK(result.actions[0].kind == IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2);
  CHECK(result.actions[1].kind == IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2);
  CHECK(result.actions[1].disposition == IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2);
  CHECK(result.actions[1].reason_code == IRFQ_INFINITE_REASON_PROTOCOL_V2);
  CHECK(result.actions[2].kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2);
  CHECK(result.actions[2].output_class == IRFQ_INFINITE_OUTPUT_ORIGINAL_APPLICATION_V2);
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
  const std::string dispositionId = "GID.LAST-REJECT";
  resume.gateway_inbound_disposition_id = slice(dispositionId);
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
  for (const std::string variant :
       {"step",
        "alias",
        "capacity",
        "action-capacity",
        "allow-disposition",
        "reject-missing-disposition",
        "reject-nonnull-empty-disposition",
        "reject-null-nonzero-disposition",
        "reject-long-disposition",
        "reject-space-disposition",
        "reject-control-disposition",
        "reject-high-byte-disposition",
        "disposition-alias"}) {
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
      const auto actionCapacityFailure = variant == "action-capacity";
      if (actionCapacityFailure) {
        invalidBuffers.state.fill(0x5a);
        invalidBuffers.output.fill(0x5a);
        std::memset(invalidBuffers.actions.data(), 0x5a, sizeof(invalidBuffers.actions));
      }
      auto invalid = actionCapacityFailure ? poisonedPlanResponse(invalidBuffers) : invalidBuffers.response();
      std::string dispositionId;
      std::uint8_t dispositionMarker{'G'};
      if (variant == "step") {
        ++resume.step;
      } else if (variant == "alias") {
        invalid.native_state.data = inbound.payload.data();
      } else if (variant == "action-capacity") {
        invalid.action_capacity = 1;
      } else if (variant == "capacity") {
        invalid.output.capacity = IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 + 1;
      } else if (variant == "allow-disposition") {
        dispositionId = "GID.NOT-ALLOWED";
        resume.gateway_inbound_disposition_id = slice(dispositionId);
      } else {
        resume.decision = IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2;
        if (variant == "reject-nonnull-empty-disposition") {
          resume.gateway_inbound_disposition_id = {&dispositionMarker, 0};
        } else if (variant == "reject-null-nonzero-disposition") {
          resume.gateway_inbound_disposition_id = {nullptr, 1};
        } else if (variant == "reject-long-disposition") {
          dispositionId.assign(IRFQ_INFINITE_MAX_GATEWAY_INBOUND_DISPOSITION_ID_BYTES_V2 + 1, 'A');
          resume.gateway_inbound_disposition_id = slice(dispositionId);
        } else if (variant == "reject-space-disposition") {
          dispositionId = "BAD ID";
          resume.gateway_inbound_disposition_id = slice(dispositionId);
        } else if (variant == "reject-control-disposition") {
          dispositionId = "BAD\nID";
          resume.gateway_inbound_disposition_id = slice(dispositionId);
        } else if (variant == "reject-high-byte-disposition") {
          dispositionId.assign(1, static_cast<char>(0x80));
          resume.gateway_inbound_disposition_id = slice(dispositionId);
        } else if (variant == "disposition-alias") {
          constexpr char aliasId[] = "GID";
          std::copy_n(aliasId, sizeof(aliasId) - 1, invalid.output.data);
          resume.gateway_inbound_disposition_id = {invalid.output.data, sizeof(aliasId) - 1};
        }
      }
      const auto expected
          = variant == "step" ? IRFQ_INFINITE_STATUS_STALE_PLAN_V2 : IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
      REQUIRE(irfq_infinite_resume_v2(session, &resume, &invalid) == expected);
      if (actionCapacityFailure) {
        CHECK(invalid.header.status == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        checkZeroPlanPayload(invalid);
        CHECK(std::all_of(invalidBuffers.state.begin(), invalidBuffers.state.end(), [](auto byte) {
          return byte == 0x5a;
        }));
        CHECK(std::all_of(invalidBuffers.output.begin(), invalidBuffers.output.end(), [](auto byte) {
          return byte == 0x5a;
        }));
        const auto *actionBytes = reinterpret_cast<const std::uint8_t *>(invalidBuffers.actions.data());
        CHECK(std::all_of(actionBytes, actionBytes + sizeof(invalidBuffers.actions), [](auto byte) {
          return byte == 0x5a;
        }));
      }
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
  for (const std::string variant : {"start", "reject", "unexpected-disposition"}) {
    DYNAMIC_SECTION(variant) {
      const auto decision = variant == "reject" ? IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2
                                                : IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2;
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
      const std::string forbiddenDisposition = "GID.NOT-APPLICATION";
      if (variant == "unexpected-disposition") {
        resume.gateway_inbound_disposition_id = slice(forbiddenDisposition);
      }
      PlanBuffers buffers;
      auto result = buffers.response();
      if (variant == "unexpected-disposition") {
        REQUIRE(irfq_infinite_resume_v2(session, &resume, &result) == IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        CHECK(irfq_infinite_destroy_v2(session) == IRFQ_INFINITE_STATUS_OK_V2);
        continue;
      }
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
  auto *session = detachedSenderSession(config, 2);
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      participantFrame('A', 2, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x84);
  inbound.request.expected_revision = 2;
  inbound.request.now_tai_ns = INT64_C(1700000000123456002);
  inbound.request.now_utc_ns = INT64_C(1700000000123456002);
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
      INT64_C(1700000000123456000),
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
    "InfiniteFrameAdapterV2 fresh reset Logon precedes weekly eligibility",
    "[infinite][adapter][v2][stock-smoke][inbound-reset-decision][fresh][eligibility]") {
  const auto config = otherwiseValidUnavailableProfile(2, {3, 72000, 4, 75600, 3, 72000, 4, 75600});
  auto *session = FIX::createInfiniteFrameAdapterStockNonconformanceSmokeSession(
      config.data(),
      config.size(),
      nullptr,
      0,
      1,
      0,
      INT64_C(1700000000123456000),
      INT64_C(1700000000123456000));
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      participantFrame(
          'A',
          1,
          "369=0\00198=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0xbd);
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
      auto *session = detachedSenderSession(config, 2);
      REQUIRE(session != nullptr);
      InboundCall inbound(
          session,
          participantFrame('A', 2, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x85);
      inbound.request.expected_revision = 2;
      inbound.request.now_tai_ns = INT64_C(1700000000123456002);
      inbound.request.now_utc_ns = INT64_C(1700000000123456002);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
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
      CHECK(result.result_revision == 3);
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
  auto *session = detachedSenderSession(config, lastLegal);
  REQUIRE(session != nullptr);
  InboundCall inbound(
      session,
      participantFrame('A', 2, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
      0x6e);
  inbound.request.expected_revision = 2;
  inbound.request.next_original_value = lastLegal;
  inbound.request.now_tai_ns = INT64_C(1700000000123456002);
  inbound.request.now_utc_ns = INT64_C(1700000000123456002);
  REQUIRE(
      FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
          session,
          inbound.request,
          inbound.request.event_identity_sha256));
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
      3,
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
      auto *session = detachedSenderSession(config, 2);
      REQUIRE(session != nullptr);
      InboundCall inbound(
          session,
          participantFrame('A', 2, "98=0\001108=30\001141=Y\0011137=10\0011407=299\0011408=INFINITE-RFQ-1.0.0\001"),
          0x9a);
      inbound.request.expected_revision = 2;
      inbound.request.now_tai_ns = INT64_C(1700000000123456002);
      inbound.request.now_utc_ns = INT64_C(1700000000123456002);
      REQUIRE(
          FIX::computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
              session,
              inbound.request,
              inbound.request.event_identity_sha256));
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
