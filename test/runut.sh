#!/bin/sh

SCRIPT=$(realpath "$0")
DIR=$(dirname "$SCRIPT")
SOURCE_DIR=${QUICKFIX_TEST_SRCDIR:-$DIR}
BUILD_DIR=${QUICKFIX_TEST_BUILDDIR:-$DIR}

cd "$BUILD_DIR" || exit 1
"$BUILD_DIR/../src/C++/test/ut" --quickfix-config-file "$SOURCE_DIR/cfg/ut.cfg" --quickfix-spec-path "$SOURCE_DIR/../spec" "$@"
RESULT=$?
exit $RESULT
