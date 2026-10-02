#!/bin/sh

set -u
ulimit -c 0 || exit 1

DIR=$(CDPATH= cd "$(dirname "$0")" && pwd -P) || exit 1
SOURCE_DIR=${QUICKFIX_TEST_SRCDIR:-$DIR}
BUILD_DIR=${QUICKFIX_TEST_BUILDDIR:-$DIR}
RUNTIME_DIR=${QUICKFIX_TEST_RUNTIMEDIR:-$BUILD_DIR/../test-runtime}
SOURCE_DIR=$(CDPATH= cd "$SOURCE_DIR" && pwd -P) || exit 1
BUILD_DIR=$(CDPATH= cd "$BUILD_DIR" && pwd -P) || exit 1
SPEC_DIR=$(cd "$SOURCE_DIR/../spec" && pwd -P) || exit 1

mkdir -p "$RUNTIME_DIR" || exit 1
RUNTIME_DIR=$(CDPATH= cd "$RUNTIME_DIR" && pwd -P) || exit 1
cd "$RUNTIME_DIR" || exit 1

RESULT=0
"$BUILD_DIR/pt" --quickfix-spec-path "$SPEC_DIR" -# "~[network]" "$@" || RESULT=$?
"$BUILD_DIR/pt" --quickfix-spec-path "$SPEC_DIR" -# "[network]" "$@" || {
  LANE_RESULT=$?
  if [ "$RESULT" -eq 0 ]; then
    RESULT=$LANE_RESULT
  fi
}
exit "$RESULT"
