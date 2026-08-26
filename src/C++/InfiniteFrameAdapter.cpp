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
#include "InfiniteSessionClassification.h"
#include "MessageStore.h"
#include "Responder.h"
#include "Session.h"
#include "TimeRange.h"
#include "Values.h"

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
};
} // namespace FIX

namespace {
using namespace FIX;

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

template <typename T> irfq_infinite_status_v1 prepareOutput(void *output, std::uint64_t capacity) {
  return prepareOutput(output, capacity, sizeof(T), sizeof(T));
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

class BoundedMemoryStore : public MessageStore {
public:
  explicit BoundedMemoryStore(const UtcTimeStamp &now)
      : m_store(now) {}

  bool set(SEQNUM sequence, const std::string &message) EXCEPT(IOException) override {
    const auto found = m_messageSizes.find(sequence);
    const auto previousSize = found == m_messageSizes.end() ? std::size_t{0} : found->second;
    const auto retainedBytes = m_storedBytes - previousSize;
    if ((found == m_messageSizes.end() && m_messageSizes.size() >= INFINITE_MAX_PLANNED_MESSAGES)
        || message.size() > INFINITE_MAX_PLANNED_BYTES - retainedBytes) {
      throw IOException("Infinite adapter message store bound exceeded");
    }
    if (!m_store.set(sequence, message)) {
      return false;
    }
    m_messageSizes[sequence] = message.size();
    m_storedBytes = retainedBytes + message.size();
    return true;
  }

  void get(SEQNUM begin, SEQNUM end, std::vector<std::string> &messages) const EXCEPT(IOException) override {
    m_store.get(begin, end, messages);
  }

  SEQNUM getNextSenderMsgSeqNum() const EXCEPT(IOException) override { return m_store.getNextSenderMsgSeqNum(); }
  SEQNUM getNextTargetMsgSeqNum() const EXCEPT(IOException) override { return m_store.getNextTargetMsgSeqNum(); }
  void setNextSenderMsgSeqNum(SEQNUM value) EXCEPT(IOException) override { m_store.setNextSenderMsgSeqNum(value); }
  void setNextTargetMsgSeqNum(SEQNUM value) EXCEPT(IOException) override { m_store.setNextTargetMsgSeqNum(value); }
  void incrNextSenderMsgSeqNum() EXCEPT(IOException) override { m_store.incrNextSenderMsgSeqNum(); }
  void incrNextTargetMsgSeqNum() EXCEPT(IOException) override { m_store.incrNextTargetMsgSeqNum(); }
  UtcTimeStamp getCreationTime() const EXCEPT(IOException) override { return m_store.getCreationTime(); }

  void reset(const UtcTimeStamp &now) EXCEPT(IOException) override {
    m_store.reset(now);
    m_messageSizes.clear();
    m_storedBytes = 0;
  }

  void refresh() EXCEPT(IOException) override { m_store.refresh(); }

private:
  MemoryStore m_store;
  std::map<SEQNUM, std::size_t> m_messageSizes;
  std::size_t m_storedBytes{0};
};

class BoundedMemoryStoreFactory : public MessageStoreFactory {
public:
  MessageStore *create(const UtcTimeStamp &now, const SessionID &) override {
    return new BoundedMemoryStore(now);
  }

  void destroy(MessageStore *store) override { delete store; }
};

struct Candidate {
  irfq_infinite_registration_result_v1 registration{};
  std::string bytes;
  bool atHead{false};
};

struct PendingClassification {
  irfq_infinite_handle_v1 handle{};
  irfq_infinite_handle_v1 authorizationHandle{};
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
  std::size_t inFlight{0};
  std::size_t pendingBootstraps{0};
  irfq_infinite_handle_v1 handle{};
  irfq_infinite_callback_table_v1 callbacks{};
  std::map<std::uint64_t, std::shared_ptr<Connection>> connections;
};

struct Connection : std::enable_shared_from_this<Connection> {
  Connection(std::shared_ptr<Engine> owner, irfq_infinite_handle_v1 rustHandle)
      : engine(std::move(owner)),
        externalHandle(rustHandle),
        dispatcher({IRFQ_INFINITE_MAX_BATCH_FRAMES_V1, IRFQ_INFINITE_MAX_BATCH_BYTES_V1}),
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
    session = std::make_unique<Session>(
        []() { return UtcTimeStamp::now(); },
        application,
        storeFactory,
        sessionId,
        dictionaries,
        sessionTime,
        0,
        nullptr);
    session->setCheckLatency(false);
    session->setResponder(&responder);
    session->next(logon, UtcTimeStamp::now());
  }

  std::weak_ptr<Engine> engine;
  irfq_infinite_handle_v1 handle{};
  irfq_infinite_handle_v1 externalHandle{};
  std::mutex lifecycleMutex;
  std::condition_variable lifecycleCondition;
  irfq_infinite_connection_lifecycle_v1 state{IRFQ_INFINITE_CONNECTION_OPEN_V1};
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
std::map<std::uint64_t, std::shared_ptr<Engine>> engines;
std::map<std::uint64_t, std::shared_ptr<Connection>> connections;
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
  const auto found = connections.find(handle.object);
  return found != connections.end() && found->second->handle.generation == handle.generation ? found->second : nullptr;
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

void fenceConnection(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason) noexcept {
  connection->faulted.store(true, std::memory_order_release);
  if (connection->fenceNotified.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  try {
    invokeConnectionCallback(engine, connection, engine->callbacks.fence, reason);
  } catch (...) {}
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

void releaseConnection(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason) noexcept {
  try {
    invokeConnectionCallback(engine, connection, engine->callbacks.release, reason);
  } catch (...) {}
}

void rejectAcceptedExternal(
    const std::shared_ptr<Engine> &engine,
    irfq_infinite_handle_v1 externalHandle,
    std::uint32_t reason) noexcept {
  try {
    invokeExternalConnectionCallback(engine, externalHandle, nullptr, engine->callbacks.fence, reason);
  } catch (...) {}
  try {
    invokeExternalConnectionCallback(engine, externalHandle, nullptr, engine->callbacks.release, reason);
  } catch (...) {}
}

void closeConnection(
    const std::shared_ptr<Engine> &engine,
    const std::shared_ptr<Connection> &connection,
    std::uint32_t reason) {
  {
    std::unique_lock<std::mutex> lock(connection->lifecycleMutex);
    if (connection->state == IRFQ_INFINITE_CONNECTION_CLOSED_V1) {
      return;
    }
    if (connection->state == IRFQ_INFINITE_CONNECTION_CLOSING_V1) {
      connection->lifecycleCondition.wait(lock, [&connection]() {
        return connection->state == IRFQ_INFINITE_CONNECTION_CLOSED_V1;
      });
      return;
    }
    connection->state = IRFQ_INFINITE_CONNECTION_CLOSING_V1;
    connection->faulted.store(true, std::memory_order_release);
  }

  fenceConnection(engine, connection, reason);

  {
    std::unique_lock<std::mutex> lock(connection->lifecycleMutex);
    connection->lifecycleCondition.wait(lock, [&connection]() { return connection->inFlight == 0; });
  }
  {
    std::lock_guard<std::mutex> lane(connection->lane);
    connection->classifications.clear();
    connection->candidates.clear();
    connection->session.reset();
  }
  {
    std::lock_guard<std::mutex> lock(registryMutex);
    connections.erase(connection->handle.object);
  }
  releaseConnection(engine, connection, reason);
  {
    std::lock_guard<std::mutex> lock(engine->mutex);
    engine->connections.erase(connection->handle.object);
  }
  {
    std::lock_guard<std::mutex> lock(connection->lifecycleMutex);
    connection->state = IRFQ_INFINITE_CONNECTION_CLOSED_V1;
  }
  connection->lifecycleCondition.notify_all();
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
  const auto prepared = prepareOutput<irfq_infinite_engine_response_v1>(output, outputCapacity);
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
      engine->state = IRFQ_INFINITE_ENGINE_CLOSING_V1;
      for (const auto &entry : engine->connections) {
        snapshot.push_back(entry.second);
      }
    }
    for (const auto &connection : snapshot) {
      closeConnection(engine, connection, 0);
    }
    {
      std::unique_lock<std::mutex> lock(engine->mutex);
      engine->condition.wait(lock, [&engine]() { return engine->inFlight == 0; });
      engine->state = IRFQ_INFINITE_ENGINE_SHUTDOWN_V1;
    }
    {
      std::lock_guard<std::mutex> lock(registryMutex);
      for (const auto &connection : snapshot) {
        connections.erase(connection->handle.object);
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
  const auto prepared = prepareOutput<irfq_infinite_bootstrap_response_v1>(output, outputCapacity);
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
    const auto dispatch = bootstrapDispatcher.process(
        reinterpret_cast<const char *>(request->frame.data),
        request->frame.length,
        [request]() { return request->observed_tai_ns; });
    if (dispatch.terminalFault || dispatch.frames.size() != 1
        || dispatch.frames.front().bytes.size() != request->frame.length) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    std::unique_ptr<Message> logon;
    try {
      logon = std::make_unique<Message>(dispatch.frames.front().bytes, true);
      MsgType messageType;
      logon->getHeader().getField(messageType);
      if (messageType != MsgType_Logon) {
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
        rejectAcceptedExternal(engine, callbackResponse.connection, 0);
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
    try {
      connection = std::make_shared<Connection>(engine, callbackResponse.connection);
      connection->handle = nextHandle();
      connection->initializeSession(*logon);
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
            engine->connections.emplace(connection->handle.object, connection);
            std::lock_guard<std::mutex> registryLock(registryMutex);
            connections.emplace(connection->handle.object, connection);
            reservation.completeWhileLocked();
          }
        }
      }
      if (registrationStatus != IRFQ_INFINITE_STATUS_OK_V1) {
        closeConnection(engine, connection, 0);
        return publishFixed(
            output,
            outputCapacity,
            bootstrapResponse(IRFQ_INFINITE_STATUS_STREAM_FENCED_V1, IRFQ_INFINITE_BOOTSTRAP_FENCED_V1));
      }
    } catch (...) {
      if (connection) {
        closeConnection(engine, connection, 0);
      } else {
        rejectAcceptedExternal(engine, callbackResponse.connection, 0);
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
  const auto prepared = prepareOutput(
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
    const auto dispatched
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
      if (returned == IRFQ_INFINITE_STATUS_STREAM_FENCED_V1) {
        fenceConnection(engine, connection, 0);
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
      const auto invalidOrdinal
          = index == 0 ? results[index].ordinal <= previousOrdinal
                       : previousOrdinal == std::numeric_limits<std::uint64_t>::max()
                             || results[index].ordinal != previousOrdinal + 1;
      if (invalidOrdinal || !validHandle(results[index].token)
          || results[index].observed_tai_ns != descriptors[index].observed_tai_ns
          || !batchTokens.insert(results[index].token).second
          || connection->candidates.count(results[index].token) != 0) {
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
      connection->candidates.emplace(
          results[index].token,
          Candidate{results[index], dispatched.frames[index].bytes, false});
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
  const auto prepared = prepareOutput<irfq_infinite_operation_response_v1>(output, outputCapacity);
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
        request->token,
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
  const auto prepared = prepareOutput<irfq_infinite_classification_response_v1>(output, outputCapacity);
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
        || connection->classifications.count(request->token) != 0) {
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
        request->token,
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
      if (sameHandle(entry.second.authorizationHandle, callbackResponse.authorization)) {
        fenceConnection(engine, connection, 0);
        return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
      }
    }
    auto authorization
        = std::make_unique<InfiniteEffectAuthorization>(InfiniteFrameAdapterAccess::authorize(*classification));
    PendingClassification pending{
        classificationHandle,
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
    response.authorization = callbackResponse.authorization;
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
  const auto prepared = prepareOutput<irfq_infinite_operation_response_v1>(output, outputCapacity);
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
  const auto prepared = prepareOutput<irfq_infinite_operation_response_v1>(output, outputCapacity);
  if (prepared != IRFQ_INFINITE_STATUS_OK_V1) {
    return prepared;
  }
  try {
    auto connection = findConnection(connectionHandle);
    if (!connection) {
      return publishFixed(
          output,
          outputCapacity,
          operationResponse(IRFQ_INFINITE_STATUS_CLOSED_V1, IRFQ_INFINITE_CONNECTION_CLOSED_V1));
    }
    if (!validRequestHeader(request)) {
      return IRFQ_INFINITE_STATUS_ABI_MISMATCH_V1;
    }
    if (!allZero(request->reserved)) {
      return IRFQ_INFINITE_STATUS_INVALID_ARGUMENT_V1;
    }
    if (activeCallbackContains(connection.get())) {
      connection->faulted.store(true, std::memory_order_release);
      return IRFQ_INFINITE_STATUS_STREAM_FENCED_V1;
    }
    auto engine = connection->engine.lock();
    if (!engine) {
      return IRFQ_INFINITE_STATUS_CLOSED_V1;
    }
    closeConnection(engine, connection, request->reason);
    return publishFixed(
        output,
        outputCapacity,
        operationResponse(IRFQ_INFINITE_STATUS_CLOSED_V1, IRFQ_INFINITE_CONNECTION_CLOSED_V1));
  } catch (...) {
    return IRFQ_INFINITE_STATUS_INTERNAL_ERROR_V1;
  }
}
