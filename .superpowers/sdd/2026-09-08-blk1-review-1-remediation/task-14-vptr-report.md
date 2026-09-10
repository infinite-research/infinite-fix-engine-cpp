# Typed-object lifetime prerequisite for Task 14

Base: `a905abcb53a684f3406e5bbb91d0f908008b2ccf`.
Worktree: `/home/eike/workspace/github.com/infinite-research/infinite-fix-engine-cpp/.worktrees/blk1-review-1-remediation`.
Implementation commit: `267194f667888bcd99b64e8ab5c35ff5e7b88b31` (`fix: construct genuine typed fields and cracked messages`).
The report is committed separately as its immediate child; both hashes are returned in the completion message.

## RED evidence and cause

Both exact direct reproducers were run before implementation, with core dumps disabled and both explicit QuickFIX paths:

```sh
(ulimit -c 0; UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ./build-security-asan/lib/ut DataDictionaryTests \
  --quickfix-config-file "$PWD/test/cfg/ut.cfg" \
  --quickfix-spec-path "$PWD/spec")

(ulimit -c 0; UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ./build-security-asan/lib/ut 'FIXT application dictionary selection' \
  --quickfix-config-file "$PWD/test/cfg/ut.cfg" \
  --quickfix-spec-path "$PWD/spec")
```

Each exited 1 at the first UBSan error, without reaching a final assertion count:

- `DataDictionaryTestCase.cpp:50`: member call on an object whose dynamic type is `FIX::Header`, not the generated version `Header`. `FIX::Message` owns genuine base Header/Trailer members; generated accessors downcast those members without creating the derived objects.
- `Field.h:271`, called by `Session::authenticateLogon` at `Session.cpp:320`: member call on an object whose dynamic type is `FIX::FieldBase`, not `StringField`. `FieldMap` stores sliced `FieldBase` values, while its typed getter and macros pretended they were derived fields.
- The cracker family had the same defect: routing tags selected C-style message downcasts even when callers supplied genuine generic or version-base messages. Tags do not create C++ object lifetimes.

Regression tests were written before production changes. The baseline Release focused build initially caught test harness include/name ambiguities; cracker tests were isolated in their own translation unit. The runnable RED check used the existing unit-test main/Catch objects and baseline library:

```sh
c++ -O0 -std=c++17 -DQUICKFIX_CMAKE_BUILD=1 \
  -Isrc/C++ -Ibuild-security/include \
  src/C++/test/MessageCrackerTestCase.cpp src/C++/test/FieldMapTestCase.cpp \
  build-security/src/C++/test/CMakeFiles/ut.dir/ut.cpp.o \
  build-security/src/C++/test/CMakeFiles/ut.dir/catch_amalgamated.cpp.o \
  build-security/src/C++/test/CMakeFiles/ut.dir/TestHelper.cpp.o \
  -Lbuild-security/lib -Wl,-rpath,"$PWD/build-security/lib" \
  -lquickfix -lpthread -o build-security/task-14-vptr-red-ut
(ulimit -c 0; ./build-security/task-14-vptr-red-ut \
  'FieldMapTests,MessageHeaderTrailerTests,MessageCrackerTests' \
  --quickfix-config-file "$PWD/test/cfg/ut.cfg" \
  --quickfix-spec-path "$PWD/spec")
```

Compilation exited 0. RED execution exited 79: **3 cases failed, 371 assertions, 292 passed / 79 failed**. Failures covered dynamic types, the base header/trailer reference contract, and field values changing after map mutation. Logs are in `build-security/task-14-vptr-red-focused{,-build}.log`.

## Implementation and source compatibility

- `src/C++/FieldMap.h`: `getField<T>()` constructs `T`, fills its base through the existing safe overload, and returns the genuine value. `FIELD_GET_REF` retains its legacy spelling but now returns a value; its common cracker callers use `getField<BeginString>()` directly. `FIELD_GET_PTR` is removed. `FIELD_SET` returns the supplied field after filling it, eliminating its unnecessary cast. Four small forwarding methods retain `set/get/isSet/getIfSet` syntax on base Header/Trailer objects.
- `spec/GeneratorCPP.rb` and all nine generated `Message.h` files: remove invalid version Header/Trailer accessors and inherit the genuine base accessors. Standalone version Header/Trailer classes and nested group names remain intact.
- `src/C++/MessageCracker.h`: uses qualified calls to its real base cracker subobjects, passing the source message directly. Version routing and FIXT application-version/session-default selection retain their original logic.
- `spec/MessageCracker.xsl` and all nine generated cracker headers: include complete message definitions, preserve existing version-message overloads, and add generic-message overloads. Const callbacks receive real typed temporaries. One private generated template per version handles mutable dispatch: construct the typed message, invoke the callback, and explicitly move its changes back on normal return or in `catch (...)` before rethrowing. No destructor performs assignment. Unknown-type callbacks receive a genuine version-base message; default UnsupportedMessageType behavior is preserved.
- Complete cracker definitions exposed previously uncompilable duplicate `FIELD_SET` methods. The compiler errors provided RED evidence for the generator repair. `GeneratorCPP.rb` now tracks emitted fields per class with a native Ruby hash and a scope stack. Regenerated deletions affect `fix43/RegistrationInstructions.h` and six FIX50 headers: `MultilegOrderCancelReplace.h`, `NewOrderMultileg.h`, `QuoteRequest.h`, `QuoteRequestReject.h`, `TradeCaptureReport.h`, and `TradeCaptureReportAck.h`. No schema or group-order changes were made.
- Tests: `FieldMapTestCase.cpp`, new `MessageCrackerTestCase.cpp`, and its CMake/Autotools source registrations. Coverage includes string/numeric/time values, missing/optional fields, value independence, all nine base header/trailer families, arbitrary field classes, all nine common and generated routes, generic input, normal/throwing callback mutation of body/header/trailer/groups, unsupported messages, and FIXT session-default application version.
- `doc/html/building.html` documents the source migration, optional-value replacement for the removed pointer macro, callback-reference lifetime, and rebuilding consumers.

This changes public inline/template source behavior. Consumers must rebuild with matching new headers and library. Mutable callback references refer to temporary typed objects and must not be retained beyond the callback. If transferring callback changes itself throws, that exception propagates through ordinary control flow. No mixed-header binary compatibility is claimed. No production RTTI, cache, polymorphic storage, factory, or new runtime dependency was introduced; no data members or virtual functions were added or changed.

## Regeneration

The existing Ruby processor requires REXML, absent from this machine's Ruby installation. Initial attempts reported `LoadError`, then a read-only user cache and sandbox DNS denial. REXML 3.4.4 was installed solely under the ignored worktree build directory using approved network access:

```sh
GEM_SPEC_CACHE="$PWD/build-security/task-14-generator-cache" \
  gem install rexml --no-document \
  --install-dir "$PWD/build-security/task-14-generator-gems"
```

No repository dependency or machine-wide installation was added. From `spec/`, the final regeneration command was run twice:

```sh
set -e
GEM_PATH="$PWD/../build-security/task-14-generator-gems" \
  ruby -I. -rProcessor -rGeneratorCPP -e '
versions = [["FIXT",1,1,0,0]] +
  (0..4).map { |i| ["FIX",4,i,0,0] } +
  (0..2).map { |i| ["FIX",5,0,i,i+7] }
versions.each do |type,major,minor,sp,verid|
  name = "#{type}#{major}#{minor}#{sp == 0 ? "" : "SP#{sp}"}"
  generator = GeneratorCPP.new(type,major.to_s,minor.to_s,sp.to_s,verid.to_s,"../src/C++")
  processor = Processor.new("#{name}.xml",[generator])
  [:front,:header,:trailer,:baseMessage].each { |step| processor.public_send(step) }
  processor.messages if name == "FIX43" || name == "FIX50"
  processor.back
end'
for version in FIX40 FIX41 FIX42 FIX43 FIX44 FIX50 FIX50SP1 FIX50SP2 FIXT11; do
  xsltproc -o "../src/C++/${version,,}/MessageCracker.h" MessageCracker.xsl "$version.xml"
done
```

After the first pass, SHA-256 hashes were saved for all 18 required headers and seven additional repaired concrete headers in `build-security/task-14-generated-final.sha256`. After the second pass, `sha256sum -c ../build-security/task-14-generated-final.sha256` exited 0: **25/25 OK**. Generated version directories are explicitly excluded by the existing `.clang-format-ignore`; their generator formatting is preserved.

An exploratory regeneration of every concrete version message confirmed no further duplicate-emission changes. It exposed an unrelated pre-existing FIX42 `QuoteAcknowledgement`/XML name mismatch (`QuoteAckStatus` versus `QuoteStatus`); that unrelated generated drift was restored to baseline. The final command above regenerates all nine base/cracker families and the concrete families affected by this repair.

## Verification

Both existing build configurations use `HAVE_SSL=ON`. `build-security` is Release. `build-security-asan` is Debug with `-fsanitize=address,undefined -fno-omit-frame-pointer` compilation and `-fsanitize=address,undefined` executable/shared linker flags. No vptr suppression was added.

```sh
cmake --build build-security --target ut --parallel 2
cmake --build build-security-asan --target ut --parallel 2
```

Final builds each exited 0 with no warnings or errors. Logs: `build-security/task-14-vptr-verified-build.log` and `build-security-asan/task-14-vptr-verified-build.log`. Earlier full rebuilds emitted existing OpenSSL 3 deprecation warnings. A new test's deprecated default timestamp constructor was replaced by `UtcTimeStamp::now()` and its object was explicitly rebuilt before final gates.

Focused commands, run after the final rebuilds:

```sh
(ulimit -c 0; ./build-security/lib/ut \
  'FieldMapTests,MessageTests,MessageHeaderTrailerTests,MessageCrackerTests,DataDictionaryTests,FIXT application dictionary selection' \
  --quickfix-config-file "$PWD/test/cfg/ut.cfg" --quickfix-spec-path "$PWD/spec")
(ulimit -c 0; ASAN_OPTIONS=detect_leaks=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build-security-asan/lib/ut \
  'FieldMapTests,MessageTests,MessageHeaderTrailerTests,MessageCrackerTests,DataDictionaryTests,FIXT application dictionary selection' \
  --quickfix-config-file "$PWD/test/cfg/ut.cfg" --quickfix-spec-path "$PWD/spec")
```

Both exited 0: **1,210 assertions passed in six cases**. The sanitizer run used approved process access outside the sandbox and emitted no ASan, UBSan, or LeakSanitizer diagnostics. Logs: `build-security/task-14-vptr-focused-final.log` and `build-security-asan/task-14-vptr-focused-final.log`.

An earlier sandbox sanitizer run passed the same 1,210 assertions but exited 1 afterward with `LeakSanitizer has encountered a fatal error` and its process-inspection/ptrace hint. It was classified as an environment failure, not a source failure or passing gate, and rerun with approved process access. No debugger, strace, or ptrace attachment was used; leak detection remained enabled on the accepted runs.

Registered full gates were run sequentially in the authorized socket-capable environment, using CTest's registered spec path and isolated `test-runtime` working directories:

```sh
(ulimit -c 0; ctest --test-dir build-security \
  -R '^quickfix_unit$' --output-on-failure -V)
(ulimit -c 0; ASAN_OPTIONS=detect_leaks=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-security-asan -R '^quickfix_unit$' --output-on-failure -V)
```

- Release: exit 0, registered entry 1/1 passed, **40,348 assertions in 237 cases**, 44.17 seconds.
- ASan+UBSan: **CTest exit 8, 0/1 registered entries passed**, 130.82 seconds. UBSan halted at a separate pre-existing non-vptr defect: `ThreadedSSLSocketConnection.cpp:156:3: runtime error: shift exponent -1 is negative`. `SessionTestCase.cpp:1666` constructs the connection with `INVALID_SOCKET_HANDLE`, and its constructor calls `FD_SET(m_socket, &m_fds)` without excluding `-1`. No final assertion/case count was emitted before halt. This is a real source/test-fixture blocker, not a sandbox socket-denial result. The full sanitizer gate therefore remains failing; it is not reported as passed.

The separate failure occurs in `unsupported FIXT admission and threaded teardown preserve refresh and rollover state`. Read-only blame at the exact base confirms `FD_SET` predates this task (`edfa5f560`, 2014) and the invalid-handle fixture predates it (`4c0a32df9`, 2026-09-08). Both files remain unchanged by the implementation commit.

Logs: `build-security/task-14-vptr-unit.log` and `build-security-asan/task-14-vptr-unit.log`. Acceptance and performance suites were not requested for this supplemental unit-gate prerequisite and were not run.

Additional checks each exited 0:

```sh
git diff --check
clang-format --dry-run --Werror src/C++/FieldMap.h src/C++/MessageCracker.h \
  src/C++/test/FieldMapTestCase.cpp src/C++/test/MessageCrackerTestCase.cpp
ruby -c spec/GeneratorCPP.rb
c++ -std=c++17 -fno-rtti -fsyntax-only -DQUICKFIX_CMAKE_BUILD=1 \
  -Isrc/C++ -Ibuild-security/include -x c++ -include MessageCracker.h /dev/null
```

The no-RTTI common-cracker compilation produced no diagnostics. It also parses all nine complete concrete message families.

## Cast and compatibility self-review

The full affected cast-family search is retained in `build-security/task-14-vptr-cast-review.txt`:

- No reinterpret downcasts remain in typed field retrieval or either typed-field macro. Remaining in-tree typed getter callers either consume values immediately or bind local const references, which extend the new value's lifetime appropriately.
- No Header/Trailer downcasts remain in `GeneratorCPP.rb` or any of the nine generated base messages.
- No message-tag downcasts remain in the common/generated crackers or their XSL generator. The 18 remaining generated `static_cast<FIX::Message&>` / `static_cast<const FIX::Message&>` expressions are valid derived-to-base upcasts in the preserved overloads.
- The three existing `static_cast<Group&>` expressions in `Message.h` (Header, Trailer, Message group getters), and the equivalent expression in `Group.cpp`, are safe: `FieldMap::getGroup(..., group)` assigns the stored base value into the caller's genuine Group and returns that same supplied object. It does not cast stored sliced group storage. Group and FieldMap forwarding implementations were also inspected.
- A separate complete scan of every header in all nine generated version directories found exactly the 18 safe upcasts described above and no other object casts (`build-security/task-14-vptr-all-generated-casts.txt`).
- Other matched casts in the reviewed implementation files are numeric conversions, not object downcasts. No object-layout, ownership, or exported out-of-line API change was needed.
- Sorted `nm -D --defined-only` strong `T/D/B` symbol names/types before and after the Release library rebuild compare byte-for-byte equal: **1,247 symbols**, `cmp` exit 0. Public inline/template instantiations remain a source rebuild requirement.

## Concerns

**DONE_WITH_CONCERNS:** the complete typed-object cast family is repaired and its focused sanitizer gates are clean, but the full registered sanitizer unit gate remains blocked by the independent `FD_SET(-1)` defect described above. The parent explicitly directed this task to keep that socket issue separate; no socket implementation or fixture was modified. That blocker must be repaired before Task 14 can claim a full sanitizer gate. Expected source migration and temporary typed-message copy costs are documented above.
