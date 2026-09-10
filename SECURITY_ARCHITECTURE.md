# Security Architecture and Control Ownership

This document records the security architecture of the QuickFIX engine in this
repository, its trust boundaries and data flows, and the split of controls
between the engine and the host that embeds it. It closes the evidence gap
recorded as finding 36 of the BLK1-Review-1 report (2026-09-08) and supplies
the deployment-half dispositions for findings whose residual risk is owned by
the deploying host.

It is an engineering record, not a compliance certification. Nothing here
asserts PCI DSS, NIST, SOC 2, AWS Well-Architected, or any other framework
conformance; such claims require deployment-specific evidence that this
repository cannot produce.

Remediation detail, per-finding plans, and verification evidence live in
`docs/superpowers/plans/2026-09-08-blk1-review-1-remediation.md` and the PR
that implements it.

## 1. System Overview and Trust Boundaries

The library implements FIX 4.0 through 5.0 SP2 and FIXT 1.1. It is a protocol
engine; it does not terminate TLS at a proxy, run an authorization service, or
manage credentials.

Components and boundaries:

| Boundary | Crossing data | Trust stance |
| --- | --- | --- |
| Network -> plaintext acceptor (`SocketAcceptor`, `ThreadedSocketAcceptor`) | Unauthenticated FIX frames | Hostile until Logon is accepted; every frame is length-bounded and admission-checked before session state changes |
| Network -> TLS acceptor (`SSLSocketAcceptor`, `ThreadedSSLSocketAcceptor`) | TLS records, then FIX frames | Peer certificate chain verified to the configured CA; optional client certificates per `CertificateVerifyLevel`; TLS 1.2 is the floor |
| Network -> initiator (`SocketInitiator`, `SSLSocketInitiator`) | Server responses | Initiator verifies the server chain and hostname; server-auth-only operation remains a supported mode |
| Embedded HTTP administration | n/a | Retired: `HttpAcceptPort` is rejected at configuration load and the compatibility surface fails closed |
| Bindings (`src/python3`, `src/ruby`, SWIG) | Caller arguments and results | Conversion, null-pointer, and exceptional-path checks; the caller owns process trust |
| Infinite frame adapter ABI (`InfiniteCompleteFrame`, `InfiniteFrameAdapter`) | Length-prefixed frames, <= 65536 bytes | Frames validated before publication; ABI consumers see validated state only |
| Stores (`MessageStore`, `FileStore`, database stores) | Sequence state and message archives | Restored state validated before publication; host owns storage confidentiality and integrity |
| Logs (`Log`, `FileLog`, database logs) | Session events and raw messages | Engine-generated logon credentials redacted; raw archives are never redacted; host owns retention |

Data flow, accepting side: socket bytes -> per-connection parser with a fixed
accumulator bound of 16 MiB (`Parser::MAX_BUFFER_BYTES`) -> adjacency check and
source allow-list (`AllowedRemoteAddresses`) before session mutation -> session
state -> application callbacks -> store and log.

Sending side: application -> `Session::send` -> serialization -> transport.

## 2. Data Classification and Storage

| Data | Where it lives | Sensitivity | Control |
| --- | --- | --- | --- |
| FIX logon credentials (tags 553/554) | Wire, configuration, engine log stream | Secret | Redacted in engine-generated log lines; must not be persisted by the host in raw form |
| Order flow and business payloads | Wire, message archives, application memory | Confidential; integrity-critical | Host encrypts storage; engine bounds parsing; archives are never log-redacted by design |
| Sequence state | `FileStore`/database stores, session memory | Integrity-critical | Restored offsets, sizes, and timestamps validated; corrupt state never published |
| Message archives | `FileStore` files, database tables | Confidential; integrity-critical | Host owns encryption, retention, destruction, and backup verification |
| Logs | `FileLog`, database logs, stdout | Confidential; may contain raw wire data | Framing escapes backslash/CR/LF and writes are serialized per log instance; host owns forwarding and retention |
| TLS private keys and certificates | Host-provided paths | Secret | Loaded at initialization; no committed keys ship with the repository; sample PKI is generated under the build tree as disposable test fixtures |
| Configuration files | Host filesystem | Sensitive | Session-level admission and TLS controls live here; host owns file permissions |

## 3. Authentication and Authorization

- Authentication is the FIX Logon exchange. Acceptor applications implement
  `Application::fromAdmin` (or `Session::Logon`) and return
  `RejectLogon` for credentials they refuse; the engine never accepts a
  session on behalf of the application.
- Authorization is owned entirely by the embedding application. The engine
  provides no roles, tenancy, or per-message permissions. Applications that
  need them enforce them in message callbacks.
- Source restrictions (`AllowedRemoteAddresses` per session,
  `SocketAcceptAddress` per acceptor) are admission controls applied before
  session state changes.
- There is intentionally no embedded management surface: administrative
  operations are ordinary in-process `Session` calls
  (`logout`, `disconnect`, `reset`, sequence accessors) made by the host
  behind its own authenticated boundary.

## 4. Finding Control Matrix

Dispositions: **Code** = fixed or retired in the engine; **Code + host** =
engine fix plus a documented deployment control; **Host** = documented
deployment ownership; **N/A** = not applicable after retirement.

| # | Finding (short) | Disposition | Evidence |
| ---: | --- | --- | --- |
| 1 | Unclamped DATA length | Code | `65bad3a` |
| 2 | TLS verification fails open without a CA | Code | `c76e1dc` |
| 3 | Pre-Logon SequenceReset rewrites persisted state | Code | `4034502` |
| 4 | Unknown ApplVerID falls back to empty dictionary | Code | `4c0a32d` |
| 5 | Over-long configuration lines drop later settings | Code | `6be651b` |
| 6 | FileStore restores unvalidated offsets/sizes/timestamps | Code | `eaab431` |
| 7 | ordermatch accepts invalid quantities | Code | `6f13c57` |
| 8 | HTTP closes reused descriptors twice | Code + N/A | `2f2d85e` (surface retired) |
| 9 | Predictable privileged `/tmp` provisioning path | Code | `d8f134c` |
| 10 | Source allow-lists missing/late | Code | `4034502` |
| 11 | mTLS trust widened without identity binding | Code | `c76e1dc` |
| 12 | Privileged/default database credential paths | Code + host | `d8f134c`; roles are operator-managed (§5) |
| 13 | FileLog record injection and unsynchronized writes | Code | `a0114e6` |
| 14 | SWIG typemaps omit conversion/pointer checks | Code | `04b1c06`, `b498b93` |
| 15 | TLS initiators ignore configured CRLs | Code | `c76e1dc` |
| 16 | Embedded HTTP administration mutates sessions unauthenticated | Code + N/A | `2f2d85e` (surface retired) |
| 17 | Unbounded parsing and blocking HTTP permit exhaustion | Code + host | `65bad3a`, `d160420`, `2f2d85e`; connection limits are host-owned (§5) |
| 18 | One counterparty can cancel another's order | Code | `6f13c57` |
| 19 | HTTP reflected XSS and CSRF-able mutations | Code + N/A | `2f2d85e` (surface retired) |
| 20 | Persistence lacks restore validation and deployment protection | Code + host | `eaab431`; storage encryption/backup ownership (§5) |
| 21 | Shipped defaults/samples select weak modes | Code + host | `2f2d85e`, `c76e1dc`, `aaefa052`; deployment profiles (§5) |
| 22 | No current SBOM or dependency scanning evidence | Code | `.github/workflows/build-security.yml` (SPDX source/installed artifacts, Grype, CodeQL, secret scan) |
| 23 | Logon credentials traverse plaintext and are logged | Code + host | `c76e1dc`, `a0114e6`; TLS termination ownership (§5) |
| 24 | Logs lack redaction, framing, retention controls | Code + host | `a0114e6`; retention/forwarding ownership (§5) |
| 25 | HTTP administrative mutations lack audit records | N/A | `2f2d85e` (surface retired) |
| 26 | HTTP administration lacks authorization/session scoping | N/A | `2f2d85e` (surface retired) |
| 27 | Committed reusable-looking keys and published passphrase | Code | `c76e1dc`, `aaefa052` |
| 28 | Security monitoring coverage incomplete | Host | §5 monitoring; engine emits redacted, framed pre-authentication events (`2f2d85e`, `a0114e6`) |
| 29 | Listeners bind `INADDR_ANY` without an accept address | Code | `2f2d85e`, `aaefa052` (`SocketAcceptAddress`) |
| 30 | ACL and SQL escaping inconsistent across sibling paths | Code | `4034502`, `d8f134c` |
| 31 | TLS parsing can fail open; obsolete protocols selectable | Code | `c76e1dc` (TLS 1.2 floor) |
| 32 | Incident-containment capabilities unclear | Host | §6 containment via existing session calls; escalation ownership follows host incident process |
| 33 | Certificate lifecycle ownership undocumented | Host | §5 certificate issuance/renewal/expiry; disposable fixtures via `test/generate-test-pki.sh` (`c76e1dc`) |
| 34 | Backup integrity and restore ownership incomplete | Code + host | `eaab431` (restore validation), `2f2d85e`; backup/drill ownership (§5) |
| 35 | HTTP idle timeout model; FIX session-window intent undocumented | Code + host | `2f2d85e`, `aaefa052`; 24-hour session intent documented in §5 |
| 36 | No consolidated threat model / data-flow record | Code | This document |

## 5. Host and Deployment Requirements

The following controls are owned by the deploying host. They are requirements
for a production deployment; the engine cannot enforce them.

- **Connection and rate limits.** Front the acceptor with connection limits,
  accept-rate limits, and bandwidth controls appropriate to expected order
  flow. The engine bounds memory per connection (16 MiB accumulator) but does
  not enforce a connection count.
- **Firewalling and network placement.** Restrict which peers reach the
  listener, and prefer TLS listeners on untrusted networks. Plaintext samples
  in `bin/cfg` are local demonstration profiles, not deployment templates.
- **Encrypted private storage.** Store message archives, logs, and any
  persisted credential material on encrypted volumes with restrictive
  permissions.
- **Service identity and directories.** Run under a dedicated service account
  with a restrictive umask and private runtime directories for stores, logs,
  and generated test PKI.
- **Backups.** Use immutable or off-host backups for sequence state, stores,
  and logs; periodically rehearse a restore and record the result. Verify
  restored store state by running the engine against it before promotion.
- **Retention and destruction.** Define retention for message archives and
  logs; destroy expired data with host tooling.
- **Log forwarding and alerting.** Forward engine logs to the host log
  pipeline and alert on pre-authentication rejections, admission-limit
  events, TLS handshake failures, and store-integrity failures.
- **Certificates.** Issue certificates from the deployment's own CA, keep
  private keys in the host keystore, monitor expiry, renew before expiry, and
  refresh CRLs/trust material. Contexts load files at initialization:
  restart or recreate transports to apply renewed material while preserving
  sequence and store files.
- **Process isolation.** Sandbox the process (container, unit sandbox, or
  equivalent) with least-privilege filesystem and network access.
- **Database credentials.** Use explicit non-empty users with least-privilege
  grants. An explicitly empty password remains supported for deployments that
  authenticate externally (for example, IAM or certificate-based database
  authentication).
- **Incident escalation.** Wire engine alerts into the host incident process
  with named owners for FIX sessions, TLS material, and storage.
- **Session-window intent.** Acceptors here are machine-to-machine FIX
  sessions. `NonStopSession=Y` and 24-hour windows are intentional for that
  protocol; they are not browser sessions and no browser-style idle timeout
  is provided by the engine. Hosts that require idle disconnection drive it
  through their own scheduling or network policy.

## 6. Containment and Incident Response

The engine exposes containment through existing session calls:

- `Session::logout` sends a Logout and begins orderly termination.
- `disconnect` (through the connection owner) severs the transport.
- Source restrictions (`AllowedRemoteAddresses`, `SocketAcceptAddress`) admit
  new connections only from permitted peers. Changing an allow-list does not
  revoke an established connection; combine it with `logout`/`disconnect` or
  a listener restart during containment.
- Stores and logs preserve sequence state and evidence for post-incident
  review; keep them when containing a session.

## 7. Verification

`.github/workflows/build-security.yml` runs, on every push and pull request:
default and TLS Release unit suites, ASan+UBSan registered tests, a focused
TSan run for `FileLogTests`, a secret scan limited to generated-PKI
exclusions, CodeQL analysis without upload, SPDX SBOM generation for source
and installed trees, Grype scans, and retention of the evidence bundle.
`test/runat.sh` provides explicit plaintext nonthreaded and threaded
acceptance modes, and the TLS transport unit tests run against a disposable
PKI generated by `test/generate-test-pki.sh`.
