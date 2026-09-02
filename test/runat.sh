#!/bin/sh

RUBY="ruby -I."
SCRIPT=$(realpath "$0")
DIR=$(dirname "$SCRIPT")
SOURCE_DIR=${QUICKFIX_TEST_SRCDIR:-$DIR}
BUILD_DIR=${QUICKFIX_TEST_BUILDDIR:-$DIR}
PORT=$1
ACCEPTOR_PID=
RUNNER_PIDS=
QUICKFIX_RUN_DIR=
ACCEPTOR_LOG=

port_is_free() {
  ruby -rsocket -e '
    begin
      server = TCPServer.new("0.0.0.0", Integer(ARGV.fetch(0)))
      server.close
    rescue ArgumentError, SystemCallError => error
      warn "Acceptance port #{ARGV[0]} is unavailable: #{error.message}"
      exit 1
    end
  ' "$1"
}

wait_for_acceptor() {
  ruby -rsocket -e '
    pid = Integer(ARGV.fetch(0))
    address = ARGV.fetch(1)
    port = Integer(ARGV.fetch(2))
    deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 5

    loop do
      begin
        Process.kill(0, pid)
      rescue Errno::ESRCH
        exit 1
      end

      begin
        socket = TCPSocket.new(address, port)
        socket.close
        Process.kill(0, pid)
        exit 0
      rescue Errno::ESRCH
        exit 1
      rescue SystemCallError
      end

      exit 1 if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
      sleep 0.02
    end
  ' "$ACCEPTOR_PID" 127.0.0.1 "$PORT"
}

show_acceptor_failure() {
  echo "Acceptance acceptor did not remain live" >&2
  if [ -n "$ACCEPTOR_LOG" ] && [ -f "$ACCEPTOR_LOG" ]; then
    cat "$ACCEPTOR_LOG" >&2
  fi
}

cleanup() {
  trap - EXIT INT TERM
  for PID in $RUNNER_PIDS; do
    kill "$PID" 2>/dev/null || :
    wait "$PID" 2>/dev/null || :
  done
  if [ -n "$ACCEPTOR_PID" ]; then
    kill "$ACCEPTOR_PID" 2>/dev/null || :
    wait "$ACCEPTOR_PID" 2>/dev/null || :
  fi
  if [ -n "$QUICKFIX_RUN_DIR" ]; then
    rm -rf "$QUICKFIX_RUN_DIR"
  fi
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cd "$BUILD_DIR" || exit 1
"$SOURCE_DIR/setup.sh" "$PORT" "$BUILD_DIR/cfg/at.cfg" "$SOURCE_DIR/../spec" || exit 1
port_is_free "$PORT" || exit 1

QUICKFIX_RUN_DIR=$(mktemp -d "$BUILD_DIR/quickfix-run.XXXXXX") || exit 1
ACCEPTOR_LOG="$QUICKFIX_RUN_DIR/acceptor.out"

"$BUILD_DIR/at" -f "$BUILD_DIR/cfg/at.cfg" >"$ACCEPTOR_LOG" 2>&1 &
ACCEPTOR_PID=$!
if ! wait_for_acceptor; then
  show_acceptor_failure
  exit 1
fi

# fix50/sp1/sp2 each have distinct FIXT.1.1 sessions — run in parallel
cd "$SOURCE_DIR" || exit 1
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix50/*.def    > "$QUICKFIX_RUN_DIR/fix50.out"    2>&1 & PID_FIX50=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_FIX50"
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix50sp1/*.def > "$QUICKFIX_RUN_DIR/fix50sp1.out" 2>&1 & PID_FIX50SP1=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_FIX50SP1"
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix50sp2/*.def > "$QUICKFIX_RUN_DIR/fix50sp2.out" 2>&1 & PID_FIX50SP2=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_FIX50SP2"

# all groups have distinct sessions — run fully in parallel
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix40/*.def    > "$QUICKFIX_RUN_DIR/fix40.out"    2>&1 & PID_FIX40=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_FIX40"
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix41/*.def    > "$QUICKFIX_RUN_DIR/fix41.out"    2>&1 & PID_FIX41=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_FIX41"
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix42/*.def    > "$QUICKFIX_RUN_DIR/fix42.out"    2>&1 & PID_FIX42=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_FIX42"
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix43/*.def    > "$QUICKFIX_RUN_DIR/fix43.out"    2>&1 & PID_FIX43=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_FIX43"
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/fix44/*.def    > "$QUICKFIX_RUN_DIR/fix44.out"    2>&1 & PID_FIX44=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_FIX44"
$RUBY Runner.rb 127.0.0.1 $PORT definitions/server/validate/*.def > "$QUICKFIX_RUN_DIR/validate.out" 2>&1 & PID_VAL=$!; RUNNER_PIDS="$RUNNER_PIDS $PID_VAL"

RESULT=0
for PID in $PID_FIX50 $PID_FIX50SP1 $PID_FIX50SP2 $PID_FIX40 $PID_FIX41 $PID_FIX42 $PID_FIX43 $PID_FIX44 $PID_VAL; do
  if wait "$PID"; then
    :
  else
    RUNNER_RESULT=$?
    if [ "$RESULT" -eq 0 ]; then
      RESULT=$RUNNER_RESULT
    fi
  fi
done
RUNNER_PIDS=

if ! kill -0 "$ACCEPTOR_PID" 2>/dev/null; then
  echo "Acceptance acceptor did not remain live" >&2
  if [ "$RESULT" -eq 0 ]; then
    RESULT=1
  fi
fi

if [ "$RESULT" -ne 0 ] && [ -f "$ACCEPTOR_LOG" ]; then
  cat "$ACCEPTOR_LOG" >&2
fi

for OUT in "$QUICKFIX_RUN_DIR"/fix40.out "$QUICKFIX_RUN_DIR"/fix41.out "$QUICKFIX_RUN_DIR"/fix42.out \
           "$QUICKFIX_RUN_DIR"/fix43.out "$QUICKFIX_RUN_DIR"/fix44.out "$QUICKFIX_RUN_DIR"/fix50.out \
           "$QUICKFIX_RUN_DIR"/fix50sp1.out "$QUICKFIX_RUN_DIR"/fix50sp2.out "$QUICKFIX_RUN_DIR"/validate.out; do
  cat "$OUT"
done

exit $RESULT
