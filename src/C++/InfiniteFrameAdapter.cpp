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

#include "Application.h"
#include "Fields.h"
#include "InfiniteCompleteFrame.h"
#include "InfiniteFrameAdapterStore.h"
#include "InfiniteSessionClassification.h"
#include "MessageStore.h"
#include "Responder.h"
#include "Session.h"
#include "TimeRange.h"
#include "Values.h"

#ifdef HAVE_SSL
#include <openssl/crypto.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace FIX {
class InfiniteFrameAdapterAccess {
public:
  static InfiniteAtHeadBinding atHead(irfq_infinite_handle_v1 token) {
    std::array<std::uint8_t, 32> value{};
    std::memcpy(value.data(), &token, sizeof(token));
    for (std::size_t index = 0; index < sizeof(token); ++index) {
      value[index + sizeof(token)] = static_cast<std::uint8_t>(value[index] ^ UINT8_C(0xa5));
    }
    return InfiniteAtHeadBinding(value);
  }

  static InfiniteSessionClassification classify(
      Session &session,
      const InfiniteAtHeadBinding &binding,
      std::string &&bytes,
      const UtcTimeStamp &now) {
    return session.classifyInfiniteFrame(binding, std::move(bytes), now);
  }

  static InfiniteEffectAuthorization authorize(const InfiniteSessionClassification &classification) {
    return InfiniteEffectAuthorization(
        classification.m_binding,
        classification.m_expected,
        classification.m_actionData);
  }

  static void apply(
      Session &session,
      const InfiniteSessionClassification &classification,
      InfiniteEffectAuthorization &&authorization) {
    session.applyInfiniteClassification(classification, std::move(authorization));
  }

  static bool fenced(const Session &session) { return session.m_infiniteSessionFenced; }

  static void cleanseCredentials(Message &message) { Session::cleanseInfiniteMessageCredentials(message); }
};
} // namespace FIX

namespace {
using namespace FIX;
using namespace FIX::infinite_frame_adapter_detail;

static_assert(sizeof(void *) == 8, "Infinite frame adapter ABI v1 requires 64-bit pointers");
static_assert(sizeof(irfq_infinite_callback_table_v1) == 64, "Infinite callback table layout drift");
static_assert(
    IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1
        == sizeof(irfq_infinite_dispatch_response_v1)
               + IRFQ_INFINITE_MAX_BATCH_FRAMES_V1 * sizeof(irfq_infinite_registration_result_v1),
    "Infinite dispatch output capacity drift");

struct HandleLess {
  bool operator()(const irfq_infinite_handle_v1 &lhs, const irfq_infinite_handle_v1 &rhs) const {
    return lhs.object < rhs.object || (lhs.object == rhs.object && lhs.generation < rhs.generation);
  }
};

bool sameHandle(irfq_infinite_handle_v1 lhs, irfq_infinite_handle_v1 rhs) {
  return lhs.object == rhs.object && lhs.generation == rhs.generation;
}

bool validHandle(irfq_infinite_handle_v1 handle) { return handle.object != 0 && handle.generation != 0; }

template <std::size_t Size> bool allZero(const std::uint8_t (&bytes)[Size]) {
  return std::all_of(std::begin(bytes), std::end(bytes), [](std::uint8_t value) { return value == 0; });
}

template <typename T> bool validRequestHeader(const T *request);

bool aligned(const void *pointer, std::size_t alignment) {
  return pointer != nullptr && reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}

irfq_infinite_status_v1 prepareOutput(
    void *output,
    std::uint64_t capacity,
    std::uint32_t structureSize,
    std::uint64_t requiredCapacity) {
  if (!aligned(output, alignof(irfq_infinite_output_header_v1)) || capacity < sizeof(irfq_infinite_output_header_v1)) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  auto *header = static_cast<irfq_infinite_output_header_v1 *>(output);
  header->written_length = 0;
  if (header->structure_size != structureSize || header->abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1) {
    return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
  }
  if (header->reserved != 0 || capacity < requiredCapacity) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  return IRFQ_INFINITE_STATUS_OK_V1;
}

bool rangesOverlap(const void *first, std::uint64_t firstLength, const void *second, std::uint64_t secondLength) {
  if (first == nullptr || second == nullptr || firstLength == 0 || secondLength == 0) {
    return false;
  }
  const auto firstBegin = reinterpret_cast<std::uintptr_t>(first);
  const auto secondBegin = reinterpret_cast<std::uintptr_t>(second);
  if (firstLength > std::numeric_limits<std::uintptr_t>::max() - firstBegin
      || secondLength > std::numeric_limits<std::uintptr_t>::max() - secondBegin) {
    return true;
  }
  return firstBegin < secondBegin + secondLength && secondBegin < firstBegin + firstLength;
}

template <typename T> bool requestOverlapsOutput(const T *request, const void *output, std::uint64_t outputCapacity) {
  return rangesOverlap(request, sizeof(T), output, outputCapacity);
}

template <typename T> irfq_infinite_status_v1 prepareOutput(void *output, std::uint64_t capacity) {
  return prepareOutput(output, capacity, sizeof(T), sizeof(T));
}

template <typename Request>
irfq_infinite_status_v1 prepareRequestOutput(
    const Request *request,
    const void *nestedInput,
    std::uint64_t nestedLength,
    void *output,
    std::uint64_t outputCapacity,
    std::uint32_t responseSize,
    std::uint64_t requiredCapacity) {
  if (!aligned(output, alignof(irfq_infinite_output_header_v1))
      || outputCapacity < sizeof(irfq_infinite_output_header_v1)) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  if (requestOverlapsOutput(request, output, outputCapacity)
      || (validRequestHeader(request) && rangesOverlap(nestedInput, nestedLength, output, outputCapacity))) {
    return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
  }
  return prepareOutput(output, outputCapacity, responseSize, requiredCapacity);
}

template <typename Request, typename Response>
irfq_infinite_status_v1 prepareRequestOutput(
    const Request *request,
    const void *nestedInput,
    std::uint64_t nestedLength,
    void *output,
    std::uint64_t outputCapacity) {
  return prepareRequestOutput(
      request,
      nestedInput,
      nestedLength,
      output,
      outputCapacity,
      sizeof(Response),
      sizeof(Response));
}

template <typename T> irfq_infinite_status_v1 publishFixed(void *output, std::uint64_t capacity, T response) {
  const auto prepared = prepareOutput<T>(output, capacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  response.header.structure_size = sizeof(T);
  response.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  response.header.reserved = 0;
  response.header.written_length = 0;
  std::memcpy(output, &response, sizeof(T));
  static_cast<irfq_infinite_output_header_v1 *>(output)->written_length = sizeof(T);
  return response.header.status;
}

irfq_infinite_status_v1 publishBytes(void *output, std::uint64_t capacity, const void *bytes, std::size_t length) {
  const auto prepared = prepareOutput(output, capacity, sizeof(irfq_infinite_dispatch_response_v1), length);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  std::memcpy(output, bytes, length);
  auto *header = static_cast<irfq_infinite_output_header_v1 *>(output);
  header->written_length = length;
  return header->status;
}

template <typename T> bool validRequestHeader(const T *request) {
  return aligned(request, alignof(T)) && request->structure_size == sizeof(T)
         && request->abi_version == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
}

class AlignedBytes {
public:
  explicit AlignedBytes(std::size_t size)
      : m_words((size + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t), 0),
        m_size(size) {}

  void *data() { return m_words.data(); }
  const void *data() const { return m_words.data(); }
  std::size_t size() const { return m_size; }

private:
  std::vector<std::uint64_t> m_words;
  std::size_t m_size;
};

void secureErase(std::string &bytes) noexcept {
#ifdef HAVE_SSL
  OPENSSL_cleanse(bytes.data(), bytes.size());
#else
  volatile char *cursor = bytes.empty() ? nullptr : &bytes[0];
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    cursor[index] = 0;
  }
#endif
  bytes.clear();
}

void removeCredentials(FieldMap &fields) {
  fields.removeField(FIELD::Username);
  fields.removeField(FIELD::Password);
  for (const auto &groupSet : fields.groups()) {
    for (auto *group : groupSet.second) {
      if (group) {
        removeCredentials(*group);
      }
    }
  }
}

void removeCredentials(Message &message) {
  removeCredentials(static_cast<FieldMap &>(message));
  removeCredentials(message.getHeader());
  removeCredentials(message.getTrailer());
}

class SensitiveFrameGuard {
public:
  explicit SensitiveFrameGuard(std::vector<InfiniteCompleteFrame> &frames)
      : m_frames(frames) {}

  ~SensitiveFrameGuard() {
    for (auto &frame : m_frames) {
      secureErase(frame.bytes);
    }
  }

private:
  std::vector<InfiniteCompleteFrame> &m_frames;
};

template <typename T> T &initializeCallbackOutput(AlignedBytes &output) {
  auto &response = *static_cast<T *>(output.data());
  response.header.structure_size = sizeof(T);
  response.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
  return response;
}

template <typename T>
bool validCallbackResponse(
    const T &response,
    irfq_infinite_status_v1 returned,
    std::uint64_t expectedLength = sizeof(T)) {
  return response.header.structure_size == sizeof(T)
         && response.header.abi_version == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1 && response.header.reserved == 0
         && response.header.status == returned && response.header.written_length == expectedLength;
}

struct Engine;
struct Connection;

using ActiveCallback = std::pair<const Engine *, const Connection *>;
thread_local std::vector<ActiveCallback> activeCallbacks;

bool activeCallbackContains(const Engine *engine) {
  return std::any_of(activeCallbacks.begin(), activeCallbacks.end(), [engine](const ActiveCallback &callback) {
    return callback.first == engine;
  });
}

bool activeCallbackContains(const Connection *connection) {
  return std::any_of(activeCallbacks.begin(), activeCallbacks.end(), [connection](const ActiveCallback &callback) {
    return callback.second == connection;
  });
}

bool insideCallback() { return !activeCallbacks.empty(); }

class CallbackScope {
public:
  CallbackScope(const Engine *engine, const Connection *connection) {
    activeCallbacks.emplace_back(engine, connection);
  }

  ~CallbackScope() { activeCallbacks.pop_back(); }
};

struct AdapterResponder : Responder {
  bool send(const std::string &) override { return true; }
  void disconnect() override { disconnected = true; }

  bool disconnected{false};
};

struct Candidate {
  irfq_infinite_registration_result_v1 registration{};
  irfq_infinite_handle_v1 externalToken{};
  std::string bytes;
  bool atHead{false};
};

struct PendingClassification {
  irfq_infinite_handle_v1 handle{};
  irfq_infinite_handle_v1 authorizationHandle{};
  irfq_infinite_handle_v1 externalAuthorizationHandle{};
  irfq_infinite_handle_v1 token{};
  std::unique_ptr<InfiniteSessionClassification> classification;
  std::unique_ptr<InfiniteEffectAuthorization> authorization;
};

struct Engine : std::enable_shared_from_this<Engine> {
  explicit Engine(const irfq_infinite_callback_table_v1 &value)
      : callbacks(value) {}

  std::mutex mutex;
  std::condition_variable condition;
  irfq_infinite_engine_lifecycle_v1 state{IRFQ_INFINITE_ENGINE_INITIALIZED_V1};
  bool terminalFailure{false};
  std::size_t inFlight{0};
  std::size_t pendingBootstraps{0};
  std::uint64_t connectionGenerationFloor{0};
  std::uint64_t lastConnectionGeneration{0};
  irfq_infinite_handle_v1 handle{};
  irfq_infinite_callback_table_v1 callbacks{};
  std::map<std::uint64_t, std::shared_ptr<Connection>> connections;
};

struct Connection : std::enable_shared_from_this<Connection> {
  Connection(std::shared_ptr<Engine> owner, irfq_infinite_handle_v1 rustHandle, std::int64_t bootstrapObservedTaiNs)
      : engine(std::move(owner)),
        externalHandle(rustHandle),
        dispatcher({IRFQ_INFINITE_MAX_BATCH_FRAMES_V1, IRFQ_INFINITE_MAX_BATCH_BYTES_V1}, bootstrapObservedTaiNs),
        sessionTime(UtcTimeOnly(0, 0, 0), UtcTimeOnly(0, 0, 0)) {}

  void initializeSession(const Message &logon) {
    BeginString beginString;
    SenderCompID sender;
    TargetCompID target;
    MsgType messageType;
    logon.getHeader().getField(beginString);
    logon.getHeader().getField(sender);
    logon.getHeader().getField(target);
    logon.getHeader().getField(messageType);
    if (messageType != MsgType_Logon) {
      throw InvalidMessage("Infinite bootstrap requires initial Logon");
    }
    sessionId = SessionID(beginString, target, sender);
    const auto dictionary = std::string(IRFQ_INFINITE_DICTIONARY_PATH) + "/" + dictionaryName(beginString, logon);
    if (beginString == BeginString_FIXT11) {
      dictionaries.addTransportDataDictionary(beginString, std::string(IRFQ_INFINITE_DICTIONARY_PATH) + "/FIXT11.xml");
      DefaultApplVerID applicationVersion;
      logon.getField(applicationVersion);
      dictionaries.addApplicationDataDictionary(ApplVerID(applicationVersion.getValue()), dictionary);
    } else {
      dictionaries.addTransportDataDictionary(beginString, dictionary);
    }
    session = std::make_unique<Session>(
        []() { return UtcTimeStamp::now(); },
        application,
        storeFactory,
        sessionId,
        dictionaries,
        sessionTime,
        0,
        nullptr);
    if (Session::lookupSession(sessionId) != session.get()) {
      throw std::logic_error("Duplicate Infinite session identity");
    }
    session->setCheckLatency(false);
    session->setResponder(&responder);
    session->next(logon, UtcTimeStamp::now());
    if (!session->isLoggedOn() || responder.disconnected) {
      throw InvalidMessage("Infinite bootstrap Logon was not accepted");
    }
  }

  static std::string dictionaryName(const BeginString &beginString, const Message &logon) {
    if (beginString == BeginString_FIX40) {
      return "FIX40.xml";
    }
    if (beginString == BeginString_FIX41) {
      return "FIX41.xml";
    }
    if (beginString == BeginString_FIX42) {
      return "FIX42.xml";
    }
    if (beginString == BeginString_FIX43) {
      return "FIX43.xml";
    }
    if (beginString == BeginString_FIX44) {
      return "FIX44.xml";
    }
    if (beginString == BeginString_FIX50) {
      return "FIX50.xml";
    }
    if (beginString == BeginString_FIXT11) {
      DefaultApplVerID applicationVersion;
      logon.getField(applicationVersion);
      const auto &value = applicationVersion.getValue();
      if (value == ApplVerID_FIX40) {
        return "FIX40.xml";
      }
      if (value == ApplVerID_FIX41) {
        return "FIX41.xml";
      }
      if (value == ApplVerID_FIX42) {
        return "FIX42.xml";
      }
      if (value == ApplVerID_FIX43) {
        return "FIX43.xml";
      }
      if (value == ApplVerID_FIX44) {
        return "FIX44.xml";
      }
      if (value == ApplVerID_FIX50) {
        return "FIX50.xml";
      }
      if (value == ApplVerID_FIX50_SP1) {
        return "FIX50SP1.xml";
      }
      if (value == ApplVerID_FIX50_SP2) {
        return "FIX50SP2.xml";
      }
    }
    throw InvalidMessage("Infinite bootstrap uses an unsupported FIX dictionary");
  }

  std::weak_ptr<Engine> engine;
  irfq_infinite_handle_v1 handle{};
  irfq_infinite_handle_v1 externalHandle{};
  std::mutex lifecycleMutex;
  std::condition_variable lifecycleCondition;
  irfq_infinite_connection_lifecycle_v1 state{IRFQ_INFINITE_CONNECTION_OPEN_V1};
  bool terminalFailure{false};
  std::size_t inFlight{0};
  std::atomic<bool> faulted{false};
  std::atomic<bool> fenceNotified{false};
  std::mutex lane;
  InfiniteCompleteFrameDispatcher dispatcher;
  NullApplication application;
  BoundedMemoryStoreFactory storeFactory;
  AdapterResponder responder;
  DataDictionaryProvider dictionaries;
  TimeRange sessionTime;
  SessionID sessionId;
  std::unique_ptr<Session> session;
  std::uint64_t lastRegisteredOrdinal{0};
  std::map<irfq_infinite_handle_v1, Candidate, HandleLess> candidates;
  std::map<irfq_infinite_handle_v1, PendingClassification, HandleLess> classifications;
};

std::mutex registryMutex;
// Terminal callback failures keep their handles queryable until process exit, so registry storage must outlive Session
// statics.
auto &engines = *new std::map<std::uint64_t, std::shared_ptr<Engine>>;
auto &connections = *new std::map<irfq_infinite_handle_v1, std::shared_ptr<Connection>, HandleLess>;
std::uint64_t nextObject{1};
std::uint64_t nextGeneration{1};

irfq_infinite_handle_v1 nextHandle() {
  std::lock_guard<std::mutex> lock(registryMutex);
  if (nextObject == std::numeric_limits<std::uint64_t>::max()
      || nextGeneration == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("Infinite adapter handle generation exhausted");
  }
  return {nextObject++, nextGeneration++};
}

std::shared_ptr<Engine> findEngine(irfq_infinite_handle_v1 handle) {
  std::lock_guard<std::mutex> lock(registryMutex);
  const auto found = engines.find(handle.object);
  return found != engines.end() && found->second->handle.generation == handle.generation ? found->second : nullptr;
}

std::shared_ptr<Connection> findConnection(irfq_infinite_handle_v1 handle) {
  std::lock_guard<std::mutex> lock(registryMutex);
  const auto found = connections.find(handle);
  return found != connections.end() ? found->second : nullptr;
}

bool isConnectionTombstone(irfq_infinite_handle_v1 handle) {
  std::shared_ptr<Engine> engine;
  {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto found = engines.find(handle.object);
    if (found != engines.end()) {
      engine = found->second;
    }
  }
  if (!engine) {
    return false;
  }
  std::lock_guard<std::mutex> lock(engine->mutex);
  return handle.generation >= engine->connectionGenerationFloor && handle.generation <= engine->lastConnectionGeneration
         && engine->connections.count(handle.generation) == 0;
}

class EngineCall {
public:
  explicit EngineCall(std::shared_ptr<Engine> engine)
      : m_engine(std::move(engine)) {
    if (!m_engine) {
      return;
    }
    std::lock_guard<std::mutex> lock(m_engine->mutex);
    if (m_engine->state == IRFQ_INFINITE_ENGINE_INITIALIZED_V1) {
      ++m_engine->inFlight;
      m_acquired = true;
    }
  }

  ~EngineCall() { release(); }
  EngineCall(const EngineCall &) = delete;
  EngineCall &operator=(const EngineCall &) = delete;

  bool acquired() const { return m_acquired; }
  const std::shared_ptr<Engine> &engine() const { return m_engine; }

  void release() {
    if (!m_acquired) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(m_engine->mutex);
      --m_engine->inFlight;
    }
    m_engine->condition.notify_all();
    m_acquired = false;
  }

private:
  std::shared_ptr<Engine> m_engine;
  bool m_acquired{false};
};

class BootstrapReservation {
public:
  explicit BootstrapReservation(std::shared_ptr<Engine> engine)
      : m_engine(std::move(engine)) {
    std::lock_guard<std::mutex> lock(m_engine->mutex);
    if (m_engine->state == IRFQ_INFINITE_ENGINE_INITIALIZED_V1
        && m_engine->connections.size() + m_engine->pendingBootstraps < IRFQ_INFINITE_MAX_CONNECTIONS_V1) {
      ++m_engine->pendingBootstraps;
      m_reserved = true;
    }
  }

  ~BootstrapReservation() { release(); }
  BootstrapReservation(const BootstrapReservation &) = delete;
  BootstrapReservation &operator=(const BootstrapReservation &) = delete;

  bool reserved() const { return m_reserved; }

  void completeWhileLocked() {
    --m_engine->pendingBootstraps;
    m_reserved = false;
  }

private:
  void release() {
    if (!m_reserved) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(m_engine->mutex);
      --m_engine->pendingBootstraps;
      m_reserved = false;
    }
    m_engine->condition.notify_all();
  }

  std::shared_ptr<Engine> m_engine;
  bool m_reserved{false};
};

bool fenceConnection(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason) noexcept;

class ConnectionCall {
public:
  explicit ConnectionCall(std::shared_ptr<Connection> connection)
      : m_connection(std::move(connection)),
        m_engine(m_connection ? m_connection->engine.lock() : nullptr) {
    if (!m_connection || !m_engine.acquired()) {
      return;
    }
    if (activeCallbackContains(m_connection.get())) {
      m_connection->faulted.store(true, std::memory_order_release);
      fenceConnection(m_engine.engine(), m_connection, 0);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(m_connection->lifecycleMutex);
      if (m_connection->state != IRFQ_INFINITE_CONNECTION_OPEN_V1) {
        return;
      }
      ++m_connection->inFlight;
      m_counted = true;
    }
    if (insideCallback()) {
      m_lane = std::unique_lock<std::mutex>(m_connection->lane, std::try_to_lock);
      if (!m_lane.owns_lock()) {
        m_connection->faulted.store(true, std::memory_order_release);
        return;
      }
    } else {
      m_lane = std::unique_lock<std::mutex>(m_connection->lane);
    }
    m_acquired = true;
  }

  ~ConnectionCall() {
    if (!m_counted) {
      return;
    }
    if (m_lane.owns_lock()) {
      m_lane.unlock();
    }
    {
      std::lock_guard<std::mutex> lock(m_connection->lifecycleMutex);
      --m_connection->inFlight;
    }
    m_connection->lifecycleCondition.notify_all();
  }

  ConnectionCall(const ConnectionCall &) = delete;
  ConnectionCall &operator=(const ConnectionCall &) = delete;
  bool acquired() const { return m_acquired; }

private:
  std::shared_ptr<Connection> m_connection;
  EngineCall m_engine;
  std::unique_lock<std::mutex> m_lane;
  bool m_counted{false};
  bool m_acquired{false};
};

irfq_infinite_dispatch_fault_v1 dispatchFault(const std::optional<InfiniteDispatchFault> &fault) {
  return fault ? static_cast<irfq_infinite_dispatch_fault_v1>(*fault) : IRFQ_INFINITE_DISPATCH_FAULT_NONE_V1;
}

irfq_infinite_action_v1 actionKind(InfiniteSessionActionKind action) {
  return static_cast<irfq_infinite_action_v1>(action);
}

irfq_infinite_sequence_disposition_v1 sequenceDisposition(InfiniteSequenceDisposition disposition) {
  return static_cast<irfq_infinite_sequence_disposition_v1>(disposition);
}

irfq_infinite_status_v1 invokeExternalConnectionCallback(
    const std::shared_ptr<Engine> &engine,
    irfq_infinite_handle_v1 externalHandle,
    const Connection *connection,
    irfq_infinite_connection_callback_v1 callback,
    std::uint32_t reason) {
  if (!callback) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
  CallbackScope scope(engine.get(), connection);
  return callback(engine->callbacks.context, externalHandle, reason);
}

irfq_infinite_status_v1 invokeConnectionCallback(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    irfq_infinite_connection_callback_v1 callback,
    std::uint32_t reason) {
  return invokeExternalConnectionCallback(engine, connection->externalHandle, connection.get(), callback, reason);
}

void markConnectionTerminalFailure(const std::shared_ptr<Connection> &connection) noexcept {
  connection->faulted.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(connection->lifecycleMutex);
    if (connection->state != IRFQ_INFINITE_CONNECTION_CLOSED_V1) {
      connection->state = IRFQ_INFINITE_CONNECTION_CLOSING_V1;
      connection->terminalFailure = true;
    }
  }
  connection->lifecycleCondition.notify_all();
}

bool fenceConnection(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason) noexcept {
  connection->faulted.store(true, std::memory_order_release);
  if (connection->fenceNotified.exchange(true, std::memory_order_acq_rel)) {
    std::lock_guard<std::mutex> lock(connection->lifecycleMutex);
    return !connection->terminalFailure;
  }
  try {
    if (invokeConnectionCallback(engine, connection, engine->callbacks.fence, reason)
        == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1) {
      connection->lifecycleCondition.notify_all();
      return true;
    }
  } catch (...) {}
  markConnectionTerminalFailure(connection);
  return false;
}

bool fenceIfFaulted(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason = 0) noexcept {
  if (!connection->faulted.load(std::memory_order_acquire)) {
    return false;
  }
  fenceConnection(engine, connection, reason);
  return true;
}

bool releaseConnection(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason) noexcept {
  try {
    return invokeConnectionCallback(engine, connection, engine->callbacks.release, reason)
           == IRFQ_INFINITE_STATUS_CLOSED_V1;
  } catch (...) {}
  return false;
}

bool rejectAcceptedExternal(
    const std::shared_ptr<Engine> &engine,
    irfq_infinite_handle_v1 externalHandle,
    std::uint32_t reason) noexcept {
  bool fenced = false;
  try {
    fenced = invokeExternalConnectionCallback(engine, externalHandle, nullptr, engine->callbacks.fence, reason)
             == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
  } catch (...) {}
  bool released = false;
  try {
    released = invokeExternalConnectionCallback(engine, externalHandle, nullptr, engine->callbacks.release, reason)
               == IRFQ_INFINITE_STATUS_CLOSED_V1;
  } catch (...) {}
  return fenced && released;
}

void discardConnectionState(const std::shared_ptr<Connection> &connection) {
  connection->classifications.clear();
  connection->candidates.clear();
  connection->session.reset();
}

void quiesceTerminalConnection(const std::shared_ptr<Connection> &connection) {
  {
    std::unique_lock<std::mutex> lock(connection->lifecycleMutex);
    connection->lifecycleCondition.wait(lock, [&connection]() { return connection->inFlight == 0; });
  }
  std::unique_lock<std::mutex> lane(connection->lane);
  discardConnectionState(connection);
}

bool completeCloseConnection(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason,
    std::unique_lock<std::mutex> lane) {
  discardConnectionState(connection);
  lane.unlock();
  if (!releaseConnection(engine, connection, reason)) {
    markConnectionTerminalFailure(connection);
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(engine->mutex);
    engine->connections.erase(connection->handle.generation);
  }
  {
    std::lock_guard<std::mutex> lock(registryMutex);
    connections.erase(connection->handle);
  }
  {
    std::lock_guard<std::mutex> lock(connection->lifecycleMutex);
    connection->state = IRFQ_INFINITE_CONNECTION_CLOSED_V1;
  }
  connection->lifecycleCondition.notify_all();
  return true;
}

bool tryCloseConnectionFromCallback(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason) {
  std::unique_lock<std::mutex> lifecycle(connection->lifecycleMutex, std::try_to_lock);
  if (!lifecycle.owns_lock()) {
    connection->faulted.store(true, std::memory_order_release);
    fenceConnection(engine, connection, reason);
    connection->lifecycleCondition.notify_all();
    return false;
  }
  if (connection->state == IRFQ_INFINITE_CONNECTION_CLOSED_V1) {
    return true;
  }
  if (connection->terminalFailure) {
    return false;
  }
  if (connection->state != IRFQ_INFINITE_CONNECTION_OPEN_V1 || connection->inFlight != 0) {
    connection->faulted.store(true, std::memory_order_release);
    lifecycle.unlock();
    fenceConnection(engine, connection, reason);
    connection->lifecycleCondition.notify_all();
    return false;
  }
  std::unique_lock<std::mutex> lane(connection->lane, std::try_to_lock);
  if (!lane.owns_lock()) {
    connection->faulted.store(true, std::memory_order_release);
    lifecycle.unlock();
    fenceConnection(engine, connection, reason);
    connection->lifecycleCondition.notify_all();
    return false;
  }
  connection->state = IRFQ_INFINITE_CONNECTION_CLOSING_V1;
  connection->faulted.store(true, std::memory_order_release);
  lifecycle.unlock();
  if (!fenceConnection(engine, connection, reason)) {
    discardConnectionState(connection);
    return false;
  }
  return completeCloseConnection(engine, connection, reason, std::move(lane));
}

bool closeConnection(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason) {
  bool terminalFailure = false;
  {
    std::unique_lock<std::mutex> lock(connection->lifecycleMutex);
    if (connection->state == IRFQ_INFINITE_CONNECTION_CLOSED_V1) {
      return true;
    }
    if (connection->state == IRFQ_INFINITE_CONNECTION_CLOSING_V1) {
      connection->lifecycleCondition.wait(lock, [&connection]() {
        return connection->state == IRFQ_INFINITE_CONNECTION_CLOSED_V1 || connection->terminalFailure;
      });
      if (connection->state == IRFQ_INFINITE_CONNECTION_CLOSED_V1) {
        return true;
      }
      terminalFailure = true;
    } else {
      connection->state = IRFQ_INFINITE_CONNECTION_CLOSING_V1;
      connection->faulted.store(true, std::memory_order_release);
    }
  }
  if (terminalFailure) {
    quiesceTerminalConnection(connection);
    return false;
  }

  if (!fenceConnection(engine, connection, reason)) {
    quiesceTerminalConnection(connection);
    return false;
  }

  {
    std::unique_lock<std::mutex> lock(connection->lifecycleMutex);
    connection->lifecycleCondition.wait(lock, [&connection]() { return connection->inFlight == 0; });
  }
  std::unique_lock<std::mutex> lane(connection->lane);
  return completeCloseConnection(engine, connection, reason, std::move(lane));
}

bool classificationConsumes(const InfiniteSessionClassification &classification) {
  return infiniteActionPlan(classification.actionData()).resultingState.targetSequence
         != classification.expected().targetSequence;
}

irfq_infinite_operation_response_v1 operationResponse(irfq_infinite_status_v1 status, std::uint32_t lifecycle) {
  irfq_infinite_operation_response_v1 response{};
  response.header.status = status;
  response.lifecycle = lifecycle;
  return response;
}

irfq_infinite_bootstrap_response_v1 bootstrapResponse(
    irfq_infinite_status_v1 status,
    irfq_infinite_bootstrap_outcome_v1 outcome) {
  irfq_infinite_bootstrap_response_v1 response{};
  response.header.status = status;
  response.outcome = outcome;
  return response;
}

bool validCallbackHeader(const irfq_infinite_output_header_v1 &header) {
  return header.abi_version == IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1 && header.reserved == 0;
}
} // namespace

extern "C" irfq_infinite_status_v1 irfq_infinite_frame_adapter_query_v1(irfq_infinite_abi_info_v1 *info) noexcept {
  try {
    const std::uint16_t littleEndian = 1;
    if (!aligned(info, alignof(irfq_infinite_abi_info_v1)) || info->structure_size != sizeof(*info)
        || info->abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1
        || *reinterpret_cast<const std::uint8_t *>(&littleEndian) != 1) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!allZero(info->reserved)) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    *info
        = {sizeof(*info),
           IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
           IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1,
           IRFQ_INFINITE_MAX_CONNECTIONS_V1,
           IRFQ_INFINITE_MAX_BATCH_FRAMES_V1,
           IRFQ_INFINITE_MAX_FRAME_BYTES_V1,
           IRFQ_INFINITE_MAX_BATCH_BYTES_V1,
           {}};
    return IRFQ_INFINITE_STATUS_OK_V1;
  } catch (...) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}

extern "C" irfq_infinite_status_v1 irfq_infinite_engine_initialize_v1(
    const irfq_infinite_engine_init_request_v1 *request,
    void *output,
    std::uint64_t outputCapacity) noexcept {
  const auto callbackInput = validRequestHeader(request) ? request->callbacks : nullptr;
  const auto prepared = prepareRequestOutput<irfq_infinite_engine_init_request_v1, irfq_infinite_engine_response_v1>(
      request,
      callbackInput,
      sizeof(irfq_infinite_callback_table_v1),
      output,
      outputCapacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  try {
    if (!validRequestHeader(request)) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (request->required_capabilities != IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1 || !allZero(request->reserved)
        || !aligned(request->callbacks, alignof(irfq_infinite_callback_table_v1))) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    const auto &callbacks = *request->callbacks;
    if (callbacks.structure_size != sizeof(callbacks)
        || callbacks.abi_version != IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!callbacks.register_batch || !callbacks.wait_head || !callbacks.authorize || !callbacks.fence
        || !callbacks.release) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }

    auto engine = std::make_shared<Engine>(callbacks);
    engine->handle = nextHandle();
    engine->connectionGenerationFloor = engine->handle.generation + 1;
    engine->lastConnectionGeneration = engine->handle.generation;
    {
      std::lock_guard<std::mutex> lock(registryMutex);
      engines.emplace(engine->handle.object, engine);
    }
    irfq_infinite_engine_response_v1 response{};
    response.header.status = IRFQ_INFINITE_STATUS_OK_V1;
    response.engine = engine->handle;
    response.capabilities = IRFQ_INFINITE_REQUIRED_CAPABILITIES_V1;
    response.lifecycle = IRFQ_INFINITE_ENGINE_INITIALIZED_V1;
    return publishFixed(output, outputCapacity, response);
  } catch (...) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}

extern "C" irfq_infinite_status_v1 irfq_infinite_engine_shutdown_v1(
    irfq_infinite_handle_v1 engineHandle,
    void *output,
    std::uint64_t outputCapacity) noexcept {
  const auto prepared = prepareOutput<irfq_infinite_operation_response_v1>(output, outputCapacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  try {
    auto engine = findEngine(engineHandle);
    if (!engine) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_SHUTDOWN_V1, IRFQ_INFINITE_ENGINE_SHUTDOWN_V1));
    }
    if (activeCallbackContains(engine.get())) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    std::vector<std::shared_ptr<Connection>> snapshot;
    {
      std::lock_guard<std::mutex> lock(engine->mutex);
      if (engine->state == IRFQ_INFINITE_ENGINE_SHUTDOWN_V1) {
        return publishFixed(
            output,
            outputCapacity,
            operationResponse(IRFQ_INFINITE_STATUS_SHUTDOWN_V1, IRFQ_INFINITE_ENGINE_SHUTDOWN_V1));
      }
      if (engine->terminalFailure) {
        return publishFixed(
            output,
            outputCapacity,
            operationResponse(IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1, IRFQ_INFINITE_ENGINE_CLOSING_V1));
      }
      engine->state = IRFQ_INFINITE_ENGINE_CLOSING_V1;
      for (const auto &entry : engine->connections) {
        snapshot.push_back(entry.second);
      }
    }
    bool closed = true;
    for (const auto &connection : snapshot) {
      closed = closeConnection(engine, connection, 0) && closed;
    }
    if (!closed) {
      {
        std::lock_guard<std::mutex> lock(engine->mutex);
        engine->terminalFailure = true;
      }
      engine->condition.notify_all();
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1, IRFQ_INFINITE_ENGINE_CLOSING_V1));
    }
    {
      std::unique_lock<std::mutex> lock(engine->mutex);
      engine->condition.wait(lock, [&engine]() { return engine->inFlight == 0; });
      engine->state = IRFQ_INFINITE_ENGINE_SHUTDOWN_V1;
    }
    {
      std::lock_guard<std::mutex> lock(registryMutex);
      for (const auto &connection : snapshot) {
        connections.erase(connection->handle);
      }
      engines.erase(engine->handle.object);
    }
    return publishFixed(
        output,
        outputCapacity,
        operationResponse(IRFQ_INFINITE_STATUS_SHUTDOWN_V1, IRFQ_INFINITE_ENGINE_SHUTDOWN_V1));
  } catch (...) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}

extern "C" irfq_infinite_status_v1 irfq_infinite_connection_bootstrap_v1(
    irfq_infinite_handle_v1 engineHandle,
    const irfq_infinite_bootstrap_request_v1 *request,
    void *output,
    std::uint64_t outputCapacity) noexcept {
  const auto frame = validRequestHeader(request) ? request->frame : irfq_infinite_slice_v1{};
  const auto prepared = prepareRequestOutput<irfq_infinite_bootstrap_request_v1, irfq_infinite_bootstrap_response_v1>(
      request,
      frame.data,
      frame.length,
      output,
      outputCapacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  try {
    auto engine = findEngine(engineHandle);
    if (engine && activeCallbackContains(engine.get())) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    EngineCall call(engine);
    if (!call.acquired()) {
      return IRFQ_INFINITE_STATUS_SHUTDOWN_V1;
    }
    if (!validRequestHeader(request)) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!engine->callbacks.bootstrap) {
      return publishFixed(
          output,
          outputCapacity,
          bootstrapResponse(IRFQ_INFINITE_STATUS_NOT_READY_V1, IRFQ_INFINITE_BOOTSTRAP_REJECTED_V1));
    }
    if (!validHandle(request->transport_nonce) || request->observed_tai_ns <= 0 || request->frame.length == 0
        || request->frame.length > IRFQ_INFINITE_MAX_FRAME_BYTES_V1 || request->frame.data == nullptr) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    InfiniteCompleteFrameDispatcher bootstrapDispatcher({1, IRFQ_INFINITE_MAX_FRAME_BYTES_V1});
    auto dispatch = bootstrapDispatcher.process(
        reinterpret_cast<const char *>(request->frame.data),
        request->frame.length,
        [request]() { return request->observed_tai_ns; });
    SensitiveFrameGuard sensitiveFrames(dispatch.frames);
    if (dispatch.terminalFault || dispatch.frames.size() != 1
        || dispatch.frames.front().bytes.size() != request->frame.length) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    try {
      if (identifyType(dispatch.frames.front().bytes) != MsgType_Logon) {
        return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
      }
    } catch (...) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }

    BootstrapReservation reservation(engine);
    if (!reservation.reserved()) {
      return publishFixed(
          output,
          outputCapacity,
          bootstrapResponse(IRFQ_INFINITE_STATUS_NOT_READY_V1, IRFQ_INFINITE_BOOTSTRAP_REJECTED_V1));
    }

    AlignedBytes callbackBytes(sizeof(irfq_infinite_bootstrap_response_v1));
    auto &callbackResponse = initializeCallbackOutput<irfq_infinite_bootstrap_response_v1>(callbackBytes);
    irfq_infinite_status_v1 returned;
    try {
      CallbackScope scope(engine.get(), nullptr);
      returned
          = engine->callbacks.bootstrap(engine->callbacks.context, request, callbackBytes.data(), callbackBytes.size());
    } catch (...) {
      if (validHandle(callbackResponse.connection)) {
        rejectAcceptedExternal(engine, callbackResponse.connection, 0);
      }
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    const auto expectedCallbackStatus
        = callbackResponse.outcome == IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1   ? IRFQ_INFINITE_STATUS_OK_V1
          : callbackResponse.outcome == IRFQ_INFINITE_BOOTSTRAP_REJECTED_V1 ? IRFQ_INFINITE_STATUS_NOT_READY_V1
          : callbackResponse.outcome == IRFQ_INFINITE_BOOTSTRAP_FENCED_V1   ? IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
                                                                            : IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    if (!validCallbackResponse(callbackResponse, returned) || !allZero(callbackResponse.reserved)
        || returned != expectedCallbackStatus
        || (callbackResponse.outcome != IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1
            && callbackResponse.outcome != IRFQ_INFINITE_BOOTSTRAP_REJECTED_V1
            && callbackResponse.outcome != IRFQ_INFINITE_BOOTSTRAP_FENCED_V1)) {
      if (validHandle(callbackResponse.connection)) {
        rejectAcceptedExternal(engine, callbackResponse.connection, 0);
      }
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (callbackResponse.outcome != IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1) {
      if (validHandle(callbackResponse.connection)) {
        if (!rejectAcceptedExternal(engine, callbackResponse.connection, 0)) {
          return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
        }
        return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
      }
      irfq_infinite_bootstrap_response_v1 response{};
      response.header.status = callbackResponse.outcome == IRFQ_INFINITE_BOOTSTRAP_FENCED_V1
                                   ? IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
                                   : IRFQ_INFINITE_STATUS_NOT_READY_V1;
      response.outcome = callbackResponse.outcome;
      return publishFixed(output, outputCapacity, response);
    }
    if (returned != IRFQ_INFINITE_STATUS_OK_V1 || !validHandle(callbackResponse.connection)) {
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }

    std::shared_ptr<Connection> connection;
    bool installed = false;
    try {
      Message logon(dispatch.frames.front().bytes, true);
      InfiniteFrameAdapterAccess::cleanseCredentials(logon);
      removeCredentials(logon);
      connection = std::make_shared<Connection>(engine, callbackResponse.connection, request->observed_tai_ns);
      connection->initializeSession(logon);
      irfq_infinite_status_v1 registrationStatus = IRFQ_INFINITE_STATUS_OK_V1;
      {
        std::lock_guard<std::mutex> lock(engine->mutex);
        if (engine->state != IRFQ_INFINITE_ENGINE_INITIALIZED_V1) {
          registrationStatus = IRFQ_INFINITE_STATUS_NOT_READY_V1;
        } else {
          for (const auto &entry : engine->connections) {
            if (sameHandle(entry.second->externalHandle, connection->externalHandle)) {
              registrationStatus = IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
              break;
            }
          }
          if (registrationStatus == IRFQ_INFINITE_STATUS_OK_V1) {
            if (engine->lastConnectionGeneration == std::numeric_limits<std::uint64_t>::max()) {
              registrationStatus = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
            } else {
              connection->handle = {engine->handle.object, engine->lastConnectionGeneration + 1};
              std::lock_guard<std::mutex> registryLock(registryMutex);
              connections.emplace(connection->handle, connection);
              try {
                engine->connections.emplace(connection->handle.generation, connection);
              } catch (...) {
                connections.erase(connection->handle);
                throw;
              }
              engine->lastConnectionGeneration = connection->handle.generation;
              reservation.completeWhileLocked();
              installed = true;
            }
          }
        }
      }
      if (registrationStatus != IRFQ_INFINITE_STATUS_OK_V1) {
        connection->session.reset();
        if (!rejectAcceptedExternal(engine, connection->externalHandle, 0)) {
          return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
        }
        return publishFixed(
            output,
            outputCapacity,
            bootstrapResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_BOOTSTRAP_FENCED_V1));
      }
    } catch (...) {
      if (connection) {
        if (installed) {
          if (!closeConnection(engine, connection, 0)) {
            return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
          }
        } else {
          connection->session.reset();
          if (!rejectAcceptedExternal(engine, connection->externalHandle, 0)) {
            return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
          }
        }
      } else {
        if (!rejectAcceptedExternal(engine, callbackResponse.connection, 0)) {
          return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
        }
      }
      return publishFixed(
          output,
          outputCapacity,
          bootstrapResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_BOOTSTRAP_FENCED_V1));
    }
    irfq_infinite_bootstrap_response_v1 response{};
    response.header.status = IRFQ_INFINITE_STATUS_OK_V1;
    response.connection = connection->handle;
    response.outcome = IRFQ_INFINITE_BOOTSTRAP_ACCEPTED_V1;
    return publishFixed(output, outputCapacity, response);
  } catch (...) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}

extern "C" irfq_infinite_status_v1 irfq_infinite_connection_dispatch_v1(
    irfq_infinite_handle_v1 connectionHandle,
    const irfq_infinite_dispatch_request_v1 *request,
    void *output,
    std::uint64_t outputCapacity) noexcept {
  const auto input = validRequestHeader(request) ? request->input : irfq_infinite_slice_v1{};
  const auto prepared = prepareRequestOutput(
      request,
      input.data,
      input.length,
      output,
      outputCapacity,
      sizeof(irfq_infinite_dispatch_response_v1),
      IRFQ_INFINITE_DISPATCH_OUTPUT_CAPACITY_V1);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  std::shared_ptr<Connection> connection;
  try {
    connection = findConnection(connectionHandle);
    ConnectionCall call(connection);
    if (!call.acquired()) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (!validRequestHeader(request)) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!allZero(request->reserved) || (request->input.length != 0 && request->input.data == nullptr)
        || request->input.length > IRFQ_INFINITE_MAX_BATCH_BYTES_V1) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    auto engine = connection->engine.lock();
    if (!engine) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (fenceIfFaulted(engine, connection) || !connection->session) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (!connection->candidates.empty() || !connection->classifications.empty()) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    auto dispatched
        = connection->dispatcher.process(reinterpret_cast<const char *>(request->input.data), request->input.length);
    const auto responseLength = sizeof(irfq_infinite_dispatch_response_v1)
                                + dispatched.frames.size() * sizeof(irfq_infinite_registration_result_v1);
    if (outputCapacity < responseLength) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    AlignedBytes responseBytes(responseLength);
    auto &response = *static_cast<irfq_infinite_dispatch_response_v1 *>(responseBytes.data());
    response.header.structure_size = sizeof(response);
    response.header.abi_version = IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1;
    response.fault = dispatchFault(dispatched.terminalFault);

    if (dispatched.frames.empty()) {
      if (fenceIfFaulted(engine, connection)) {
        return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
      }
      response.header.status
          = dispatched.terminalFault ? IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1 : IRFQ_INFINITE_STATUS_OK_V1;
      if (dispatched.terminalFault) {
        fenceConnection(engine, connection, response.fault);
      }
      return publishBytes(output, outputCapacity, responseBytes.data(), responseLength);
    }

    std::vector<irfq_infinite_frame_descriptor_v1> descriptors;
    descriptors.reserve(dispatched.frames.size());
    for (const auto &frame : dispatched.frames) {
      descriptors.push_back(
          {reinterpret_cast<const std::uint8_t *>(frame.bytes.data()), frame.bytes.size(), frame.observedTaiNs});
    }
    if (fenceIfFaulted(engine, connection)) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    const auto callbackLength = sizeof(irfq_infinite_dispatch_response_v1)
                                + descriptors.size() * sizeof(irfq_infinite_registration_result_v1);
    AlignedBytes callbackBytes(callbackLength);
    auto &callbackResponse = initializeCallbackOutput<irfq_infinite_dispatch_response_v1>(callbackBytes);
    const irfq_infinite_registration_callback_request_v1 callbackRequest{
        sizeof(callbackRequest),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        connection->externalHandle,
        descriptors.data(),
        static_cast<std::uint32_t>(descriptors.size()),
        0};
    irfq_infinite_status_v1 returned = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    try {
      CallbackScope scope(engine.get(), connection.get());
      returned = engine->callbacks.register_batch(
          engine->callbacks.context,
          &callbackRequest,
          callbackBytes.data(),
          callbackBytes.size());
    } catch (...) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (fenceIfFaulted(engine, connection)) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (!validCallbackResponse(callbackResponse, returned, callbackLength)
        || !validCallbackHeader(callbackResponse.header)
        || (returned != IRFQ_INFINITE_STATUS_OK_V1 && returned != IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1
            && returned != IRFQ_INFINITE_STATUS_STREAM_FENCED_V1)) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (returned != IRFQ_INFINITE_STATUS_OK_V1) {
      if (callbackResponse.result_count != 0) {
        fenceConnection(engine, connection, 0);
        return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
      }
      response.header.status = returned;
      response.result_count = 0;
      if (!fenceConnection(engine, connection, 0)) {
        return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
      }
      return publishBytes(output, outputCapacity, responseBytes.data(), sizeof(response));
    }
    if (callbackResponse.result_count != descriptors.size()) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    const auto *results = reinterpret_cast<const irfq_infinite_registration_result_v1 *>(
        static_cast<const std::uint8_t *>(callbackBytes.data()) + sizeof(callbackResponse));
    std::set<irfq_infinite_handle_v1, HandleLess> batchTokens;
    auto previousOrdinal = connection->lastRegisteredOrdinal;
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
      const auto invalidOrdinal = index == 0 ? results[index].ordinal <= previousOrdinal
                                             : previousOrdinal == std::numeric_limits<std::uint64_t>::max()
                                                   || results[index].ordinal != previousOrdinal + 1;
      const auto liveExternalToken = std::any_of(
          connection->candidates.begin(),
          connection->candidates.end(),
          [&results, index](const auto &entry) {
            return sameHandle(entry.second.externalToken, results[index].token);
          });
      if (invalidOrdinal || !validHandle(results[index].token)
          || results[index].observed_tai_ns != descriptors[index].observed_tai_ns
          || !batchTokens.insert(results[index].token).second || liveExternalToken) {
        fenceConnection(engine, connection, 0);
        return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
      }
      previousOrdinal = results[index].ordinal;
    }
    if (fenceIfFaulted(engine, connection)) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    auto *publishedResults = reinterpret_cast<irfq_infinite_registration_result_v1 *>(
        static_cast<std::uint8_t *>(responseBytes.data()) + sizeof(response));
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
      publishedResults[index] = results[index];
      publishedResults[index].token = nextHandle();
      connection->candidates.emplace(
          publishedResults[index].token,
          Candidate{publishedResults[index], results[index].token, std::move(dispatched.frames[index].bytes), false});
    }
    connection->lastRegisteredOrdinal = previousOrdinal;
    response.header.status = IRFQ_INFINITE_STATUS_OK_V1;
    response.result_count = static_cast<std::uint32_t>(descriptors.size());
    if (dispatched.terminalFault) {
      response.header.status = IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
      fenceConnection(engine, connection, response.fault);
    } else if (fenceIfFaulted(engine, connection)) {
      response.header.status = IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    return publishBytes(output, outputCapacity, responseBytes.data(), responseLength);
  } catch (...) {
    if (connection) {
      if (auto engine = connection->engine.lock()) {
        fenceConnection(engine, connection, 0);
      }
    }
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}

extern "C" irfq_infinite_status_v1 irfq_infinite_connection_wait_head_v1(
    irfq_infinite_handle_v1 connectionHandle,
    const irfq_infinite_head_request_v1 *request,
    void *output,
    std::uint64_t outputCapacity) noexcept {
  const auto prepared = prepareRequestOutput<irfq_infinite_head_request_v1, irfq_infinite_operation_response_v1>(
      request,
      nullptr,
      0,
      output,
      outputCapacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  std::shared_ptr<Connection> connection;
  try {
    connection = findConnection(connectionHandle);
    ConnectionCall call(connection);
    if (!call.acquired()) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (!validRequestHeader(request)) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!allZero(request->reserved) || !validHandle(request->token)) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    auto engine = connection->engine.lock();
    const auto candidate = connection->candidates.find(request->token);
    if (!engine || candidate == connection->candidates.end()) {
      if (engine && candidate == connection->candidates.end()) {
        fenceConnection(engine, connection, 0);
      }
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    if (fenceIfFaulted(engine, connection)) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    if (candidate->second.atHead) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_AT_HEAD_V1, IRFQ_INFINITE_CONNECTION_OPEN_V1));
    }
    AlignedBytes callbackBytes(sizeof(irfq_infinite_operation_response_v1));
    auto &callbackResponse = initializeCallbackOutput<irfq_infinite_operation_response_v1>(callbackBytes);
    const irfq_infinite_head_callback_request_v1 callbackRequest{
        sizeof(callbackRequest),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        connection->externalHandle,
        candidate->second.externalToken,
        {}};
    if (fenceIfFaulted(engine, connection)) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    irfq_infinite_status_v1 returned = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    try {
      CallbackScope scope(engine.get(), connection.get());
      returned = engine->callbacks.wait_head(
          engine->callbacks.context,
          &callbackRequest,
          callbackBytes.data(),
          callbackBytes.size());
    } catch (...) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (fenceIfFaulted(engine, connection)) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    if (!validCallbackResponse(callbackResponse, returned) || callbackResponse.reserved != 0
        || (returned == IRFQ_INFINITE_STATUS_AT_HEAD_V1
            && callbackResponse.lifecycle != IRFQ_INFINITE_CONNECTION_OPEN_V1)
        || (returned == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
            && callbackResponse.lifecycle != IRFQ_INFINITE_CONNECTION_CLOSING_V1)
        || (returned != IRFQ_INFINITE_STATUS_AT_HEAD_V1 && returned != IRFQ_INFINITE_STATUS_STREAM_FENCED_V1)) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (returned == IRFQ_INFINITE_STATUS_AT_HEAD_V1) {
      candidate->second.atHead = true;
    } else {
      fenceConnection(engine, connection, 0);
    }
    return publishFixed(output, outputCapacity, callbackResponse);
  } catch (...) {
    if (connection) {
      if (auto engine = connection->engine.lock()) {
        fenceConnection(engine, connection, 0);
      }
    }
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}

extern "C" irfq_infinite_status_v1 irfq_infinite_connection_classify_v1(
    irfq_infinite_handle_v1 connectionHandle,
    const irfq_infinite_head_request_v1 *request,
    void *output,
    std::uint64_t outputCapacity) noexcept {
  const auto prepared = prepareRequestOutput<irfq_infinite_head_request_v1, irfq_infinite_classification_response_v1>(
      request,
      nullptr,
      0,
      output,
      outputCapacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  std::shared_ptr<Connection> connection;
  try {
    connection = findConnection(connectionHandle);
    ConnectionCall call(connection);
    if (!call.acquired()) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (!validRequestHeader(request)) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!allZero(request->reserved) || !validHandle(request->token)) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    auto engine = connection->engine.lock();
    const auto candidate = connection->candidates.find(request->token);
    if (!engine || !connection->session || candidate == connection->candidates.end() || !candidate->second.atHead
        || !connection->classifications.empty()) {
      if (engine) {
        fenceConnection(engine, connection, 0);
      }
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (fenceIfFaulted(engine, connection)) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    const auto binding = InfiniteFrameAdapterAccess::atHead(request->token);
    auto classification = std::make_unique<InfiniteSessionClassification>(InfiniteFrameAdapterAccess::classify(
        *connection->session,
        binding,
        std::move(candidate->second.bytes),
        UtcTimeStamp::now()));
    const auto &expected = classification->expected();
    const auto &plan = infiniteActionPlan(classification->actionData());
    if (plan.failure.size() > IRFQ_INFINITE_MAX_FAILURE_BYTES_V1
        || plan.operationCount > std::numeric_limits<std::uint32_t>::max()) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    const auto classificationHandle = nextHandle();
    const irfq_infinite_classification_callback_request_v1 callbackRequest{
        sizeof(callbackRequest),
        IRFQ_INFINITE_FRAME_ADAPTER_ABI_VERSION_V1,
        connection->externalHandle,
        candidate->second.externalToken,
        classificationHandle,
        expected.revision,
        expected.senderSequence,
        expected.targetSequence,
        actionKind(classification->kind()),
        sequenceDisposition(plan.sequenceDisposition),
        static_cast<std::uint32_t>(plan.operationCount),
        static_cast<std::uint32_t>(plan.failure.size()),
        reinterpret_cast<const std::uint8_t *>(plan.failure.data()),
        candidate->second.registration.observed_tai_ns};
    if (fenceIfFaulted(engine, connection)) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    AlignedBytes callbackBytes(sizeof(irfq_infinite_classification_callback_response_v1));
    auto &callbackResponse = initializeCallbackOutput<irfq_infinite_classification_callback_response_v1>(callbackBytes);
    irfq_infinite_status_v1 returned = IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    try {
      CallbackScope scope(engine.get(), connection.get());
      returned = engine->callbacks.authorize(
          engine->callbacks.context,
          &callbackRequest,
          callbackBytes.data(),
          callbackBytes.size());
    } catch (...) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (fenceIfFaulted(engine, connection)) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (!validCallbackResponse(callbackResponse, returned) || callbackResponse.reserved != 0
        || callbackResponse.outcome != returned
        || (returned != IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1
            && returned != IRFQ_INFINITE_STATUS_AUTHORIZED_NO_CONSUME_V1
            && returned != IRFQ_INFINITE_STATUS_STREAM_FENCED_V1)) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (returned == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
        || classification->kind() == InfiniteSessionActionKind::Failure) {
      fenceConnection(engine, connection, 0);
      return publishFixed(
          output,
          outputCapacity,
          irfq_infinite_classification_response_v1{
              {0, 0, IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, 0, 0},
              classificationHandle,
              {},
              expected.revision,
              expected.senderSequence,
              expected.targetSequence,
              actionKind(classification->kind()),
              sequenceDisposition(plan.sequenceDisposition),
              IRFQ_INFINITE_STATUS_STREAM_FENCED_V1,
              0});
    }
    const auto expectedAuthorization = classificationConsumes(*classification)
                                           ? IRFQ_INFINITE_STATUS_AUTHORIZED_CONSUME_V1
                                           : IRFQ_INFINITE_STATUS_AUTHORIZED_NO_CONSUME_V1;
    if (returned != expectedAuthorization) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (!validHandle(callbackResponse.authorization)) {
      fenceConnection(engine, connection, 0);
      return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    for (const auto &entry : connection->classifications) {
      if (sameHandle(entry.second.externalAuthorizationHandle, callbackResponse.authorization)) {
        fenceConnection(engine, connection, 0);
        return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
      }
    }
    auto authorization
        = std::make_unique<InfiniteEffectAuthorization>(InfiniteFrameAdapterAccess::authorize(*classification));
    const auto authorizationHandle = nextHandle();
    PendingClassification pending{
        classificationHandle,
        authorizationHandle,
        callbackResponse.authorization,
        request->token,
        std::move(classification),
        std::move(authorization)};
    const auto &stored = *pending.classification;
    const auto &storedExpected = stored.expected();
    const auto &storedPlan = infiniteActionPlan(stored.actionData());
    connection->classifications.emplace(request->token, std::move(pending));

    irfq_infinite_classification_response_v1 response{};
    response.header.status = IRFQ_INFINITE_STATUS_CLASSIFIED_V1;
    response.classification = classificationHandle;
    response.authorization = authorizationHandle;
    response.session_revision = storedExpected.revision;
    response.sender_sequence = storedExpected.senderSequence;
    response.target_sequence = storedExpected.targetSequence;
    response.action = actionKind(stored.kind());
    response.sequence_disposition = sequenceDisposition(storedPlan.sequenceDisposition);
    response.outcome = returned;
    return publishFixed(output, outputCapacity, response);
  } catch (...) {
    if (connection) {
      if (auto engine = connection->engine.lock()) {
        fenceConnection(engine, connection, 0);
      }
    }
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}

extern "C" irfq_infinite_status_v1 irfq_infinite_connection_apply_v1(
    irfq_infinite_handle_v1 connectionHandle,
    const irfq_infinite_apply_request_v1 *request,
    void *output,
    std::uint64_t outputCapacity) noexcept {
  const auto prepared = prepareRequestOutput<irfq_infinite_apply_request_v1, irfq_infinite_operation_response_v1>(
      request,
      nullptr,
      0,
      output,
      outputCapacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  std::shared_ptr<Connection> connection;
  try {
    connection = findConnection(connectionHandle);
    ConnectionCall call(connection);
    if (!call.acquired()) {
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    if (!validRequestHeader(request)) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!allZero(request->reserved) || !validHandle(request->classification) || !validHandle(request->authorization)) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    auto engine = connection->engine.lock();
    auto selected = connection->classifications.end();
    for (auto entry = connection->classifications.begin(); entry != connection->classifications.end(); ++entry) {
      if (sameHandle(entry->second.handle, request->classification)) {
        selected = entry;
        break;
      }
    }
    if (!engine || !connection->session || selected == connection->classifications.end()
        || !sameHandle(selected->second.authorizationHandle, request->authorization)) {
      if (engine) {
        fenceConnection(engine, connection, 0);
      }
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    if (fenceIfFaulted(engine, connection)) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    auto classification = std::move(selected->second.classification);
    auto authorization = std::move(selected->second.authorization);
    const auto token = selected->second.token;
    connection->classifications.erase(selected);
    connection->candidates.erase(token);
    try {
      InfiniteFrameAdapterAccess::apply(*connection->session, *classification, std::move(*authorization));
      if (InfiniteFrameAdapterAccess::fenced(*connection->session)) {
        fenceConnection(engine, connection, 0);
        return publishFixed(
            output,
            outputCapacity,
            operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
      }
    } catch (...) {
      fenceConnection(engine, connection, 0);
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    if (fenceIfFaulted(engine, connection)) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    return publishFixed(
        output,
        outputCapacity,
        operationResponse(IRFQ_INFINITE_STATUS_APPLIED_V1, IRFQ_INFINITE_CONNECTION_OPEN_V1));
  } catch (...) {
    if (connection) {
      if (auto engine = connection->engine.lock()) {
        fenceConnection(engine, connection, 0);
      }
    }
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}

extern "C" irfq_infinite_status_v1 irfq_infinite_connection_close_v1(
    irfq_infinite_handle_v1 connectionHandle,
    const irfq_infinite_close_request_v1 *request,
    void *output,
    std::uint64_t outputCapacity) noexcept {
  const auto prepared = prepareRequestOutput<irfq_infinite_close_request_v1, irfq_infinite_operation_response_v1>(
      request,
      nullptr,
      0,
      output,
      outputCapacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  try {
    if (!validRequestHeader(request)) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!allZero(request->reserved)) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    auto connection = findConnection(connectionHandle);
    if (!connection) {
      if (isConnectionTombstone(connectionHandle)) {
        return publishFixed(
            output,
            outputCapacity,
            operationResponse(IRFQ_INFINITE_STATUS_CLOSED_V1, IRFQ_INFINITE_CONNECTION_CLOSED_V1));
      }
      return publishFixed(output, outputCapacity, operationResponse(IRFQ_INFINITE_STATUS_NOT_REGISTERED_V1, 0));
    }
    auto engine = connection->engine.lock();
    if (!engine) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    if (activeCallbackContains(connection.get())) {
      connection->faulted.store(true, std::memory_order_release);
      return fenceConnection(engine, connection, request->reason) ? IRFQ_INFINITE_STATUS_STREAM_FENCED_V1
                                                                  : IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
    }
    if (insideCallback()) {
      if (!tryCloseConnectionFromCallback(engine, connection, request->reason)) {
        std::lock_guard<std::mutex> lock(connection->lifecycleMutex);
        return connection->terminalFailure ? IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1
                                           : IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
      }
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_CLOSED_V1, IRFQ_INFINITE_CONNECTION_CLOSED_V1));
    }
    if (!closeConnection(engine, connection, request->reason)) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1, IRFQ_INFINITE_CONNECTION_CLOSING_V1));
    }
    return publishFixed(
        output,
        outputCapacity,
        operationResponse(IRFQ_INFINITE_STATUS_CLOSED_V1, IRFQ_INFINITE_CONNECTION_CLOSED_V1));
  } catch (...) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}
