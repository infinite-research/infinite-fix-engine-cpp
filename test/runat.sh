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

"$BUILD_DIR/at" -f "$BUILD_DIR/cfg/at.cfg" >/dev/null 2>&1 &
ACCEPTOR_PID=$!

QUICKFIX_RUN_DIR=$(mktemp -d) || exit 1

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
  wait $PID || RESULT=$?
done
RUNNER_PIDS=

for OUT in "$QUICKFIX_RUN_DIR"/fix40.out "$QUICKFIX_RUN_DIR"/fix41.out "$QUICKFIX_RUN_DIR"/fix42.out \
           "$QUICKFIX_RUN_DIR"/fix43.out "$QUICKFIX_RUN_DIR"/fix44.out "$QUICKFIX_RUN_DIR"/fix50.out \
           "$QUICKFIX_RUN_DIR"/fix50sp1.out "$QUICKFIX_RUN_DIR"/fix50sp2.out "$QUICKFIX_RUN_DIR"/validate.out; do
  cat "$OUT"
done

exit $RESULT
