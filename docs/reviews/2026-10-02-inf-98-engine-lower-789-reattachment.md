Specification pin: `infinite-research/infinite-rfq` `23ecd154784d473090566498e1717443075701fc`, tree `61e2f737da74d17b915c55b33da8ba1d4b464f10`, bundle `sha256:437cf978419177c719a604496315395f57f1bbbafc3a9fc6b2f148a7b54230da`.

# INF-98 engine half: lower-789 direct reattachment and re-pin

Base `e2bff808e5d9b001d91ca432cd6de0356c29746d` (tree `a82e1e26…`). Code head `f337e3620bba9bbd5cdaa008c688059412c19e0c`, tree `2dc8474da6f8be5632322db279c3a403b335cc60`; this record is the only commit after it. Risk tier R3. Lane logs: `~/infinite-evidence/inf-98/f1/f337e3620bba/`; superseded heads and runs (`6c78f451`, `cac18825`, `c3740aa4`): `~/infinite-evidence/inf-98/f1/SUPERSEDED.md`. An independent review of `c3740aa4` raised two P2s, both fixed in this head (§Review fixes); no independent pass has yet read `f337e362`.

## What changed

- `src/C++/InfiniteFrameAdapter.cpp` `registeredInboundPlan`: at an eligible VALUE target in a detached direct `STORED_RANGE` recovery, an authenticated ordinary Logon with present 789 is accepted when `B<=p<=N` with sender and original both `VALUE,N`, or `B<=p<=C` with original `VALUE,C` the exact predecessor of the native sender. The cursor `+244` becomes `min(r,p)` only on the additional branch `p<C` (C the supplied original; `C=N` in the equal form); absent 789, `p==N` and `p==C` keep the saved cursor. An `EXHAUSTED` sender now reaches these rules instead of a pre-plan refusal: only the predecessor form (`C=FIX_SEQ_BOUND-1`) is accepted; every other exhausted shape returns the zero-output fail-closed plan. No header, ABI constant, fixture, dictionary or layout change; `+244` is the only new native-state write (the diff adds `write64(plan->state.data() + 244, …)` twice and no other state write).
- `src/C++/test/InfiniteFrameAdapterTestCase.cpp`: new test case `reattaches detached direct recovery from an authenticated lower 789` and the changed expectations below.
- `cmake/VerifyInfiniteAdapterPackage.cmake`: authority `b6a225c1/f5dbe650/1444e4f0` to `23ecd154/61e2f737/437cf978` (self-test constants and verifier, 6 lines); `_irfq_obsolete_spec` moved to the retired `b6a225c1` triple (3 lines).

## Authority (verbatim at `23ecd154`; `git show 23ecd154:<path> | sed -n '<range>p' | sha256sum`)

| Range | sha256 | Governs |
|---|---|---|
| `05c-outbound-delivery-and-recovery.md:55` | `cd09b969…134e45d` | the additional branch: `B<=p<=C`, sender `VALUE,N` with `p<N` or sender `EXHAUSTED`, both authorities, `min(r,p)`, `C<p<N` refused |
| `05c-…:53` | `c08eb68a…ed1069c` | absent / equal-N 789 and the `p=C` predecessor retry: "the recovery cursor is unchanged" |
| `05c-…:59` | `5872ab06…fb344d` | every other shape: zero output, `DURABLE_NO_CONSUME,SEQUENCE` + `DISCONNECT(SEQUENCE)`; "The same fail-closed result applies when sender and supplied original are already `EXHAUSTED` after confirmed response handoff"; target `EXHAUSTED` fails before a plan |
| `06-system-context-and-gateway.md:1057-1075` | `5f4f7375…7d19429` | the branch in gateway terms |
| `06-…:1076-1085` | `4830d65f…43d6c23` | line 1076: "At such a VALUE target, every other `O`/`p` shape returns `READY`, zero output,"; lines 1082–1083 (one sentence across the wrap): "includes sender and original both `EXHAUSTED` after confirmed response" ⏎ "handoff" — agrees with `05c:59` |
| `06-…:1495-1503` | `b6e57751…727d086e` | action rows |
| `06-…:1223-1233` | `e8e09a86…c833bb` | final target `VALUE,FIX_SEQ_BOUND-1`: `NO_CONSUME`, optional Logout, `DISCONNECT` |
| `06-…:602-608` | `283f7995…cff003f` | legal movement `cursor=min(saved_cursor,p)` |
| `07-fix-protocol.md:100` | `9a857c5d…8f75a8d` | direct resend uses §5c reattachment |
| `19-fix-conformance.md:116-126` | `546a6804…ad026742` | vectors T1–T8 |

## Pre-flight (brief §8)

P1 `origin/master`=`e2bff808…`. P2 `commit`, tree `61e2f737…`, recomputed bundle `437cf978…`, the four §4 hashes equal. P3 `75ecae39…`/`d9ce75d2…`/`f76489fe…`, no dictionary diff. P4 image Id `sha256:c333c04c1f5ca0496016b43996fbdeed30c3b1b91f5e1581a418bf68a518139c`; image cmake 3.28.3 (verifier `cmake=cmake 3.28.3` passed in Build A). P5 header `4ad399be…` 17402 B, fixture `4fd4bd7a…` 13536 B, equal to platform `origin/main` `ba6e24eb`. P6 own worktree, packaging from fresh clones. P7 clang-format 22.1.8. P8 no other C ABI clause (06:940 is already enforced at `processingFrontierPlan`; checkpoint recomputation is Rust). P10 vendored `source_commit=47ba4cee…`, `specification_commit=b6a225c1…`.

## Stop conditions

- **S9 hit** (resolution approved by Main). The brief's E2 sentence "`:2208` must not change a byte" is false under its own E1: its row `equal-original-conflicting-peer` (`[B,E)=[2,5)`, `S=O=VALUE,7`, `p=6`) lies inside the widened `B<=p<=N`, so E1 accepts it. Likewise `:8653` `wrong-789` (`p=6`) at the final target now reaches `finalTargetDirect`.
- **S14 hit**, recorded as a spec question; engine unchanged on this point. Two sites set no bound on `E` relative to the sender: `InfiniteSessionClassification.cpp:1042-1045` `resendRangeValid = … (resendEndInclusive == 0 || resendEndInclusive >= resendBegin)`, and `InfiniteFrameAdapter.cpp` `const auto end = inbound.resendEndInclusive == 0 ? senderSequence : inbound.resendEndInclusive + 1;`. Reproduction: a throwaway test, not committed (`mutants/S14-repro.log`, EXIT 0). With sender 6, ResendRequest `7=2|16=10` stores `[B,E)=[2,11)` and the state restores. Because `r>C` is reachable, the cursor precedence below matters.
- **Gap found after the brief** (Main): the pre-plan guard refused an `EXHAUSTED` sender at a non-final target, although `05c:55` admits "sender `EXHAUSTED`". Fixed as described above; brief D11 is not optional.
- Not hit: S1–S8, S10–S13. For S7, the packaged header, fixture and licenses digests equal the pins (below).

## Review fixes (independent review of `c3740aa4`)

- P2-1: on `c3740aa4` the rewind fired on `p==C` (and `p==N`) whenever `p<r`, contradicting `05c:53`. Now `lowerReattachmentRewind = present && p < C && p < r`. Row `peer-equal-counter-and-predecessor-keep-cursor-above-C` uses `[2,11)`, sender 6, `r=7`:
  - equal form, 789=6: one `SESSION_ADMIN` at 6, `+244`=7;
  - after loss, `O=VALUE,6`, 789=6: `NEED_STORE_RANGE [6,7)`, one `SESSION_RETRANSMIT`, `+244`=7.
  Mutant M13 targets this row.
- P2-2: the predecessor `B<=p` bound had no `p=B` row. New row `predecessor-peer-begin-10` (`O=30`, `S=31`, `p=10`) expects `NEED_STORE_RANGE`, `SESSION_RETRANSMIT` at 30 and cursor 10. Mutant M14 targets this row.

## Changed expectations (before → after, justification)

| Test, row | Before | After | Authority | Refusal class still pinned |
|---|---|---|---|---|
| `fail-closes invalid direct-recovery…`, `equal-original-conflicting-peer` p=6 | NO_CONSUME + DISCONNECT | row now p=1 (`equal-original-peer-below-begin`) plus new `equal-original-peer-above-sender` p=8; p=6 acceptance moved to `equal-counter-peer-6-inside-window` | 05c:55, 06:1057-1060 | `p<B` and `p>N` (05c:55 "A value below B, above C … cannot select this branch"); predecessor `p>C` stays `predecessor-conflicting-peer` |
| `rejects inexact direct recovery identities at the final target`, `wrong-789` p=6 | INVALID_ARGUMENT | `wrong-789-below-begin` p=1 and new `wrong-789-above-sender` p=8 (both INVALID_ARGUMENT); p=6 joins the terminalization matrix as `initial-peer-lower` | 06:1223-1233 | `p<B`, `p>N` |
| `reborrows one lost direct-recovery Logon response…`, response at `FIX_SEQ_BOUND-1`, `p==C` | INVALID_ARGUMENT | NEED_STORE_RANGE, one `SESSION_RETRANSMIT`, sender stays `EXHAUSTED,0` | 05c:55 "or sender `EXHAUSTED`" | — |
| `fail-closes…`, `S=O=EXHAUSTED`, p absent, target 3 | INVALID_ARGUMENT | READY, zero output, `DURABLE_NO_CONSUME,SEQUENCE` + `DISCONNECT(SEQUENCE)`, recovery bytes preserved | 05c:59, 06:1076-1085 | INVALID_ARGUMENT class kept by the new `target-exhausted-before-plan-sender-exhausted=0/1`, by the attached-barrier Heartbeat (`holds direct recovery…`), and by the detached non-Logon test (`heartbeat-range-detached-non-logon-invalid`) |

`holds direct recovery behind the exact Logon response handoff` and `round-trips the negotiated heartbeat…` are unchanged and green.

## Lanes (head `f337e362…`, tree `2dc8474d…`)

All lanes except (f) are container runs in image Id `c333c04c…`. Each container log opens with HEAD, TREE, image Id, loadavg and command, and closes with CONTAINER_ID, END and EXIT. Lane (f) is a host run of `clang-format --dry-run --Werror`. Its log carries HEAD, TREE, the clang-format version, START, END and EXIT; it has no image or container stamps.

| Lane | Log | EXIT | Result line |
|---|---|---|---|
| (a) Build A | `build-A.log` | 0 | `Infinite adapter package published` |
| (b) Build B | `build-B.log` | 0 | `ab-compare.log`: `diff SHA256SUMS-A SHA256SUMS-B` empty, `diff_exit=0` |
| (c) ctest | `ctest.log` | 0 | `100% tests passed, 0 tests failed out of 23`. The brief expected 22, but `infinite_performance_harness_contract` is already registered at base. After the full build, `post-ctest-sha256-check.log` shows all package files OK. |
| adapter suite | `adapter-ut.log` | 0 | `All tests passed (15153 assertions in 144 test cases)` |
| (d) self-test | `self-test.log` | 0 | `Infinite adapter package self-test passed` (includes the retired-triple refusal) |
| (e) retired pin | `negative.log` | 1 (required) | `specification_commit mismatch: expected '23ecd154…', got 'b6a225c1…'` at `VerifyInfiniteAdapterPackage.cmake:1524`; `infinite-adapter-package/` holds 0 files |
| (f) format (host) | `../format-f337e3620bba.log` | 0 | host clang-format 22.1.8 `--dry-run --Werror` on both changed `.cpp` |
| Engine CI (Formatting, Linux Release OFF/ON) | — | NOT_RUN | not pushed by the author |
| Windows MSVC (D5), Build assurance (D6), Autotools | — | SKIPPED / NOT_RUN | |
| (g) Build M | — | NOT_RUN | no merge |

Build A = Build B (`SHA256SUMS-A`, `SHA256SUMS-B`):
- `libquickfix.a`: 8616708 B, `586c12cde0635df8475d7174f51897eb8c576baa9c8b813efaa91a8f2a4f3558`
- `InfiniteFrameAdapter.h`: `4ad399be…`
- `infinite-frame-adapter-abi.v2.tsv`: `4fd4bd7a…`
- `LICENSES.txt`: `92b106d2…`
- `manifest.sha256`: `61227b6cfb31b2d06882eea96e16a5439825cbe48bbb80dd018805bde5a12ee6`

## Mutation ledger

Logs: `~/infinite-evidence/inf-98/f1/mutants/<M>.log` and `<M>-revert.log`, plus `run-mutants.out`. Each mutant was applied to a clean clone at `f337e362`, built and run, then reverted, rebuilt and rerun. Every revert log reads `All tests passed (3427 assertions in 11 test cases)`, except M7, whose revert log reads `(34 assertions in 2 test cases)`. There are 14 patch/revert mutants; M6 is the retired-pin lane. The 12-mutant ledger for `c3740aa4` is archived in `mutants/c3740aa4d76b-ledger/` and superseded by this one (see `SUPERSEDED.md`).

| Mutant | Patch sha256 | Caught by (section @ line) |
|---|---|---|
| M1: drop `B<=p` (equal form) | `1df6353c…` | `T9-peer-below-begin-9` @2707; `equal-original-peer-below-begin` @2242; `wrong-789-below-begin` @9262 |
| M2a: drop `p<=N` | `90168f99…` | `equal-original-peer-above-sender` @2243; `T10-peer-above-sender-31` |
| M2b: drop `p<=C` (predecessor) | `4979d276…` | `predecessor-conflicting-peer` @2242. The brief named T12, which tests `p<B` and cannot catch this mutant. |
| M3: rewind whenever 789 is present | `2e5b3858…` | `T2` @2525/2591; existing `p==N` pins @1578 |
| M4: predecessor form advances the sender | `adb62a8f…` | `T5-predecessor-peer-12`, `predecessor-peer-begin-10` @2875; `exhausted-predecessor-peer-12` @2865 |
| M5: authorize `C<p<N` | `b05292f2…` | `T7`/`T8` @2707; `exhausted-original-not-predecessor` @2840 |
| M6: retired triple | — | lane (e) EXIT 1 on `specification_commit`; self-test `_irfq_obsolete_spec` |
| M7: fixture `native_state_bytes` 312→313 | `766fac8f…` | `fixture pins the exact compiled C ABI` @6709; the mutated fixture's sha is `90e7c8e7…`, not `4fd4bd7a…` |
| M8 (M-predecessor): old guard, no exhausted admission | `0bd381be…` | `exhausted-predecessor-peer-12` @2846; `S=O=EXHAUSTED` @2315 |
| M9: blanket exhausted admission | `68d29731…` | `recovery-none-native-exhausted=1` @2379 |
| M10: blanket exhausted acceptance | `bf184745…` | exhausted fail-closed rows @2840; `S=O=EXHAUSTED` @2315 |
| M11 (M-failclosed): other exhausted shapes throw | `3b9ff9ff…` | same rows @2840/2315 |
| M12 (M-target-invalid): drop the `targetExhausted` refusal | `7ed2aebc…` | `target-exhausted-before-plan-sender-exhausted=0/1` @2730 |
| M13: rewind on `p==C` / `p==N` | `658d26b5…` | `peer-equal-counter-and-predecessor-keep-cursor-above-C` @2907/2940/2970 |
| M14: `B<p` in the predecessor predicate | `cc865959…` | `predecessor-peer-begin-10` @2846 |

## F1 → F2 (from the PR head; F2 vendors Build M only)

Against the manifest vendored on platform `origin/main` `ba6e24eb`, **8 of 47 lines** change (`manifest-delta-vs-vendored.diff`):
- `source_commit`, `source_tree`, `source_diff_sha256`
- `specification_commit=23ecd154…`, `specification_tree=61e2f737…`, `specification_bundle_sha256=437cf978…`
- `archive_size`, `archive_sha256`

The brief said "9 of 47" but lists these same eight. Keys and order are identical, and the other 39 lines are byte-identical.

`archive_size` moves from 4077146 to 8616708 B. Most of the growth is nine `fixNN/MessageCracker.cpp.o` members (4439776 B in the `c3740aa4` member diff). They come from `7d805f97` (PR #7, already on base), not from F1. Header, fixture and licenses are unchanged.

## Not verified

- Engine CI on any head.
- Build M.
- Independent review of `f337e362`.
- Windows and sanitizers.
- Whether `E>N` is intended (S14).
- The size of `libquickfix.a` built at base; the archive delta is attributed by archive member diff and `git log`.

## Decisions taken on defaults

- D1: branch `codex/inf-98-engine-lower-789-reattachment`.
- D2: this record.
- D3: retired `b6a225c1` triple.
- D4: widen two predicates and add one conditional write.
- D5: Windows SKIPPED.
- D6: sanitizers NOT_RUN.
- D7: Build M by the F1 engineer after merge (not run).
- D8: the F2 PR files the R3 ledger row.
- D9: no NEWS.
- D10: not exercised.
- D11: superseded by the normative exhausted-sender sentence.
