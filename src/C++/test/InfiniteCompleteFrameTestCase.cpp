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

#include <InfiniteCompleteFrame.h>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace FIX;

namespace {
constexpr const char *FIXTURE_PATH = INFINITE_COMPLETE_FRAME_FIXTURE_PATH;
constexpr std::size_t MAX_FRAME_BYTES = 65'536;

struct FramingRow {
  std::int64_t observedTaiNs;
  std::string chunk;
  std::vector<std::string> expectedFrames;
};

std::vector<std::string> split(const std::string &value, char delimiter) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const auto end = value.find(delimiter, start);
    fields.push_back(value.substr(start, end - start));
    if (end == std::string::npos) {
      return fields;
    }
    start = end + 1;
  }
}

unsigned char hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<unsigned char>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<unsigned char>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<unsigned char>(value - 'A' + 10);
  }
  throw std::runtime_error("malformed fixture hex");
}

std::string unhex(const std::string &value) {
  if (value.size() % 2 != 0 || value.size() > MAX_FRAME_BYTES * 2) {
    throw std::runtime_error("malformed fixture hex length");
  }
  std::string bytes;
  bytes.reserve(value.size() / 2);
  for (std::size_t index = 0; index < value.size(); index += 2) {
    bytes.push_back(static_cast<char>((hexNibble(value[index]) << 4) | hexNibble(value[index + 1])));
  }
  return bytes;
}

std::vector<FramingRow> loadCanonicalFramingRows(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("missing canonical fixture");
  }

  constexpr const char *GATE_MAGIC = "IRFQ-COMPLETE-FRAME-GATE-V1";
  constexpr const char *GATE_HEADER
      = "case_id\tpartition\tconnection\tentry_kind\tprotocol\tobserved_tai_ns\tcandidate_tai_ns\t"
        "payload_hex\texpected";
  constexpr const char *FRAMING_MAGIC = "IRFQ-FIX-FRAMING-V1";
  constexpr const char *FRAMING_HEADER
      = "case_id\tread_index\tobserved_tai_ns\tchunk_hex\texpected_frames_hex\texpected_fault";

  std::string line;
  if (!std::getline(input, line) || line != GATE_MAGIC || !std::getline(input, line) || line != GATE_HEADER) {
    throw std::runtime_error("malformed gate fixture header");
  }

  std::vector<std::string> gateFrames;
  while (std::getline(input, line) && line != FRAMING_MAGIC) {
    const auto fields = split(line, '\t');
    if (fields.size() != 9) {
      throw std::runtime_error("malformed gate fixture row");
    }
    if (fields[4] == "fix" && fields[8].rfind("REGISTER:", 0) == 0) {
      const auto bytes = unhex(fields[7]);
      if (bytes.rfind("8=", 0) == 0) {
        gateFrames.push_back(bytes);
      }
    }
  }
  if (line != FRAMING_MAGIC || !std::getline(input, line) || line != FRAMING_HEADER) {
    throw std::runtime_error("malformed framing fixture header");
  }

  std::vector<FramingRow> rows;
  std::vector<std::string> framed;
  while (std::getline(input, line)) {
    const auto fields = split(line, '\t');
    if (fields.size() != 6 || fields[5] != "-") {
      throw std::runtime_error("malformed framing fixture row");
    }
    FramingRow row{std::stoll(fields[2]), unhex(fields[3]), {}};
    if (fields[4] != "-") {
      for (const auto &expected : split(fields[4], ',')) {
        row.expectedFrames.push_back(unhex(expected));
        framed.push_back(row.expectedFrames.back());
      }
    }
    rows.push_back(std::move(row));
  }
  if (rows.empty() || framed != gateFrames) {
    throw std::runtime_error("framing bytes differ from gate payloads");
  }
  return rows;
}

std::string makeMessageOfSize(std::size_t totalSize) {
  constexpr const char *PREFIX = "8=FIX.4.2\0019=";
  constexpr const char *CHECKSUM = "10=000\001";
  for (std::size_t bodyLength = 9; bodyLength < totalSize; ++bodyLength) {
    const auto bodyLengthText = std::to_string(bodyLength);
    if (std::char_traits<char>::length(PREFIX) + bodyLengthText.size() + 1 + bodyLength
            + std::char_traits<char>::length(CHECKSUM)
        != totalSize) {
      continue;
    }
    std::string body = "35=A\00158=";
    body.append(bodyLength - 9, 'x');
    body.push_back('\001');
    return std::string(PREFIX) + bodyLengthText + '\001' + body + CHECKSUM;
  }
  throw std::runtime_error("requested FIX size cannot be represented");
}

std::vector<std::string> frameBytes(const InfiniteDispatchResult &result) {
  std::vector<std::string> bytes;
  for (const auto &frame : result.frames) {
    bytes.push_back(frame.bytes);
  }
  return bytes;
}
} // namespace

TEST_CASE("InfiniteCompleteFrameDispatcherTests") {
  SECTION("batch limits must be positive") {
    CHECK_THROWS_AS(InfiniteCompleteFrameDispatcher({0, 1}), std::invalid_argument);
    CHECK_THROWS_AS(InfiniteCompleteFrameDispatcher({1, 0}), std::invalid_argument);
  }

  SECTION("canonical framing transcript preserves wire order") {
    InfiniteCompleteFrameDispatcher dispatcher({4, 262'144});
    const auto rows = loadCanonicalFramingRows(FIXTURE_PATH);
    for (const auto &row : rows) {
      const auto result = dispatcher.process(row.chunk.data(), row.chunk.size(), row.observedTaiNs);
      CHECK(frameBytes(result) == row.expectedFrames);
      CHECK_FALSE(result.terminalFault.has_value());
      for (const auto &frame : result.frames) {
        CHECK(frame.observedTaiNs == row.observedTaiNs);
      }
    }
  }

  SECTION("byte-by-byte completion uses the final byte observation") {
    const auto rows = loadCanonicalFramingRows(FIXTURE_PATH);
    std::string stream;
    std::vector<std::string> expected;
    for (const auto &row : rows) {
      stream += row.chunk;
      expected.insert(expected.end(), row.expectedFrames.begin(), row.expectedFrames.end());
    }

    InfiniteCompleteFrameDispatcher dispatcher({4, 262'144});
    std::vector<InfiniteCompleteFrame> actual;
    for (std::size_t index = 0; index < stream.size(); ++index) {
      auto result = dispatcher.process(stream.data() + index, 1, static_cast<std::int64_t>(index + 1));
      CHECK_FALSE(result.terminalFault.has_value());
      actual.insert(actual.end(), result.frames.begin(), result.frames.end());
    }
    REQUIRE(actual.size() == expected.size());
    CHECK(actual[0].bytes == expected[0]);
    CHECK(actual[0].observedTaiNs == static_cast<std::int64_t>(expected[0].size()));
    CHECK(actual[1].bytes == expected[1]);
    CHECK(actual[1].observedTaiNs == static_cast<std::int64_t>(stream.size()));
  }

  SECTION("exact frame limit is inclusive and one byte over is terminal") {
    InfiniteCompleteFrameDispatcher exactDispatcher({1, MAX_FRAME_BYTES});
    const auto exact = makeMessageOfSize(MAX_FRAME_BYTES);
    const auto exactResult = exactDispatcher.process(exact.data(), exact.size(), 1);
    REQUIRE(exactResult.frames.size() == 1);
    CHECK(exactResult.frames[0].bytes == exact);
    CHECK_FALSE(exactResult.terminalFault.has_value());

    InfiniteCompleteFrameDispatcher oversizedDispatcher({1, MAX_FRAME_BYTES});
    const auto oversized = makeMessageOfSize(MAX_FRAME_BYTES + 1);
    const auto oversizedResult = oversizedDispatcher.process(oversized.data(), oversized.size(), 1);
    CHECK(oversizedResult.frames.empty());
    CHECK(oversizedResult.terminalFault == InfiniteDispatchFault::FrameTooLarge);
  }

  SECTION("accumulator overflow is rejected before the next append") {
    InfiniteCompleteFrameDispatcher dispatcher({1, MAX_FRAME_BYTES});
    const std::string starved = "8=" + std::string(MAX_FRAME_BYTES - 2, 'x');
    const auto first = dispatcher.process(starved.data(), starved.size(), 1);
    CHECK(first.frames.empty());
    CHECK(first.terminalFault == InfiniteDispatchFault::AccumulatorOverflow);

    InfiniteCompleteFrameDispatcher oneRead({1, MAX_FRAME_BYTES});
    const std::string oversizedStarved = "8=" + std::string(MAX_FRAME_BYTES - 1, 'x');
    const auto sameRead = oneRead.process(oversizedStarved.data(), oversizedStarved.size(), 1);
    CHECK(sameRead.frames.empty());
    CHECK(sameRead.terminalFault == InfiniteDispatchFault::AccumulatorOverflow);

    const std::string valid = "8=FIX.4.2\0019=12\00135=A\001108=30\00110=026\001";
    InfiniteCompleteFrameDispatcher prefixed({2, MAX_FRAME_BYTES + valid.size()});
    const auto stream = valid + starved;
    const auto prefixedResult = prefixed.process(stream.data(), stream.size(), 2);
    REQUIRE(prefixedResult.frames.size() == 1);
    CHECK(prefixedResult.frames[0].bytes == valid);
    CHECK(prefixedResult.terminalFault == InfiniteDispatchFault::AccumulatorOverflow);
  }

  SECTION("declared BodyLength cannot consume a following frame") {
    InfiniteCompleteFrameDispatcher dispatcher({2, MAX_FRAME_BYTES});
    const std::string following = "8=FIX.4.2\0019=17\00135=4\00136=88\001123=Y\00110=028\001";
    const auto overstated = std::string("8=FIX.4.2\0019=30\00135=A\001108=30\00110=026\001") + following;
    const auto result = dispatcher.process(overstated.data(), overstated.size(), 1);
    CHECK(result.frames.empty());
    CHECK(result.terminalFault == InfiniteDispatchFault::MalformedFrame);
  }

  SECTION("checksum framing must be exact") {
    InfiniteCompleteFrameDispatcher dispatcher({1, MAX_FRAME_BYTES});
    const std::string malformed = "8=FIX.4.2\0019=12\00135=A\001108=30\00110=6\001";
    const auto result = dispatcher.process(malformed.data(), malformed.size(), 1);
    CHECK(result.frames.empty());
    CHECK(result.terminalFault == InfiniteDispatchFault::MalformedFrame);
  }

  SECTION("BodyLength overflow is malformed") {
    InfiniteCompleteFrameDispatcher dispatcher({1, MAX_FRAME_BYTES});
    const std::string malformed = "8=FIX.4.2\0019=999999999999999999999\00135=A\00110=000\001";
    const auto result = dispatcher.process(malformed.data(), malformed.size(), 1);
    CHECK(result.frames.empty());
    CHECK(result.terminalFault == InfiniteDispatchFault::MalformedFrame);

    const std::string emptyLength = "8=FIX.4.2\0019=\00135=A\00110=000\001";
    InfiniteCompleteFrameDispatcher emptyLengthDispatcher({1, MAX_FRAME_BYTES});
    CHECK(
        emptyLengthDispatcher.process(emptyLength.data(), emptyLength.size(), 2).terminalFault
        == InfiniteDispatchFault::MalformedFrame);

    const std::string belowDigit = "8=FIX.4.2\0019=/\00135=A\00110=000\001";
    InfiniteCompleteFrameDispatcher belowDigitDispatcher({1, MAX_FRAME_BYTES});
    CHECK(
        belowDigitDispatcher.process(belowDigit.data(), belowDigit.size(), 2).terminalFault
        == InfiniteDispatchFault::MalformedFrame);

    const std::string aboveDigit = "8=FIX.4.2\0019=bbbbb\00135=A\00110=000\001";
    InfiniteCompleteFrameDispatcher aboveDigitDispatcher({1, MAX_FRAME_BYTES});
    CHECK(
        aboveDigitDispatcher.process(aboveDigit.data(), aboveDigit.size(), 2).terminalFault
        == InfiniteDispatchFault::MalformedFrame);
  }

  SECTION("representable BodyLength arithmetic beyond the frame bound is rejected") {
    InfiniteCompleteFrameDispatcher dispatcher({1, MAX_FRAME_BYTES});
    const auto maximum = std::to_string(std::numeric_limits<std::size_t>::max());
    const std::string malformed = "8=FIX.4.2\0019=" + maximum + "\001";
    const auto result = dispatcher.process(malformed.data(), malformed.size(), 1);
    CHECK(result.frames.empty());
    CHECK(result.terminalFault == InfiniteDispatchFault::FrameTooLarge);

    const auto bodyBegin = std::string("8=FIX.4.2\0019=").size() + maximum.size() + 1;
    const auto checksumOverflowLength = std::numeric_limits<std::size_t>::max() - bodyBegin;
    const std::string checksumOverflow = "8=FIX.4.2\0019=" + std::to_string(checksumOverflowLength) + "\001";
    InfiniteCompleteFrameDispatcher checksumOverflowDispatcher({1, MAX_FRAME_BYTES});
    const auto checksumOverflowResult
        = checksumOverflowDispatcher.process(checksumOverflow.data(), checksumOverflow.size(), 1);
    CHECK(checksumOverflowResult.frames.empty());
    CHECK(checksumOverflowResult.terminalFault == InfiniteDispatchFault::FrameTooLarge);
  }

  SECTION("valid maximal prefix is returned with a malformed suffix") {
    InfiniteCompleteFrameDispatcher dispatcher({2, MAX_FRAME_BYTES * 2});
    const auto valid = makeMessageOfSize(128);
    const std::string read = valid + "8=FIX.4.2\0019=bad\001";
    const auto result = dispatcher.process(read.data(), read.size(), 1);
    REQUIRE(result.frames.size() == 1);
    CHECK(result.frames[0].bytes == valid);
    CHECK(result.terminalFault == InfiniteDispatchFault::MalformedFrame);
  }

  SECTION("valid maximal prefix is returned with a provably oversized suffix") {
    InfiniteCompleteFrameDispatcher dispatcher({2, MAX_FRAME_BYTES * 2});
    const auto valid = makeMessageOfSize(128);
    const std::string read = valid + "8=FIX.4.2\0019=65537\001";
    const auto result = dispatcher.process(read.data(), read.size(), 1);
    REQUIRE(result.frames.size() == 1);
    CHECK(result.frames[0].bytes == valid);
    CHECK(result.terminalFault == InfiniteDispatchFault::FrameTooLarge);
  }

  SECTION("a fragmented maximum frame can complete alongside a valid successor") {
    InfiniteCompleteFrameDispatcher dispatcher({2, MAX_FRAME_BYTES * 2});
    const auto maximum = makeMessageOfSize(MAX_FRAME_BYTES);
    const auto successor = makeMessageOfSize(128);
    constexpr std::size_t FIRST_READ_BYTES = 65'000;

    const auto first = dispatcher.process(maximum.data(), FIRST_READ_BYTES, 1);
    CHECK(first.frames.empty());
    CHECK_FALSE(first.terminalFault.has_value());

    const std::string secondRead = maximum.substr(FIRST_READ_BYTES) + successor;
    const auto second = dispatcher.process(secondRead.data(), secondRead.size(), 2);
    REQUIRE(second.frames.size() == 2);
    CHECK(second.frames[0].bytes == maximum);
    CHECK(second.frames[0].observedTaiNs == 2);
    CHECK(second.frames[1].bytes == successor);
    CHECK(second.frames[1].observedTaiNs == 2);
    CHECK_FALSE(second.terminalFault.has_value());
  }

  SECTION("batch frame and byte limits reject the complete read") {
    const auto first = makeMessageOfSize(128);
    const auto second = makeMessageOfSize(129);
    const auto read = first + second;

    InfiniteCompleteFrameDispatcher frameLimited({1, read.size()});
    const auto frames = frameLimited.process(read.data(), read.size(), 1);
    CHECK(frames.frames.empty());
    CHECK(frames.terminalFault == InfiniteDispatchFault::BatchLimit);

    InfiniteCompleteFrameDispatcher byteLimited({2, read.size() - 1});
    const auto bytes = byteLimited.process(read.data(), read.size(), 1);
    CHECK(bytes.frames.empty());
    CHECK(bytes.terminalFault == InfiniteDispatchFault::BatchLimit);
  }

  SECTION("invalid observation and pointer fail without consuming input") {
    const auto message = makeMessageOfSize(128);
    InfiniteCompleteFrameDispatcher dispatcher({1, MAX_FRAME_BYTES});
    CHECK(
        dispatcher.process(message.data(), message.size(), 0).terminalFault
        == InfiniteDispatchFault::InvalidObservation);
    CHECK(dispatcher.process(nullptr, 1, 1).terminalFault == InfiniteDispatchFault::MalformedFrame);
    const auto empty = dispatcher.process(nullptr, 0, 1);
    CHECK(empty.frames.empty());
    CHECK_FALSE(empty.terminalFault.has_value());
    const auto valid = dispatcher.process(message.data(), message.size(), 1);
    REQUIRE(valid.frames.size() == 1);
    CHECK(valid.frames[0].bytes == message);
  }
}
