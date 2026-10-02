#!/bin/sh

set -u
ulimit -c 0 || exit 1

DIR=$(CDPATH= cd "$(dirname "$0")" && pwd -P) || exit 1
SOURCE_DIR=${QUICKFIX_TEST_SRCDIR:-$DIR}
BUILD_DIR=${QUICKFIX_TEST_BUILDDIR:-$DIR}
RUNTIME_DIR=${QUICKFIX_TEST_RUNTIMEDIR:-$BUILD_DIR}
SOURCE_DIR=$(CDPATH= cd "$SOURCE_DIR" && pwd -P) || exit 1
BUILD_DIR=$(CDPATH= cd "$BUILD_DIR" && pwd -P) || exit 1
CONFIG_FILE=$SOURCE_DIR/cfg/ut.cfg
SPEC_DIR=$(CDPATH= cd "$SOURCE_DIR/../spec" && pwd -P) || exit 1
[ -f "$CONFIG_FILE" ] || exit 1
UNIT=${QUICKFIX_TEST_UT:-}
if [ -z "$UNIT" ]; then
  if [ -x "$BUILD_DIR/ut" ]; then
    UNIT=$BUILD_DIR/ut
  else
    UNIT=$BUILD_DIR/../src/C++/test/ut
  fi
fi

mkdir -p "$RUNTIME_DIR" || exit 1
RUNTIME_DIR=$(CDPATH= cd "$RUNTIME_DIR" && pwd -P) || exit 1
cd "$RUNTIME_DIR" || exit 1
exec "$UNIT" --quickfix-config-file "$CONFIG_FILE" --quickfix-spec-path "$SPEC_DIR" "$@"
