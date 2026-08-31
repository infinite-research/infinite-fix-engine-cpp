#!/bin/sh

set -u

SOURCE_ROOT=${1:?source root is required}
SCRATCH_ROOT=${2:?scratch root is required}
CASE_ROOT=$(mktemp -d "$SCRATCH_ROOT/acceptance-harness.XXXXXX") || exit 1
PIDS=
FAILURES=0

cleanup() {
  trap - EXIT INT TERM
  for PID in $PIDS; do
    kill "$PID" 2>/dev/null || :
    wait "$PID" 2>/dev/null || :
  done
  rm -rf "$CASE_ROOT"
}

fail() {
  echo "FAIL: $1" >&2
  FAILURES=$((FAILURES + 1))
}

run_runner() {
  (cd "$SOURCE_ROOT/test" && ruby Runner.rb "$@")
}

wait_for_file() {
  FILE=$1
  PID=$2
  COUNT=0
  while [ ! -s "$FILE" ] && [ "$COUNT" -lt 200 ]; do
    kill -0 "$PID" 2>/dev/null || return 1
    ruby -e 'sleep 0.01'
    COUNT=$((COUNT + 1))
  done
  [ -s "$FILE" ]
}

start_listener() {
  READY_FILE=$1
  ruby "$CASE_ROOT/listener.rb" 0 "$READY_FILE" &
  LAST_PID=$!
  PIDS="$PIDS $LAST_PID"
  wait_for_file "$READY_FILE" "$LAST_PID" || return 1
  LAST_PORT=$(cat "$READY_FILE")
}

run_bounded() {
  ruby "$CASE_ROOT/bounded.rb" "$@"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cat > "$CASE_ROOT/listener.rb" <<'RUBY'
require "socket"

server = TCPServer.new("127.0.0.1", Integer(ARGV.fetch(0)))
File.write(ARGV.fetch(1), server.addr[1].to_s)
trap("TERM") { exit }
loop do
  socket = server.accept
  socket.close
end
RUBY

cat > "$CASE_ROOT/bounded.rb" <<'RUBY'
deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 5
pid = Process.spawn(*ARGV, pgroup: true)
loop do
  waited = Process.waitpid2(pid, Process::WNOHANG)
  exit waited[1].exitstatus if waited
  break if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
  sleep 0.02
end
Process.kill("TERM", -pid) rescue nil
sleep 0.1
Process.kill("KILL", -pid) rescue nil
Process.wait(pid) rescue nil
exit 124
RUBY

if run_runner 127.0.0.1 1 >"$CASE_ROOT/runner-zero.out" 2>&1; then
  fail "Runner accepted zero definition files"
fi

if run_runner 127.0.0.1 1 "$CASE_ROOT/missing.def" >"$CASE_ROOT/runner-missing.out" 2>&1; then
  fail "Runner accepted a missing definition file"
fi

start_listener "$CASE_ROOT/runner-listener.port" || exit 1
mkdir "$CASE_ROOT/directory.def"
if run_runner 127.0.0.1 "$LAST_PORT" "$CASE_ROOT/directory.def" >"$CASE_ROOT/runner-directory.out" 2>&1; then
  fail "Runner accepted a readable directory as a definition"
fi

printf 'not a definition\n' > "$CASE_ROOT/malformed.def"
if run_runner 127.0.0.1 "$LAST_PORT" "$CASE_ROOT/malformed.def" >"$CASE_ROOT/runner-malformed.out" 2>&1; then
  fail "Runner accepted a malformed definition"
fi

printf 'iCONNECT\niDISCONNECT\n' > "$CASE_ROOT/minimal.def"
if ! run_runner 127.0.0.1 "$LAST_PORT" "$CASE_ROOT/minimal.def" >"$CASE_ROOT/runner-valid.out" 2>&1; then
  fail "Runner rejected a minimal valid definition"
fi

FAKE_SOURCE="$CASE_ROOT/source"
FAKE_BUILD="$CASE_ROOT/build"
mkdir -p "$FAKE_SOURCE" "$FAKE_BUILD/cfg"
for GROUP in fix40 fix41 fix42 fix43 fix44 fix50 fix50sp1 fix50sp2 validate; do
  mkdir -p "$FAKE_SOURCE/definitions/server/$GROUP"
  printf 'iCONNECT\n' > "$FAKE_SOURCE/definitions/server/$GROUP/test.def"
done

cat > "$FAKE_SOURCE/setup.sh" <<'SH'
#!/bin/sh
mkdir -p "$(dirname "$2")"
printf '[DEFAULT]\nSocketAcceptPort=%s\n' "$1" > "$2"
SH

cat > "$FAKE_SOURCE/Runner.rb" <<'RUBY'
require "socket"

exit 0 if ENV["FAKE_RUNNER_MODE"] == "success"
socket = TCPSocket.new(ARGV.fetch(0), Integer(ARGV.fetch(1)))
socket.close
RUBY

cat > "$FAKE_BUILD/at" <<'RUBY'
#!/usr/bin/env ruby
require "socket"

if ENV["FAKE_ACCEPTOR_MODE"] == "die"
  warn "fake acceptor died before readiness"
  exit 23
end

config = ARGV.fetch(ARGV.index("-f") + 1)
port = Integer(File.read(config)[/^SocketAcceptPort=(\d+)$/, 1])
server = TCPServer.new("127.0.0.1", port)
File.write(ENV.fetch("FAKE_ACCEPTOR_PID_FILE"), Process.pid.to_s)
trap("TERM") { exit }
if ENV["FAKE_ACCEPTOR_MODE"] == "ready_then_die"
  socket = server.accept
  socket.close
  warn "fake acceptor died after readiness"
  exit 24
end
loop do
  socket = server.accept
  socket.close
end
RUBY
chmod +x "$FAKE_SOURCE/setup.sh" "$FAKE_BUILD/at"

start_listener "$CASE_ROOT/occupied.port" || exit 1
OCCUPIED_PID=$LAST_PID
if QUICKFIX_TEST_SRCDIR="$FAKE_SOURCE" QUICKFIX_TEST_BUILDDIR="$FAKE_BUILD" \
    FAKE_ACCEPTOR_MODE=live FAKE_ACCEPTOR_PID_FILE="$CASE_ROOT/occupied-at.pid" \
    run_bounded "$SOURCE_ROOT/test/runat.sh" "$LAST_PORT" >"$CASE_ROOT/occupied.out" 2>&1; then
  fail "runat accepted an independently occupied port"
fi
kill -0 "$OCCUPIED_PID" 2>/dev/null || fail "runat killed the unrelated port owner"

FREE_PORT=$(ruby -rsocket -e 's = TCPServer.new("127.0.0.1", 0); puts s.addr[1]; s.close')
if QUICKFIX_TEST_SRCDIR="$FAKE_SOURCE" QUICKFIX_TEST_BUILDDIR="$FAKE_BUILD" \
    FAKE_ACCEPTOR_MODE=die FAKE_ACCEPTOR_PID_FILE="$CASE_ROOT/dying-at.pid" \
    run_bounded "$SOURCE_ROOT/test/runat.sh" "$FREE_PORT" >"$CASE_ROOT/dying.out" 2>&1; then
  fail "runat accepted an acceptor that died before readiness"
fi
grep -q 'fake acceptor died before readiness' "$CASE_ROOT/dying.out" || \
  fail "runat hid the failed acceptor diagnostic"

FREE_PORT=$(ruby -rsocket -e 's = TCPServer.new("127.0.0.1", 0); puts s.addr[1]; s.close')
if QUICKFIX_TEST_SRCDIR="$FAKE_SOURCE" QUICKFIX_TEST_BUILDDIR="$FAKE_BUILD" \
    FAKE_ACCEPTOR_MODE=ready_then_die FAKE_RUNNER_MODE=success \
    FAKE_ACCEPTOR_PID_FILE="$CASE_ROOT/late-dying-at.pid" \
    run_bounded "$SOURCE_ROOT/test/runat.sh" "$FREE_PORT" >"$CASE_ROOT/late-dying.out" 2>&1; then
  fail "runat accepted an acceptor that died after readiness"
fi
grep -q 'fake acceptor died after readiness' "$CASE_ROOT/late-dying.out" || \
  fail "runat hid the post-readiness acceptor diagnostic"

start_listener "$CASE_ROOT/sentinel.port" || exit 1
SENTINEL_PID=$LAST_PID
FREE_PORT=$(ruby -rsocket -e 's = TCPServer.new("127.0.0.1", 0); puts s.addr[1]; s.close')
if ! QUICKFIX_TEST_SRCDIR="$FAKE_SOURCE" QUICKFIX_TEST_BUILDDIR="$FAKE_BUILD" \
    FAKE_ACCEPTOR_MODE=live FAKE_ACCEPTOR_PID_FILE="$CASE_ROOT/live-at.pid" \
    run_bounded "$SOURCE_ROOT/test/runat.sh" "$FREE_PORT" >"$CASE_ROOT/live.out" 2>&1; then
  fail "runat rejected a live owned acceptor"
fi
kill -0 "$SENTINEL_PID" 2>/dev/null || fail "runat killed an unrelated process"
if [ ! -s "$CASE_ROOT/live-at.pid" ]; then
  fail "live acceptor did not record its PID"
elif kill -0 "$(cat "$CASE_ROOT/live-at.pid")" 2>/dev/null; then
  fail "runat left its owned acceptor running"
fi

if [ "$FAILURES" -ne 0 ]; then
  echo "$FAILURES acceptance harness contract checks failed" >&2
  exit 1
fi

echo "acceptance harness contract passed"
