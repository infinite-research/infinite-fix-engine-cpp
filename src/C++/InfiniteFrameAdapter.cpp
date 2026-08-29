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

#ifdef _MSC_VER
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "InfiniteFrameAdapter.h"

#include "DataDictionaryProvider.h"
#include "InfiniteCompleteFrame.h"
#include "InfiniteSessionClassification.h"
#include "TimeRange.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::array<std::uint8_t, 8> STATE_MAGIC{{'I', 'R', 'F', 'Q', 'N', 'S', '2', 0}};
constexpr char PROFILE_DOMAIN[] = "IRFQ-FIX-SESSION-PROFILE-V1";
constexpr char NATIVE_STATE_DOMAIN[] = "IRFQ-FIX-NATIVE-STATE-V1";
constexpr char EVENT_PAYLOAD_DOMAIN[] = "IRFQ-FIX-ABI-V2-EVENT-PAYLOAD-V1";
constexpr char EVENT_IDENTITY_DOMAIN[] = "IRFQ-FIX-ABI-V2-EVENT-V1";
constexpr char DECISION_SUBJECT_DOMAIN[] = "IRFQ-FIX-ABI-V2-DECISION-SUBJECT-V1";
constexpr char REBORROW_DOMAIN[] = "IRFQ-FIX-ABI-V2-REBORROW-V1";
constexpr std::uint64_t SESSION_FLAGS_MASK = UINT64_C(0x1ff);
constexpr std::uint64_t SESSION_FLAG_ENABLED = UINT64_C(1);
constexpr std::uint32_t RECOVERY_NONE = 0;
constexpr std::uint32_t RECOVERY_RESEND_REQUEST = 1;
constexpr std::uint32_t RECOVERY_LOGON_789 = 2;
constexpr std::uint32_t RECOVERY_PHASE_NONE = 0;
constexpr std::uint32_t RECOVERY_PHASE_PEER_PREFIX = 1;
constexpr std::uint32_t RECOVERY_PHASE_LOGON_RESPONSE = 2;
constexpr std::uint32_t RECOVERY_PHASE_STORED_RANGE = 3;
constexpr std::uint32_t RECOVERY_PHASE_FINAL_GAP_FILL = 4;
constexpr std::uint32_t CONTINUATION_NONE = 0;
constexpr std::uint32_t CONTINUATION_RESEND = 1;
constexpr std::uint32_t CONTINUATION_READ_RESULT = 4;

struct Range {
  std::uintptr_t begin{0};
  std::uintptr_t end{0};
  bool empty{true};
};

bool range(const void *pointer, std::uint64_t length, Range &result) noexcept {
  if (length == 0) {
    result = {};
    return true;
  }
  if (pointer == nullptr || length > std::numeric_limits<std::uintptr_t>::max()) {
    return false;
  }
  const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
  const auto size = static_cast<std::uintptr_t>(length);
  if (begin > std::numeric_limits<std::uintptr_t>::max() - size) {
    return false;
  }
  result = {begin, begin + size, false};
  return true;
}

bool overlaps(const Range &left, const Range &right) noexcept {
  return !left.empty && !right.empty && left.begin < right.end && right.begin < left.end;
}

template <typename T> bool aligned(const T *pointer) noexcept {
  return pointer != nullptr && reinterpret_cast<std::uintptr_t>(pointer) % alignof(T) == 0;
}

template <typename T> bool validInputHeader(const T *value) noexcept {
  return value->header.structure_size == sizeof(T)
         && value->header.abi_version == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2 && value->header.reserved == 0;
}

template <typename T> bool validOutputHeader(const T *value) noexcept {
  return value->header.structure_size == sizeof(T)
         && value->header.abi_version == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2 && value->header.reserved == 0;
}

template <typename T> void clearResponse(T *response) noexcept {
  const auto structureSize = response->header.structure_size;
  const auto abiVersion = response->header.abi_version;
  *response = {};
  response->header.structure_size = structureSize;
  response->header.abi_version = abiVersion;
}

template <typename T> irfq_infinite_status_v2 publish(T *response, irfq_infinite_status_v2 status) noexcept {
  if (aligned(response)) {
    response->header.status = status;
  }
  return status;
}

template <typename Request, typename Response>
irfq_infinite_status_v2 validateEnvelope(const Request *request, Response *response) noexcept {
  if (!aligned(response)) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
  }
  if (!aligned(request)) {
    return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }
  if (!validInputHeader(request) || !validOutputHeader(response)) {
    return publish(response, IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2);
  }
  Range requestRange;
  Range responseRange;
  if (!range(request, sizeof(*request), requestRange) || !range(response, sizeof(*response), responseRange)
      || overlaps(requestRange, responseRange)) {
    return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }
  clearResponse(response);
  return IRFQ_INFINITE_STATUS_OK_V2;
}

bool sliceRange(const irfq_infinite_slice_v2 &slice, std::uint64_t maximum, Range &result) noexcept {
  return slice.length <= maximum && range(slice.data, slice.length, result);
}

bool disjoint(const Range &candidate, const std::vector<Range> &ranges) noexcept {
  return std::none_of(ranges.begin(), ranges.end(), [&candidate](const Range &other) {
    return overlaps(candidate, other);
  });
}

class Sha256 {
public:
  void update(const std::uint8_t *data, std::size_t length) noexcept {
    m_bits += static_cast<std::uint64_t>(length) * 8;
    while (length != 0) {
      const auto count = std::min(length, m_block.size() - m_used);
      std::memcpy(m_block.data() + m_used, data, count);
      m_used += count;
      data += count;
      length -= count;
      if (m_used == m_block.size()) {
        transform();
        m_used = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> finish() noexcept {
    m_block[m_used++] = 0x80;
    if (m_used > 56) {
      std::fill(m_block.begin() + m_used, m_block.end(), 0);
      transform();
      m_used = 0;
    }
    std::fill(m_block.begin() + m_used, m_block.begin() + 56, 0);
    for (unsigned index = 0; index < 8; ++index) {
      m_block[63 - index] = static_cast<std::uint8_t>(m_bits >> (index * 8));
    }
    transform();
    std::array<std::uint8_t, 32> result{};
    for (std::size_t index = 0; index < m_state.size(); ++index) {
      for (unsigned byte = 0; byte < 4; ++byte) {
        result[index * 4 + byte] = static_cast<std::uint8_t>(m_state[index] >> ((3 - byte) * 8));
      }
    }
    return result;
  }

private:
  static std::uint32_t rotate(std::uint32_t value, unsigned count) noexcept {
    return (value >> count) | (value << (32 - count));
  }

  void transform() noexcept {
    static constexpr std::array<std::uint32_t, 64> constants{
        {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
         0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
         0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
         0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
         0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
         0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
         0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
         0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2}};
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      words[index] = static_cast<std::uint32_t>(m_block[index * 4]) << 24
                     | static_cast<std::uint32_t>(m_block[index * 4 + 1]) << 16
                     | static_cast<std::uint32_t>(m_block[index * 4 + 2]) << 8 | m_block[index * 4 + 3];
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      const auto s0 = rotate(words[index - 15], 7) ^ rotate(words[index - 15], 18) ^ (words[index - 15] >> 3);
      const auto s1 = rotate(words[index - 2], 17) ^ rotate(words[index - 2], 19) ^ (words[index - 2] >> 10);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    auto a = m_state[0];
    auto b = m_state[1];
    auto c = m_state[2];
    auto d = m_state[3];
    auto e = m_state[4];
    auto f = m_state[5];
    auto g = m_state[6];
    auto h = m_state[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
      const auto s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
      const auto choice = (e & f) ^ (~e & g);
      const auto first = h + s1 + choice + constants[index] + words[index];
      const auto s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto second = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + first;
      d = c;
      c = b;
      b = a;
      a = first + second;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
  }

  std::array<std::uint32_t, 8> m_state{
      {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};
  std::array<std::uint8_t, 64> m_block{};
  std::uint64_t m_bits{0};
  std::size_t m_used{0};
};

std::uint32_t read32(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) << 24 | static_cast<std::uint32_t>(bytes[1]) << 16
         | static_cast<std::uint32_t>(bytes[2]) << 8 | bytes[3];
}

std::uint64_t read64(const std::uint8_t *bytes) noexcept {
  std::uint64_t value = 0;
  for (unsigned index = 0; index < 8; ++index) {
    value = value << 8 | bytes[index];
  }
  return value;
}

std::int64_t readI64(const std::uint8_t *bytes) noexcept {
  const auto value = read64(bytes);
  std::int64_t result = 0;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

bool nonzero(const std::uint8_t *bytes, std::size_t length) noexcept {
  return std::any_of(bytes, bytes + length, [](std::uint8_t byte) { return byte != 0; });
}

struct CborValue {
  enum class Type {
    Unsigned,
    Bytes,
    Boolean
  } type{Type::Unsigned};
  std::uint64_t number{0};
  const std::uint8_t *bytes{nullptr};
  std::size_t length{0};
  bool boolean{false};
};

class CborReader {
public:
  CborReader(const std::uint8_t *bytes, std::size_t length)
      : m_cursor(bytes),
        m_end(bytes + length) {}

  bool array(std::uint64_t expected) noexcept {
    std::uint8_t major = 0;
    std::uint64_t value = 0;
    return head(major, value) && major == 4 && value == expected;
  }

  bool value(CborValue &result) noexcept {
    if (m_cursor == m_end) {
      return false;
    }
    const auto *begin = m_cursor;
    std::uint8_t major = 0;
    std::uint64_t argument = 0;
    if (!head(major, argument)) {
      return false;
    }
    if (major == 0) {
      result = {CborValue::Type::Unsigned, argument, nullptr, 0, false};
      return true;
    }
    if (major == 2) {
      if (argument > static_cast<std::uint64_t>(m_end - m_cursor)) {
        m_cursor = begin;
        return false;
      }
      result = {CborValue::Type::Bytes, 0, m_cursor, static_cast<std::size_t>(argument), false};
      m_cursor += argument;
      return true;
    }
    if (major == 7 && (argument == 20 || argument == 21)) {
      result = {CborValue::Type::Boolean, 0, nullptr, 0, argument == 21};
      return true;
    }
    m_cursor = begin;
    return false;
  }

  bool done() const noexcept { return m_cursor == m_end; }

private:
  bool head(std::uint8_t &major, std::uint64_t &argument) noexcept {
    if (m_cursor == m_end) {
      return false;
    }
    const auto initial = *m_cursor++;
    major = initial >> 5;
    const auto additional = initial & 0x1f;
    if (additional < 24) {
      argument = additional;
      return true;
    }
    const unsigned width = additional == 24   ? 1
                           : additional == 25 ? 2
                           : additional == 26 ? 4
                           : additional == 27 ? 8
                                              : 0;
    if (width == 0 || static_cast<std::size_t>(m_end - m_cursor) < width) {
      return false;
    }
    argument = 0;
    for (unsigned index = 0; index < width; ++index) {
      argument = argument << 8 | *m_cursor++;
    }
    const std::uint64_t minimum = width == 1   ? 24
                                  : width == 2 ? 256
                                  : width == 4 ? UINT64_C(65536)
                                               : UINT64_C(4294967296);
    return argument >= minimum;
  }

  const std::uint8_t *m_cursor;
  const std::uint8_t *m_end;
};

struct Profile {
  std::array<std::uint8_t, 32> digest{};
  std::array<std::uint8_t, 32> binding{};
  std::string beginString;
  std::string venueCompId;
  std::string venueSubId;
  std::string venueLocationId;
  std::string participantCompId;
  std::string participantSubId;
  std::string participantLocationId;
  std::string qualifier;
  std::string defaultCustomApplicationVersion;
  std::uint32_t heartbeatMode{0};
  std::uint32_t scheduleMode{0};
  std::array<std::uint32_t, 8> schedule{};
  std::uint32_t configuredHeartbeat{0};
  std::uint32_t minimumHeartbeat{0};
  std::uint32_t maximumHeartbeat{0};
  std::uint32_t logonTimeout{0};
  std::uint32_t logoutTimeout{0};
  std::uint32_t timestampPrecision{0};
  std::uint32_t maximumLatency{0};
  bool sendRedundantResendRequests{false};
  bool checkCompId{false};
  bool checkLatency{false};
  bool resetOnLogon{false};
  bool resetOnLogout{false};
  bool resetOnDisconnect{false};
  bool refreshOnLogon{false};
  bool persistMessages{false};
  bool validateLengthAndChecksum{false};
  bool sendNextExpectedMsgSeqNum{false};
};

bool printable(const CborValue &value, std::size_t minimum, std::size_t maximum) noexcept {
  if (value.type != CborValue::Type::Bytes || value.length < minimum || value.length > maximum) {
    return false;
  }
  return std::all_of(value.bytes, value.bytes + value.length, [](std::uint8_t byte) {
    return byte >= 0x21 && byte <= 0x7e;
  });
}

bool bytesEqual(const CborValue &value, const char *expected) noexcept {
  const auto length = std::strlen(expected);
  return value.type == CborValue::Type::Bytes && value.length == length
         && std::equal(value.bytes, value.bytes + value.length, reinterpret_cast<const std::uint8_t *>(expected));
}

bool unsigned32(const CborValue &value) noexcept {
  return value.type == CborValue::Type::Unsigned && value.number <= UINT32_MAX;
}

bool weeklyLogonContained(const std::array<CborValue, 50> &fields) noexcept {
  constexpr std::uint64_t WEEK_SECONDS = UINT64_C(7) * 86400;
  const auto position
      = [&fields](std::size_t day) { return fields[day].number * UINT64_C(86400) + fields[day + 1].number; };
  const auto distance = [](std::uint64_t start, std::uint64_t value) {
    return value >= start ? value - start : WEEK_SECONDS - start + value;
  };
  const auto sessionStart = position(12);
  const auto sessionEnd = position(14);
  const auto logonStart = distance(sessionStart, position(16));
  const auto logonEnd = distance(sessionStart, position(18));
  const auto sessionEndOffset = distance(sessionStart, sessionEnd);
  return logonStart <= logonEnd && logonEnd <= sessionEndOffset;
}

bool parseProfile(const irfq_infinite_slice_v2 &config, Profile &profile) noexcept {
  if (config.length == 0 || config.length > 4096 || config.data == nullptr) {
    return false;
  }
  CborReader reader(config.data, static_cast<std::size_t>(config.length));
  std::array<CborValue, 50> fields{};
  if (!reader.array(fields.size())) {
    return false;
  }
  for (auto &field : fields) {
    if (!reader.value(field)) {
      return false;
    }
  }
  if (!reader.done() || fields[0].type != CborValue::Type::Unsigned || fields[0].number != 1
      || !bytesEqual(fields[1], "FIXT.1.1") || !printable(fields[2], 1, 64) || !printable(fields[3], 0, 64)
      || !printable(fields[4], 0, 64) || !printable(fields[5], 1, 64) || !printable(fields[6], 0, 64)
      || !printable(fields[7], 0, 64) || !printable(fields[8], 0, 64) || fields[9].type != CborValue::Type::Bytes
      || fields[9].length != 32 || !nonzero(fields[9].bytes, 32) || fields[10].type != CborValue::Type::Unsigned
      || fields[10].number != 1 || fields[11].type != CborValue::Type::Unsigned
      || (fields[11].number != 1 && fields[11].number != 2)) {
    return false;
  }
  for (std::size_t index = 12; index <= 26; ++index) {
    if (!unsigned32(fields[index])) {
      return false;
    }
  }
  for (std::size_t index = 27; index <= 29; ++index) {
    if (fields[index].type != CborValue::Type::Boolean) {
      return false;
    }
  }
  if (!unsigned32(fields[30])) {
    return false;
  }
  for (std::size_t index = 31; index <= 41; ++index) {
    if (fields[index].type != CborValue::Type::Boolean) {
      return false;
    }
  }
  if (!bytesEqual(fields[42], "INFINITE-RFQ-1.0.0") || !unsigned32(fields[43]) || fields[43].number != 10
      || !unsigned32(fields[44]) || fields[44].number != 299 || !printable(fields[45], 1, 64)
      || !printable(fields[46], 1, 64) || fields[47].type != CborValue::Type::Bytes || fields[47].length != 32
      || !nonzero(fields[47].bytes, 32) || !printable(fields[48], 1, 64) || fields[49].type != CborValue::Type::Bytes
      || fields[49].length != 32 || !nonzero(fields[49].bytes, 32)) {
    return false;
  }
  const auto scheduleMode = fields[11].number;
  if ((scheduleMode == 1
       && std::any_of(
           fields.begin() + 12,
           fields.begin() + 20,
           [](const CborValue &field) { return field.number != 0; }))
      || (scheduleMode == 2
          && (fields[12].number > 6 || fields[14].number > 6 || fields[16].number > 6 || fields[18].number > 6
              || fields[13].number > 86399 || fields[15].number > 86399 || fields[17].number > 86399
              || fields[19].number > 86399 || !weeklyLogonContained(fields)))) {
    return false;
  }
  profile.scheduleMode = static_cast<std::uint32_t>(scheduleMode);
  for (std::size_t index = 0; index < profile.schedule.size(); ++index) {
    profile.schedule[index] = static_cast<std::uint32_t>(fields[index + 12].number);
  }
  profile.heartbeatMode = static_cast<std::uint32_t>(fields[20].number);
  profile.configuredHeartbeat = static_cast<std::uint32_t>(fields[21].number);
  profile.minimumHeartbeat = static_cast<std::uint32_t>(fields[22].number);
  profile.maximumHeartbeat = static_cast<std::uint32_t>(fields[23].number);
  profile.logonTimeout = static_cast<std::uint32_t>(fields[24].number);
  profile.logoutTimeout = static_cast<std::uint32_t>(fields[25].number);
  if ((profile.heartbeatMode == 1
       && (profile.configuredHeartbeat == 0 || profile.minimumHeartbeat != profile.configuredHeartbeat
           || profile.maximumHeartbeat != profile.configuredHeartbeat))
      || (profile.heartbeatMode == 2
          && (profile.configuredHeartbeat != 0 || profile.minimumHeartbeat == 0
              || profile.minimumHeartbeat > profile.maximumHeartbeat))
      || (profile.heartbeatMode != 1 && profile.heartbeatMode != 2) || profile.logonTimeout == 0
      || profile.logoutTimeout == 0 || fields[26].number != 6 || !fields[28].boolean || !fields[29].boolean
      || fields[31].boolean || fields[32].boolean || fields[33].boolean || fields[34].boolean || !fields[35].boolean
      || !fields[36].boolean || !fields[37].boolean || !fields[38].boolean || !fields[39].boolean || !fields[40].boolean
      || fields[41].boolean) {
    return false;
  }
  Sha256 sha;
  sha.update(reinterpret_cast<const std::uint8_t *>(PROFILE_DOMAIN), sizeof(PROFILE_DOMAIN) - 1);
  const std::uint8_t prefix[]{0, 0x83, 0x01, 0x02};
  sha.update(prefix, sizeof(prefix));
  if (config.length < 24) {
    const auto header = static_cast<std::uint8_t>(0x40 | config.length);
    sha.update(&header, 1);
  } else if (config.length <= UINT8_MAX) {
    const std::uint8_t header[]{0x58, static_cast<std::uint8_t>(config.length)};
    sha.update(header, sizeof(header));
  } else {
    const std::uint8_t header[]{
        0x59,
        static_cast<std::uint8_t>(config.length >> 8),
        static_cast<std::uint8_t>(config.length)};
    sha.update(header, sizeof(header));
  }
  sha.update(config.data, static_cast<std::size_t>(config.length));
  profile.digest = sha.finish();
  std::copy_n(fields[9].bytes, profile.binding.size(), profile.binding.begin());
  profile.beginString.assign(reinterpret_cast<const char *>(fields[1].bytes), fields[1].length);
  profile.venueCompId.assign(reinterpret_cast<const char *>(fields[2].bytes), fields[2].length);
  profile.venueSubId.assign(reinterpret_cast<const char *>(fields[3].bytes), fields[3].length);
  profile.venueLocationId.assign(reinterpret_cast<const char *>(fields[4].bytes), fields[4].length);
  profile.participantCompId.assign(reinterpret_cast<const char *>(fields[5].bytes), fields[5].length);
  profile.participantSubId.assign(reinterpret_cast<const char *>(fields[6].bytes), fields[6].length);
  profile.participantLocationId.assign(reinterpret_cast<const char *>(fields[7].bytes), fields[7].length);
  profile.qualifier.assign(reinterpret_cast<const char *>(fields[8].bytes), fields[8].length);
  profile.timestampPrecision = static_cast<std::uint32_t>(fields[26].number);
  profile.sendRedundantResendRequests = fields[27].boolean;
  profile.checkCompId = fields[28].boolean;
  profile.checkLatency = fields[29].boolean;
  profile.maximumLatency = static_cast<std::uint32_t>(fields[30].number);
  profile.resetOnLogon = fields[31].boolean;
  profile.resetOnLogout = fields[32].boolean;
  profile.resetOnDisconnect = fields[33].boolean;
  profile.refreshOnLogon = fields[34].boolean;
  profile.persistMessages = fields[35].boolean;
  profile.validateLengthAndChecksum = fields[36].boolean;
  profile.sendNextExpectedMsgSeqNum = fields[37].boolean;
  profile.defaultCustomApplicationVersion.assign(reinterpret_cast<const char *>(fields[45].bytes), fields[45].length);
  return true;
}

bool scheduledBoundary(const Profile &profile, std::int64_t creationUtcNs, std::int64_t &boundaryUtcNs) noexcept {
  constexpr std::int64_t NANOSECONDS_PER_SECOND = INT64_C(1000000000);
  constexpr std::int64_t SECONDS_PER_DAY = INT64_C(86400);
  constexpr std::int64_t SECONDS_PER_WEEK = INT64_C(604800);
  if (profile.scheduleMode != 2 || creationUtcNs <= 0) {
    return false;
  }
  const auto creationSecond = creationUtcNs / NANOSECONDS_PER_SECOND;
  const auto daysSinceEpoch = creationSecond / SECONDS_PER_DAY;
  const auto weekday = (daysSinceEpoch + 4) % 7;
  const auto secondOfDay = creationSecond % SECONDS_PER_DAY;
  const auto currentWeekSecond = weekday * SECONDS_PER_DAY + secondOfDay;
  const auto targetWeekSecond = static_cast<std::int64_t>(profile.schedule[2]) * SECONDS_PER_DAY + profile.schedule[3];
  auto delta = (targetWeekSecond - currentWeekSecond + SECONDS_PER_WEEK) % SECONDS_PER_WEEK;
  if (delta == 0) {
    delta = SECONDS_PER_WEEK;
  }
  constexpr auto LAST_WHOLE_SECOND = INT64_MAX / NANOSECONDS_PER_SECOND;
  if (creationSecond > LAST_WHOLE_SECOND - delta) {
    return false;
  }
  boundaryUtcNs = (creationSecond + delta) * NANOSECONDS_PER_SECOND;
  return boundaryUtcNs > creationUtcNs;
}

void write32(std::uint8_t *bytes, std::uint32_t value) noexcept {
  for (unsigned index = 0; index < 4; ++index) {
    bytes[index] = static_cast<std::uint8_t>(value >> ((3 - index) * 8));
  }
}

void write64(std::uint8_t *bytes, std::uint64_t value) noexcept {
  for (unsigned index = 0; index < 8; ++index) {
    bytes[index] = static_cast<std::uint8_t>(value >> ((7 - index) * 8));
  }
}

std::array<std::uint8_t, 32> domainDigest(const char *domain, const std::uint8_t *bytes, std::size_t length) noexcept {
  Sha256 sha;
  sha.update(reinterpret_cast<const std::uint8_t *>(domain), std::strlen(domain));
  const std::uint8_t zero = 0;
  sha.update(&zero, 1);
  sha.update(bytes, length);
  return sha.finish();
}

std::array<std::uint8_t, 32> eventIdentity(
    const Profile &profile,
    const irfq_infinite_prepare_request_v2 &request) noexcept {
  const auto payloadDigest
      = domainDigest(EVENT_PAYLOAD_DOMAIN, request.payload.data, static_cast<std::size_t>(request.payload.length));
  Sha256 sha;
  sha.update(reinterpret_cast<const std::uint8_t *>(EVENT_IDENTITY_DOMAIN), sizeof(EVENT_IDENTITY_DOMAIN) - 1);
  const std::uint8_t zero = 0;
  sha.update(&zero, 1);
  sha.update(profile.binding.data(), profile.binding.size());
  std::array<std::uint8_t, 64> fields{};
  write32(fields.data(), request.kind);
  write32(fields.data() + 4, request.stage);
  write32(fields.data() + 8, request.event);
  write32(fields.data() + 12, request.application_block_mode);
  write64(fields.data() + 16, request.expected_epoch);
  write64(fields.data() + 24, request.expected_revision);
  write64(fields.data() + 32, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(fields.data() + 40, static_cast<std::uint64_t>(request.now_utc_ns));
  write32(fields.data() + 48, request.next_original_state);
  write64(fields.data() + 52, request.next_original_value);
  sha.update(fields.data(), 60);
  sha.update(payloadDigest.data(), payloadDigest.size());
  return sha.finish();
}

std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> freshState(
    const Profile &profile,
    std::uint64_t epoch,
    std::int64_t creationTaiNs,
    std::int64_t creationUtcNs) noexcept {
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::copy(STATE_MAGIC.begin(), STATE_MAGIC.end(), state.begin());
  write32(state.data() + 8, IRFQ_INFINITE_NATIVE_STATE_SCHEMA_VERSION_V2);
  write32(state.data() + 12, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2);
  std::copy(profile.digest.begin(), profile.digest.end(), state.begin() + 16);
  write64(state.data() + 48, epoch);
  write64(state.data() + 64, static_cast<std::uint64_t>(creationTaiNs));
  write64(state.data() + 72, static_cast<std::uint64_t>(creationUtcNs));
  write64(state.data() + 80, static_cast<std::uint64_t>(creationTaiNs));
  write64(state.data() + 88, static_cast<std::uint64_t>(creationUtcNs));
  write32(state.data() + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(state.data() + 132, 1);
  write32(state.data() + 140, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
  write64(state.data() + 144, 1);
  write64(state.data() + 180, SESSION_FLAG_ENABLED);
  write32(state.data() + 188, profile.heartbeatMode == 1 ? profile.configuredHeartbeat : 0);
  write32(state.data() + 284, 10);
  return state;
}

bool pairEitherZeroOrPositive(std::int64_t tai, std::int64_t utc) noexcept {
  return (tai == 0 && utc == 0) || (tai > 0 && utc > 0);
}

bool validSequence(std::uint32_t state, std::uint64_t value) noexcept {
  return (state == IRFQ_INFINITE_SEQUENCE_VALUE_V2 && value > 0 && value < IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2)
         || (state == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 && value == 0);
}

bool validRangeTriple(std::uint64_t begin, std::uint64_t end, std::uint64_t cursor, bool active) noexcept {
  if (!active) {
    return begin == 0 && end == 0 && cursor == 0;
  }
  return begin > 0 && begin <= cursor && cursor < end
         && end <= static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2);
}

bool validNativeState(
    const irfq_infinite_slice_v2 &state,
    const Profile &profile,
    std::uint64_t expectedEpoch,
    std::uint64_t expectedRevision) noexcept {
  if (state.length != IRFQ_INFINITE_NATIVE_STATE_BYTES_V2 || state.data == nullptr
      || !std::equal(STATE_MAGIC.begin(), STATE_MAGIC.end(), state.data) || read32(state.data + 8) != 1
      || read32(state.data + 12) != IRFQ_INFINITE_NATIVE_STATE_BYTES_V2
      || !std::equal(profile.digest.begin(), profile.digest.end(), state.data + 16)
      || read64(state.data + 48) != expectedEpoch || read64(state.data + 56) != expectedRevision
      || expectedRevision == 0) {
    return false;
  }
  const auto creationTai = readI64(state.data + 64);
  const auto creationUtc = readI64(state.data + 72);
  const auto evaluatedTai = readI64(state.data + 80);
  const auto evaluatedUtc = readI64(state.data + 88);
  const auto sentTai = readI64(state.data + 96);
  const auto sentUtc = readI64(state.data + 104);
  const auto receivedTai = readI64(state.data + 112);
  const auto receivedUtc = readI64(state.data + 120);
  const auto senderState = read32(state.data + 128);
  const auto senderValue = read64(state.data + 132);
  const auto targetState = read32(state.data + 140);
  const auto targetValue = read64(state.data + 144);
  const auto venueFrontier = read64(state.data + 152);
  const auto peerFrontier = read64(state.data + 160);
  const auto peerPresence = read32(state.data + 168);
  const auto peerValue = read64(state.data + 172);
  const auto flags = read64(state.data + 180);
  const auto heartbeat = read32(state.data + 188);
  const auto gapBegin = read64(state.data + 196);
  const auto gapEnd = read64(state.data + 204);
  const auto gapCursor = read64(state.data + 212);
  const auto recoveryKind = read32(state.data + 220);
  const auto recoveryPhase = read32(state.data + 224);
  const auto recoveryBegin = read64(state.data + 228);
  const auto recoveryEnd = read64(state.data + 236);
  const auto recoveryCursor = read64(state.data + 244);
  const auto senderApplicationVersion = read32(state.data + 284);
  const auto targetApplicationVersion = read32(state.data + 288);
  const auto continuation = read32(state.data + 292);
  const auto blockMode = read32(state.data + 296);
  const auto continuationCursor = read64(state.data + 300);
  const auto reason = read32(state.data + 308);
  const bool recoveryActive = recoveryKind != RECOVERY_NONE;
  std::int64_t boundary = 0;
  if (creationTai <= 0 || creationUtc <= 0 || evaluatedTai <= 0 || evaluatedUtc <= 0 || evaluatedTai < creationTai
      || evaluatedUtc < creationUtc || !pairEitherZeroOrPositive(sentTai, sentUtc)
      || !pairEitherZeroOrPositive(receivedTai, receivedUtc) || !validSequence(senderState, senderValue)
      || !validSequence(targetState, targetValue)
      || (senderState == IRFQ_INFINITE_SEQUENCE_VALUE_V2 && peerFrontier >= senderValue)
      || (targetState == IRFQ_INFINITE_SEQUENCE_VALUE_V2 && venueFrontier >= targetValue)
      || peerFrontier >= static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2)
      || venueFrontier >= static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) || peerPresence > 1
      || (peerPresence == 0) != (peerValue == 0) || (flags & ~SESSION_FLAGS_MASK) != 0
      || (flags & SESSION_FLAG_ENABLED) == 0 || !validRangeTriple(gapBegin, gapEnd, gapCursor, gapBegin != 0)
      || !validRangeTriple(recoveryBegin, recoveryEnd, recoveryCursor, recoveryActive) || senderApplicationVersion != 10
      || (targetApplicationVersion != 0 && targetApplicationVersion != 10) || continuation > CONTINUATION_READ_RESULT
      || blockMode > IRFQ_INFINITE_APPLICATION_BLOCK_SEMANTIC_REPLAY_V2 || reason > IRFQ_INFINITE_REASON_INTEGRITY_V2
      || (profile.scheduleMode == 2 && !scheduledBoundary(profile, creationUtc, boundary))) {
    return false;
  }
  if ((profile.heartbeatMode == 1 && heartbeat != profile.configuredHeartbeat)
      || (profile.heartbeatMode == 2
          && ((flags == SESSION_FLAG_ENABLED && heartbeat != 0)
              || (flags != SESSION_FLAG_ENABLED
                  && (heartbeat < profile.minimumHeartbeat || heartbeat > profile.maximumHeartbeat))))) {
    return false;
  }
  if (recoveryKind == RECOVERY_NONE) {
    if (recoveryPhase != RECOVERY_PHASE_NONE || nonzero(state.data + 252, 32)) {
      return false;
    }
  } else if (
      !nonzero(state.data + 252, 32)
      || (recoveryKind == RECOVERY_RESEND_REQUEST && recoveryPhase != RECOVERY_PHASE_STORED_RANGE)
      || (recoveryKind == RECOVERY_LOGON_789
          && (recoveryPhase < RECOVERY_PHASE_PEER_PREFIX || recoveryPhase > RECOVERY_PHASE_FINAL_GAP_FILL))) {
    return false;
  }
  if ((continuation == CONTINUATION_NONE && (blockMode != 0 || continuationCursor != 0))
      || (continuation == CONTINUATION_RESEND
          && (!recoveryActive || blockMode != 0 || continuationCursor != recoveryCursor))
      || ((continuation == 2 || continuation == 4) && blockMode != 0) || (continuation == 3 && blockMode == 0)) {
    return false;
  }
  return true;
}

bool validScanCursor(const irfq_infinite_slice_v2 &input, const irfq_infinite_scan_cursor_v2 &cursor) noexcept {
  if (cursor.stage > IRFQ_INFINITE_SCAN_BODY_V2 || cursor.body_length_has_digit > IRFQ_INFINITE_YES_V2
      || cursor.scan_offset > input.length || cursor.checksum_begin > input.length) {
    return false;
  }
  if (cursor.scan_offset == 0 && cursor.body_length == 0 && cursor.checksum_begin == 0
      && cursor.stage == IRFQ_INFINITE_SCAN_BEGIN_STRING_V2 && cursor.body_length_has_digit == IRFQ_INFINITE_NO_V2) {
    return true;
  }
  const auto matchesAt = [&input, &cursor](std::uint64_t length) {
    if (length > input.length) {
      return false;
    }
    FIX::InfiniteDeclaredFrameCursor derived{};
    std::size_t complete = 0;
    const auto result = FIX::scanInfiniteDeclaredFrame(
        reinterpret_cast<const char *>(input.data),
        static_cast<std::size_t>(length),
        derived,
        complete);
    return result == FIX::InfiniteDeclaredFrameScanResult::NeedMore && derived.frameStart == 0
           && derived.scanOffset == cursor.scan_offset && derived.bodyLength == cursor.body_length
           && derived.checksumBegin == cursor.checksum_begin && derived.stage == cursor.stage
           && derived.bodyLengthHasDigit == (cursor.body_length_has_digit != 0);
  };
  return matchesAt(cursor.scan_offset) || matchesAt(cursor.scan_offset + 1) || matchesAt(cursor.scan_offset + 2)
         || matchesAt(cursor.scan_offset + 3);
}

bool validPrepareKind(std::uint32_t kind) noexcept {
  return kind >= IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2 && kind <= IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
}

bool validStage(std::uint32_t stage) noexcept {
  return stage >= IRFQ_INFINITE_STAGE_HEAD_V2 && stage <= IRFQ_INFINITE_STAGE_EVENT_V2;
}

bool validEvent(std::uint32_t event) noexcept {
  return event >= IRFQ_INFINITE_EVENT_INBOUND_FRAME_V2 && event <= IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2;
}
} // namespace

struct PendingPlan {
  irfq_infinite_prepare_id_v2 id{};
  std::uint32_t step{0};
  irfq_infinite_prepare_kind_v2 kind{0};
  irfq_infinite_stage_v2 stage{0};
  irfq_infinite_event_v2 event{0};
  std::array<std::uint8_t, 32> eventIdentity{};
  std::uint64_t baseEpoch{0};
  std::uint64_t baseRevision{0};
  std::uint64_t resultEpoch{0};
  std::uint64_t resultRevision{0};
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::array<std::uint8_t, 32> stateDigest{};
  std::vector<std::uint8_t> output;
  std::vector<irfq_infinite_declarative_action_v2> actions;
  irfq_infinite_status_v2 pendingStatus{IRFQ_INFINITE_STATUS_READY_V2};
  std::uint64_t subjectSequence{0};
  std::uint64_t classifiedSequence{0};
  std::array<std::uint8_t, 32> subject{};
  std::string msgType;
  irfq_infinite_input_source_v2 inputSource{IRFQ_INFINITE_INPUT_NONE_V2};
  std::uint32_t inputItemIndex{0};
  std::uint64_t inputOffset{0};
  std::uint64_t inputLength{0};
  std::uint64_t sourceLength{0};
  std::array<std::uint8_t, 32> reborrowDigest{};
  std::array<std::uint8_t, 32> contentDigest{};
  bool applicationDispatch{false};
  bool materialized{false};
};

struct irfq_infinite_session_v2 {
  Profile profile;
  FIX::InfiniteSessionStaticProfile staticProfile;
  FIX::DataDictionaryProvider dictionaries;
  std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> state{};
  std::uint64_t epoch{0};
  std::uint64_t revision{0};
  std::uint64_t identity{0};
  std::uint64_t nextPlan{1};
  std::unique_ptr<PendingPlan> pending;
  bool terminal{false};
};

namespace {
FIX::InfiniteSessionStaticProfile staticProfile(const Profile &profile) {
  return {
      profile.venueSubId,
      profile.venueLocationId,
      profile.participantSubId,
      profile.participantLocationId,
      profile.qualifier,
      profile.defaultCustomApplicationVersion,
      profile.scheduleMode,
      profile.schedule,
      profile.heartbeatMode,
      profile.configuredHeartbeat,
      profile.minimumHeartbeat,
      profile.maximumHeartbeat,
      profile.timestampPrecision,
      profile.maximumLatency,
      profile.sendRedundantResendRequests,
      profile.checkCompId,
      profile.checkLatency,
      profile.resetOnLogon,
      profile.resetOnLogout,
      profile.resetOnDisconnect,
      profile.refreshOnLogon,
      profile.persistMessages,
      profile.validateLengthAndChecksum,
      profile.sendNextExpectedMsgSeqNum};
}

void writeSenderSuccessor(
    std::array<std::uint8_t, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2> &state,
    std::uint64_t successor) {
  if (successor == static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2)) {
    write32(state.data() + 128, IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2);
    write64(state.data() + 132, 0);
  } else if (successor > 0 && successor < static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2)) {
    write32(state.data() + 128, IRFQ_INFINITE_SEQUENCE_VALUE_V2);
    write64(state.data() + 132, successor);
  } else {
    throw std::length_error("Sender successor");
  }
}

bool copyMessageType(
    const std::string &source,
    std::uint32_t &length,
    std::uint8_t (&destination)[IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2]) noexcept {
  if (source.empty() || source.size() > std::size(destination)
      || !std::all_of(source.begin(), source.end(), [](unsigned char byte) { return byte >= 0x21 && byte <= 0x7e; })) {
    return false;
  }
  std::fill(std::begin(destination), std::end(destination), std::uint8_t{0});
  length = static_cast<std::uint32_t>(source.size());
  std::copy(source.begin(), source.end(), destination);
  return true;
}

struct PrepareOutputView {
  irfq_infinite_buffer_v2 state{};
  irfq_infinite_buffer_v2 output{};
  irfq_infinite_declarative_action_v2 *actions{nullptr};
  std::uint32_t actionCapacity{0};
};

bool validPrepareOutput(
    const irfq_infinite_prepare_request_v2 &request,
    const irfq_infinite_prepare_response_v2 &response,
    const PrepareOutputView &view) noexcept {
  if (view.state.length != 0 || view.state.capacity != IRFQ_INFINITE_NATIVE_STATE_BYTES_V2 || view.state.data == nullptr
      || view.output.length != 0 || view.output.capacity > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2
      || (view.output.capacity != 0 && view.output.data == nullptr)
      || view.actionCapacity > IRFQ_INFINITE_MAX_ACTIONS_V2 || (view.actionCapacity != 0 && view.actions == nullptr)
      || (view.actionCapacity == 0 && view.actions != nullptr)) {
    return false;
  }
  Range requestRange;
  Range responseRange;
  Range payloadRange;
  Range stateRange;
  Range outputRange;
  Range actionRange;
  if (!range(&request, sizeof(request), requestRange) || !range(&response, sizeof(response), responseRange)
      || !sliceRange(request.payload, IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2, payloadRange)
      || !range(view.state.data, view.state.capacity, stateRange)
      || !range(view.output.data, view.output.capacity, outputRange)
      || !range(
          view.actions,
          static_cast<std::uint64_t>(view.actionCapacity) * sizeof(irfq_infinite_declarative_action_v2),
          actionRange)) {
    return false;
  }
  const std::vector<Range> all{requestRange, responseRange, payloadRange, stateRange, outputRange, actionRange};
  for (std::size_t left = 0; left < all.size(); ++left) {
    for (std::size_t right = left + 1; right < all.size(); ++right) {
      if (overlaps(all[left], all[right])) {
        return false;
      }
    }
  }
  return true;
}

bool validResumeOutput(
    const irfq_infinite_resume_request_v2 &request,
    const irfq_infinite_prepare_response_v2 &response,
    const PrepareOutputView &view) noexcept {
  if (view.state.length != 0 || view.state.capacity != IRFQ_INFINITE_NATIVE_STATE_BYTES_V2 || view.state.data == nullptr
      || view.output.length != 0 || view.output.capacity > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2
      || (view.output.capacity != 0 && view.output.data == nullptr)
      || view.actionCapacity > IRFQ_INFINITE_MAX_ACTIONS_V2 || (view.actionCapacity != 0 && view.actions == nullptr)
      || (view.actionCapacity == 0 && view.actions != nullptr)) {
    return false;
  }
  Range requestRange;
  Range responseRange;
  Range sourceRange;
  Range stateRange;
  Range outputRange;
  Range actionRange;
  Range rowRange;
  if (!range(&request, sizeof(request), requestRange) || !range(&response, sizeof(response), responseRange)
      || !sliceRange(request.input_source_bytes, IRFQ_INFINITE_MAX_STORE_RANGE_BYTES_V2, sourceRange)
      || !range(view.state.data, view.state.capacity, stateRange)
      || !range(view.output.data, view.output.capacity, outputRange)
      || !range(
          view.actions,
          static_cast<std::uint64_t>(view.actionCapacity) * sizeof(irfq_infinite_declarative_action_v2),
          actionRange)
      || !range(
          request.store_rows,
          static_cast<std::uint64_t>(request.store_row_count) * sizeof(irfq_infinite_store_row_v2),
          rowRange)) {
    return false;
  }
  const std::vector<Range>
      all{requestRange, responseRange, sourceRange, stateRange, outputRange, actionRange, rowRange};
  for (std::size_t left = 0; left < all.size(); ++left) {
    for (std::size_t right = left + 1; right < all.size(); ++right) {
      if (overlaps(all[left], all[right])) {
        return false;
      }
    }
  }
  return true;
}

irfq_infinite_status_v2 describePlan(
    PendingPlan &plan,
    const Profile &profile,
    const PrepareOutputView &view,
    irfq_infinite_prepare_response_v2 *response) noexcept {
  response->prepare_id = plan.id;
  response->step = plan.step;
  response->kind = plan.kind;
  response->stage = plan.stage;
  response->event = plan.event;
  std::copy(plan.eventIdentity.begin(), plan.eventIdentity.end(), response->event_identity_sha256);
  response->base_epoch = plan.baseEpoch;
  response->base_revision = plan.baseRevision;
  if (plan.pendingStatus == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2
      || plan.pendingStatus == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2) {
    response->subject_sequence = plan.subjectSequence;
    std::copy(plan.subject.begin(), plan.subject.end(), response->subject_sha256);
    if (plan.pendingStatus == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2
        && !copyMessageType(plan.msgType, response->msg_type_length, response->msg_type)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    response->input_source = plan.inputSource;
    response->input_item_index = plan.inputItemIndex;
    response->input_offset = plan.inputOffset;
    response->input_length = plan.inputLength;
    return publish(response, plan.pendingStatus);
  }
  response->result_epoch = plan.resultEpoch;
  response->result_revision = plan.resultRevision;
  response->native_state = view.state;
  response->output = view.output;
  response->actions = view.actions;
  response->action_capacity = view.actionCapacity;
  if (view.output.capacity < plan.output.size()) {
    response->required_output_capacity = plan.output.size();
    return publish(response, IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  }
  if (view.actionCapacity < plan.actions.size()) {
    return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }
  const irfq_infinite_slice_v2 state{plan.state.data(), plan.state.size()};
  if (plan.output.size() > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 || plan.actions.size() > IRFQ_INFINITE_MAX_ACTIONS_V2
      || !validNativeState(state, profile, plan.resultEpoch, plan.resultRevision)
      || std::any_of(plan.actions.begin(), plan.actions.end(), [](const auto &action) {
           return action.msg_type_length > IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2
                  || (action.msg_type_length != 0
                      && !std::all_of(action.msg_type, action.msg_type + action.msg_type_length, [](std::uint8_t byte) {
                           return byte >= 0x21 && byte <= 0x7e;
                         }));
         })) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  }
  std::copy(plan.state.begin(), plan.state.end(), response->native_state.data);
  response->native_state.length = plan.state.size();
  std::copy(plan.output.begin(), plan.output.end(), response->output.data);
  response->output.length = plan.output.size();
  std::copy(plan.actions.begin(), plan.actions.end(), response->actions);
  response->action_count = plan.actions.size();
  response->output_frame_count
      = static_cast<std::uint32_t>(std::count_if(plan.actions.begin(), plan.actions.end(), [](const auto &action) {
          return action.kind == IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
        }));
  std::copy(plan.stateDigest.begin(), plan.stateDigest.end(), response->native_state_sha256);
  plan.materialized = true;
  return publish(response, IRFQ_INFINITE_STATUS_READY_V2);
}

std::unique_ptr<PendingPlan> adminOutputPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request,
    const FIX::InfiniteHeartbeatPlan &output,
    char msgType,
    std::uint32_t testRequestCount,
    std::uint32_t disconnectReason) {
  const auto senderSequence = read64(session.state.data() + 132);
  const auto &wire = output.output;
  if (wire.empty() || wire.size() > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2) {
    throw std::length_error("Admin output");
  }
  auto plan = std::make_unique<PendingPlan>();
  plan->id = {session.identity, session.nextPlan++};
  plan->kind = request.kind;
  plan->stage = request.stage;
  plan->event = request.event;
  std::copy_n(request.event_identity_sha256, plan->eventIdentity.size(), plan->eventIdentity.begin());
  plan->baseEpoch = session.epoch;
  plan->baseRevision = session.revision;
  plan->resultEpoch = session.epoch;
  plan->resultRevision = session.revision + 1;
  plan->state = session.state;
  write64(plan->state.data() + 56, plan->resultRevision);
  write64(plan->state.data() + 80, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 88, static_cast<std::uint64_t>(request.now_utc_ns));
  write64(plan->state.data() + 96, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 104, static_cast<std::uint64_t>(request.now_utc_ns));
  writeSenderSuccessor(plan->state, output.nextSenderSequence);
  write32(plan->state.data() + 192, testRequestCount);
  if (disconnectReason != IRFQ_INFINITE_REASON_NONE_V2) {
    write64(plan->state.data() + 180, read64(plan->state.data() + 180) | UINT64_C(0x110));
    write32(plan->state.data() + 308, disconnectReason);
  }
  plan->output.assign(wire.begin(), wire.end());
  irfq_infinite_declarative_action_v2 action{};
  action.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
  action.output_class = IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2;
  action.msg_type_length = 1;
  action.msg_type[0] = static_cast<std::uint8_t>(msgType);
  action.sequence_begin = senderSequence;
  action.sequence_end_exclusive = senderSequence + 1;
  action.output_length = plan->output.size();
  Sha256 wireSha;
  wireSha.update(plan->output.data(), plan->output.size());
  const auto wireDigest = wireSha.finish();
  std::copy(wireDigest.begin(), wireDigest.end(), action.binding_sha256);
  plan->actions.push_back(action);
  if (disconnectReason != IRFQ_INFINITE_REASON_NONE_V2) {
    irfq_infinite_declarative_action_v2 disconnect{};
    disconnect.kind = IRFQ_INFINITE_ACTION_DISCONNECT_V2;
    disconnect.reason_code = disconnectReason;
    plan->actions.push_back(disconnect);
  }
  plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
  return plan;
}

bool attachedForAdmin(const irfq_infinite_session_v2 &session) noexcept {
  constexpr std::uint64_t attached = UINT64_C(1) | UINT64_C(2) | UINT64_C(4) | UINT64_C(128);
  const auto senderSequence = read64(session.state.data() + 132);
  return (read64(session.state.data() + 180) & attached) == attached
         && read32(session.state.data() + 128) == IRFQ_INFINITE_SEQUENCE_VALUE_V2 && senderSequence != 0
         && senderSequence < static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2);
}

std::unique_ptr<PendingPlan> adminHeartbeatPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  const auto testRequestIdLength = read32(request.payload.data + 32);
  if (!attachedForAdmin(session) || request.payload.length != 36 + testRequestIdLength
      || testRequestIdLength > IRFQ_INFINITE_MAX_TEST_REQUEST_ID_BYTES_V2) {
    throw std::invalid_argument("Heartbeat event");
  }
  const std::string testRequestId(
      reinterpret_cast<const char *>(request.payload.data + 36),
      static_cast<std::size_t>(testRequestIdLength));
  const auto output = FIX::InfiniteSessionPlanner::heartbeat(
      session.profile.beginString,
      session.profile.venueCompId,
      session.profile.participantCompId,
      read32(session.state.data() + 188),
      read64(session.state.data() + 132),
      read64(session.state.data() + 144),
      request.now_utc_ns,
      testRequestId,
      read64(session.state.data() + 152),
      &session.dictionaries,
      &session.staticProfile);
  return adminOutputPlan(
      session,
      request,
      output,
      '0',
      read32(session.state.data() + 192),
      IRFQ_INFINITE_REASON_NONE_V2);
}

std::unique_ptr<PendingPlan> adminTestRequestPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  const auto testRequestCount = read32(session.state.data() + 192);
  if (!attachedForAdmin(session) || request.payload.length != 32 || testRequestCount == UINT32_MAX) {
    throw std::invalid_argument("TestRequest event");
  }
  const auto output = FIX::InfiniteSessionPlanner::testRequest(
      session.profile.beginString,
      session.profile.venueCompId,
      session.profile.participantCompId,
      read32(session.state.data() + 188),
      read64(session.state.data() + 132),
      read64(session.state.data() + 144),
      request.now_utc_ns,
      read64(session.state.data() + 152),
      &session.dictionaries,
      &session.staticProfile);
  return adminOutputPlan(session, request, output, '1', testRequestCount + 1, IRFQ_INFINITE_REASON_NONE_V2);
}

std::unique_ptr<PendingPlan> adminRejectPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  const auto refSequence = read64(request.payload.data + 32);
  const auto refTag = read32(request.payload.data + 40);
  const auto rejectReason = read32(request.payload.data + 44);
  const auto refMsgTypeLength = read32(request.payload.data + 48);
  if (!attachedForAdmin(session) || refSequence == 0
      || refSequence >= static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2)
      || refTag > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || (rejectReason > 18 && rejectReason != 99) || refMsgTypeLength > IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2
      || request.payload.length != 52 + refMsgTypeLength
      || !std::all_of(request.payload.data + 52, request.payload.data + 52 + refMsgTypeLength, [](std::uint8_t byte) {
           return byte >= 0x21 && byte <= 0x7e;
         })) {
    throw std::invalid_argument("Reject event");
  }
  const std::string refMsgType(
      reinterpret_cast<const char *>(request.payload.data + 52),
      static_cast<std::size_t>(refMsgTypeLength));
  const auto output = FIX::InfiniteSessionPlanner::reject(
      session.profile.beginString,
      session.profile.venueCompId,
      session.profile.participantCompId,
      read32(session.state.data() + 188),
      read64(session.state.data() + 132),
      read64(session.state.data() + 144),
      request.now_utc_ns,
      refSequence,
      refTag,
      rejectReason,
      refMsgType,
      read64(session.state.data() + 152),
      &session.dictionaries,
      &session.staticProfile);
  return adminOutputPlan(
      session,
      request,
      output,
      '3',
      read32(session.state.data() + 192),
      IRFQ_INFINITE_REASON_NONE_V2);
}

std::unique_ptr<PendingPlan> registeredInboundPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  const auto frameLength = read32(request.payload.data + 32);
  const auto sessionFlags = read64(session.state.data() + 180);
  const bool detached = sessionFlags == SESSION_FLAG_ENABLED;
  if (request.payload.length < 68 || frameLength == 0 || frameLength > IRFQ_INFINITE_MAX_FRAME_BYTES_V2
      || request.payload.length != 68 + frameLength || (!attachedForAdmin(session) && !detached)
      || read32(session.state.data() + 128) != IRFQ_INFINITE_SEQUENCE_VALUE_V2
      || read32(session.state.data() + 140) != IRFQ_INFINITE_SEQUENCE_VALUE_V2) {
    throw std::invalid_argument("Inbound event");
  }
  const auto *frameBytes = request.payload.data + 68;
  Sha256 sha;
  sha.update(frameBytes, frameLength);
  const auto frameDigest = sha.finish();
  if (!std::equal(frameDigest.begin(), frameDigest.end(), request.payload.data + 36)) {
    throw std::invalid_argument("Inbound frame digest");
  }
  FIX::InfiniteDeclaredFrameCursor cursor{};
  std::size_t complete = 0;
  if (FIX::scanInfiniteDeclaredFrame(reinterpret_cast<const char *>(frameBytes), frameLength, cursor, complete)
          != FIX::InfiniteDeclaredFrameScanResult::Ready
      || complete != frameLength) {
    throw std::invalid_argument("Inbound complete frame");
  }
  const std::string wire(reinterpret_cast<const char *>(frameBytes), frameLength);
  const auto inbound = FIX::InfiniteSessionPlanner::inbound(
      session.profile.beginString,
      session.profile.venueCompId,
      session.profile.participantCompId,
      read32(session.state.data() + 188),
      read64(session.state.data() + 132),
      read64(session.state.data() + 144),
      request.now_utc_ns,
      readI64(session.state.data() + 104) == 0 ? readI64(session.state.data() + 72)
                                               : readI64(session.state.data() + 104),
      readI64(session.state.data() + 120) == 0 ? readI64(session.state.data() + 72)
                                               : readI64(session.state.data() + 120),
      sessionFlags,
      read32(session.state.data() + 192),
      read64(session.state.data() + 152),
      wire,
      session.dictionaries,
      staticProfile(session.profile));
  if ((!inbound.admin && !inbound.application && inbound.identityMatches && inbound.timeMatches
       && inbound.outputs.empty())
      || (inbound.admin && inbound.application) || inbound.outputs.size() != inbound.outputMsgTypes.size()
      || inbound.outputs.size() != inbound.outputSequences.size()) {
    throw std::invalid_argument("Inbound classification");
  }

  auto plan = std::make_unique<PendingPlan>();
  plan->id = {session.identity, session.nextPlan++};
  plan->kind = request.kind;
  plan->stage = request.stage;
  plan->event = request.event;
  std::copy_n(request.event_identity_sha256, plan->eventIdentity.size(), plan->eventIdentity.begin());
  plan->baseEpoch = session.epoch;
  plan->baseRevision = session.revision;
  plan->resultEpoch = session.epoch;
  plan->resultRevision = session.revision + 1;
  plan->state = session.state;
  write64(plan->state.data() + 56, plan->resultRevision);
  write64(plan->state.data() + 80, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 88, static_cast<std::uint64_t>(request.now_utc_ns));
  write64(plan->state.data() + 112, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 120, static_cast<std::uint64_t>(request.now_utc_ns));
  write32(plan->state.data() + 192, inbound.testRequestCount);
  if (detached && inbound.disconnected && session.profile.heartbeatMode == 2) {
    write32(plan->state.data() + 188, inbound.heartbeatSeconds);
  }
  const auto expectedTarget = read64(session.state.data() + 144);
  const auto appendOutputs = [&] {
    std::uint64_t offset = 0;
    for (std::size_t index = 0; index < inbound.outputs.size(); ++index) {
      const auto &output = inbound.outputs[index];
      if (output.empty() || output.size() > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 - offset
          || inbound.outputMsgTypes[index].empty()
          || inbound.outputMsgTypes[index].size() > IRFQ_INFINITE_MAX_MESSAGE_TYPE_BYTES_V2) {
        throw std::length_error("Inbound output");
      }
      plan->output.insert(plan->output.end(), output.begin(), output.end());
      irfq_infinite_declarative_action_v2 action{};
      action.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
      action.output_class = IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2;
      if (!copyMessageType(inbound.outputMsgTypes[index], action.msg_type_length, action.msg_type)) {
        throw std::length_error("Inbound output MsgType");
      }
      action.sequence_begin = inbound.outputSequences[index];
      action.sequence_end_exclusive = inbound.outputSequences[index] + 1;
      action.output_offset = offset;
      action.output_length = output.size();
      Sha256 outputSha;
      outputSha.update(reinterpret_cast<const std::uint8_t *>(output.data()), output.size());
      const auto outputDigest = outputSha.finish();
      std::copy(outputDigest.begin(), outputDigest.end(), action.binding_sha256);
      plan->actions.push_back(action);
      offset += output.size();
    }
    if (!inbound.outputs.empty()) {
      write64(plan->state.data() + 96, static_cast<std::uint64_t>(request.now_tai_ns));
      write64(plan->state.data() + 104, static_cast<std::uint64_t>(request.now_utc_ns));
      writeSenderSuccessor(plan->state, inbound.nextSenderSequence);
    }
  };
  if (!inbound.identified || !inbound.sequenceValid) {
    appendOutputs();
    const auto sentLogout
        = std::find(inbound.outputMsgTypes.begin(), inbound.outputMsgTypes.end(), "5") != inbound.outputMsgTypes.end();
    const auto terminalReason = !inbound.identified || (inbound.identityMatches && inbound.timeMatches)
                                    ? IRFQ_INFINITE_REASON_PROTOCOL_V2
                                : !inbound.identityMatches ? IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2
                                                           : IRFQ_INFINITE_REASON_LATENCY_V2;
    write64(
        plan->state.data() + 180,
        read64(plan->state.data() + 180) | UINT64_C(256) | (sentLogout ? UINT64_C(16) : UINT64_C(0)));
    write32(plan->state.data() + 308, terminalReason);
    irfq_infinite_declarative_action_v2 disconnect{};
    disconnect.kind = IRFQ_INFINITE_ACTION_DISCONNECT_V2;
    disconnect.reason_code = terminalReason;
    plan->actions.push_back(disconnect);
    plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
    return plan;
  }
  if (inbound.resetLogon && inbound.identityMatches && inbound.timeMatches) {
    plan->pendingStatus = IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2;
    std::copy_n(request.payload.data, plan->subject.size(), plan->subject.begin());
    plan->inputSource = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
    plan->inputOffset = 0;
    plan->inputLength = request.payload.length;
    plan->sourceLength = request.payload.length;
    plan->classifiedSequence = inbound.sequence;
    Sha256 reborrowSha;
    reborrowSha.update(reinterpret_cast<const std::uint8_t *>(REBORROW_DOMAIN), sizeof(REBORROW_DOMAIN) - 1);
    const std::uint8_t zero = 0;
    reborrowSha.update(&zero, 1);
    std::array<std::uint8_t, 16> reborrowFields{};
    write32(reborrowFields.data(), plan->inputSource);
    write32(reborrowFields.data() + 4, plan->inputItemIndex);
    write64(reborrowFields.data() + 8, plan->sourceLength);
    reborrowSha.update(reborrowFields.data(), reborrowFields.size());
    reborrowSha.update(request.payload.data, static_cast<std::size_t>(request.payload.length));
    plan->reborrowDigest = reborrowSha.finish();
    return plan;
  }
  if (detached && inbound.identityMatches && inbound.timeMatches && !inbound.disconnected) {
    if (!inbound.admin || inbound.application || inbound.msgType != "A" || inbound.sequence != expectedTarget
        || inbound.outputs.size() != 1 || inbound.outputMsgTypes[0] != "A" || inbound.disconnected
        || inbound.nextSenderSequence != read64(session.state.data() + 132) + 1
        || inbound.nextTargetSequence != expectedTarget + 1) {
      throw std::invalid_argument("Inbound attachment Logon");
    }
    irfq_infinite_declarative_action_v2 disposition{};
    disposition.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
    disposition.disposition = IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2;
    disposition.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
    disposition.sequence_begin = inbound.sequence;
    disposition.sequence_end_exclusive = inbound.sequence + 1;
    disposition.input_offset = 68;
    disposition.input_length = frameLength;
    std::copy_n(request.payload.data, 32, disposition.binding_sha256);
    plan->actions.push_back(disposition);
    appendOutputs();
    const auto recoveryKind = read32(plan->state.data() + 220);
    const auto recoveryPhase = read32(plan->state.data() + 224);
    const bool peerPrefix = recoveryKind == RECOVERY_LOGON_789 && recoveryPhase == RECOVERY_PHASE_PEER_PREFIX;
    write64(plan->state.data() + 180, peerPrefix ? SESSION_FLAG_ENABLED : UINT64_C(135));
    write32(plan->state.data() + 188, inbound.heartbeatSeconds);
    write32(plan->state.data() + 288, 10);
    if (recoveryKind != RECOVERY_NONE && recoveryPhase != RECOVERY_PHASE_LOGON_RESPONSE) {
      write32(plan->state.data() + 292, CONTINUATION_RESEND);
      write32(plan->state.data() + 296, IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2);
      write64(plan->state.data() + 300, read64(plan->state.data() + 244));
    }
    plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
    return plan;
  }
  if (inbound.sequence > expectedTarget && inbound.identityMatches && inbound.timeMatches && inbound.outputs.size() == 1
      && inbound.outputMsgTypes[0] == "2" && !inbound.disconnected && inbound.msgType != "A"
      && inbound.nextTargetSequence == expectedTarget) {
    if (inbound.admin) {
      irfq_infinite_declarative_action_v2 queue{};
      queue.kind = IRFQ_INFINITE_ACTION_QUEUE_INSERT_V2;
      if (!copyMessageType(inbound.msgType, queue.msg_type_length, queue.msg_type)) {
        throw std::length_error("Inbound queue MsgType");
      }
      queue.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
      queue.sequence_begin = inbound.sequence;
      queue.sequence_end_exclusive = inbound.sequence + 1;
      queue.input_offset = 68;
      queue.input_length = frameLength;
      std::copy(frameDigest.begin(), frameDigest.end(), queue.binding_sha256);
      plan->actions.push_back(queue);
      write64(plan->state.data() + 196, expectedTarget);
      write64(plan->state.data() + 204, inbound.sequence + 1);
      write64(plan->state.data() + 212, expectedTarget);
    }
    irfq_infinite_declarative_action_v2 disposition{};
    disposition.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
    disposition.disposition = IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2;
    disposition.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
    disposition.sequence_begin = inbound.sequence;
    disposition.sequence_end_exclusive = inbound.sequence + 1;
    disposition.input_offset = 68;
    disposition.input_length = frameLength;
    std::copy_n(request.payload.data, 32, disposition.binding_sha256);
    disposition.reason_code = IRFQ_INFINITE_REASON_SEQUENCE_V2;
    plan->actions.push_back(disposition);
    appendOutputs();
    plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
    return plan;
  }
  if (inbound.sequence < expectedTarget && inbound.identityMatches && inbound.timeMatches) {
    irfq_infinite_declarative_action_v2 disposition{};
    disposition.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
    disposition.disposition = IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2;
    disposition.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
    disposition.sequence_begin = inbound.sequence;
    disposition.sequence_end_exclusive = inbound.sequence + 1;
    disposition.input_offset = 68;
    disposition.input_length = frameLength;
    std::copy_n(request.payload.data, 32, disposition.binding_sha256);
    disposition.reason_code
        = inbound.disconnected ? IRFQ_INFINITE_REASON_PROTOCOL_V2 : IRFQ_INFINITE_REASON_SEQUENCE_V2;
    plan->actions.push_back(disposition);
    appendOutputs();
    if (inbound.disconnected) {
      const auto sentLogout = std::find(inbound.outputMsgTypes.begin(), inbound.outputMsgTypes.end(), "5")
                              != inbound.outputMsgTypes.end();
      const auto logoutFlags
          = (sentLogout ? UINT64_C(16) : UINT64_C(0)) | (inbound.msgType == "5" ? UINT64_C(8) : UINT64_C(0));
      write64(plan->state.data() + 180, read64(plan->state.data() + 180) | UINT64_C(256) | logoutFlags);
      write32(plan->state.data() + 308, IRFQ_INFINITE_REASON_PROTOCOL_V2);
      irfq_infinite_declarative_action_v2 disconnect{};
      disconnect.kind = IRFQ_INFINITE_ACTION_DISCONNECT_V2;
      disconnect.reason_code = IRFQ_INFINITE_REASON_PROTOCOL_V2;
      plan->actions.push_back(disconnect);
    }
    plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
    return plan;
  }
  if (inbound.application
      || (inbound.msgType == "4" && inbound.outputs.empty() && !inbound.disconnected
          && inbound.nextTargetSequence > expectedTarget)) {
    if (inbound.bodyLength == 0 || inbound.bodyOffset + inbound.bodyLength > frameLength) {
      throw std::invalid_argument("Inbound application body");
    }
    const auto bodyDigest = [&] {
      Sha256 bodySha;
      bodySha.update(frameBytes + inbound.bodyOffset, static_cast<std::size_t>(inbound.bodyLength));
      return bodySha.finish();
    }();
    Sha256 subjectSha;
    subjectSha.update(
        reinterpret_cast<const std::uint8_t *>(DECISION_SUBJECT_DOMAIN),
        sizeof(DECISION_SUBJECT_DOMAIN) - 1);
    const std::uint8_t zero = 0;
    subjectSha.update(&zero, 1);
    subjectSha.update(session.profile.binding.data(), session.profile.binding.size());
    std::array<std::uint8_t, 80> fields{};
    write32(fields.data(), request.kind);
    write32(fields.data() + 4, request.stage);
    write32(fields.data() + 8, request.event);
    write64(fields.data() + 12, request.expected_epoch);
    write64(fields.data() + 20, request.expected_revision);
    std::copy_n(request.payload.data, 32, fields.data() + 28);
    write64(fields.data() + 60, inbound.sequence);
    write32(fields.data() + 68, IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2);
    write32(fields.data() + 72, 0);
    subjectSha.update(fields.data(), 76);
    std::array<std::uint8_t, 20> descriptor{};
    write64(descriptor.data(), 68 + inbound.bodyOffset);
    write64(descriptor.data() + 8, inbound.bodyLength);
    write32(descriptor.data() + 16, static_cast<std::uint32_t>(inbound.msgType.size()));
    subjectSha.update(descriptor.data(), descriptor.size());
    subjectSha.update(
        reinterpret_cast<const std::uint8_t *>(inbound.msgType.data()),
        static_cast<std::size_t>(inbound.msgType.size()));
    subjectSha.update(bodyDigest.data(), bodyDigest.size());
    plan->pendingStatus = IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2;
    plan->applicationDispatch = inbound.application;
    plan->subjectSequence = inbound.sequence;
    plan->subject = subjectSha.finish();
    plan->msgType = inbound.msgType;
    plan->inputSource = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
    plan->inputOffset = 68 + inbound.bodyOffset;
    plan->inputLength = inbound.bodyLength;
    plan->sourceLength = request.payload.length;
    plan->contentDigest = bodyDigest;
    Sha256 reborrowSha;
    reborrowSha.update(reinterpret_cast<const std::uint8_t *>(REBORROW_DOMAIN), sizeof(REBORROW_DOMAIN) - 1);
    reborrowSha.update(&zero, 1);
    std::array<std::uint8_t, 16> reborrowFields{};
    write32(reborrowFields.data(), plan->inputSource);
    write32(reborrowFields.data() + 4, plan->inputItemIndex);
    write64(reborrowFields.data() + 8, plan->sourceLength);
    reborrowSha.update(reborrowFields.data(), reborrowFields.size());
    reborrowSha.update(request.payload.data, static_cast<std::size_t>(request.payload.length));
    plan->reborrowDigest = reborrowSha.finish();
    return plan;
  }
  irfq_infinite_declarative_action_v2 disposition{};
  disposition.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
  const auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  const bool ordinarySuccessor
      = (expectedTarget < lastLegal && inbound.nextTargetSequence == expectedTarget + 1)
        || (expectedTarget == lastLegal
            && inbound.nextTargetSequence == static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2));
  if (inbound.nextTargetSequence != expectedTarget && !ordinarySuccessor) {
    throw std::invalid_argument("Inbound target transition");
  }
  disposition.disposition = ordinarySuccessor ? IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2
                                              : IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2;
  disposition.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
  disposition.sequence_begin = inbound.sequence;
  disposition.sequence_end_exclusive = inbound.sequence + 1;
  disposition.input_offset = 68;
  disposition.input_length = frameLength;
  std::copy_n(request.payload.data, 32, disposition.binding_sha256);
  const auto dispositionReason
      = !inbound.identityMatches ? IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2
        : !inbound.timeMatches   ? IRFQ_INFINITE_REASON_LATENCY_V2
        : inbound.disconnected   ? IRFQ_INFINITE_REASON_PROTOCOL_V2
        : std::find(inbound.outputMsgTypes.begin(), inbound.outputMsgTypes.end(), "3") != inbound.outputMsgTypes.end()
            ? IRFQ_INFINITE_REASON_PROTOCOL_V2
            : IRFQ_INFINITE_REASON_NONE_V2;
  disposition.reason_code = dispositionReason;
  plan->actions.push_back(disposition);
  appendOutputs();
  if (inbound.disconnected || !inbound.identityMatches || !inbound.timeMatches) {
    const auto sentLogout
        = std::find(inbound.outputMsgTypes.begin(), inbound.outputMsgTypes.end(), "5") != inbound.outputMsgTypes.end();
    const auto logoutFlags
        = (sentLogout ? UINT64_C(16) : UINT64_C(0)) | (inbound.msgType == "5" ? UINT64_C(8) : UINT64_C(0));
    write64(plan->state.data() + 180, read64(plan->state.data() + 180) | UINT64_C(256) | logoutFlags);
    write32(
        plan->state.data() + 308,
        !inbound.identityMatches ? IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2
        : !inbound.timeMatches   ? IRFQ_INFINITE_REASON_LATENCY_V2
                                 : IRFQ_INFINITE_REASON_PROTOCOL_V2);
    irfq_infinite_declarative_action_v2 disconnect{};
    disconnect.kind = IRFQ_INFINITE_ACTION_DISCONNECT_V2;
    disconnect.reason_code = !inbound.identityMatches ? IRFQ_INFINITE_REASON_IDENTITY_MISMATCH_V2
                             : !inbound.timeMatches   ? IRFQ_INFINITE_REASON_LATENCY_V2
                                                      : IRFQ_INFINITE_REASON_PROTOCOL_V2;
    plan->actions.push_back(disconnect);
  }
  plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
  return plan;
}

const char *reasonText(std::uint32_t reason) {
  static constexpr const char *TEXT[]{
      "",
      "Identity mismatch",
      "Session time",
      "Latency",
      "Sequence",
      "Dictionary",
      "Reset rejected",
      "Heartbeat timeout",
      "Protocol",
      "Integrity"};
  if (reason == 0 || reason >= std::size(TEXT)) {
    throw std::invalid_argument("Admin reason");
  }
  return TEXT[reason];
}

std::unique_ptr<PendingPlan> adminLogoutPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  const auto reason = read32(request.payload.data + 32);
  if (!attachedForAdmin(session) || request.payload.length != 36) {
    throw std::invalid_argument("Logout event");
  }
  const auto output = FIX::InfiniteSessionPlanner::logout(
      session.profile.beginString,
      session.profile.venueCompId,
      session.profile.participantCompId,
      read32(session.state.data() + 188),
      read64(session.state.data() + 132),
      read64(session.state.data() + 144),
      request.now_utc_ns,
      reasonText(reason),
      read64(session.state.data() + 152),
      &session.dictionaries,
      &session.staticProfile);
  return adminOutputPlan(session, request, output, '5', read32(session.state.data() + 192), reason);
}

std::unique_ptr<PendingPlan> adminResendRequestPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  const auto begin = read64(request.payload.data + 32);
  const auto end = read64(request.payload.data + 40);
  if (!attachedForAdmin(session) || request.payload.length != 48 || begin != read64(session.state.data() + 144)
      || end != 0) {
    throw std::invalid_argument("ResendRequest event");
  }
  const auto output = FIX::InfiniteSessionPlanner::resendRequest(
      session.profile.beginString,
      session.profile.venueCompId,
      session.profile.participantCompId,
      read32(session.state.data() + 188),
      read64(session.state.data() + 132),
      read64(session.state.data() + 144),
      request.now_utc_ns,
      read64(session.state.data() + 152),
      &session.dictionaries,
      &session.staticProfile);
  return adminOutputPlan(
      session,
      request,
      output,
      '2',
      read32(session.state.data() + 192),
      IRFQ_INFINITE_REASON_NONE_V2);
}

std::unique_ptr<PendingPlan> adminLogonPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  if (request.payload.length != 32 || read64(session.state.data() + 180) != SESSION_FLAG_ENABLED
      || read32(session.state.data() + 128) != IRFQ_INFINITE_SEQUENCE_VALUE_V2
      || read32(session.state.data() + 140) != IRFQ_INFINITE_SEQUENCE_VALUE_V2
      || request.next_original_state != IRFQ_INFINITE_SEQUENCE_VALUE_V2
      || request.next_original_value != read64(session.state.data() + 132)) {
    throw std::invalid_argument("Logon event");
  }
  const auto output = FIX::InfiniteSessionPlanner::logon(
      session.profile.beginString,
      session.profile.venueCompId,
      session.profile.participantCompId,
      read32(session.state.data() + 188),
      read64(session.state.data() + 132),
      read64(session.state.data() + 144),
      request.now_utc_ns,
      read64(session.state.data() + 152),
      &session.dictionaries,
      &session.staticProfile);
  auto plan = adminOutputPlan(session, request, output, 'A', 0, IRFQ_INFINITE_REASON_NONE_V2);
  write64(plan->state.data() + 112, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 120, static_cast<std::uint64_t>(request.now_utc_ns));
  write64(plan->state.data() + 180, UINT64_C(1) | UINT64_C(2) | UINT64_C(4) | UINT64_C(128));
  write32(plan->state.data() + 288, 10);
  plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
  return plan;
}

std::unique_ptr<PendingPlan> timerPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  const auto lastSentTai = readI64(session.state.data() + 96);
  const auto lastReceivedTai = readI64(session.state.data() + 112);
  if (request.payload.length != 32 || lastSentTai <= 0 || lastReceivedTai <= 0) {
    throw std::invalid_argument("Timer event");
  }
  const auto planTimer = [&](const FIX::TimeRange &sessionTime, const FIX::TimeRange &logonTime, bool nonStop) {
    return FIX::InfiniteSessionPlanner::timer(
        session.profile.beginString,
        session.profile.venueCompId,
        session.profile.participantCompId,
        read32(session.state.data() + 188),
        read64(session.state.data() + 132),
        read64(session.state.data() + 144),
        readI64(session.state.data() + 72),
        request.now_tai_ns,
        request.now_utc_ns,
        lastSentTai,
        lastReceivedTai,
        read64(session.state.data() + 180),
        read32(session.state.data() + 192),
        session.profile.logonTimeout,
        session.profile.logoutTimeout,
        sessionTime,
        logonTime,
        nonStop,
        read64(session.state.data() + 152),
        &session.dictionaries,
        &session.staticProfile);
  };
  const auto seconds = [](std::uint32_t value) {
    return FIX::UtcTimeOnly(
        static_cast<int>(value / 3600),
        static_cast<int>(value / 60 % 60),
        static_cast<int>(value % 60));
  };
  const auto &schedule = session.profile.schedule;
  const FIX::TimeRange sessionTime(
      seconds(schedule[1]),
      seconds(schedule[3]),
      static_cast<int>(schedule[0] + 1),
      static_cast<int>(schedule[2] + 1));
  const FIX::TimeRange logonTime(
      seconds(schedule[5]),
      seconds(schedule[7]),
      static_cast<int>(schedule[4] + 1),
      static_cast<int>(schedule[6] + 1));
  const auto output = planTimer(sessionTime, logonTime, session.profile.scheduleMode == 1);
  const auto disconnectReason
      = output.disconnected ? IRFQ_INFINITE_REASON_HEARTBEAT_TIMEOUT_V2 : IRFQ_INFINITE_REASON_NONE_V2;
  if (!output.output.empty()) {
    const char msgType = output.output.find("\00135=0\001") != std::string::npos   ? '0'
                         : output.output.find("\00135=1\001") != std::string::npos ? '1'
                         : output.output.find("\00135=5\001") != std::string::npos ? '5'
                                                                                   : '\0';
    if (msgType == '\0') {
      throw std::logic_error("Timer output type");
    }
    return adminOutputPlan(session, request, output, msgType, output.testRequestCount, disconnectReason);
  }
  auto plan = std::make_unique<PendingPlan>();
  plan->id = {session.identity, session.nextPlan++};
  plan->kind = request.kind;
  plan->stage = request.stage;
  plan->event = request.event;
  std::copy_n(request.event_identity_sha256, plan->eventIdentity.size(), plan->eventIdentity.begin());
  plan->baseEpoch = session.epoch;
  plan->baseRevision = session.revision;
  plan->resultEpoch = session.epoch;
  plan->resultRevision = session.revision + 1;
  plan->state = session.state;
  write64(plan->state.data() + 56, plan->resultRevision);
  write64(plan->state.data() + 80, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 88, static_cast<std::uint64_t>(request.now_utc_ns));
  write32(plan->state.data() + 192, output.testRequestCount);
  if (output.disconnected) {
    write64(plan->state.data() + 180, read64(plan->state.data() + 180) | UINT64_C(256));
    write32(plan->state.data() + 308, disconnectReason);
    irfq_infinite_declarative_action_v2 disconnect{};
    disconnect.kind = IRFQ_INFINITE_ACTION_DISCONNECT_V2;
    disconnect.reason_code = disconnectReason;
    plan->actions.push_back(disconnect);
  }
  plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
  return plan;
}

std::unique_ptr<PendingPlan> transportClosedPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  auto plan = std::make_unique<PendingPlan>();
  plan->id = {session.identity, session.nextPlan++};
  plan->kind = request.kind;
  plan->stage = request.stage;
  plan->event = request.event;
  std::copy_n(request.event_identity_sha256, plan->eventIdentity.size(), plan->eventIdentity.begin());
  plan->baseEpoch = session.epoch;
  plan->baseRevision = session.revision;
  plan->resultEpoch = session.epoch;
  plan->resultRevision = session.revision + 1;
  plan->state = session.state;
  write64(plan->state.data() + 56, plan->resultRevision);
  write64(plan->state.data() + 80, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 88, static_cast<std::uint64_t>(request.now_utc_ns));
  const auto recoveryKind = read32(plan->state.data() + 220);
  if (recoveryKind != RECOVERY_LOGON_789) {
    write32(plan->state.data() + 168, 0);
    write64(plan->state.data() + 172, 0);
  }
  write64(plan->state.data() + 180, SESSION_FLAG_ENABLED);
  write32(plan->state.data() + 188, session.profile.heartbeatMode == 1 ? session.profile.configuredHeartbeat : 0);
  write32(plan->state.data() + 192, 0);
  if (recoveryKind != RECOVERY_NONE && read32(plan->state.data() + 292) == CONTINUATION_RESEND) {
    write32(plan->state.data() + 292, CONTINUATION_NONE);
    write32(plan->state.data() + 296, IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2);
    write64(plan->state.data() + 300, 0);
  }
  write32(plan->state.data() + 288, 0);
  write32(plan->state.data() + 308, IRFQ_INFINITE_REASON_NONE_V2);
  plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
  return plan;
}

std::unique_ptr<PendingPlan> scheduledResetDecisionPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  std::int64_t boundary = 0;
  if (request.payload.length != 40 || !scheduledBoundary(session.profile, readI64(session.state.data() + 72), boundary)
      || readI64(request.payload.data + 32) != boundary || request.now_utc_ns < boundary) {
    throw std::invalid_argument("Scheduled reset trigger");
  }
  auto plan = std::make_unique<PendingPlan>();
  plan->id = {session.identity, session.nextPlan++};
  plan->kind = request.kind;
  plan->stage = request.stage;
  plan->event = request.event;
  std::copy_n(request.event_identity_sha256, plan->eventIdentity.size(), plan->eventIdentity.begin());
  plan->baseEpoch = session.epoch;
  plan->baseRevision = session.revision;
  plan->resultEpoch = session.epoch;
  plan->resultRevision = session.revision + 1;
  plan->state = session.state;
  write64(plan->state.data() + 56, plan->resultRevision);
  write64(plan->state.data() + 80, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 88, static_cast<std::uint64_t>(request.now_utc_ns));
  plan->pendingStatus = IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2;
  std::copy_n(request.payload.data, plan->subject.size(), plan->subject.begin());
  return plan;
}

std::unique_ptr<PendingPlan> targetCasPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  if (request.payload.length != 56) {
    throw std::invalid_argument("target CAS payload");
  }
  const auto expectedState = read32(request.payload.data + 32);
  const auto expectedValue = read64(request.payload.data + 36);
  const auto successorState = read32(request.payload.data + 44);
  const auto successorValue = read64(request.payload.data + 48);
  const auto lastLegal = static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2) - 1;
  const bool legalSuccessor = expectedState == IRFQ_INFINITE_SEQUENCE_VALUE_V2
                              && ((expectedValue < lastLegal && successorState == IRFQ_INFINITE_SEQUENCE_VALUE_V2
                                   && successorValue > expectedValue && successorValue <= lastLegal)
                                  || (expectedValue == lastLegal
                                      && successorState == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2 && successorValue == 0));
  if (expectedState != read32(session.state.data() + 140) || expectedValue != read64(session.state.data() + 144)
      || !validSequence(successorState, successorValue) || !legalSuccessor) {
    throw std::invalid_argument("target CAS transition");
  }
  auto plan = std::make_unique<PendingPlan>();
  plan->id = {session.identity, session.nextPlan++};
  plan->kind = request.kind;
  plan->stage = request.stage;
  plan->event = request.event;
  std::copy_n(request.event_identity_sha256, plan->eventIdentity.size(), plan->eventIdentity.begin());
  plan->baseEpoch = session.epoch;
  plan->baseRevision = session.revision;
  plan->resultEpoch = session.epoch;
  plan->resultRevision = session.revision + 1;
  plan->state = session.state;
  write64(plan->state.data() + 56, plan->resultRevision);
  write64(plan->state.data() + 80, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 88, static_cast<std::uint64_t>(request.now_utc_ns));
  write32(plan->state.data() + 140, successorState);
  write64(plan->state.data() + 144, successorValue);
  irfq_infinite_declarative_action_v2 action{};
  action.kind = IRFQ_INFINITE_ACTION_TARGET_ADVANCE_V2;
  action.sequence_begin = expectedValue;
  action.sequence_end_exclusive = successorValue;
  std::copy_n(request.payload.data, 32, action.binding_sha256);
  plan->actions.push_back(action);
  plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
  return plan;
}

std::unique_ptr<PendingPlan> processingFrontierPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  if (request.payload.length != 80 || !nonzero(request.payload.data + 48, 32)) {
    throw std::invalid_argument("processing frontier payload");
  }
  const auto expected = read64(request.payload.data + 32);
  const auto successor = read64(request.payload.data + 40);
  const auto targetState = read32(session.state.data() + 140);
  const auto targetValue = read64(session.state.data() + 144);
  if (expected != read64(session.state.data() + 152) || successor <= expected
      || (targetState == IRFQ_INFINITE_SEQUENCE_VALUE_V2 && successor >= targetValue)
      || (targetState == IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2
          && successor >= static_cast<std::uint64_t>(IRFQ_INFINITE_FIX_SEQUENCE_BOUND_V2))) {
    throw std::invalid_argument("processing frontier transition");
  }
  auto plan = std::make_unique<PendingPlan>();
  plan->id = {session.identity, session.nextPlan++};
  plan->kind = request.kind;
  plan->stage = request.stage;
  plan->event = request.event;
  std::copy_n(request.event_identity_sha256, plan->eventIdentity.size(), plan->eventIdentity.begin());
  plan->baseEpoch = session.epoch;
  plan->baseRevision = session.revision;
  plan->resultEpoch = session.epoch;
  plan->resultRevision = session.revision + 1;
  plan->state = session.state;
  write64(plan->state.data() + 56, plan->resultRevision);
  write64(plan->state.data() + 80, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 88, static_cast<std::uint64_t>(request.now_utc_ns));
  write64(plan->state.data() + 152, successor);
  plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
  return plan;
}

std::unique_ptr<PendingPlan> resetFinalPlan(
    irfq_infinite_session_v2 &session,
    const irfq_infinite_prepare_request_v2 &request) {
  constexpr std::uint32_t RESET_LOGON = 1;
  constexpr std::uint32_t SCHEDULED = 2;
  const auto *payload = request.payload.data + 32;
  const auto responseMode = read32(payload + 32);
  const auto newEpoch = read64(payload + 36);
  const auto creationTaiNs = readI64(payload + 44);
  const auto creationUtcNs = readI64(payload + 52);
  const auto heldLogonLength = read32(payload + 60);
  std::int64_t boundary = 0;
  if (!nonzero(payload, 32) || (responseMode != RESET_LOGON && responseMode != SCHEDULED) || session.epoch == UINT64_MAX
      || newEpoch != session.epoch + 1 || creationTaiNs <= 0 || creationUtcNs <= 0 || creationTaiNs > request.now_tai_ns
      || creationUtcNs > request.now_utc_ns || heldLogonLength > IRFQ_INFINITE_MAX_FRAME_BYTES_V2
      || request.payload.length != 128 + heldLogonLength
      || (responseMode == SCHEDULED
          && (session.profile.scheduleMode != 2 || heldLogonLength != 0 || nonzero(payload + 64, 32)))
      || (responseMode == RESET_LOGON && heldLogonLength == 0)
      || (session.profile.scheduleMode == 2 && !scheduledBoundary(session.profile, creationUtcNs, boundary))) {
    throw std::invalid_argument("Reset final");
  }
  auto plan = std::make_unique<PendingPlan>();
  plan->id = {session.identity, session.nextPlan++};
  plan->kind = request.kind;
  plan->stage = request.stage;
  plan->event = request.event;
  std::copy_n(request.event_identity_sha256, plan->eventIdentity.size(), plan->eventIdentity.begin());
  plan->baseEpoch = session.epoch;
  plan->baseRevision = session.revision;
  plan->resultEpoch = newEpoch;
  plan->resultRevision = 1;
  plan->state = freshState(session.profile, newEpoch, creationTaiNs, creationUtcNs);
  write64(plan->state.data() + 56, plan->resultRevision);
  write64(plan->state.data() + 80, static_cast<std::uint64_t>(request.now_tai_ns));
  write64(plan->state.data() + 88, static_cast<std::uint64_t>(request.now_utc_ns));
  if (responseMode == RESET_LOGON) {
    Sha256 heldSha;
    heldSha.update(payload + 96, heldLogonLength);
    const auto heldDigest = heldSha.finish();
    FIX::InfiniteDeclaredFrameCursor cursor{};
    std::size_t complete = 0;
    if (!std::equal(heldDigest.begin(), heldDigest.end(), payload + 64)
        || FIX::scanInfiniteDeclaredFrame(
               reinterpret_cast<const char *>(payload + 96),
               heldLogonLength,
               cursor,
               complete)
               != FIX::InfiniteDeclaredFrameScanResult::Ready
        || complete != heldLogonLength) {
      throw std::invalid_argument("Held reset Logon");
    }
    const std::string held(reinterpret_cast<const char *>(payload + 96), heldLogonLength);
    const auto inbound = FIX::InfiniteSessionPlanner::inbound(
        session.profile.beginString,
        session.profile.venueCompId,
        session.profile.participantCompId,
        session.profile.heartbeatMode == 1 ? session.profile.configuredHeartbeat : session.profile.minimumHeartbeat,
        1,
        1,
        request.now_utc_ns,
        creationUtcNs,
        creationUtcNs,
        SESSION_FLAG_ENABLED,
        0,
        1,
        held,
        session.dictionaries,
        staticProfile(session.profile),
        true);
    if (!inbound.admin || inbound.application || !inbound.resetLogon || inbound.msgType != "A" || inbound.sequence != 1
        || inbound.outputs.size() != 1 || inbound.outputMsgTypes.size() != 1 || inbound.outputMsgTypes[0] != "A"
        || inbound.outputSequences.size() != 1 || inbound.outputSequences[0] != 1 || inbound.nextSenderSequence != 2
        || inbound.nextTargetSequence != 2 || inbound.disconnected
        || (session.profile.heartbeatMode == 1 && inbound.heartbeatSeconds != session.profile.configuredHeartbeat)
        || (session.profile.heartbeatMode == 2
            && (inbound.heartbeatSeconds < session.profile.minimumHeartbeat
                || inbound.heartbeatSeconds > session.profile.maximumHeartbeat))) {
      throw std::invalid_argument("Reset Logon semantics");
    }
    write64(plan->state.data() + 96, static_cast<std::uint64_t>(request.now_tai_ns));
    write64(plan->state.data() + 104, static_cast<std::uint64_t>(request.now_utc_ns));
    write64(plan->state.data() + 112, static_cast<std::uint64_t>(request.now_tai_ns));
    write64(plan->state.data() + 120, static_cast<std::uint64_t>(request.now_utc_ns));
    write64(plan->state.data() + 132, 2);
    write64(plan->state.data() + 144, 2);
    write64(plan->state.data() + 152, 1);
    write64(plan->state.data() + 180, UINT64_C(231));
    write32(plan->state.data() + 188, inbound.heartbeatSeconds);
    write32(plan->state.data() + 288, 10);
    plan->output.assign(inbound.outputs[0].begin(), inbound.outputs[0].end());
    irfq_infinite_declarative_action_v2 action{};
    action.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
    action.output_class = IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2;
    action.msg_type_length = 1;
    action.msg_type[0] = 'A';
    action.sequence_begin = 1;
    action.sequence_end_exclusive = 2;
    action.output_length = plan->output.size();
    Sha256 outputSha;
    outputSha.update(plan->output.data(), plan->output.size());
    const auto outputDigest = outputSha.finish();
    std::copy(outputDigest.begin(), outputDigest.end(), action.binding_sha256);
    plan->actions.push_back(action);
  }
  plan->stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan->state.data(), plan->state.size());
  return plan;
}
} // namespace

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
    const DataDictionaryProvider &dictionaries) noexcept {
  try {
    Profile profile;
    const irfq_infinite_slice_v2 configSlice{config, configLength};
    if (!parseProfile(configSlice, profile) || epoch == 0 || revision == UINT64_MAX) {
      return nullptr;
    }
    auto session = std::make_unique<irfq_infinite_session_v2>();
    session->profile = profile;
    session->staticProfile = staticProfile(profile);
    session->dictionaries = dictionaries;
    session->epoch = epoch;
    session->revision = revision;
    if (nativeStateLength == 0) {
      if (revision != 0 || creationTaiNs <= 0 || creationUtcNs <= 0) {
        return nullptr;
      }
      std::int64_t boundary = 0;
      if (profile.scheduleMode == 2 && !scheduledBoundary(profile, creationUtcNs, boundary)) {
        return nullptr;
      }
      session->state = freshState(profile, epoch, creationTaiNs, creationUtcNs);
    } else {
      const irfq_infinite_slice_v2 stateSlice{nativeState, nativeStateLength};
      if (creationTaiNs != 0 || creationUtcNs != 0 || !validNativeState(stateSlice, profile, epoch, revision)) {
        return nullptr;
      }
      std::copy_n(nativeState, session->state.size(), session->state.begin());
    }
    static std::atomic<std::uint64_t> nextIdentity{1};
    session->identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
    if (session->identity == 0) {
      return nullptr;
    }
    return session.release();
  } catch (...) {
    return nullptr;
  }
}

bool computeInfiniteFrameAdapterStockNonconformanceSmokeIdentity(
    const irfq_infinite_session_v2 *session,
    const irfq_infinite_prepare_request_v2 &request,
    std::uint8_t *identity) noexcept {
  if (session == nullptr || identity == nullptr) {
    return false;
  }
  const auto value = eventIdentity(session->profile, request);
  std::copy(value.begin(), value.end(), identity);
  return true;
}

std::array<std::uint8_t, 32> computeInfiniteFrameAdapterStockNonconformanceSmokeSha256(
    const std::uint8_t *bytes,
    std::size_t length) noexcept {
  Sha256 sha;
  sha.update(bytes, length);
  return sha.finish();
}
} // namespace FIX

extern "C" irfq_infinite_status_v2 irfq_infinite_scan_v2(
    const irfq_infinite_scan_request_v2 *request,
    irfq_infinite_scan_response_v2 *response) noexcept {
  try {
    const auto envelope = validateEnvelope(request, response);
    if (envelope != IRFQ_INFINITE_STATUS_OK_V2) {
      return envelope;
    }
    Range requestRange;
    Range responseRange;
    Range inputRange;
    range(request, sizeof(*request), requestRange);
    range(response, sizeof(*response), responseRange);
    if (request->input.length > IRFQ_INFINITE_MAX_SCAN_BYTES_V2) {
      return publish(response, IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
    }
    if (!sliceRange(request->input, IRFQ_INFINITE_MAX_SCAN_BYTES_V2, inputRange)
        || !disjoint(inputRange, {requestRange, responseRange}) || !validScanCursor(request->input, request->cursor)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    FIX::InfiniteDeclaredFrameCursor cursor{
        0,
        static_cast<std::size_t>(request->cursor.scan_offset),
        static_cast<std::size_t>(request->cursor.body_length),
        static_cast<std::size_t>(request->cursor.checksum_begin),
        request->cursor.stage,
        request->cursor.body_length_has_digit != 0};
    std::size_t complete = 0;
    const auto result = FIX::scanInfiniteDeclaredFrame(
        reinterpret_cast<const char *>(request->input.data),
        static_cast<std::size_t>(request->input.length),
        cursor,
        complete);
    if (result == FIX::InfiniteDeclaredFrameScanResult::Malformed) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (result == FIX::InfiniteDeclaredFrameScanResult::TooLarge) {
      return publish(response, IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
    }
    if (result == FIX::InfiniteDeclaredFrameScanResult::Ready) {
      response->complete_prefix_length = complete;
      return publish(response, IRFQ_INFINITE_STATUS_FRAME_READY_V2);
    }
    if (cursor.stage == IRFQ_INFINITE_SCAN_BODY_V2 && cursor.checksumBegin > request->input.length) {
      cursor.stage = IRFQ_INFINITE_SCAN_BODY_LENGTH_V2;
      cursor.checksumBegin = 0;
    }
    response->cursor
        = {cursor.scanOffset,
           cursor.bodyLength,
           cursor.checksumBegin,
           cursor.stage,
           cursor.bodyLengthHasDigit ? IRFQ_INFINITE_YES_V2 : IRFQ_INFINITE_NO_V2};
    return publish(response, IRFQ_INFINITE_STATUS_NEED_MORE_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_session_create_v2(
    const irfq_infinite_session_create_request_v2 *request,
    irfq_infinite_session_create_response_v2 *response) noexcept {
  try {
    const auto envelope = validateEnvelope(request, response);
    if (envelope != IRFQ_INFINITE_STATUS_OK_V2) {
      return envelope;
    }
    Range requestRange;
    Range responseRange;
    Range configRange;
    Range stateRange;
    range(request, sizeof(*request), requestRange);
    range(response, sizeof(*response), responseRange);
    if (request->canonical_session_create_config.length > IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2
        || request->native_state.length > IRFQ_INFINITE_NATIVE_STATE_BYTES_V2) {
      return publish(response, IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
    }
    if (request->reserved != 0 || request->snapshot_codec_version != IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2
        || request->session_epoch == 0 || request->cache_revision == UINT64_MAX
        || !sliceRange(
            request->canonical_session_create_config,
            IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2,
            configRange)
        || !sliceRange(request->native_state, IRFQ_INFINITE_NATIVE_STATE_BYTES_V2, stateRange)
        || !disjoint(configRange, {requestRange, responseRange, stateRange})
        || !disjoint(stateRange, {requestRange, responseRange, configRange})) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    Profile profile;
    if (!parseProfile(request->canonical_session_create_config, profile)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (request->native_state.length == 0) {
      if (request->cache_revision != 0 || request->creation_tai_ns <= 0 || request->creation_utc_ns <= 0) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      std::int64_t boundary = 0;
      if (profile.scheduleMode == 2 && !scheduledBoundary(profile, request->creation_utc_ns, boundary)) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
    } else if (
        request->creation_tai_ns != 0 || request->creation_utc_ns != 0
        || !validNativeState(request->native_state, profile, request->session_epoch, request->cache_revision)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    return publish(response, IRFQ_INFINITE_STATUS_PROFILE_UNAVAILABLE_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_prepare_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_prepare_request_v2 *request,
    irfq_infinite_prepare_response_v2 *response) noexcept {
  try {
    PrepareOutputView outputView;
    if (aligned(response)) {
      outputView = {response->native_state, response->output, response->actions, response->action_capacity};
    }
    const auto envelope = validateEnvelope(request, response);
    if (envelope != IRFQ_INFINITE_STATUS_OK_V2) {
      return envelope;
    }
    if (session == nullptr) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (session->terminal) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (request->payload.length > IRFQ_INFINITE_MAX_PREPARE_PAYLOAD_BYTES_V2) {
      return publish(response, IRFQ_INFINITE_STATUS_LIMIT_EXCEEDED_V2);
    }
    if (!validPrepareOutput(*request, *response, outputView) || !validPrepareKind(request->kind)
        || !validStage(request->stage) || !validEvent(request->event)
        || request->application_block_mode > IRFQ_INFINITE_APPLICATION_BLOCK_SEMANTIC_REPLAY_V2
        || !nonzero(request->event_identity_sha256, 32) || request->expected_epoch == 0
        || request->expected_revision == UINT64_MAX || request->now_tai_ns <= 0 || request->now_utc_ns <= 0
        || request->reserved != 0 || request->next_original_state > IRFQ_INFINITE_SEQUENCE_EXHAUSTED_V2
        || (request->next_original_state == IRFQ_INFINITE_SEQUENCE_ABSENT_V2 && request->next_original_value != 0)
        || (request->next_original_state != IRFQ_INFINITE_SEQUENCE_ABSENT_V2
            && !validSequence(request->next_original_state, request->next_original_value))) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (session->pending) {
      return publish(response, IRFQ_INFINITE_STATUS_PLAN_PENDING_V2);
    }
    if (request->expected_epoch != session->epoch) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (request->expected_revision != session->revision || session->revision == UINT64_MAX) {
      return publish(response, IRFQ_INFINITE_STATUS_REVISION_MISMATCH_V2);
    }
    if (request->now_tai_ns < readI64(session->state.data() + 80)
        || request->now_utc_ns < readI64(session->state.data() + 88)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    const auto expectedIdentity = eventIdentity(session->profile, *request);
    if (!std::equal(expectedIdentity.begin(), expectedIdentity.end(), request->event_identity_sha256)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    const bool timerEvent
        = request->kind == IRFQ_INFINITE_PREPARE_RUST_TIMER_V2 && request->event == IRFQ_INFINITE_EVENT_TIMER_TICK_V2;
    const bool controlEvent = request->kind == IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
    const bool inboundEvent = request->kind == IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2
                              && request->stage == IRFQ_INFINITE_STAGE_HEAD_V2
                              && request->event == IRFQ_INFINITE_EVENT_INBOUND_FRAME_V2;
    const bool logonNeedsOriginal = request->event == IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2;
    const bool inboundNeedsOriginal = inboundEvent;
    if ((!timerEvent && !controlEvent && !inboundEvent)
        || request->application_block_mode != IRFQ_INFINITE_APPLICATION_BLOCK_NONE_V2
        || ((logonNeedsOriginal || inboundNeedsOriginal)
            && request->next_original_state == IRFQ_INFINITE_SEQUENCE_ABSENT_V2)
        || (!logonNeedsOriginal && !inboundNeedsOriginal
            && request->next_original_state != IRFQ_INFINITE_SEQUENCE_ABSENT_V2)
        || request->payload.length < 32 || !nonzero(request->payload.data, 32)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (inboundEvent && request->payload.length >= 68) {
      session->pending = registeredInboundPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_EVENT_V2 && request->event == IRFQ_INFINITE_EVENT_TRANSPORT_CLOSED_V2
        && request->payload.length == 32) {
      session->pending = transportClosedPlan(*session, *request);
    } else if (
        request->kind == IRFQ_INFINITE_PREPARE_RUST_TIMER_V2 && request->stage == IRFQ_INFINITE_STAGE_EVENT_V2
        && request->event == IRFQ_INFINITE_EVENT_TIMER_TICK_V2 && request->payload.length == 32) {
      session->pending = timerPlan(*session, *request);
    } else if (
        request->kind == IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2 && request->stage == IRFQ_INFINITE_STAGE_EVENT_V2
        && request->event == IRFQ_INFINITE_EVENT_SCHEDULED_RESET_TRIGGER_V2 && request->payload.length == 40) {
      session->pending = scheduledResetDecisionPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_EVENT_V2 && request->event == IRFQ_INFINITE_EVENT_ADMIN_HEARTBEAT_V2
        && request->payload.length >= 36) {
      session->pending = adminHeartbeatPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_EVENT_V2 && request->event == IRFQ_INFINITE_EVENT_ADMIN_TEST_REQUEST_V2
        && request->payload.length == 32) {
      session->pending = adminTestRequestPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_EVENT_V2 && request->event == IRFQ_INFINITE_EVENT_ADMIN_REJECT_V2
        && request->payload.length >= 52) {
      session->pending = adminRejectPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_EVENT_V2 && request->event == IRFQ_INFINITE_EVENT_ADMIN_LOGOUT_V2
        && request->payload.length == 36) {
      session->pending = adminLogoutPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_EVENT_V2 && request->event == IRFQ_INFINITE_EVENT_ADMIN_RESEND_REQUEST_V2
        && request->payload.length == 48) {
      session->pending = adminResendRequestPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_EVENT_V2 && request->event == IRFQ_INFINITE_EVENT_ADMIN_LOGON_V2
        && request->payload.length == 32) {
      session->pending = adminLogonPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_TARGET_CAS_V2 && request->event == IRFQ_INFINITE_EVENT_ADVANCE_TARGET_V2
        && request->payload.length == 56) {
      session->pending = targetCasPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_EVENT_V2
        && request->event == IRFQ_INFINITE_EVENT_ADVANCE_PROCESSING_FRONTIER_V2 && request->payload.length == 80) {
      session->pending = processingFrontierPlan(*session, *request);
    } else if (
        request->stage == IRFQ_INFINITE_STAGE_RESET_FINAL_V2 && request->event == IRFQ_INFINITE_EVENT_FINALIZE_RESET_V2
        && request->payload.length >= 128) {
      session->pending = resetFinalPlan(*session, *request);
    } else {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    const auto status = describePlan(*session->pending, session->profile, outputView, response);
    if (status != IRFQ_INFINITE_STATUS_READY_V2 && status != IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2
        && status != IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2
        && status != IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2) {
      session->pending.reset();
      session->terminal = true;
    }
    return status;
  } catch (const std::invalid_argument &) {
    return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_resume_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_resume_request_v2 *request,
    irfq_infinite_prepare_response_v2 *response) noexcept {
  try {
    PrepareOutputView outputView;
    if (aligned(response)) {
      outputView = {response->native_state, response->output, response->actions, response->action_capacity};
    }
    const auto envelope = validateEnvelope(request, response);
    if (envelope != IRFQ_INFINITE_STATUS_OK_V2) {
      return envelope;
    }
    if (session == nullptr) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    const auto failPending = [&](irfq_infinite_status_v2 status) {
      session->pending.reset();
      session->terminal = true;
      return publish(response, status);
    };
    if (session->terminal) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (!validResumeOutput(*request, *response, outputView) || request->reserved != 0 || request->reserved2 != 0
        || request->kind < IRFQ_INFINITE_RESUME_STORE_RANGE_V2 || request->kind > IRFQ_INFINITE_RESUME_OUTPUT_V2) {
      return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (!session->pending) {
      return failPending(IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (request->prepare_id.high != session->pending->id.high || request->prepare_id.low != session->pending->id.low
        || request->step != session->pending->step) {
      return failPending(IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (session->pending->pendingStatus == IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2) {
      auto &plan = *session->pending;
      if (request->kind != IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2
          || request->subject_sequence != plan.subjectSequence
          || !std::equal(std::begin(request->subject_sha256), std::end(request->subject_sha256), plan.subject.begin())
          || (request->decision != IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2
              && request->decision != IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2)
          || request->input_source != plan.inputSource || request->input_item_index != plan.inputItemIndex
          || request->input_source_bytes.length != plan.sourceLength || request->store_range_begin != 0
          || request->store_range_end_exclusive != 0 || request->store_rows != nullptr || request->store_row_count != 0
          || plan.sourceLength < 68 || plan.inputOffset < 68
          || plan.inputOffset + plan.inputLength > plan.sourceLength) {
        return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      Sha256 reborrowSha;
      reborrowSha.update(reinterpret_cast<const std::uint8_t *>(REBORROW_DOMAIN), sizeof(REBORROW_DOMAIN) - 1);
      const std::uint8_t zero = 0;
      reborrowSha.update(&zero, 1);
      std::array<std::uint8_t, 16> reborrowFields{};
      write32(reborrowFields.data(), request->input_source);
      write32(reborrowFields.data() + 4, request->input_item_index);
      write64(reborrowFields.data() + 8, request->input_source_bytes.length);
      reborrowSha.update(reborrowFields.data(), reborrowFields.size());
      reborrowSha.update(
          request->input_source_bytes.data,
          static_cast<std::size_t>(request->input_source_bytes.length));
      if (reborrowSha.finish() != plan.reborrowDigest) {
        return failPending(IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
      }
      const auto *payload = request->input_source_bytes.data;
      const auto frameLength = read32(payload + 32);
      if (frameLength != plan.sourceLength - 68) {
        return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      Sha256 frameSha;
      frameSha.update(payload + 68, frameLength);
      const auto frameDigest = frameSha.finish();
      Sha256 bodySha;
      bodySha.update(payload + plan.inputOffset, static_cast<std::size_t>(plan.inputLength));
      FIX::InfiniteDeclaredFrameCursor cursor{};
      std::size_t complete = 0;
      if (!std::equal(frameDigest.begin(), frameDigest.end(), payload + 36) || bodySha.finish() != plan.contentDigest
          || FIX::scanInfiniteDeclaredFrame(reinterpret_cast<const char *>(payload + 68), frameLength, cursor, complete)
                 != FIX::InfiniteDeclaredFrameScanResult::Ready
          || complete != frameLength) {
        return failPending(IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
      }
      if (plan.applicationDispatch) {
        irfq_infinite_declarative_action_v2 dispatch{};
        dispatch.kind = IRFQ_INFINITE_ACTION_APPLICATION_DISPATCH_V2;
        if (!copyMessageType(plan.msgType, dispatch.msg_type_length, dispatch.msg_type)) {
          return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        }
        dispatch.input_source = plan.inputSource;
        dispatch.input_item_index = plan.inputItemIndex;
        dispatch.sequence_begin = plan.subjectSequence;
        dispatch.sequence_end_exclusive = plan.subjectSequence + 1;
        dispatch.input_offset = plan.inputOffset;
        dispatch.input_length = plan.inputLength;
        std::copy(plan.contentDigest.begin(), plan.contentDigest.end(), dispatch.binding_sha256);
        plan.actions.push_back(dispatch);
      }
      irfq_infinite_declarative_action_v2 disposition{};
      disposition.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
      const bool gapFillDecision = !plan.applicationDispatch && plan.msgType == "4";
      disposition.disposition = gapFillDecision ? request->decision == IRFQ_INFINITE_APPLICATION_DECISION_ALLOW_V2
                                                      ? IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2
                                                      : IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2
                                : request->decision == IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2
                                    ? IRFQ_INFINITE_DISPOSITION_DURABLE_CONSUME_V2
                                : plan.msgType == "EC" ? IRFQ_INFINITE_DISPOSITION_PENDING_READ_V2
                                                       : IRFQ_INFINITE_DISPOSITION_PENDING_CORE_V2;
      disposition.input_source = plan.inputSource;
      disposition.input_item_index = plan.inputItemIndex;
      disposition.sequence_begin = plan.subjectSequence;
      disposition.sequence_end_exclusive = plan.subjectSequence + 1;
      disposition.input_offset = 68;
      disposition.input_length = frameLength;
      std::copy(plan.subject.begin(), plan.subject.end(), disposition.binding_sha256);
      disposition.reason_code
          = gapFillDecision && request->decision == IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2
                ? IRFQ_INFINITE_REASON_SEQUENCE_V2
            : plan.applicationDispatch && request->decision == IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2
                ? IRFQ_INFINITE_REASON_PROTOCOL_V2
                : IRFQ_INFINITE_REASON_NONE_V2;
      plan.actions.push_back(disposition);
      if (request->decision == IRFQ_INFINITE_APPLICATION_DECISION_REJECT_V2
          && (plan.applicationDispatch || gapFillDecision)) {
        const auto reject = gapFillDecision ? FIX::InfiniteSessionPlanner::resendRequest(
                                                  session->profile.beginString,
                                                  session->profile.venueCompId,
                                                  session->profile.participantCompId,
                                                  read32(session->state.data() + 188),
                                                  read64(session->state.data() + 132),
                                                  read64(session->state.data() + 144),
                                                  readI64(plan.state.data() + 88),
                                                  read64(session->state.data() + 152),
                                                  &session->dictionaries,
                                                  &session->staticProfile)
                                            : FIX::InfiniteSessionPlanner::businessReject(
                                                  session->profile.beginString,
                                                  session->profile.venueCompId,
                                                  session->profile.participantCompId,
                                                  read32(session->state.data() + 188),
                                                  read64(session->state.data() + 132),
                                                  read64(session->state.data() + 144),
                                                  readI64(plan.state.data() + 88),
                                                  plan.subjectSequence,
                                                  plan.msgType,
                                                  read64(session->state.data() + 152),
                                                  &session->dictionaries,
                                                  &session->staticProfile);
        plan.output.assign(reject.output.begin(), reject.output.end());
        write64(plan.state.data() + 96, read64(plan.state.data() + 80));
        write64(plan.state.data() + 104, read64(plan.state.data() + 88));
        writeSenderSuccessor(plan.state, reject.nextSenderSequence);
        irfq_infinite_declarative_action_v2 output{};
        output.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
        output.output_class = IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2;
        output.msg_type_length = 1;
        output.msg_type[0] = gapFillDecision ? '2' : 'j';
        output.sequence_begin = read64(session->state.data() + 132);
        output.sequence_end_exclusive = output.sequence_begin + 1;
        output.output_length = plan.output.size();
        Sha256 outputSha;
        outputSha.update(plan.output.data(), plan.output.size());
        const auto outputDigest = outputSha.finish();
        std::copy(outputDigest.begin(), outputDigest.end(), output.binding_sha256);
        plan.actions.push_back(output);
      }
      plan.pendingStatus = IRFQ_INFINITE_STATUS_READY_V2;
      plan.stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan.state.data(), plan.state.size());
      if (plan.step >= IRFQ_INFINITE_MAX_RESUME_STEPS_V2) {
        return failPending(IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      }
      ++plan.step;
      const auto status = describePlan(plan, session->profile, outputView, response);
      return status == IRFQ_INFINITE_STATUS_READY_V2 || status == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2
                 ? status
                 : failPending(status);
    }
    if (session->pending->pendingStatus == IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2) {
      auto &plan = *session->pending;
      if (request->kind != IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2 || request->subject_sequence != 0
          || !std::equal(std::begin(request->subject_sha256), std::end(request->subject_sha256), plan.subject.begin())
          || (request->decision != IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2
              && request->decision != IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2)
          || request->input_source != plan.inputSource || request->input_item_index != plan.inputItemIndex
          || request->input_source_bytes.length != plan.sourceLength || request->store_range_begin != 0
          || request->store_range_end_exclusive != 0 || request->store_rows != nullptr
          || request->store_row_count != 0) {
        return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      if (plan.inputSource == IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2) {
        Sha256 reborrowSha;
        reborrowSha.update(reinterpret_cast<const std::uint8_t *>(REBORROW_DOMAIN), sizeof(REBORROW_DOMAIN) - 1);
        const std::uint8_t zero = 0;
        reborrowSha.update(&zero, 1);
        std::array<std::uint8_t, 16> fields{};
        write32(fields.data(), request->input_source);
        write32(fields.data() + 4, request->input_item_index);
        write64(fields.data() + 8, request->input_source_bytes.length);
        reborrowSha.update(fields.data(), fields.size());
        reborrowSha.update(
            request->input_source_bytes.data,
            static_cast<std::size_t>(request->input_source_bytes.length));
        if (reborrowSha.finish() != plan.reborrowDigest || plan.sourceLength < 68) {
          return failPending(IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
        }
        const auto *payload = request->input_source_bytes.data;
        const auto frameLength = read32(payload + 32);
        if (frameLength != plan.sourceLength - 68) {
          return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        }
        Sha256 frameSha;
        frameSha.update(payload + 68, frameLength);
        const auto frameDigest = frameSha.finish();
        FIX::InfiniteDeclaredFrameCursor cursor{};
        std::size_t complete = 0;
        if (!std::equal(frameDigest.begin(), frameDigest.end(), payload + 36)
            || FIX::scanInfiniteDeclaredFrame(
                   reinterpret_cast<const char *>(payload + 68),
                   frameLength,
                   cursor,
                   complete)
                   != FIX::InfiniteDeclaredFrameScanResult::Ready
            || complete != frameLength) {
          return failPending(IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
        }
        const std::string held(reinterpret_cast<const char *>(payload + 68), frameLength);
        const auto inbound = FIX::InfiniteSessionPlanner::inbound(
            session->profile.beginString,
            session->profile.venueCompId,
            session->profile.participantCompId,
            read32(session->state.data() + 188),
            read64(session->state.data() + 132),
            read64(session->state.data() + 144),
            readI64(plan.state.data() + 88),
            readI64(session->state.data() + 104),
            readI64(session->state.data() + 120),
            read64(session->state.data() + 180),
            read32(session->state.data() + 192),
            read64(session->state.data() + 152),
            held,
            session->dictionaries,
            staticProfile(session->profile));
        if (!inbound.resetLogon || inbound.msgType != "A" || inbound.sequence != plan.classifiedSequence) {
          return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        }
        irfq_infinite_declarative_action_v2 disposition{};
        disposition.kind = IRFQ_INFINITE_ACTION_INBOUND_PROTOCOL_DISPOSITION_V2;
        disposition.disposition = request->decision == IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2
                                      ? IRFQ_INFINITE_DISPOSITION_PENDING_RESET_LOGON_V2
                                      : IRFQ_INFINITE_DISPOSITION_DURABLE_NO_CONSUME_V2;
        disposition.input_source = IRFQ_INFINITE_INPUT_PREPARE_PAYLOAD_V2;
        disposition.sequence_begin = plan.classifiedSequence;
        disposition.sequence_end_exclusive = plan.classifiedSequence + 1;
        disposition.input_offset = 68;
        disposition.input_length = frameLength;
        std::copy(plan.subject.begin(), plan.subject.end(), disposition.binding_sha256);
        disposition.reason_code = request->decision == IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2
                                      ? IRFQ_INFINITE_REASON_RESET_REJECTED_V2
                                      : IRFQ_INFINITE_REASON_NONE_V2;
        plan.actions.push_back(disposition);
        if (request->decision == IRFQ_INFINITE_EPOCH_RESET_DECISION_REJECT_TRIGGER_V2) {
          const auto logout = FIX::InfiniteSessionPlanner::logout(
              session->profile.beginString,
              session->profile.venueCompId,
              session->profile.participantCompId,
              read32(session->state.data() + 188),
              read64(session->state.data() + 132),
              read64(session->state.data() + 144),
              readI64(plan.state.data() + 88),
              "Reset rejected",
              read64(session->state.data() + 152),
              &session->dictionaries,
              &session->staticProfile);
          plan.output.assign(logout.output.begin(), logout.output.end());
          write64(plan.state.data() + 96, read64(plan.state.data() + 80));
          write64(plan.state.data() + 104, read64(plan.state.data() + 88));
          writeSenderSuccessor(plan.state, logout.nextSenderSequence);
          write64(plan.state.data() + 180, read64(plan.state.data() + 180) | UINT64_C(0x110));
          write32(plan.state.data() + 308, IRFQ_INFINITE_REASON_RESET_REJECTED_V2);
          irfq_infinite_declarative_action_v2 output{};
          output.kind = IRFQ_INFINITE_ACTION_OUTPUT_FRAME_V2;
          output.output_class = IRFQ_INFINITE_OUTPUT_SESSION_ADMIN_V2;
          output.msg_type_length = 1;
          output.msg_type[0] = '5';
          output.sequence_begin = read64(session->state.data() + 132);
          output.sequence_end_exclusive = output.sequence_begin + 1;
          output.output_length = plan.output.size();
          Sha256 outputSha;
          outputSha.update(plan.output.data(), plan.output.size());
          const auto outputDigest = outputSha.finish();
          std::copy(outputDigest.begin(), outputDigest.end(), output.binding_sha256);
          plan.actions.push_back(output);
          irfq_infinite_declarative_action_v2 disconnect{};
          disconnect.kind = IRFQ_INFINITE_ACTION_DISCONNECT_V2;
          disconnect.reason_code = IRFQ_INFINITE_REASON_RESET_REJECTED_V2;
          plan.actions.push_back(disconnect);
        }
      } else if (plan.inputSource != IRFQ_INFINITE_INPUT_NONE_V2) {
        return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      if (request->decision == IRFQ_INFINITE_EPOCH_RESET_DECISION_START_SAGA_V2) {
        irfq_infinite_declarative_action_v2 trigger{};
        trigger.kind = IRFQ_INFINITE_ACTION_RESET_TRIGGER_V2;
        trigger.disposition = plan.inputSource == IRFQ_INFINITE_INPUT_NONE_V2 ? 2 : 1;
        std::copy(plan.subject.begin(), plan.subject.end(), trigger.binding_sha256);
        plan.actions.push_back(trigger);
      }
      plan.pendingStatus = IRFQ_INFINITE_STATUS_READY_V2;
      plan.stateDigest = domainDigest(NATIVE_STATE_DOMAIN, plan.state.data(), plan.state.size());
      if (plan.step >= IRFQ_INFINITE_MAX_RESUME_STEPS_V2) {
        return failPending(IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
      }
      ++plan.step;
      const auto status = describePlan(plan, session->profile, outputView, response);
      return status == IRFQ_INFINITE_STATUS_READY_V2 || status == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2
                 ? status
                 : failPending(status);
    }
    if (request->kind != IRFQ_INFINITE_RESUME_OUTPUT_V2 || request->decision != 0 || request->subject_sequence != 0
        || nonzero(request->subject_sha256, 32) || request->input_source != IRFQ_INFINITE_INPUT_NONE_V2
        || request->input_item_index != 0 || request->input_source_bytes.length != 0 || request->store_range_begin != 0
        || request->store_range_end_exclusive != 0 || request->store_rows != nullptr || request->store_row_count != 0
        || session->pending->output.empty()) {
      return failPending(IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (outputView.output.capacity < session->pending->output.size()) {
      const auto status = describePlan(*session->pending, session->profile, outputView, response);
      return status == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2 ? status : failPending(status);
    }
    if (session->pending->step >= IRFQ_INFINITE_MAX_RESUME_STEPS_V2) {
      return failPending(IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    ++session->pending->step;
    const auto status = describePlan(*session->pending, session->profile, outputView, response);
    return status == IRFQ_INFINITE_STATUS_READY_V2 || status == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2
               ? status
               : failPending(status);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_apply_committed_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_apply_committed_request_v2 *request,
    irfq_infinite_operation_response_v2 *response) noexcept {
  try {
    const auto envelope = validateEnvelope(request, response);
    if (envelope != IRFQ_INFINITE_STATUS_OK_V2) {
      return envelope;
    }
    if (session == nullptr) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (session->terminal) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (!session->pending || !session->pending->materialized || request->prepare_id.high != session->pending->id.high
        || request->prepare_id.low != session->pending->id.low) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (request->result_revision != session->pending->resultRevision) {
      return publish(response, IRFQ_INFINITE_STATUS_REVISION_MISMATCH_V2);
    }
    if (!std::equal(
            std::begin(request->native_state_sha256),
            std::end(request->native_state_sha256),
            session->pending->stateDigest.begin())) {
      return publish(response, IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
    }
    session->state = session->pending->state;
    session->epoch = session->pending->resultEpoch;
    session->revision = session->pending->resultRevision;
    session->pending.reset();
    response->cache_revision = session->revision;
    return publish(response, IRFQ_INFINITE_STATUS_OK_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_abort_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_abort_request_v2 *request,
    irfq_infinite_operation_response_v2 *response) noexcept {
  try {
    const auto envelope = validateEnvelope(request, response);
    if (envelope != IRFQ_INFINITE_STATUS_OK_V2) {
      return envelope;
    }
    if (session == nullptr) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (session->terminal) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (!session->pending || request->prepare_id.high != session->pending->id.high
        || request->prepare_id.low != session->pending->id.low) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    session->pending.reset();
    response->cache_revision = session->revision;
    return publish(response, IRFQ_INFINITE_STATUS_OK_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_destroy_v2(irfq_infinite_session_v2 *session) noexcept {
  if (session == nullptr) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
  }
  delete session;
  return IRFQ_INFINITE_STATUS_OK_V2;
}
