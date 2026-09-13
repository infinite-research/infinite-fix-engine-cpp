# BLK1 Review 1 Security Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the exploitable defects reported in `blk1-review-1.pdf`, retire the unsafe embedded HTTP administration surface, and record explicit ownership for controls that belong to the embedding deployment.

**Architecture:** Fix shared trust boundaries once: wire parsing, connection admission, TLS verification, persistence restore, and log emission. Remove the legacy HTTP administration capability instead of building a second authentication/authorization stack inside QuickFIX; use the existing public `Session`, `LogFactory`, and `MessageStore` extension points for host-owned operations.

**Tech Stack:** C++17, CMake/CTest, Catch2 amalgamated unit tests, Ruby acceptance harness, OpenSSL when `HAVE_SSL=ON`, SWIG Python/Ruby bindings, optional MySQL/PostgreSQL integration tests.

**Spec:** `blk1-review-1.pdf` (AWS Security Agent report dated 2026-09-08; untracked source artifact in the repository root)

## Global Constraints

- Treat `6bf4779322ea4ee40e988c151442ae8a9005a482` as the validated baseline; the report's extracted source paths name `47ba4ce`, and line numbers or CI claims may have drifted.
- Preserve C++17, existing FIX 4.0 through 5.0 SP2/FIXT 1.1 behavior, on-disk store compatibility, and public ABI unless a task explicitly records a migration.
- Keep `UseDataDictionary=N`, server-authentication-only TLS, external database authentication, 24-hour FIX sessions, custom `LogFactory`, and custom `MessageStore` as supported intentional modes.
- Do not add an embedded RBAC engine, token/session store, ACME client, SIEM exporter, backup scheduler, archive HMAC format, or secure-erase routine in this remediation.
- Public API/configuration changes require Doxygen and `doc/html/configuration.html` or `README.SSL` updates.
- Format changed C/C++ files with the repository `.clang-format`; use 2-space indentation and a 120-character line limit.
- Acceptance tests may run only after confirming ports 6666-6670 are available.
- Every finding is closed only by a reproducer/falsification on the current tree, sibling-path coverage, passing relevant tests, and a recorded code, deployment, accepted-risk, or not-applicable disposition.

---

## Staff Committee Decision

The committee comprised a staff C++/security reviewer, a staff platform/security architect, and a staff delivery/test lead. Each reviewed the report independently against the current tree; the primary agent reconciled their conclusions.

Consensus decisions:

1. Retire the embedded HTTP admin listener with source/ABI-compatible fail-closed stubs now; discuss physical API removal only in the next ABI-major release.
2. Treat findings 3 and 10 as one high-priority admission-order defect. Rejected candidates must never become `m_pSession`, and rejected Logon authentication must precede persistent reset/refresh effects.
3. Fix TLS using OpenSSL verification facilities. Do not require client certificates globally, but make configured verification, names, private trust roots, and CRLs effective and fail closed.
4. Preserve explicit dictionary-free and external-authentication modes. Unknown FIXT versions must fail only when application dictionaries are configured; database users must be explicit, while an empty password may remain valid for peer/certificate authentication.
5. Limit library persistence work to memory-safe restore and exact round trips. Encryption at rest, immutable/off-host backup, retention, media destruction, certificate issuance, alerting, and rate limiting remain named deployment responsibilities.
6. Rebaseline CI rather than copying the report's stale workflow claims: `.github/workflows/build_test_cmake.yml` is absent, while current `format.yml` already uses `actions/checkout@v4` but not immutable action SHAs.

### Finding disposition and owner

| # | Committee disposition | Owner |
|---:|---|---|
| 1 | Confirmed High; direct pre-authentication memory-safety fix | Task 2 |
| 2 | Confirmed High; initiator verification/name binding fails open | Task 7 |
| 3 | Confirmed High; combine with admission and rejected-Logon state ordering | Task 4 |
| 4 | Confirmed Medium with narrowed wording; some structural validation remains | Task 6 |
| 5 | Confirmed Medium, configuration-dependent | Task 5 |
| 6 | Confirmed Medium, requiring writable/corrupt store | Task 8 |
| 7 | Confirmed Medium in the sample venue; includes fractional/nonfinite conversion | Task 11 |
| 8 | Confirmed Medium; made unreachable by retirement and fixed in compatibility stub | Task 1 |
| 9 | Confirmed Medium in an unused EOL bootstrap | Task 10 |
| 10 | Confirmed; elevate with finding 3 because buffered rejected frames bypass admission | Task 4 |
| 11 | Partly confirmed; public-root widening is real, "any DV certificate" is too broad | Task 7 |
| 12 | Partly confirmed; unsafe MySQL/bootstrap defaults are real, passwordless PostgreSQL is not automatically open access | Task 10 |
| 13 | Confirmed Medium; record injection plus a C++ data race | Task 9 |
| 14 | Confirmed Low; include adjacent conversion-status/leak defects | Task 12 |
| 15 | Confirmed Low alone; ship inside the High-priority TLS package | Task 7 |
| 16 | Confirmed Critical when enabled/reachable; retire the surface | Task 1 |
| 17 | Confirmed parser bound; HTTP portion disappears; host owns aggregate connection/rate limits | Tasks 1, 3, 14 |
| 18 | Confirmed High in the sample venue, not the core FIX engine | Task 11 |
| 19 | Confirmed High if HTTP remains; retire rather than add web security | Task 1 |
| 20 | Partly confirmed; code owns safe restore, deployment owns encryption/integrity boundary | Tasks 8, 14 |
| 21 | Partly confirmed umbrella; fix unsafe samples without redefining optional modes | Tasks 1, 7, 13, 14 |
| 22 | Partly confirmed and materially stale; add current CI/SBOM/scanning evidence | Task 14 |
| 23 | Partly confirmed; raw credentials are logged, but `Application::fromAdmin` already supports `RejectLogon` | Tasks 7, 9, 14 |
| 24 | Partly confirmed; code owns redaction/framing, deployment owns retention/immutability | Tasks 9, 14 |
| 25 | Confirmed but subordinate; admin actions disappear with the retired listener | Task 1 |
| 26 | Confirmed but subordinate; no embedded replacement/RBAC | Task 1 |
| 27 | Partly confirmed test-fixture hygiene; no evidence of a production-secret leak | Task 13 |
| 28 | Partly confirmed; custom log destinations already exist, admin telemetry disappears | Tasks 1, 14 |
| 29 | Confirmed missing bind-address configuration for surviving FIX listeners | Tasks 1, 13 |
| 30 | Confirmed concrete ACL and UPDATE escaping inconsistencies | Tasks 4, 10 |
| 31 | Confirmed protocol parse/floor defects; cipher/build claims narrowed | Task 7 |
| 32 | Partly confirmed; containment APIs exist, HTTP evidence destruction disappears | Tasks 1, 14 |
| 33 | Operator-owned certificate lifecycle; document restart/reload expectations | Task 14 |
| 34 | Partly confirmed; safe restore is code-owned, backup lifecycle is host-owned | Tasks 8, 14 |
| 35 | HTTP half becomes not applicable; 24-hour machine FIX sessions remain valid | Tasks 1, 13, 14 |
| 36 | Confirmed repository documentation/evidence gap | Task 14 |

## Delivery sequence

- **P0 security release:** Tasks 1-4 and 7. These remove the critical surface and close unauthenticated memory, admission, and TLS defects.
- **P1 hardening release:** Tasks 5-6 and 8-12. These close configuration, storage, logging, database, sample, and binding defects.
- **P2 assurance release:** Tasks 13-14. These make network scope explicit and leave repeatable build, operational, and risk evidence.
- Tasks within a priority may run in parallel except that Task 13 consumes the TLS behavior from Task 7, and Task 14 is the final release gate.

## Execution setup

Create the shared Release, ASan/UBSan, and TSan build trees before Task 1. Reconfigure `build-security` with
`-DHAVE_SSL=ON` when starting Task 7; create separate optional database and binding build trees only for their tasks.

```sh
cmake -S . -B build-security -DCMAKE_BUILD_TYPE=Release
cmake --build build-security --parallel 2

cmake -S . -B build-security-asan -DCMAKE_BUILD_TYPE=Debug \
  -DQUICKFIX_EXAMPLES=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-security-asan --parallel 2

cmake -S . -B build-security-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DQUICKFIX_EXAMPLES=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-security-tsan --parallel 2
```

All direct `ut` commands below run from the repository root and use the configured runtime path
`build-security/lib/ut` (or the corresponding sanitizer tree). Stop on the first red test; each task's expected-failure
step is the red half of its regression check.

---

### Task 1: Retire Embedded HTTP Administration

**Priority:** P0

**Findings:** 8, 16, 19, 25, 26; HTTP portions of 17, 21, 28, 29, 32, 34, 35

**Files:**

- Modify: `src/C++/HttpServer.h`
- Modify: `src/C++/HttpServer.cpp`
- Modify: `src/C++/HttpConnection.h`
- Modify: `src/C++/HttpConnection.cpp`
- Modify: `src/C++/test/CMakeLists.txt`
- Create: `src/C++/test/HttpServerTestCase.cpp`
- Modify: `bin/cfg/tradeclient.cfg`
- Modify: `doc/html/configuration.html`
- Modify: `NEWS`

**Interfaces:**

- Consumes: existing `HTTP_ACCEPT_PORT`, `ConfigError`, `HttpServer::startGlobal`, and public `HttpServer`/`HttpConnection` symbols.
- Produces: fail-closed `HttpAcceptPort` behavior with no bound socket, no request dispatch, and retained link symbols for the current ABI.

- [ ] **Step 1: Add failing retirement tests**

Add `HttpServerTestCase.cpp` with a `SessionSettings` default containing `HttpAcceptPort=9911`; assert both `HttpServer::startGlobal(settings)` and `HttpServer(settings).start()` throw `ConfigError` containing `HttpAcceptPort is no longer supported`. Assert settings without the key return from `startGlobal` without binding or changing global reference state.

```cpp
TEST_CASE("HttpServerTests") {
  SessionSettings configured;
  Dictionary defaults;
  defaults.setString(HTTP_ACCEPT_PORT, "9911");
  configured.set(defaults);

  CHECK_THROWS_WITH(HttpServer::startGlobal(configured),
                    Catch::Matchers::ContainsSubstring("HttpAcceptPort is no longer supported"));
  CHECK_THROWS_WITH(HttpServer(configured).start(),
                    Catch::Matchers::ContainsSubstring("HttpAcceptPort is no longer supported"));
}
```

- [ ] **Step 2: Run the focused test and confirm it fails**

Run: `cmake --build build-security --target ut && build-security/lib/ut "HttpServerTests" --quickfix-spec-path spec`

Expected: FAIL because the listener still starts or attempts to bind.

- [ ] **Step 3: Replace the network implementation with compatibility stubs**

Make `HttpServer::startGlobal` throw only when `HttpAcceptPort` is present, make direct `start()` always throw, and make `stopGlobal()`/`stop()` idempotent no-ops. Make `HttpConnection::read()` return `false` without parsing or dispatching; remove private route/page-building bodies that mutate `Session` state. Keep existing public class layouts and symbols for this ABI line, mark them deprecated in Doxygen, and do not add authentication, cookies, CSRF, HTML encoding, or RBAC.

```cpp
void HttpServer::startGlobal(const SessionSettings &settings) {
  if (settings.get().has(HTTP_ACCEPT_PORT)) {
    throw ConfigError("HttpAcceptPort is no longer supported; use an authenticated external control plane");
  }
}

void HttpServer::start() {
  throw ConfigError("HttpAcceptPort is no longer supported; use an authenticated external control plane");
}

bool HttpConnection::read() { return false; }
```

- [ ] **Step 4: Remove shipped activation and document migration**

Delete `HttpAcceptPort=9911` from `tradeclient.cfg`. Mark the setting removed/always rejected in `configuration.html` and `NEWS`; point users to the existing in-process `Session` methods behind a host-owned authenticated management boundary. Record physical header/source removal as an ABI-major follow-up, not part of this patch.

- [ ] **Step 5: Verify and commit**

Run: `build-security/lib/ut "HttpServerTests,HttpMessageTests,HttpParserTests" --quickfix-spec-path spec`

Run: `git diff --check`

Stage: `git add src/C++/test/HttpServerTestCase.cpp`

Commit: `git commit -am "security: retire embedded HTTP administration"`

---

### Task 2: Bound Length-Prefixed DATA Fields

**Priority:** P0

**Finding:** 1

**Files:**

- Modify: `src/C++/Message.cpp`
- Modify: `src/C++/test/MessagesTestCase.cpp`

**Interfaces:**

- Consumes: `Message::extractField`, `IntConvertor`, dictionary DATA/LENGTH metadata.
- Produces: valid binary DATA parsing or `InvalidMessage` before iterator arithmetic.

- [ ] **Step 1: Add failing DATA-boundary cases**

Under `MessageTests`, add table-driven RawDataLength/RawData and SignatureLength/Signature cases for `-1`, nonnumeric text, `INT_MAX`, remaining bytes plus one, exactly remaining bytes without a trailing SOH, and a wrong byte at the declared boundary. Add valid zero-length, embedded SOH, embedded NUL, and repeating-group DATA cases.

```cpp
for (const std::string &length : {"-1", "x", "2147483647"}) {
  INFO(length);
  CHECK_THROWS_AS(Message(rawLogon(length), sessionDictionary, true), InvalidMessage);
}
```

- [ ] **Step 2: Reproduce under sanitizers**

Run: `build-security-asan/lib/ut "MessageTests" --quickfix-spec-path spec`

Expected: at least one oversized case fails by sanitizer/crash or fails to throw `InvalidMessage`.

- [ ] **Step 3: Add the single shared bounds check**

In `Message::extractField`, parse into a signed `int`, reject conversion failure/negative values, compare as `size_t` against the remaining buffer before `valueStart + length`, and require the computed byte to be SOH. Do not reject SOH/NUL inside valid DATA.

```cpp
int dataLength = 0;
if (!IntConvertor::convert(fieldLength.getString(), dataLength) || dataLength < 0
    || static_cast<std::size_t>(dataLength) >= static_cast<std::size_t>(std::distance(valueStart, strEnd))) {
  throw InvalidMessage("Invalid data length for field " + IntConvertor::convert(field));
}
soh = valueStart + dataLength;
if (*soh != '\001') {
  throw InvalidMessage("SOH not found at declared end of data field " + IntConvertor::convert(field));
}
```

- [ ] **Step 4: Verify all parsing callers**

Run: `build-security-asan/lib/ut "MessageTests,GroupTests,SessionTestCase" --quickfix-spec-path spec`

Expected: PASS with no ASan/UBSan diagnostic.

- [ ] **Step 5: Format and commit**

Run: `clang-format -i src/C++/Message.cpp src/C++/test/MessagesTestCase.cpp && git diff --check`

Commit: `git commit -am "security: bound FIX data field lengths"`

---

### Task 3: Bound the Legacy FIX Stream Parser

**Priority:** P0

**Finding:** FIX portion of 17

**Files:**

- Modify: `src/C++/Parser.h`
- Modify: `src/C++/Parser.cpp`
- Modify: `src/C++/SocketConnection.cpp`
- Modify: `src/C++/ThreadedSocketConnection.cpp`
- Modify: `src/C++/SSLSocketConnection.cpp`
- Modify: `src/C++/ThreadedSSLSocketConnection.cpp`
- Modify: `src/C++/test/ParserTestCase.cpp`
- Modify: `doc/html/configuration.html`

**Interfaces:**

- Consumes: all four socket connections' shared `Parser` and `MessageParseError`.
- Produces: a 16 MiB maximum incomplete frame/accumulator and terminal connection rejection on malformed framing.

- [ ] **Step 1: Add failing parser limit tests**

Add fragmented inputs with no BeginString, no tag 9 terminator, `9=2147483647`, a declared body larger than 16 MiB, a missing checksum, a frame exactly at the limit, and two coalesced valid small frames. Assert malformed/oversized input throws and valid frames drain independently.

```cpp
Parser parser;
std::string oversized = "8=FIX.4.2\0019=2147483647\001";
parser.addToStream(oversized);
CHECK_THROWS_AS(parser.readFixMessage(message), MessageParseError);
```

- [ ] **Step 2: Run the focused test and confirm unbounded retention**

Run: `build-security/lib/ut "ParserTests" --quickfix-spec-path spec`

Expected: FAIL because the oversized declaration remains buffered.

- [ ] **Step 3: Enforce one parser invariant**

Move `addToStream` out of the header, check subtraction before append, reject a BodyLength that cannot fit inside the same 16 MiB ceiling, and clear the buffer on terminal framing errors. Keep the limit in one constant.

```cpp
static constexpr std::size_t MAX_BUFFER_BYTES = 16U * 1024U * 1024U;

void Parser::addToStream(const char *data, std::size_t size) {
  // ponytail: 16 MiB bounds unauthenticated buffering; add a transport setting only if production frames need it.
  if (m_buffer.size() > MAX_BUFFER_BYTES || size > MAX_BUFFER_BYTES - m_buffer.size()) {
    m_buffer.clear();
    throw MessageParseError("FIX frame exceeds 16 MiB");
  }
  m_buffer.append(data, size);
}
```

- [ ] **Step 4: Make framing errors terminal in all four transports**

Stop swallowing `MessageParseError` in each connection's `readMessage`. Catch it at the connection read boundary, log the reason without raw credentials, and drop/disconnect that connection. Do not let an empty message enter session lookup and do not retry a poisoned parser.

- [ ] **Step 5: Verify and commit**

Run: `build-security/lib/ut "ParserTests,SocketConnectionTests,InfiniteCompleteFrameDispatcherTests" --quickfix-spec-path spec`

Run: `build-security-asan/lib/ut "ParserTests" --quickfix-spec-path spec`

Commit: `git commit -am "security: bound FIX stream accumulation"`

---

### Task 4: Make Connection Admission Precede Session Side Effects

**Priority:** P0

**Findings:** 3, 10, ACL portion of 30

**Files:**

- Modify: `src/C++/SocketConnection.cpp`
- Modify: `src/C++/ThreadedSocketConnection.cpp`
- Modify: `src/C++/SSLSocketConnection.cpp`
- Modify: `src/C++/ThreadedSSLSocketConnection.cpp`
- Modify: `src/C++/Session.h`
- Modify: `src/C++/Session.cpp`
- Modify: `src/C++/test/SocketConnectionTestCase.cpp`
- Modify: `src/C++/test/SessionTestCase.cpp`
- Create: `test/definitions/server/fix42/1f_PreLogonSequenceResetRejected.def`

**Interfaces:**

- Consumes: `Session::lookupSession`, `identifyType`, listener session sets, `AllowedRemoteAddresses`, `Session::verify`, and `Application::fromAdmin`/`RejectLogon`.
- Produces: one ordering invariant across all transports: initial Logon, listener membership, source ACL, and application authentication succeed before registration, responder attachment, refresh/reset, callbacks beyond authentication, or persistence changes.

- [ ] **Step 1: Add failing admission/state tests**

Cover initial SequenceReset and Reject; denied source; wrong listener; denied Logon followed by SequenceReset in the same socket write; and `RejectLogon` combined with `141=Y`. Snapshot callback counts, registration, responder, and FileStore sender/target sequence numbers before each case and assert they remain identical after rejection and reopen.

```cpp
CHECK_FALSE(Session::isSessionRegistered(sessionID));
CHECK(beforeTarget == store.getNextTargetMsgSeqNum());
CHECK(0 == application.fromAdminAccepted);
CHECK(0 == application.fromAppCount);
```

- [ ] **Step 2: Confirm the buffered-frame bypass**

Run the new unit sections and plaintext acceptance definition against the current threaded and nonthreaded acceptors.

Expected: FAIL because threaded `setSession(false)` can leave `m_pSession` assigned and `processStream` continues; nonthreaded plaintext mutates before ACL evaluation.

- [ ] **Step 3: Keep rejected candidates local until admission completes**

In each acceptor connection path, parse the candidate into a local `Session *`, require `identifyType(message) == MsgType_Logon`, verify it belongs to that listener, and enforce `AllowedRemoteAddresses` before assigning `m_pSession`, registering, or setting the responder. On failure, clear no global/session state and return from `processStream`; never `continue` over another already-buffered frame.

```cpp
Session *candidate = Session::lookupSession(message, true);
if (!candidate || identifyType(message) != MsgType_Logon || m_sessions.count(candidate->getSessionID()) == 0
    || (!candidate->getAllowedRemoteAddresses().empty()
        && !candidate->inAllowedRemoteAddresses(socket_peername(m_socket)))) {
  return false;
}
```

- [ ] **Step 4: Authenticate reset Logon before mutation**

Refactor `Session::verify` with an `invokeCallback` flag or an equivalently small split so `fromAdmin` runs exactly once before `RefreshOnLogon`, `ResetSeqNumFlag`, or `ResetOnLogon` changes state. After authentication, preserve the existing reset-mode sequence rule: do not blindly enable ordinary too-low checks for all SequenceReset messages. Reject every non-Logon while `receivedLogon()` is false.

- [ ] **Step 5: Verify valid protocol behavior**

Run: `build-security/lib/ut "SocketConnectionTests,SessionTestCase,AcceptorSessionTestCase,AcceptorT11TestCase" --quickfix-spec-path spec`

After confirming ports: `QUICKFIX_TEST_SRCDIR="$PWD/test" QUICKFIX_TEST_BUILDDIR="$PWD/build-security/test" sh test/runat.sh 6666`

Expected: successful Logon/reset, reset-mode SequenceReset, gap fill, resend, logout, and reconnect tests remain green.

- [ ] **Step 6: Format and commit**

Stage: `git add test/definitions/server/fix42/1f_PreLogonSequenceResetRejected.def`

Commit: `git commit -am "security: admit FIX peers before session mutation"`

---

### Task 5: Parse Complete Configuration Lines or Fail

**Priority:** P1

**Finding:** 5

**Files:**

- Modify: `src/C++/Settings.cpp`
- Modify: `src/C++/SessionSettings.cpp`
- Modify: `src/C++/test/SettingsTestCase.cpp`
- Modify: `src/C++/test/SessionSettingsTestCase.cpp`

**Interfaces:**

- Consumes: stream extraction for `Settings` and `SessionSettings`.
- Produces: unbounded logical line reads and no partially published configuration after an I/O failure.

- [ ] **Step 1: Add failing stream cases**

Test 1022, 1023, 1024, and 4096-byte values; a long `AllowedRemoteAddresses` followed by another `[SESSION]`; CRLF; a final line without newline; and a custom streambuf that returns an I/O error mid-file.

- [ ] **Step 2: Confirm the current parser drops trailing sections**

Run: `build-security/lib/ut "SettingsTests,SessionSettingsTests" --quickfix-spec-path spec`

Expected: FAIL for the long-line/trailing-session assertion.

- [ ] **Step 3: Use the standard library and publish only a complete parse**

Replace the fixed buffer with `std::getline(stream, line)`. Parse into the local `Settings` already used by `SessionSettings`; before merging it into the target, throw `ConfigError("Unable to read complete settings stream")` when `stream.bad()` or `stream.fail() && !stream.eof()`.

```cpp
std::string line;
while (std::getline(stream, line)) {
  line = string_strip(line);
  // Existing section/comment/key handling remains unchanged.
}
```

- [ ] **Step 4: Verify and commit**

Run: `build-security/lib/ut "SettingsTests,SessionSettingsTests,SessionFactoryTests" --quickfix-spec-path spec`

Commit: `git commit -am "fix: read complete QuickFIX settings lines"`

---

### Task 6: Fail Closed on Unknown Configured FIXT Application Versions

**Priority:** P1

**Finding:** 4

**Files:**

- Modify: `src/C++/DataDictionaryProvider.cpp`
- Modify: `src/C++/Session.cpp`
- Modify: `src/C++/test/DataDictionaryProviderTestCase.cpp`
- Modify: `src/C++/test/SessionTestCase.cpp`

**Interfaces:**

- Consumes: `DataDictionaryProvider::getApplicationDataDictionary`, peer `DefaultApplVerID(1137)`/`ApplVerID(1128)`, and existing session reject reason 18.
- Produces: empty fallback only for an entirely dictionary-free provider; explicit rejection for unknown versions when validation is configured.

- [ ] **Step 1: Add failing configured/unconfigured cases**

Keep the existing empty-provider fallback test. Add a provider with FIX 5.0 configured and assert a lookup for FIX 4.2 throws `DataDictionaryNotFound`. Add FIXT session cases for unknown Logon 1137, valid-but-unconfigured 1128, configured alternate 1128, non-FIXT, resend, outbound creation, and the existing Infinite classification callers.

- [ ] **Step 2: Confirm the configured miss returns an empty dictionary**

Run: `build-security/lib/ut "DataDictionaryProviderTests,AcceptorT11TestCase,InitiatorT11TestCase,[session][dictionary],[infinite]" --quickfix-spec-path spec`

Expected: FAIL because the configured miss does not throw.

- [ ] **Step 3: Narrow the provider fallback**

Return `emptyDataDictionary` only when no application dictionaries exist; otherwise throw the exception already declared by the method.

```cpp
if (find != m_applicationDictionaries.end()) {
  return *find->second;
}
if (!m_applicationDictionaries.empty()) {
  throw DataDictionaryNotFound(applVerID.getValue());
}
return emptyDataDictionary;
```

- [ ] **Step 4: Reject peer selections at the session boundary**

Validate Logon 1137 before storing `m_targetDefaultApplVerID`. Convert an unknown Logon version to Logout/disconnect; convert an unknown application-message version to session Reject reason 18. Do not let `DataDictionaryNotFound` escape a socket thread.

- [ ] **Step 5: Verify and commit**

Run: `build-security/lib/ut "DataDictionaryProviderTests,SessionTestCase,AcceptorT11TestCase,InitiatorT11TestCase,[infinite]" --quickfix-spec-path spec`

Commit: `git commit -am "security: reject unknown configured FIXT versions"`

---

### Task 7: Make TLS Trust, Identity, Protocol, and Revocation Fail Closed

**Priority:** P0

**Findings:** 2, 11, 15, 31; TLS portions of 21, 23, 27, 33

**Files:**

- Modify: `src/C++/UtilitySSL.h`
- Modify: `src/C++/UtilitySSL.cpp`
- Modify: `src/C++/SSLSocketInitiator.cpp`
- Modify: `src/C++/ThreadedSSLSocketInitiator.cpp`
- Modify: `src/C++/SSLSocketAcceptor.cpp`
- Modify: `src/C++/ThreadedSSLSocketAcceptor.cpp`
- Modify: `src/C++/SSLSocketConnection.cpp`
- Modify: `src/C++/ThreadedSSLSocketConnection.cpp`
- Modify: `src/C++/SessionSettings.h`
- Modify: `src/C++/SessionFactory.cpp`
- Modify: `src/C++/Session.h`
- Modify: `src/C++/test/UtilitySSLTestCase.cpp`
- Create: `src/C++/test/TLSVerificationTestCase.cpp`
- Create: `test/generate-test-pki.sh`
- Modify: `src/C++/test/CMakeLists.txt`
- Modify: `README.SSL`
- Modify: `doc/html/configuration.html`
- Modify: `bin/cfg/executor.cfg`
- Modify: `bin/cfg/tradeclient.cfg`

**Interfaces:**

- Consumes: OpenSSL context/connection verification, configured CA/CRL settings, socket host/IP, and candidate FIX `SessionID`.
- Produces: verified initiator DNS/IP identity, isolated private client-auth trust, explicit optional/required mTLS, TLS 1.2+ policy, effective CRLs, and optional per-session SAN binding through `CertificateAcceptedPeerName`.

- [ ] **Step 1: Generate a local test PKI and add failing handshakes**

Generate short-lived test-only roots, server/client certificates with correct and wrong DNS/IP SANs, a wrong client EKU, an expired cert, and a CRL revoking one leaf into `build-security/test-runtime/certs`. Tests must cover both threaded/nonthreaded initiator and acceptor directions; no committed private key is reused.

- [ ] **Step 2: Reproduce the fail-open matrix**

Assert current failure for missing trust, wrong DNS SAN, wrong IP SAN, an unrelated OS/private root, malformed `CertificateVerifyLevel`, malformed `SSLProtocol`, and configured initiator CRL. Record whether the linked OpenSSL rejects wrong-purpose client certificates before assigning finding 11's final severity.

- [ ] **Step 3: Separate client and server trust semantics**

For initiators, always set `SSL_VERIFY_PEER`; load configured roots exclusively, or system defaults only when no CA was configured. For acceptors, keep mTLS optional unless `CertificateVerifyLevel` requests it, validate the value as 0/1/2, and never add system roots when an explicit client-auth CA is supplied. A verify level without usable trust must fail startup.

- [ ] **Step 4: Bind initiators to the configured DNS/IP**

Before `SSL_connect`, set OpenSSL verification parameters using `X509_VERIFY_PARAM_set1_host` for DNS or `X509_VERIFY_PARAM_set1_ip_asc` for numeric IP; set SNI only for DNS. Check the handshake result and `SSL_get_verify_result` before publishing the connection.

- [ ] **Step 5: Bind mTLS identity to the selected FIX session**

Add `CertificateAcceptedPeerName` as an optional per-session exact SAN value. Store it on `Session`; after parsing the initial Logon and before Task 4's registration/responder assignment, compare the leaf SAN with OpenSSL's host/IP verifier. A configured name with no certificate or a mismatched certificate is denied. Do not add fingerprint registries or custom certificate parsing.

- [ ] **Step 6: Apply CRLs and the protocol floor consistently**

Call `loadCRLInfo` from both initiators after trust setup. Treat configured CRL load/flag failures as startup errors; preserve the modern OpenSSL context-store ownership and the legacy store lifetime where still supported. Reject an invalid `protocolOptions` result before applying it and enforce a TLS 1.2 minimum using OpenSSL's minimum-protocol API; do not paste a custom cipher suite over OpenSSL policy.

- [ ] **Step 7: Correct settings and migration documentation**

Document the actual `CertificateVerifyLevel` name, system-vs-private trust rules, hostname/IP matching, `CertificateAcceptedPeerName`, CRLs, TLS 1.2+, server-auth-only mode, and application credential validation via `fromAdmin`. Replace weak sample `SSLProtocol` values and configure the generated test CA where the samples demonstrate TLS.

- [ ] **Step 8: Verify and commit**

Run: `cmake -S . -B build-security -DCMAKE_BUILD_TYPE=Release -DHAVE_SSL=ON`

Run: `cmake --build build-security --parallel 2 && ctest --test-dir build-security --output-on-failure`

Run: `build-security/lib/ut "UtilitySSLTests,TLSVerificationTests" --quickfix-spec-path spec`

Stage: `git add src/C++/test/TLSVerificationTestCase.cpp test/generate-test-pki.sh`

Commit: `git commit -am "security: verify TLS peers and revocation"`

---

### Task 8: Validate FileStore State Before Publishing or Reading It

**Priority:** P1

**Findings:** 6; code-owned portions of 20 and 34

**Files:**

- Modify: `src/C++/FileStore.cpp`
- Modify: `src/C++/test/FileStoreTestCase.cpp`
- Modify: `doc/html/configuration.html`

**Interfaces:**

- Consumes: existing `.header`, `.body`, `.seqnums`, and `.session` formats.
- Produces: all-or-nothing validated cache restoration and binary-exact message reads without changing those formats.

- [ ] **Step 1: Add corrupt-store fixtures**

Create fixture cases for a 22+ byte timestamp, invalid timestamp, negative offset, numeric overflow, `SIZE_MAX`, `size + 1` wrap, offset beyond EOF, offset/size range beyond EOF, incomplete header row, duplicate sequence row, truncated body, invalid sequence numbers, embedded NUL, valid old 32-bit sequence format, and valid current format.

- [ ] **Step 2: Reproduce memory errors and partial publication**

Run: `build-security-asan/lib/ut "resetFileStoreTests,noResetFileStoreTests,FileStoreTests*" --quickfix-spec-path spec`

Expected: at least one corrupt fixture crashes, allocates excessively, truncates at NUL, or leaves partially loaded state.

- [ ] **Step 3: Parse into temporary state and validate against the body file**

Stat the opened body once. Parse header records completely into a local offset map; reject negative offsets, sizes greater than body length, and ranges using `size > bodySize - offset` after proving `offset <= bodySize`. Parse the timestamp with a width of 21 and require `UtcTimeStampConvertor` to consume a valid value. Assign the local map/cache only after every artifact passes.

- [ ] **Step 4: Replace raw allocation with a sized string**

```cpp
std::string value(offset.second, '\0');
const std::size_t count = fread(value.data(), 1, value.size(), m_msgFile);
if (ferror(m_msgFile) || count != value.size()) {
  throw IOException("Unable to read complete message body");
}
msg = std::move(value);
```

Never silently reset or rewrite corrupt files during `open(false)`/`refresh()`.

- [ ] **Step 5: Verify compatibility and commit**

Run: `build-security-asan/lib/ut "resetFileStoreTests,noResetFileStoreTests,FileStoreTests*" --quickfix-spec-path spec`

Run the shared `MessageStoreTestCase` coverage through FileStore and confirm byte-exact embedded-NUL round trips.

Commit: `git commit -am "security: validate restored file store state"`

---

### Task 9: Redact Credentials and Serialize File Log Records

**Priority:** P1

**Findings:** 13; code-owned portions of 23 and 24

**Files:**

- Modify: `src/C++/Log.h`
- Modify: `src/C++/Log.cpp`
- Modify: `src/C++/SessionState.h`
- Modify: `src/C++/FileLog.h`
- Modify: `src/C++/FileLog.cpp`
- Modify: `src/C++/SocketConnection.cpp`
- Modify: `src/C++/ThreadedSocketConnection.cpp`
- Modify: `src/C++/SSLSocketConnection.cpp`
- Modify: `src/C++/ThreadedSSLSocketConnection.cpp`
- Modify: `src/C++/test/FileLogTestCase.cpp`
- Modify: `doc/html/configuration.html`
- Modify: `NEWS`

**Interfaces:**

- Consumes: raw incoming/outgoing/event strings before any `Log` sink.
- Produces: engine-generated logs with FIX tags 553/554 masked and FileLog records with reversible CR/LF/backslash escaping and per-instance serialization.

- [ ] **Step 1: Add failing redaction/framing/concurrency cases**

Test valid Logon credentials, malformed credential fields without final SOH, credentials inside an unknown-session event prefix, literal `\\n`, physical CR/LF, embedded SOH/NUL DATA, simultaneous writers, and `backup()`/`clear()` racing with writes. Assert secret bytes never occur and attacker CR/LF cannot increase physical record count.

- [ ] **Step 2: Confirm current leakage and record injection**

Run: `build-security-tsan/lib/ut "FileLogTests" --quickfix-spec-path spec`

Expected: FAIL secret/line-count assertions; TSan may report concurrent `ofstream` access.

- [ ] **Step 3: Add one bounded log sanitizer**

Add a small bytewise helper used by `SessionState::onIncoming/onOutgoing/onEvent` and the four pre-session connection log sites. Match only field boundaries for `553=` and `554=`; replace through the next SOH with `<redacted>`, or replace the rest when malformed. This operates on log copies only and must never alter `MessageStore` payloads.

- [ ] **Step 4: Make FileLog records unambiguous and serialized**

Add one `Mutex` member and take `Locker` in `onIncoming`, `onOutgoing`, `onEvent`, `clear`, and `backup`. Escape `\\` as `\\\\`, CR as `\\r`, and LF as `\\n` before appending exactly one newline. Keep SOH byte-exact and document the representation change.

- [ ] **Step 5: Verify all built-in sinks and commit**

Run: `build-security/lib/ut "FileLogTests,SessionStateTests,SocketConnectionTests" --quickfix-spec-path spec`

Run: `build-security-tsan/lib/ut "FileLogTests" --quickfix-spec-path spec`

Commit: `git commit -am "security: redact and frame QuickFIX logs"`

---

### Task 10: Remove Unsafe Database Bootstrap and Reuse Escaping on UPDATE

**Priority:** P1

**Findings:** 9, 12, SQL portion of 30

**Files:**

- Delete: `scripts/ubuntu_12/`
- Delete: `src/sql/mysql/create_user.sh`
- Delete: `src/sql/mysql/user.sql`
- Delete: `src/sql/postgresql/create_user.sh`
- Delete: `src/sql/postgresql/user.sql`
- Modify: `src/C++/MySQLStore.cpp`
- Modify: `src/C++/PostgreSQLStore.cpp`
- Modify: `src/C++/MySQLLog.cpp`
- Modify: `src/C++/PostgreSQLLog.cpp`
- Modify: `src/C++/test/MySQLStoreTestCase.cpp`
- Modify: `src/C++/test/PostgreSQLStoreTestCase.cpp`
- Modify: `doc/html/configuration.html`
- Modify: `NEWS`

**Interfaces:**

- Consumes: optional database factories and existing driver escaping functions.
- Produces: no privileged default account, no published password/global trust rewrite, explicit database user selection, and escaped message content in INSERT and UPDATE paths.

- [ ] **Step 1: Add failing factory and duplicate-key tests**

Assert MySQL/PostgreSQL store and log factories reject an omitted user but allow an explicitly empty password for external/peer auth. Force the duplicate-key UPDATE path with quotes, backslashes, CR/LF, SOH, and binary bytes; assert exact round trip and no unrelated row change.

- [ ] **Step 2: Confirm the UPDATE path interpolates raw messages**

Run the optional MySQL/PostgreSQL store tests against isolated test databases.

Expected: quoted payload fails, changes query semantics, or does not round-trip.

- [ ] **Step 3: Reuse the existing escaped buffer in both branches**

Keep `msgCopy` alive through INSERT and fallback UPDATE, and interpolate it in both. Apply the same change to MySQL and PostgreSQL; do not add a new SQL abstraction in this patch.

```cpp
queryString2 << "UPDATE messages SET message='" << msgCopy << "' WHERE ";
```

Use the backend's existing quote character/escape routine exactly as the INSERT path does.

- [ ] **Step 4: Require explicit nonprivileged users and delete the EOL bootstrap**

Remove default `root`/`postgres` usernames from all four factories; retain explicit empty passwords for approved external-auth modes. Delete the unreferenced Ubuntu 12 scripts and bundled user-creation scripts rather than repairing an obsolete privilege-changing installer. Document table-specific grants and secret/external-auth injection without embedding credentials.

- [ ] **Step 5: Verify and commit**

Run: `build-security/lib/ut "MySQLStoreTests*,PostgreSQLStoreTests*" --quickfix-spec-path spec` with each optional backend enabled.

Commit: `git commit -am "security: remove unsafe database defaults"`

---

### Task 11: Enforce Ordermatch Economics and Ownership at the Book Boundary

**Priority:** P1

**Findings:** 7, 18

**Files:**

- Modify: `examples/ordermatch/Order.h`
- Modify: `examples/ordermatch/Market.h`
- Modify: `examples/ordermatch/Market.cpp`
- Modify: `examples/ordermatch/OrderMatcher.h`
- Modify: `examples/ordermatch/Application.h`
- Modify: `examples/ordermatch/Application.cpp`
- Create: `examples/ordermatch/OrderMatcherTestCase.cpp`
- Modify: `examples/ordermatch/CMakeLists.txt`

**Interfaces:**

- Consumes: FIX42 NewOrderSingle/OrderCancelRequest and existing `Order::owner`.
- Produces: whole positive representable quantities, finite valid prices, and `(owner, ClOrdID, side)`-scoped insert/find/erase.

- [ ] **Step 1: Add failing venue tests**

Register the new Catch test executable as `ordermatch_tests` with CTest. Cover zero, negative, fractional/sub-unit, infinity, NaN, and above-`LONG_MAX` quantities; nonpositive/nonfinite limit prices; conserved valid partial fills; two owners with identical ClOrdIDs; duplicate same-owner IDs; unauthorized cancellation; legitimate cancellation; and owner-scoped erase.

- [ ] **Step 2: Confirm victim mutation and unscoped cancellation**

Run: `ctest --test-dir build-security --output-on-failure -R ordermatch`

Expected: FAIL for invalid quantity and cross-owner cases.

- [ ] **Step 3: Validate before conversion and insertion**

Change the example `Order` constructor to accept wire `double quantity`; reject nonfinite, nonpositive, fractional, and greater-than-`LONG_MAX` values before casting. Reject nonfinite/nonpositive limit prices. Guard `execute` against `quantity <= 0` or `quantity > m_openQuantity`.

```cpp
if (!std::isfinite(quantity) || quantity <= 0 || std::floor(quantity) != quantity
    || quantity > static_cast<double>(LONG_MAX)) {
  throw std::invalid_argument("OrderQty must be a positive whole quantity");
}
```

- [ ] **Step 4: Scope all book operations by owner**

Pass the requester's verified session counterparty ID from `Application::onMessage(OrderCancelRequest, sessionID)` to `processCancel`. Make `Market::insert`, `find`, and `erase` compare owner and ClOrdID together; reject same-owner duplicates. Send `OrderCancelReject` for missing/foreign orders and do not swallow the error.

- [ ] **Step 5: Verify and commit**

Run: `cmake --build build-security --target ordermatch ordermatch_tests`

Run: `ctest --test-dir build-security --output-on-failure -R ordermatch`

Stage: `git add examples/ordermatch/OrderMatcherTestCase.cpp`

Commit: `git commit -am "security: scope and validate sample orders"`

---

### Task 12: Repair SWIG Conversion Failure Handling

**Priority:** P1

**Finding:** 14

**Files:**

- Modify: `src/python/quickfix.i`
- Modify: `src/ruby/quickfix.i`
- Regenerate: `src/python/QuickfixPython.cpp`
- Regenerate: `src/ruby/QuickfixRuby.cpp`
- Modify: `src/ruby/extconf.rb`
- Modify: `src/python/test/DataDictionaryTestCase.py`
- Modify: `src/ruby/test/DataDictionaryTestCase.rb`

**Interfaces:**

- Consumes: existing binding result shapes and `DataDictionary::getGroup` boolean/out parameter.
- Produces: language exceptions or the existing false result, never an uninitialized/null dereference or leaked temporary.

- [ ] **Step 1: Add subprocess regression cases**

In both languages, call `getGroup` for an absent message/tag, pass the wrong output object type, pass non-string/non-int objects to reference typemaps, repeat failures 10,000 times, and verify a valid group still populates the caller's dictionary. Run each crash-prone case in a subprocess so a pre-fix crash is a test failure rather than a killed suite.

- [ ] **Step 2: Confirm current wrappers crash or mis-handle errors**

Run: `sh src/python3/test.sh` and `sh src/ruby/test.sh` in binding-enabled builds.

Expected: at least one subprocess exits abnormally or fails to raise the expected language exception.

- [ ] **Step 3: Replace the heap temporary and check every producer**

Use an initialized stack pointer for the `DataDictionary const *&` typemap, copy only when the wrapped boolean result is true, `SWIG_ConvertPtr` succeeds, and both pointers are non-null. Remove the obsolete heap/free typemap. Check `PyUnicode_AsUTF8` for null and `SWIG_AsVal_int` with `SWIG_IsOK`; Ruby conversions must likewise honor `SWIG_ConvertPtr`.

```swig
%typemap(in) FIX::DataDictionary const *& (FIX::DataDictionary *temp = nullptr) {
  $1 = &temp;
}
```

- [ ] **Step 4: Regenerate reproducibly and restore warnings**

Run `(cd src/python && ./swig.sh)` and `(cd src/ruby && ./swig.sh)`. The Python 3 tree already consumes the Python wrapper through its tracked symlink; do not copy or generate a second file. Remove `-Wno-uninitialized` from `src/ruby/extconf.rb`. Confirm a second generation produces no diff.

- [ ] **Step 5: Verify and commit**

Run: `cmake -S . -B build-bindings -DHAVE_PYTHON3=ON && cmake --build build-bindings --parallel 2`

Run: `(cd src/python3 && ./test.sh)`

Run: `(cd src/ruby && ruby extconf.rb && make && ./test.sh)`

Commit: `git commit -am "security: check SWIG conversion failures"`

---

### Task 13: Add FIX Bind Scoping and Remove Production-Looking Test Secrets

**Priority:** P2

**Findings:** code/sample portions of 21, 27, 29, 35

**Files:**

- Modify: `src/C++/SessionSettings.h`
- Modify: `src/C++/Utility.h`
- Modify: `src/C++/Utility.cpp`
- Modify: `src/C++/SocketServer.h`
- Modify: `src/C++/SocketServer.cpp`
- Modify: `src/C++/SocketAcceptor.cpp`
- Modify: `src/C++/ThreadedSocketAcceptor.cpp`
- Modify: `src/C++/SSLSocketAcceptor.cpp`
- Modify: `src/C++/ThreadedSSLSocketAcceptor.cpp`
- Modify: `src/C++/test/SocketServerTestCase.cpp`
- Modify: `src/C++/test/SocketAcceptorTestCase.cpp`
- Modify: `bin/cfg/executor.cfg`
- Modify: `bin/cfg/ordermatch.cfg`
- Modify: `bin/cfg/tradeclient.cfg`
- Modify: `bin/cfg/banzai.cfg`
- Delete: `bin/cfg/certs/`
- Modify: `README.SSL`
- Modify: `doc/html/configuration.html`

**Interfaces:**

- Consumes: existing `socket_createAcceptor(port, reuse)` and acceptor settings.
- Produces: backward-compatible `SocketAcceptAddress` scoping for all FIX acceptors; wildcard remains the compatibility default, while shipped local examples bind loopback explicitly.

- [ ] **Step 1: Add failing bind-address tests**

Test an existing two-argument acceptor still binds wildcard, the new overload binds `127.0.0.1`, invalid/nonlocal addresses fail with `ConfigError`/`RuntimeError`, and threaded/nonthreaded TLS/plain acceptors pass the configured address. Assert sessions sharing a port cannot specify conflicting addresses.

- [ ] **Step 2: Implement one native bind overload**

Add `SocketAcceptAddress`; make a new `socket_createAcceptor(address, port, reuse)` populate `sockaddr_in` from the validated address, and make the existing overload delegate with an empty/wildcard address. Thread the value through `SocketServer::add` and all four acceptors without changing initiator source-address behavior.

- [ ] **Step 3: Make sample intent explicit**

Set `SocketAcceptAddress=127.0.0.1` and `NonStopSession=Y` in local samples. Keep plaintext examples explicitly labeled local/demo; network deployment documentation requires TLS or host termination. Do not redefine `StartTime=EndTime` globally.

- [ ] **Step 4: Remove reusable-looking secrets**

Delete `bin/cfg/certs/` in full, including its Ubuntu 10.04-era scripts, published passphrase, private keys,
CA database, CSRs, and generated certificates. Use Task 7's single `test/generate-test-pki.sh` for disposable local files
under the build tree, and point sample paths there. Do not add a secrets-manager dependency.

- [ ] **Step 5: Verify and commit**

Run: `build-security/lib/ut "SocketServerTests,SocketAcceptorTests,TLSVerificationTests" --quickfix-spec-path spec`

Run: `git grep -l 'password\|PRIVATE KEY' -- bin/cfg/certs` and verify only explanatory/script text remains.

Commit: `git commit -am "security: scope listeners and remove demo keys"`

---

### Task 14: Add Repeatable Security Assurance and Deployment Ownership

**Priority:** P2 and final release gate

**Findings:** 22, 28, 33, 36; operator-owned portions of 17, 20, 21, 23, 24, 32, 34, 35

**Files:**

- Modify: `.github/workflows/format.yml`
- Create: `.github/workflows/build-security.yml`
- Create: `.github/dependabot.yml`
- Create: `SECURITY_ARCHITECTURE.md`
- Modify: `SECURITY.md`
- Modify: `CONTRIBUTING.md`
- Modify: `README.md`
- Modify: `README.SSL`
- Modify: `doc/html/building.html`
- Modify: `doc/html/configuration.html`
- Modify: `NEWS`

**Interfaces:**

- Consumes: existing CMake/CTest, acceptance, performance, package-governance, `LogFactory`, `MessageStore`, and `Application::fromAdmin` contracts.
- Produces: least-privilege immutable CI, generated release SBOM/dependency evidence, threat/data/control documentation, and explicit host control acceptance criteria.

- [ ] **Step 1: Pin and scope the existing format workflow**

Set top-level `permissions: contents: read`, pin every third-party action to a reviewed 40-character commit SHA with its release tag in a comment, and expand format coverage to changed C/C++ files under `src`, `examples`, and public headers while retaining explicit vendored/generated exclusions. Add Dependabot updates for the `github-actions` ecosystem.

- [ ] **Step 2: Add the actual build/security matrix**

Create `build-security.yml` with no write permissions and four independent jobs:

```yaml
permissions:
  contents: read
jobs:
  unit-default:
    # Release, default options, ctest.
  unit-tls:
    # Release, HAVE_SSL=ON, ctest including TLSVerificationTests.
  sanitizers:
    # Debug, ASan+UBSan, QUICKFIX_EXAMPLES=OFF, ctest.
  codeql-and-sbom:
    # C/C++ CodeQL plus an SPDX release artifact from the checked-out tree/build.
```

Pin checkout, CodeQL, upload, and SBOM actions by full SHA. Use separate TSan execution for `FileLogTests`; never combine ASan and TSan. Enable secret scanning with an allowlist limited to generated test-PKI paths after Task 13.

- [ ] **Step 3: Make acceptance/performance exit status real**

Register or invoke plaintext nonthreaded/threaded acceptance modes explicitly; add Task 7's TLS transport test rather than assuming `HAVE_SSL=ON` exercises sockets. Fix `test/runpt.sh` to retain both invocation statuses, run `pt` from `test-runtime` with `--quickfix-spec-path`, and compare repeated medians without a noisy single-run percentage gate.

- [ ] **Step 4: Generate dependency/SBOM evidence**

Generate SPDX for release artifacts, including linked OpenSSL/database libraries and vendored pugixml/double-conversion; retain versions, origins, licenses, and scan results as workflow artifacts. Keep the governed Infinite adapter's existing pinned package/provenance checks intact; do not claim PIE/RELRO for a static archive.

- [ ] **Step 5: Add one security architecture and responsibility document**

`SECURITY_ARCHITECTURE.md` must contain:

- trust boundaries and data flow for plaintext/TLS acceptors, initiator responses, the retired HTTP surface, bindings, stores/logs, and the Infinite ABI;
- classification/storage of order flow, tags 553/554, sequence state, message archives, logs, TLS keys, and test fixtures;
- a control matrix mapping all 36 findings to code, deployment, accepted-risk, or not-applicable evidence;
- application authentication through `fromAdmin`/`RejectLogon` and authorization ownership;
- host requirements for connection/rate limits, firewalling, encrypted private storage, service umask/private directories, immutable/off-host backups, restore drills, retention/destruction, log forwarding/alerting, certificate issuance/renewal/expiry, process isolation, and incident escalation;
- containment using existing `Session::logout`, `disconnect`, and source restrictions, noting that changing an allow-list does not revoke an established connection automatically;
- explicit acceptance that 24-hour FIX sessions are machine protocol sessions, not browser sessions.

Link the document from `SECURITY.md`, `README.md`, `README.SSL`, and the contributor security-review checklist. Do not claim blanket PCI/NIST/AWS compliance without deployment evidence.

- [ ] **Step 6: Run the final release matrix**

```sh
cmake -S . -B build-security-release -DCMAKE_BUILD_TYPE=Release -DHAVE_SSL=ON
cmake --build build-security-release --parallel 2
ctest --test-dir build-security-release --output-on-failure

cmake -S . -B build-security-asan -DCMAKE_BUILD_TYPE=Debug -DHAVE_SSL=ON \
  -DQUICKFIX_EXAMPLES=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-security-asan --parallel 2
ctest --test-dir build-security-asan --output-on-failure
```

After confirming ports 6666-6670 are free:

```sh
QUICKFIX_TEST_SRCDIR="$PWD/test" \
QUICKFIX_TEST_BUILDDIR="$PWD/build-security-release/test" \
sh test/runat.sh 6666
```

Run optional MySQL/PostgreSQL and Python/Ruby matrices where dependencies exist. Run governed adapter package/fixture-tamper/C17-consumer checks unchanged after any shared parser/session changes.

- [ ] **Step 7: Reconcile the finding ledger and prepare release/backports**

For each finding, attach current-tree evidence, the failing-before/passing-after test or documented falsification, affected release range, and final disposition. Backport Tasks 1-10 and 12 where source-compatible; release-note TLS/default/log-format behavior, the 16 MiB frame ceiling, explicit database users, retired HTTP, and removed demo keys. Canary a low-volume session and preserve sequence/store files for rollback.

- [ ] **Step 8: Commit**

Stage: `git add .github/dependabot.yml .github/workflows/build-security.yml SECURITY_ARCHITECTURE.md`

Commit: `git commit -am "ci: add security assurance and ownership evidence"`

---

## Plan Self-Review

- **Coverage:** Every report finding maps to at least one task or an explicit host/not-applicable disposition in the table.
- **Root causes:** HTTP is retired once; four transports share the same admission invariant; TLS findings share one verification package; persistence and logging remain separate because wire archives must not be redacted.
- **Compatibility:** Explicit dictionary-free mode, external database auth, server-only TLS, 24-hour FIX sessions, store formats, public log/store extension points, and reset-mode SequenceReset behavior are preserved.
- **Deliberate exclusions:** No bespoke web security stack, cryptographic archive format, ACME client, SIEM, backup scheduler, secure erase, or tenant RBAC is justified by this report.
- **Release criterion:** No finding is marked resolved solely because a broad compliance label appears addressed; evidence is per current code path and deployment profile.
