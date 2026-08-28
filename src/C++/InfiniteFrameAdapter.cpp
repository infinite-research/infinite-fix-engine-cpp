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

#include "FieldNumbers.h"
#include "InfiniteCompleteFrame.h"
#include "InfiniteSensitiveString.h"
#include "Message.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr std::array<std::uint8_t, 8> STATE_MAGIC{{'I', 'R', 'F', 'Q', 'N', 'S', '2', 0}};
constexpr char STATE_DOMAIN[] = "IRFQ-FIX-NATIVE-STATE-V1";
constexpr char IDENTITY_DOMAIN[] = "IRFQ-FIX-NATIVE-IDENTITY-V1";
constexpr std::uint64_t FIX_SEQ_BOUND = INT64_MAX;
constexpr std::uint32_t SESSION_TIME_STATE_BOUND = INT32_MAX;

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

template <typename T> bool inputHeader(const T *value) noexcept {
  return aligned(value) && value->header.structure_size == sizeof(T)
         && value->header.abi_version == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2 && value->header.reserved == 0;
}

template <typename T> bool outputHeader(const T *value) noexcept {
  return aligned(value) && value->header.structure_size == sizeof(T)
         && value->header.abi_version == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V2 && value->header.reserved == 0;
}

template <typename T> irfq_infinite_status_v2 publish(T *response, irfq_infinite_status_v2 status) noexcept {
  if (aligned(response)) {
    response->header.status = status;
  }
  return status;
}

bool sliceRange(const irfq_infinite_slice_v2 &slice, std::uint64_t maximum, Range &result) noexcept {
  return slice.length <= maximum && range(slice.data, slice.length, result);
}

bool bufferRange(const irfq_infinite_buffer_v2 &buffer, std::uint64_t maximum, Range &result) noexcept {
  return buffer.length == 0 && buffer.capacity <= maximum && range(buffer.data, buffer.capacity, result);
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

std::array<std::uint8_t, 32> digest(const char *domain, const std::uint8_t *bytes, std::size_t length) noexcept {
  Sha256 value;
  value.update(reinterpret_cast<const std::uint8_t *>(domain), std::strlen(domain));
  const std::uint8_t separator = 0;
  value.update(&separator, 1);
  value.update(bytes, length);
  return value.finish();
}

void append64(std::vector<std::uint8_t> &bytes, std::uint64_t value) {
  for (unsigned index = 0; index < 8; ++index) {
    bytes.push_back(static_cast<std::uint8_t>(value >> ((7 - index) * 8)));
  }
}

void append32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) {
    bytes.push_back(static_cast<std::uint8_t>(value >> ((3 - index) * 8)));
  }
}

bool read64(const std::uint8_t *&cursor, const std::uint8_t *end, std::uint64_t &value) noexcept {
  if (end - cursor < 8) {
    return false;
  }
  value = 0;
  for (unsigned index = 0; index < 8; ++index) {
    value = value << 8 | *cursor++;
  }
  return true;
}

bool read32(const std::uint8_t *&cursor, const std::uint8_t *end, std::uint32_t &value) noexcept {
  if (end - cursor < 4) {
    return false;
  }
  value = 0;
  for (unsigned index = 0; index < 4; ++index) {
    value = value << 8 | *cursor++;
  }
  return true;
}

struct NativeState {
  std::array<std::uint8_t, 32> identity{};
  std::uint64_t epoch{1};
  std::uint64_t revision{0};
  std::uint64_t nextSender{1};
  std::uint64_t nextTarget{1};
  std::uint64_t creationTaiNs{0};
  std::uint64_t lastNowTaiNs{0};
  std::uint64_t resendBegin{0};
  std::uint64_t resendEnd{0};
  std::uint32_t loggedOn{0};
  std::uint32_t resetState{0};
  std::uint32_t sessionTimeState{0};
  std::uint32_t applicationVersion{10};
};

std::vector<std::uint8_t> serialize(const NativeState &state) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(STATE_MAGIC.size() + state.identity.size() + 80);
  bytes.insert(bytes.end(), STATE_MAGIC.begin(), STATE_MAGIC.end());
  bytes.insert(bytes.end(), state.identity.begin(), state.identity.end());
  append64(bytes, state.epoch);
  append64(bytes, state.revision);
  append64(bytes, state.nextSender);
  append64(bytes, state.nextTarget);
  append64(bytes, state.creationTaiNs);
  append64(bytes, state.lastNowTaiNs);
  append64(bytes, state.resendBegin);
  append64(bytes, state.resendEnd);
  append32(bytes, state.loggedOn);
  append32(bytes, state.resetState);
  append32(bytes, state.sessionTimeState);
  append32(bytes, state.applicationVersion);
  return bytes;
}

bool deserialize(const irfq_infinite_slice_v2 &source, NativeState &state) noexcept {
  constexpr std::size_t SIZE = 8 + 32 + 8 * 8 + 4 * 4;
  if (source.length != SIZE || !std::equal(STATE_MAGIC.begin(), STATE_MAGIC.end(), source.data)) {
    return false;
  }
  const auto *cursor = source.data + STATE_MAGIC.size();
  const auto *end = source.data + source.length;
  std::copy_n(cursor, state.identity.size(), state.identity.begin());
  cursor += state.identity.size();
  return read64(cursor, end, state.epoch) && read64(cursor, end, state.revision)
         && read64(cursor, end, state.nextSender) && read64(cursor, end, state.nextTarget)
         && read64(cursor, end, state.creationTaiNs) && read64(cursor, end, state.lastNowTaiNs)
         && read64(cursor, end, state.resendBegin) && read64(cursor, end, state.resendEnd)
         && read32(cursor, end, state.loggedOn) && read32(cursor, end, state.resetState)
         && read32(cursor, end, state.sessionTimeState) && read32(cursor, end, state.applicationVersion)
         && cursor == end && state.epoch != 0 && state.revision != UINT64_MAX && state.nextSender != 0
         && state.nextSender <= FIX_SEQ_BOUND && state.nextTarget != 0 && state.nextTarget <= FIX_SEQ_BOUND
         && state.loggedOn <= 1 && state.resetState <= 1 && state.sessionTimeState <= SESSION_TIME_STATE_BOUND
         && state.applicationVersion == IRFQ_INFINITE_APPLICATION_VERSION_FIX_LATEST_V2;
}

std::array<std::uint8_t, 32> identityDigest(
    const irfq_infinite_slice_v2 &begin,
    const irfq_infinite_slice_v2 &sender,
    const irfq_infinite_slice_v2 &target,
    const irfq_infinite_slice_v2 &qualifier,
    const irfq_infinite_session_create_request_v2 &request) noexcept {
  Sha256 value;
  value.update(reinterpret_cast<const std::uint8_t *>(IDENTITY_DOMAIN), sizeof(IDENTITY_DOMAIN) - 1);
  const std::uint8_t separator = 0;
  value.update(&separator, 1);
  for (const auto *part : {&begin, &sender, &target, &qualifier}) {
    std::array<std::uint8_t, 8> length{};
    for (unsigned index = 0; index < 8; ++index) {
      length[index] = static_cast<std::uint8_t>(part->length >> ((7 - index) * 8));
    }
    value.update(length.data(), length.size());
    value.update(part->data, static_cast<std::size_t>(part->length));
  }
  std::array<std::uint8_t, 24> profile{};
  for (unsigned index = 0; index < 4; ++index) {
    profile[index] = static_cast<std::uint8_t>(request.snapshot_codec_version >> ((3 - index) * 8));
    profile[4 + index] = static_cast<std::uint8_t>(request.default_application_version >> ((3 - index) * 8));
  }
  for (unsigned index = 0; index < 8; ++index) {
    profile[8 + index] = static_cast<std::uint8_t>(request.session_policy_flags >> ((7 - index) * 8));
  }
  value.update(profile.data(), profile.size());
  value.update(request.transport_dictionary_sha256, sizeof(request.transport_dictionary_sha256));
  value.update(request.application_dictionary_sha256, sizeof(request.application_dictionary_sha256));
  value.update(request.authenticated_session_binding_sha256, sizeof(request.authenticated_session_binding_sha256));
  return value.finish();
}

bool validIdentityPart(const irfq_infinite_slice_v2 &part, std::uint64_t maximum, bool required, Range &bytes) {
  if (!sliceRange(part, maximum, bytes) || (required && part.length == 0)) {
    return false;
  }
  for (std::uint64_t index = 0; index < part.length; ++index) {
    if (part.data[index] < 0x21 || part.data[index] > 0x7e || part.data[index] == '\001') {
      return false;
    }
  }
  return true;
}

std::uint64_t parseSequence(const std::string &value) {
  if (value.empty() || value.size() > 19) {
    throw std::invalid_argument("sequence");
  }
  std::uint64_t result = 0;
  for (char character : value) {
    if (character < '0' || character > '9') {
      throw std::invalid_argument("sequence");
    }
    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (result > (FIX_SEQ_BOUND - digit) / 10) {
      throw std::invalid_argument("sequence");
    }
    result = result * 10 + digit;
  }
  if (result == 0) {
    throw std::invalid_argument("sequence");
  }
  return result;
}

void wipe(std::string &value) noexcept {
  volatile char *cursor = value.empty() ? nullptr : &value[0];
  for (std::size_t index = 0; index < value.size(); ++index) {
    cursor[index] = 0;
  }
  value.clear();
}

void cleanse(FIX::FieldMap &fields) noexcept {
  for (auto &field : fields) {
    auto &value = const_cast<std::string &>(field.getString());
    wipe(value);
    field.setString({});
  }
  for (const auto &groupSet : fields.groups()) {
    for (auto *group : groupSet.second) {
      if (group != nullptr) {
        cleanse(*group);
      }
    }
  }
}

void cleanse(FIX::Message &message) noexcept {
  cleanse(static_cast<FIX::FieldMap &>(message));
  cleanse(message.getHeader());
  cleanse(message.getTrailer());
}

struct ParsedFrame {
  std::string type;
  std::uint64_t sequence{0};
  FIX::Message message;

  ParsedFrame(std::string frameType, std::uint64_t frameSequence, FIX::Message &&frameMessage)
      : type(std::move(frameType)),
        sequence(frameSequence),
        message(std::move(frameMessage)) {}
  ParsedFrame(const ParsedFrame &) = delete;
  ParsedFrame &operator=(const ParsedFrame &) = delete;
  ParsedFrame(ParsedFrame &&other) noexcept
      : type(std::move(other.type)),
        sequence(other.sequence),
        message(std::move(other.message)) {}
  ~ParsedFrame() { cleanse(message); }
};

ParsedFrame parseFrame(const irfq_infinite_slice_v2 &input) {
  FIX::InfiniteSensitiveString bytes(
      std::string(reinterpret_cast<const char *>(input.data), static_cast<std::size_t>(input.length)));
  FIX::Message message(static_cast<const std::string &>(bytes), true);
  auto type = message.getHeader().getField(FIX::FIELD::MsgType);
  auto sequence = parseSequence(message.getHeader().getField(FIX::FIELD::MsgSeqNum));
  return ParsedFrame(std::move(type), sequence, std::move(message));
}

bool nonzeroDigest(const std::uint8_t *value) noexcept {
  return std::any_of(value, value + 32, [](std::uint8_t byte) { return byte != 0; });
}

bool checkedSuccessor(std::uint64_t value, std::uint64_t &result) noexcept {
  if (value == 0 || value >= FIX_SEQ_BOUND) {
    return false;
  }
  result = value + 1;
  return true;
}

std::string sendingTime(std::int64_t taiNanoseconds) {
  const auto seconds = static_cast<std::time_t>(taiNanoseconds / INT64_C(1000000000));
  const auto micros = static_cast<unsigned>((taiNanoseconds % INT64_C(1000000000)) / INT64_C(1000));
  std::tm utc{};
#ifdef _WIN32
  if (gmtime_s(&utc, &seconds) != 0) {
#else
  if (gmtime_r(&seconds, &utc) == nullptr) {
#endif
    throw std::invalid_argument("time");
  }
  std::ostringstream value;
  value << std::put_time(&utc, "%Y%m%d-%H:%M:%S") << '.' << std::setw(6) << std::setfill('0') << micros;
  return value.str();
}

void setOutboundHeader(
    FIX::Message &message,
    const std::string &begin,
    const std::string &sender,
    const std::string &target,
    const std::string &type,
    std::uint64_t sequence,
    const std::string &time) {
  auto &header = message.getHeader();
  header.setField(FIX::FIELD::BeginString, begin);
  header.setField(FIX::FIELD::MsgType, type);
  header.setField(FIX::FIELD::SenderCompID, sender);
  header.setField(FIX::FIELD::TargetCompID, target);
  header.setField(FIX::FIELD::MsgSeqNum, std::to_string(sequence));
  header.setField(FIX::FIELD::SendingTime, time);
}

void parseApplicationBody(const irfq_infinite_slice_v2 &body, FIX::Message &message) {
  std::size_t offset = 0;
  while (offset < body.length) {
    const auto *begin = body.data + offset;
    const auto *end = std::find(begin, body.data + body.length, std::uint8_t{'\001'});
    if (end == body.data + body.length || end == begin) {
      throw std::invalid_argument("body");
    }
    const auto *equals = std::find(begin, end, std::uint8_t{'='});
    if (equals == begin || equals == end) {
      throw std::invalid_argument("body");
    }
    std::uint64_t tag = 0;
    for (auto *cursor = begin; cursor != equals; ++cursor) {
      if (*cursor < '0' || *cursor > '9' || tag > (INT_MAX - (*cursor - '0')) / 10) {
        throw std::invalid_argument("tag");
      }
      tag = tag * 10 + (*cursor - '0');
    }
    if (tag == 0 || tag == FIX::FIELD::BeginString || tag == FIX::FIELD::BodyLength || tag == FIX::FIELD::CheckSum
        || tag == FIX::FIELD::MsgType || tag == FIX::FIELD::SenderCompID || tag == FIX::FIELD::TargetCompID
        || tag == FIX::FIELD::MsgSeqNum || tag == FIX::FIELD::SendingTime
        || message.isSetField(static_cast<int>(tag))) {
      throw std::invalid_argument("tag");
    }
    message.setField(
        static_cast<int>(tag),
        std::string(reinterpret_cast<const char *>(equals + 1), reinterpret_cast<const char *>(end)));
    offset = static_cast<std::size_t>(end - body.data) + 1;
  }
}

std::vector<std::uint8_t> renderMessage(FIX::Message &message) {
  auto bytes = message.toString();
  if (bytes.size() > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2) {
    throw std::length_error("output");
  }
  return {bytes.begin(), bytes.end()};
}

struct Plan {
  irfq_infinite_prepare_id_v2 id{};
  irfq_infinite_prepare_kind_v2 kind{0};
  irfq_infinite_stage_v2 stage{0};
  irfq_infinite_status_v2 status{IRFQ_INFINITE_STATUS_READY_V2};
  irfq_infinite_resume_kind_v2 expectedResume{0};
  std::uint32_t step{0};
  std::vector<irfq_infinite_declarative_action_v2> actions;
  NativeState proposed;
  std::vector<std::uint8_t> snapshot;
  std::array<std::uint8_t, 32> snapshotDigest{};
  std::vector<std::uint8_t> output;
  std::uint64_t storeBegin{0};
  std::uint64_t storeEnd{0};
  std::string applicationType;
};
} // namespace

struct irfq_infinite_session_v2 {
  NativeState state;
  std::string beginString;
  std::string senderCompId;
  std::string targetCompId;
  std::string qualifier;
  std::unique_ptr<Plan> pending;
  std::uint64_t identity{0};
  std::uint64_t nextPrepare{1};
};

namespace {
std::atomic<std::uint64_t> nextSessionIdentity{1};

bool allocateSessionIdentity(std::uint64_t &identity) noexcept {
  auto current = nextSessionIdentity.load(std::memory_order_relaxed);
  while (current != 0 && current != UINT64_MAX) {
    if (nextSessionIdentity.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
      identity = current;
      return true;
    }
  }
  return false;
}

bool validPrepareKind(irfq_infinite_prepare_kind_v2 kind) noexcept {
  return kind >= IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2 && kind <= IRFQ_INFINITE_PREPARE_RUST_SESSION_CONTROL_V2;
}

bool validStage(irfq_infinite_stage_v2 stage) noexcept {
  return stage >= IRFQ_INFINITE_STAGE_HEAD_V2 && stage <= IRFQ_INFINITE_STAGE_RESET_FINAL_V2;
}

bool validApplicationType(const irfq_infinite_prepare_request_v2 &request) noexcept {
  if (request.application_message_type_length == 0
      || request.application_message_type_length > IRFQ_INFINITE_MAX_APPLICATION_MESSAGE_TYPE_BYTES_V2) {
    return false;
  }
  for (std::uint32_t index = 0; index < request.application_message_type_length; ++index) {
    const auto byte = request.application_message_type[index];
    if (byte < 0x21 || byte > 0x7e || byte == '=') {
      return false;
    }
  }
  return std::all_of(
      request.application_message_type + request.application_message_type_length,
      request.application_message_type + IRFQ_INFINITE_MAX_APPLICATION_MESSAGE_TYPE_BYTES_V2,
      [](std::uint8_t byte) { return byte == 0; });
}

bool validPrepareEnvelope(const irfq_infinite_prepare_request_v2 &request) noexcept {
  if (request.event_flags != 0 || request.reserved != 0) {
    return false;
  }
  if (request.kind == IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2) {
    return request.stage == IRFQ_INFINITE_STAGE_HEAD_V2 && request.event_code == IRFQ_INFINITE_EVENT_NONE_V2
           && request.event_identity == 0 && request.application_message_type_length == 0;
  }
  if (request.kind == IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2) {
    return request.stage == IRFQ_INFINITE_STAGE_TARGET_CAS_V2 && request.event_code == IRFQ_INFINITE_EVENT_NONE_V2
           && request.event_identity == 0 && validApplicationType(request);
  }
  if (request.kind == IRFQ_INFINITE_PREPARE_RUST_TIMER_V2) {
    return request.event_code == IRFQ_INFINITE_TIMER_HEARTBEAT_DUE_V2 && request.event_identity != 0
           && request.application_message_type_length == 0 && request.payload.length == 0;
  }
  return request.event_code == IRFQ_INFINITE_CONTROL_ADVANCE_STAGE_V2 && request.event_identity != 0
         && request.application_message_type_length == 0 && request.payload.length == 0;
}

void addAction(
    Plan &plan,
    irfq_infinite_action_v2 kind,
    std::uint64_t begin = 0,
    std::uint64_t end = 0,
    std::uint64_t offset = 0,
    std::uint64_t length = 0) {
  if (plan.actions.size() == IRFQ_INFINITE_MAX_ACTIONS_V2) {
    throw std::length_error("actions");
  }
  plan.actions.push_back({kind, 0, begin, end, offset, length});
}

bool validatePrepareOutput(
    const void *request,
    std::size_t requestSize,
    const irfq_infinite_slice_v2 *input,
    irfq_infinite_prepare_response_v2 *response,
    irfq_infinite_buffer_v2 &stateBuffer,
    irfq_infinite_buffer_v2 &outputBuffer) noexcept {
  if (!outputHeader(response)) {
    return false;
  }
  stateBuffer = response->native_state;
  outputBuffer = response->output;
  Range requestRange;
  Range responseRange;
  Range inputRange;
  Range stateRange;
  Range outputRange;
  if (!range(request, requestSize, requestRange) || !range(response, sizeof(*response), responseRange)
      || !bufferRange(stateBuffer, IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2, stateRange)
      || !bufferRange(outputBuffer, IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2, outputRange)
      || (input != nullptr && !sliceRange(*input, IRFQ_INFINITE_MAX_STORE_RANGE_BYTES_V2, inputRange))) {
    return false;
  }
  return !overlaps(requestRange, responseRange) && !overlaps(requestRange, stateRange)
         && !overlaps(requestRange, outputRange) && !overlaps(responseRange, stateRange)
         && !overlaps(responseRange, outputRange) && !overlaps(stateRange, outputRange)
         && (input == nullptr
             || (!overlaps(inputRange, requestRange) && !overlaps(inputRange, responseRange)
                 && !overlaps(inputRange, stateRange) && !overlaps(inputRange, outputRange)));
}

void prepareResponse(
    irfq_infinite_prepare_response_v2 *response,
    const irfq_infinite_buffer_v2 &stateBuffer,
    const irfq_infinite_buffer_v2 &outputBuffer) noexcept {
  const auto header = response->header;
  *response = {};
  response->header = header;
  response->native_state = stateBuffer;
  response->output = outputBuffer;
}

void finishPlan(Plan &plan) {
  plan.snapshot = serialize(plan.proposed);
  plan.snapshotDigest = digest(STATE_DOMAIN, plan.snapshot.data(), plan.snapshot.size());
}

irfq_infinite_status_v2 describe(
    const Plan &plan,
    irfq_infinite_prepare_response_v2 *response,
    irfq_infinite_buffer_v2 stateBuffer,
    irfq_infinite_buffer_v2 outputBuffer) noexcept {
  response->prepare_id = plan.id;
  response->step = plan.step;
  response->action_count = static_cast<std::uint32_t>(plan.actions.size());
  if (!plan.actions.empty()) {
    std::copy(plan.actions.begin(), plan.actions.end(), std::begin(response->actions));
  }
  response->base_revision = plan.proposed.revision - 1;
  response->result_revision = plan.proposed.revision;
  response->store_range_begin = plan.storeBegin;
  response->store_range_end = plan.storeEnd;
  response->required_native_state_capacity = plan.snapshot.size();
  response->required_output_capacity = plan.output.size();
  if (!plan.applicationType.empty()) {
    response->application_message_type_length = static_cast<std::uint32_t>(plan.applicationType.size());
    std::memcpy(
        response->application_message_type,
        plan.applicationType.data(),
        std::min(sizeof(response->application_message_type), plan.applicationType.size()));
  }
  if (plan.status != IRFQ_INFINITE_STATUS_READY_V2) {
    return publish(response, plan.status);
  }
  if (stateBuffer.capacity < plan.snapshot.size() || outputBuffer.capacity < plan.output.size()) {
    return publish(response, IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2);
  }
  if (!plan.snapshot.empty()) {
    std::memcpy(stateBuffer.data, plan.snapshot.data(), plan.snapshot.size());
  }
  if (!plan.output.empty()) {
    std::memcpy(outputBuffer.data, plan.output.data(), plan.output.size());
  }
  response->native_state.length = plan.snapshot.size();
  response->output.length = plan.output.size();
  std::memcpy(response->native_state_sha256, plan.snapshotDigest.data(), plan.snapshotDigest.size());
  return publish(response, IRFQ_INFINITE_STATUS_READY_V2);
}

irfq_infinite_status_v2 makeReady(
    Plan &plan,
    irfq_infinite_prepare_response_v2 *response,
    const irfq_infinite_buffer_v2 &stateBuffer,
    const irfq_infinite_buffer_v2 &outputBuffer) {
  plan.status = IRFQ_INFINITE_STATUS_READY_V2;
  plan.expectedResume = 0;
  finishPlan(plan);
  auto status = describe(plan, response, stateBuffer, outputBuffer);
  if (status == IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2) {
    plan.status = status;
    plan.expectedResume = IRFQ_INFINITE_RESUME_OUTPUT_V2;
    ++plan.step;
    status = describe(plan, response, stateBuffer, outputBuffer);
  }
  return status;
}

bool matchingPlan(const irfq_infinite_session_v2 &session, const irfq_infinite_prepare_id_v2 &id) noexcept {
  return session.pending != nullptr && id.session == session.identity && id.value == session.pending->id.value;
}

bool validateStoreItems(
    const irfq_infinite_resume_request_v2 &request,
    const irfq_infinite_prepare_response_v2 &response,
    const irfq_infinite_buffer_v2 &stateBuffer,
    const irfq_infinite_buffer_v2 &outputBuffer) noexcept {
  if (request.input_item_count > IRFQ_INFINITE_MAX_STORE_ITEMS_V2 || request.reserved2 != 0
      || (request.input_item_count == 0) != (request.store_items == nullptr)
      || (request.store_items != nullptr && !aligned(request.store_items))) {
    return false;
  }
  if (request.kind != IRFQ_INFINITE_RESUME_STORE_RANGE_V2) {
    return request.store_range_begin == 0 && request.store_range_end == 0 && request.store_items == nullptr
           && request.input_item_count == 0;
  }
  if (request.store_range_begin == 0 || request.store_range_begin > FIX_SEQ_BOUND
      || request.store_range_end > FIX_SEQ_BOUND
      || (request.store_range_end != 0 && request.store_range_end < request.store_range_begin)) {
    return false;
  }
  Range itemsRange;
  Range requestRange;
  Range responseRange;
  Range stateRange;
  Range outputRange;
  const auto itemBytes = static_cast<std::uint64_t>(request.input_item_count) * sizeof(irfq_infinite_store_item_v2);
  if (!range(request.store_items, itemBytes, itemsRange) || !range(&request, sizeof(request), requestRange)
      || !range(&response, sizeof(response), responseRange)
      || !bufferRange(stateBuffer, IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2, stateRange)
      || !bufferRange(outputBuffer, IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2, outputRange)
      || overlaps(itemsRange, requestRange) || overlaps(itemsRange, responseRange) || overlaps(itemsRange, stateRange)
      || overlaps(itemsRange, outputRange)) {
    return false;
  }
  std::uint64_t total = 0;
  for (std::uint32_t index = 0; index < request.input_item_count; ++index) {
    const auto &item = request.store_items[index];
    Range bodyRange;
    if (item.reserved != 0 || item.sequence == 0 || item.sequence > FIX_SEQ_BOUND
        || (item.kind != IRFQ_INFINITE_STORE_ITEM_MESSAGE_V2 && item.kind != IRFQ_INFINITE_STORE_ITEM_GAP_V2)
        || !sliceRange(item.body, IRFQ_INFINITE_MAX_FRAME_BYTES_V2, bodyRange)
        || total > IRFQ_INFINITE_MAX_STORE_RANGE_BYTES_V2 - item.body.length || overlaps(bodyRange, requestRange)
        || overlaps(bodyRange, responseRange) || overlaps(bodyRange, itemsRange) || overlaps(bodyRange, stateRange)
        || overlaps(bodyRange, outputRange)) {
      return false;
    }
    for (std::uint32_t previous = 0; previous < index; ++previous) {
      Range previousRange;
      if (!sliceRange(request.store_items[previous].body, IRFQ_INFINITE_MAX_FRAME_BYTES_V2, previousRange)
          || overlaps(bodyRange, previousRange)) {
        return false;
      }
    }
    total += item.body.length;
  }
  return true;
}

void appendOutput(
    Plan &plan,
    std::vector<std::uint8_t> bytes,
    irfq_infinite_action_v2 action,
    std::uint64_t begin,
    std::uint64_t end) {
  if (bytes.size() > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2 - plan.output.size()) {
    throw std::length_error("output");
  }
  const auto offset = plan.output.size();
  plan.output.insert(plan.output.end(), bytes.begin(), bytes.end());
  addAction(plan, action, begin, end, offset, bytes.size());
  std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
}

std::vector<std::uint8_t> renderStoredMessage(
    const irfq_infinite_store_item_v2 &item,
    const irfq_infinite_session_v2 &session,
    const std::string &time) {
  auto parsed = parseFrame(item.body);
  if (parsed.sequence != item.sequence
      || parsed.message.getHeader().getField(FIX::FIELD::BeginString) != session.beginString
      || parsed.message.getHeader().getField(FIX::FIELD::SenderCompID) != session.targetCompId
      || parsed.message.getHeader().getField(FIX::FIELD::TargetCompID) != session.senderCompId) {
    throw std::invalid_argument("stored message");
  }
  const auto originalTime = parsed.message.getHeader().getField(FIX::FIELD::SendingTime);
  parsed.message.getHeader().setField(FIX::FIELD::PossDupFlag, "Y");
  parsed.message.getHeader().setField(FIX::FIELD::OrigSendingTime, originalTime);
  parsed.message.getHeader().setField(FIX::FIELD::SendingTime, time);
  return renderMessage(parsed.message);
}

std::vector<std::uint8_t> renderGapFill(
    const irfq_infinite_session_v2 &session,
    std::uint64_t begin,
    std::uint64_t end,
    const std::string &time) {
  std::uint64_t successor = 0;
  if (!checkedSuccessor(end, successor)) {
    throw std::invalid_argument("gap");
  }
  FIX::Message message;
  setOutboundHeader(message, session.beginString, session.targetCompId, session.senderCompId, "4", begin, time);
  message.setField(FIX::FIELD::GapFillFlag, "Y");
  message.setField(FIX::FIELD::NewSeqNo, std::to_string(successor));
  auto result = renderMessage(message);
  cleanse(message);
  return result;
}
} // namespace

extern "C" irfq_infinite_status_v2 irfq_infinite_scan_v2(
    const irfq_infinite_scan_request_v2 *request,
    irfq_infinite_scan_response_v2 *response) noexcept {
  try {
    if (!aligned(response)) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
    }
    if (!aligned(request)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (!inputHeader(request) || !outputHeader(response)) {
      return publish(response, IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2);
    }
    Range requestRange;
    Range responseRange;
    Range inputRange;
    if (!range(request, sizeof(*request), requestRange) || !range(response, sizeof(*response), responseRange)
        || !sliceRange(request->input, IRFQ_INFINITE_MAX_SCAN_BYTES_V2, inputRange)
        || overlaps(requestRange, responseRange) || overlaps(inputRange, responseRange)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (request->cursor.body_length_has_digit > 1) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    FIX::InfiniteDeclaredFrameCursor cursor{
        static_cast<std::size_t>(request->cursor.frame_start),
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
    if (result == FIX::InfiniteDeclaredFrameScanResult::Malformed
        || result == FIX::InfiniteDeclaredFrameScanResult::TooLarge) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    const auto header = response->header;
    *response = {};
    response->header = header;
    response->cursor
        = {cursor.frameStart,
           cursor.scanOffset,
           cursor.bodyLength,
           cursor.checksumBegin,
           cursor.stage,
           cursor.bodyLengthHasDigit ? UINT32_C(1) : UINT32_C(0)};
    response->complete_prefix_length = complete;
    return publish(
        response,
        result == FIX::InfiniteDeclaredFrameScanResult::Ready ? IRFQ_INFINITE_STATUS_FRAME_READY_V2
                                                              : IRFQ_INFINITE_STATUS_NEED_MORE_V2);
  } catch (const std::bad_alloc &) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_session_create_v2(
    const irfq_infinite_session_create_request_v2 *request,
    irfq_infinite_session_create_response_v2 *response) noexcept {
  try {
    if (!aligned(response)) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
    }
    if (!aligned(request)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (!inputHeader(request) || !outputHeader(response)) {
      return publish(response, IRFQ_INFINITE_STATUS_ABI_MISMATCH_V2);
    }
    Range requestRange;
    Range responseRange;
    Range beginRange;
    Range senderRange;
    Range targetRange;
    Range qualifierRange;
    Range stateRange;
    if (!range(request, sizeof(*request), requestRange) || !range(response, sizeof(*response), responseRange)
        || overlaps(requestRange, responseRange) || !validIdentityPart(request->begin_string, 16, true, beginRange)
        || !validIdentityPart(request->sender_comp_id, 64, true, senderRange)
        || !validIdentityPart(request->target_comp_id, 64, true, targetRange)
        || !validIdentityPart(request->session_qualifier, 64, false, qualifierRange)
        || !sliceRange(request->native_state, IRFQ_INFINITE_MAX_NATIVE_STATE_BYTES_V2, stateRange)
        || overlaps(beginRange, responseRange) || overlaps(senderRange, responseRange)
        || overlaps(targetRange, responseRange) || overlaps(qualifierRange, responseRange)
        || overlaps(stateRange, responseRange) || overlaps(beginRange, requestRange)
        || overlaps(senderRange, requestRange) || overlaps(targetRange, requestRange)
        || overlaps(qualifierRange, requestRange) || overlaps(stateRange, requestRange) || request->session_epoch == 0
        || request->cache_revision == UINT64_MAX || request->creation_tai_ns <= 0
        || request->snapshot_codec_version != IRFQ_INFINITE_SNAPSHOT_CODEC_VERSION_V2
        || request->default_application_version != IRFQ_INFINITE_APPLICATION_VERSION_FIX_LATEST_V2
        || request->session_policy_flags != IRFQ_INFINITE_SESSION_POLICY_VALIDATE_LENGTH_CHECKSUM_V2
        || !nonzeroDigest(request->transport_dictionary_sha256)
        || !nonzeroDigest(request->application_dictionary_sha256)
        || !nonzeroDigest(request->authenticated_session_binding_sha256)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    const auto identity = identityDigest(
        request->begin_string,
        request->sender_comp_id,
        request->target_comp_id,
        request->session_qualifier,
        *request);
    NativeState state;
    if (request->native_state.length == 0) {
      if (request->cache_revision != 0) {
        return publish(response, IRFQ_INFINITE_STATUS_REVISION_MISMATCH_V2);
      }
      state.identity = identity;
      state.epoch = request->session_epoch;
      state.creationTaiNs = static_cast<std::uint64_t>(request->creation_tai_ns);
      state.applicationVersion = request->default_application_version;
    } else if (
        !deserialize(request->native_state, state) || state.identity != identity
        || state.epoch != request->session_epoch || state.revision != request->cache_revision) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    auto session = std::unique_ptr<irfq_infinite_session_v2>(new (std::nothrow) irfq_infinite_session_v2);
    if (!session) {
      return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
    }
    session->state = state;
    session->beginString.assign(
        reinterpret_cast<const char *>(request->begin_string.data),
        request->begin_string.length);
    session->senderCompId.assign(
        reinterpret_cast<const char *>(request->sender_comp_id.data),
        request->sender_comp_id.length);
    session->targetCompId.assign(
        reinterpret_cast<const char *>(request->target_comp_id.data),
        request->target_comp_id.length);
    session->qualifier.assign(
        reinterpret_cast<const char *>(request->session_qualifier.data),
        request->session_qualifier.length);
    if (!allocateSessionIdentity(session->identity)) {
      return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
    }
    const auto header = response->header;
    *response = {};
    response->header = header;
    response->session = session.release();
    response->cache_revision = state.revision;
    return publish(response, IRFQ_INFINITE_STATUS_OK_V2);
  } catch (const std::bad_alloc &) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_prepare_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_prepare_request_v2 *request,
    irfq_infinite_prepare_response_v2 *response) noexcept {
  try {
    irfq_infinite_buffer_v2 stateBuffer{};
    irfq_infinite_buffer_v2 outputBuffer{};
    if (session == nullptr || !inputHeader(request)
        || !validatePrepareOutput(request, sizeof(*request), &request->payload, response, stateBuffer, outputBuffer)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    prepareResponse(response, stateBuffer, outputBuffer);
    Range payloadRange;
    if (!validPrepareKind(request->kind) || !validStage(request->stage) || !validPrepareEnvelope(*request)
        || request->now_tai_ns <= 0
        || !sliceRange(request->payload, IRFQ_INFINITE_MAX_STORE_RANGE_BYTES_V2, payloadRange)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (session->pending) {
      return publish(response, IRFQ_INFINITE_STATUS_PLAN_PENDING_V2);
    }
    if (request->expected_session_epoch != session->state.epoch) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (request->expected_revision != session->state.revision || session->state.revision == UINT64_MAX) {
      return publish(response, IRFQ_INFINITE_STATUS_REVISION_MISMATCH_V2);
    }
    auto plan = std::make_unique<Plan>();
    plan->kind = request->kind;
    plan->stage = request->stage;
    plan->proposed = session->state;
    ++plan->proposed.revision;
    plan->proposed.lastNowTaiNs = static_cast<std::uint64_t>(request->now_tai_ns);

    if (request->kind == IRFQ_INFINITE_PREPARE_REGISTERED_INBOUND_V2) {
      if (request->payload.length == 0 || request->payload.length > IRFQ_INFINITE_MAX_FRAME_BYTES_V2) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      const auto parsed = parseFrame(request->payload);
      if (parsed.message.getHeader().getField(FIX::FIELD::BeginString) != session->beginString
          || parsed.message.getHeader().getField(FIX::FIELD::SenderCompID) != session->senderCompId
          || parsed.message.getHeader().getField(FIX::FIELD::TargetCompID) != session->targetCompId
          || parsed.sequence != session->state.nextTarget || (!session->state.loggedOn && parsed.type != "A")) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      plan->applicationType = parsed.type;
      if (parsed.type == "A" && parsed.message.isSetField(141) && parsed.message.getField(141) == "Y") {
        plan->status = IRFQ_INFINITE_STATUS_NEED_EPOCH_RESET_DECISION_V2;
        plan->expectedResume = IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2;
        addAction(*plan, IRFQ_INFINITE_ACTION_RESET_V2);
        plan->step = 1;
      } else if (parsed.type == "2") {
        plan->storeBegin = parseSequence(parsed.message.getField(7));
        const auto endValue = parsed.message.getField(16);
        plan->storeEnd = endValue == "0" ? 0 : parseSequence(endValue);
        plan->status = IRFQ_INFINITE_STATUS_NEED_STORE_RANGE_V2;
        plan->expectedResume = IRFQ_INFINITE_RESUME_STORE_RANGE_V2;
        addAction(*plan, IRFQ_INFINITE_ACTION_RESEND_V2, plan->storeBegin, plan->storeEnd);
        plan->step = 1;
      } else if (
          parsed.type != "A" && parsed.type != "0" && parsed.type != "1" && parsed.type != "3" && parsed.type != "4"
          && parsed.type != "5") {
        plan->status = IRFQ_INFINITE_STATUS_NEED_APPLICATION_DECISION_V2;
        plan->expectedResume = IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2;
        addAction(*plan, IRFQ_INFINITE_ACTION_APPLICATION_V2, parsed.sequence, parsed.sequence);
        plan->step = 1;
      } else {
        addAction(*plan, IRFQ_INFINITE_ACTION_SESSION_V2, parsed.sequence, parsed.sequence);
        if (!checkedSuccessor(plan->proposed.nextTarget, plan->proposed.nextTarget)) {
          return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        }
        if (parsed.type == "A") {
          plan->proposed.loggedOn = 1;
        }
      }
    } else if (request->kind == IRFQ_INFINITE_PREPARE_AUTHORIZED_APPLICATION_V2) {
      if (!session->state.loggedOn || request->payload.length == 0
          || request->payload.length > IRFQ_INFINITE_MAX_OUTPUT_BYTES_V2) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      std::uint64_t successor = 0;
      if (!checkedSuccessor(plan->proposed.nextSender, successor)) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      plan->applicationType.assign(
          reinterpret_cast<const char *>(request->application_message_type),
          request->application_message_type_length);
      FIX::Message outgoing;
      try {
        setOutboundHeader(
            outgoing,
            session->beginString,
            session->targetCompId,
            session->senderCompId,
            plan->applicationType,
            plan->proposed.nextSender,
            sendingTime(request->now_tai_ns));
        parseApplicationBody(request->payload, outgoing);
        plan->output = renderMessage(outgoing);
      } catch (...) {
        cleanse(outgoing);
        throw;
      }
      cleanse(outgoing);
      addAction(
          *plan,
          IRFQ_INFINITE_ACTION_APPLICATION_V2,
          plan->proposed.nextSender,
          plan->proposed.nextSender,
          0,
          plan->output.size());
      plan->proposed.nextSender = successor;
    } else if (request->kind == IRFQ_INFINITE_PREPARE_RUST_TIMER_V2) {
      if (plan->proposed.sessionTimeState == SESSION_TIME_STATE_BOUND) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      addAction(*plan, IRFQ_INFINITE_ACTION_TIMER_V2);
      ++plan->proposed.sessionTimeState;
    } else {
      addAction(
          *plan,
          request->stage == IRFQ_INFINITE_STAGE_RESET_FINAL_V2 ? IRFQ_INFINITE_ACTION_RESET_V2
                                                               : IRFQ_INFINITE_ACTION_SESSION_V2);
      if (request->stage == IRFQ_INFINITE_STAGE_RESET_FINAL_V2) {
        plan->proposed.resetState = 0;
      }
    }

    if (plan->status == IRFQ_INFINITE_STATUS_READY_V2) {
      finishPlan(*plan);
      if (stateBuffer.capacity < plan->snapshot.size() || outputBuffer.capacity < plan->output.size()) {
        plan->status = IRFQ_INFINITE_STATUS_NEED_OUTPUT_V2;
        plan->expectedResume = IRFQ_INFINITE_RESUME_OUTPUT_V2;
        plan->step = 1;
      }
    }
    if (session->nextPrepare == 0 || session->nextPrepare == UINT64_MAX) {
      return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
    }
    plan->id = {session->identity, session->nextPrepare};
    ++session->nextPrepare;
    session->pending = std::move(plan);
    return describe(*session->pending, response, stateBuffer, outputBuffer);
  } catch (const std::bad_alloc &) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_resume_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_resume_request_v2 *request,
    irfq_infinite_prepare_response_v2 *response) noexcept {
  try {
    irfq_infinite_buffer_v2 stateBuffer{};
    irfq_infinite_buffer_v2 outputBuffer{};
    if (session == nullptr || !inputHeader(request) || request->reserved != 0
        || !validatePrepareOutput(request, sizeof(*request), nullptr, response, stateBuffer, outputBuffer)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    prepareResponse(response, stateBuffer, outputBuffer);
    if (request->kind < IRFQ_INFINITE_RESUME_STORE_RANGE_V2 || request->kind > IRFQ_INFINITE_RESUME_OUTPUT_V2
        || request->decision > IRFQ_INFINITE_DECISION_REJECT_NO_CONSUME_V2 || request->reserved2 != 0) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (!matchingPlan(*session, request->prepare_id) || request->step != session->pending->step
        || request->kind != session->pending->expectedResume || request->step > IRFQ_INFINITE_MAX_RESUME_STEPS_V2) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (!validateStoreItems(*request, *response, stateBuffer, outputBuffer)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    auto &plan = *session->pending;
    if (request->kind == IRFQ_INFINITE_RESUME_STORE_RANGE_V2) {
      if (request->decision != IRFQ_INFINITE_DECISION_NONE_V2 || request->store_range_begin != plan.storeBegin
          || request->store_range_end != plan.storeEnd || request->input_item_count == 0) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      Plan candidate = plan;
      candidate.output.clear();
      candidate.actions.clear();
      const auto time = sendingTime(candidate.proposed.lastNowTaiNs);
      auto expectedSequence = request->store_range_begin;
      for (std::uint32_t index = 0; index < request->input_item_count;) {
        const auto &item = request->store_items[index];
        if (item.sequence != expectedSequence) {
          return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        }
        if (item.kind == IRFQ_INFINITE_STORE_ITEM_MESSAGE_V2) {
          if (item.body.length == 0) {
            return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
          }
          appendOutput(
              candidate,
              renderStoredMessage(item, *session, time),
              IRFQ_INFINITE_ACTION_RESEND_V2,
              item.sequence,
              item.sequence);
          ++index;
          if (!checkedSuccessor(expectedSequence, expectedSequence) && index != request->input_item_count) {
            return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
          }
          continue;
        }
        if (item.body.length != 0) {
          return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        }
        const auto gapBegin = item.sequence;
        auto gapEnd = item.sequence;
        ++index;
        while (index < request->input_item_count
               && request->store_items[index].kind == IRFQ_INFINITE_STORE_ITEM_GAP_V2) {
          std::uint64_t next = 0;
          if (!checkedSuccessor(gapEnd, next) || request->store_items[index].sequence != next
              || request->store_items[index].body.length != 0) {
            return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
          }
          gapEnd = next;
          ++index;
        }
        appendOutput(
            candidate,
            renderGapFill(*session, gapBegin, gapEnd, time),
            IRFQ_INFINITE_ACTION_RESEND_V2,
            gapBegin,
            gapEnd);
        if (!checkedSuccessor(gapEnd, expectedSequence) && index != request->input_item_count) {
          return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        }
      }
      const auto finalSequence = request->store_items[request->input_item_count - 1].sequence;
      if (request->store_range_end != 0 && finalSequence != request->store_range_end) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      if (!checkedSuccessor(candidate.proposed.nextTarget, candidate.proposed.nextTarget)) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      candidate.proposed.resendBegin = candidate.storeBegin;
      candidate.proposed.resendEnd = finalSequence;
      auto status = makeReady(candidate, response, stateBuffer, outputBuffer);
      plan = std::move(candidate);
      return status;
    } else if (request->kind == IRFQ_INFINITE_RESUME_APPLICATION_DECISION_V2) {
      if (request->decision != IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2
          && request->decision != IRFQ_INFINITE_DECISION_REJECT_NO_CONSUME_V2) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      Plan candidate = plan;
      if (request->decision == IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2) {
        if (!checkedSuccessor(candidate.proposed.nextTarget, candidate.proposed.nextTarget)) {
          return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
        }
      }
      auto status = makeReady(candidate, response, stateBuffer, outputBuffer);
      plan = std::move(candidate);
      return status;
    } else if (request->kind == IRFQ_INFINITE_RESUME_EPOCH_RESET_DECISION_V2) {
      if (request->decision != IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2
          && request->decision != IRFQ_INFINITE_DECISION_REJECT_NO_CONSUME_V2) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      Plan candidate = plan;
      if (request->decision == IRFQ_INFINITE_DECISION_ACCEPT_CONSUME_V2) {
        candidate.proposed.resetState = 1;
        candidate.proposed.loggedOn = 0;
      }
      auto status = makeReady(candidate, response, stateBuffer, outputBuffer);
      plan = std::move(candidate);
      return status;
    } else if (request->kind == IRFQ_INFINITE_RESUME_OUTPUT_V2) {
      if (request->decision != IRFQ_INFINITE_DECISION_NONE_V2) {
        return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
      }
      if (stateBuffer.capacity < plan.snapshot.size() || outputBuffer.capacity < plan.output.size()) {
        return describe(plan, response, stateBuffer, outputBuffer);
      }
      Plan candidate = plan;
      candidate.status = IRFQ_INFINITE_STATUS_READY_V2;
      candidate.expectedResume = 0;
      const auto status = describe(candidate, response, stateBuffer, outputBuffer);
      plan = std::move(candidate);
      return status;
    } else {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  } catch (const std::bad_alloc &) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_apply_committed_v2(
    irfq_infinite_session_v2 *session,
    const irfq_infinite_apply_committed_request_v2 *request,
    irfq_infinite_operation_response_v2 *response) noexcept {
  try {
    if (session == nullptr || !inputHeader(request) || !outputHeader(response)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    Range requestRange;
    Range responseRange;
    if (!range(request, sizeof(*request), requestRange) || !range(response, sizeof(*response), responseRange)
        || overlaps(requestRange, responseRange)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (!matchingPlan(*session, request->prepare_id) || session->pending->status != IRFQ_INFINITE_STATUS_READY_V2) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    if (request->result_revision != session->pending->proposed.revision) {
      return publish(response, IRFQ_INFINITE_STATUS_REVISION_MISMATCH_V2);
    }
    if (!std::equal(
            std::begin(request->native_state_sha256),
            std::end(request->native_state_sha256),
            session->pending->snapshotDigest.begin())) {
      return publish(response, IRFQ_INFINITE_STATUS_DIGEST_MISMATCH_V2);
    }
    session->state = session->pending->proposed;
    session->pending.reset();
    const auto header = response->header;
    *response = {};
    response->header = header;
    response->cache_revision = session->state.revision;
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
    if (session == nullptr || !inputHeader(request) || !outputHeader(response)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    Range requestRange;
    Range responseRange;
    if (!range(request, sizeof(*request), requestRange) || !range(response, sizeof(*response), responseRange)
        || overlaps(requestRange, responseRange)) {
      return publish(response, IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2);
    }
    if (!matchingPlan(*session, request->prepare_id)) {
      return publish(response, IRFQ_INFINITE_STATUS_STALE_PLAN_V2);
    }
    session->pending.reset();
    const auto header = response->header;
    *response = {};
    response->header = header;
    response->cache_revision = session->state.revision;
    return publish(response, IRFQ_INFINITE_STATUS_OK_V2);
  } catch (...) {
    return publish(response, IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2);
  }
}

extern "C" irfq_infinite_status_v2 irfq_infinite_destroy_v2(irfq_infinite_session_v2 *session) noexcept {
  try {
    if (session == nullptr) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V2;
    }
    delete session;
    return IRFQ_INFINITE_STATUS_OK_V2;
  } catch (...) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V2;
  }
}
