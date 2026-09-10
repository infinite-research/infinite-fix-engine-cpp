#!/bin/sh

set -u

if [ "$#" -eq 0 ]; then
  SOURCE_ROOT=$(CDPATH= cd "${QUICKFIX_TEST_SRCDIR:?}/.." && pwd -P) || exit 1
  SCRATCH_ROOT=${QUICKFIX_TEST_BUILDDIR:?}
elif [ "$#" -eq 2 ]; then
  SOURCE_ROOT=$1
  SCRATCH_ROOT=$2
else
  echo "Usage: $0 [SOURCE_ROOT SCRATCH_ROOT]" >&2
  exit 2
fi
CASE_ROOT=$(mktemp -d "$SCRATCH_ROOT/performance-harness.XXXXXX") || exit 1
trap 'rm -rf "$CASE_ROOT"' EXIT INT TERM

SOURCE_DIR="$CASE_ROOT/source/test"
BUILD_DIR="$CASE_ROOT/build/test"
RUNTIME_DIR="$CASE_ROOT/runtime"
mkdir -p "$SOURCE_DIR" "$BUILD_DIR" "$RUNTIME_DIR" "$CASE_ROOT/source/spec"

mkdir -p "$CASE_ROOT/build/src/C++/test" "$SOURCE_DIR/cfg"
: >"$SOURCE_DIR/cfg/ut.cfg"
cat >"$CASE_ROOT/build/src/C++/test/ut" <<'SH'
#!/bin/sh

[ "$(ulimit -c)" = 0 ] || exit 90
printf 'cwd=%s\n' "$PWD" >"$QUICKFIX_FAKE_UT_LOG"
printf 'args=' >>"$QUICKFIX_FAKE_UT_LOG"
printf '<%s>' "$@" >>"$QUICKFIX_FAKE_UT_LOG"
printf '\n' >>"$QUICKFIX_FAKE_UT_LOG"
SH
chmod +x "$CASE_ROOT/build/src/C++/test/ut"

UT_LOG="$CASE_ROOT/ut.log"
if ! QUICKFIX_TEST_SRCDIR="$SOURCE_DIR" QUICKFIX_TEST_BUILDDIR="$BUILD_DIR" \
    QUICKFIX_TEST_RUNTIMEDIR="$RUNTIME_DIR" QUICKFIX_FAKE_UT_LOG="$UT_LOG" \
    sh "$SOURCE_ROOT/test/runut.sh" example-filter; then
  echo "runut did not disable core dumps" >&2
  exit 1
fi
[ "$(sed -n '1p' "$UT_LOG")" = "cwd=$RUNTIME_DIR" ] || {
  echo "runut did not use the configured runtime directory" >&2
  exit 1
}
grep -Fqx \
  "args=<--quickfix-config-file><$SOURCE_DIR/cfg/ut.cfg><--quickfix-spec-path><$CASE_ROOT/source/spec><example-filter>" \
  "$UT_LOG" || {
  echo "runut did not supply both absolute QuickFIX paths" >&2
  exit 1
}

cat >"$BUILD_DIR/pt" <<'SH'
#!/bin/sh

[ "$(ulimit -c)" = 0 ] || exit 90
printf 'cwd=%s\n' "$PWD" >>"$QUICKFIX_FAKE_PT_LOG"
printf 'args=' >>"$QUICKFIX_FAKE_PT_LOG"
printf '<%s>' "$@" >>"$QUICKFIX_FAKE_PT_LOG"
printf '\n' >>"$QUICKFIX_FAKE_PT_LOG"
case "$*" in
  *'~[network]'*) exit "${QUICKFIX_FAKE_PT_NONNETWORK_STATUS:-0}" ;;
  *'[network]'*) exit "${QUICKFIX_FAKE_PT_NETWORK_STATUS:-0}" ;;
esac
exit 91
SH
chmod +x "$BUILD_DIR/pt"

LOG="$CASE_ROOT/pt.log"
QUICKFIX_TEST_SRCDIR="$SOURCE_DIR" QUICKFIX_TEST_BUILDDIR="$BUILD_DIR" \
  QUICKFIX_TEST_RUNTIMEDIR="$RUNTIME_DIR" QUICKFIX_FAKE_PT_LOG="$LOG" \
  QUICKFIX_FAKE_PT_NONNETWORK_STATUS=23 QUICKFIX_FAKE_PT_NETWORK_STATUS=24 \
  ruby -e 'pid = Process.spawn(*ARGV, pgroup: true); Process.wait(pid); exit $?.exitstatus' \
    sh "$SOURCE_ROOT/test/runpt.sh" --port 54322 --benchmark-no-analysis
STATUS=$?

[ "$STATUS" -eq 23 ] || {
  echo "runpt returned $STATUS instead of the first failure 23" >&2
  exit 1
}
[ "$(grep -c '^cwd=' "$LOG")" -eq 2 ] || {
  echo "runpt did not run both benchmark lanes" >&2
  exit 1
}
[ "$(grep -c "^cwd=$RUNTIME_DIR$" "$LOG")" -eq 2 ] || {
  echo "runpt did not use the configured runtime directory" >&2
  exit 1
}
SPEC_DIR="$CASE_ROOT/source/spec"
grep -Fqx "args=<--quickfix-spec-path><$SPEC_DIR><-#><~[network]><--port><54322><--benchmark-no-analysis>" "$LOG" || {
  echo "runpt did not forward the nonnetwork arguments with an absolute spec path" >&2
  exit 1
}
grep -Fqx "args=<--quickfix-spec-path><$SPEC_DIR><-#><[network]><--port><54322><--benchmark-no-analysis>" "$LOG" || {
  echo "runpt did not forward the network arguments with an absolute spec path" >&2
  exit 1
}

echo "performance harness contract passed"
