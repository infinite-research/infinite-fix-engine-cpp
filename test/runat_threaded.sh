#!/bin/sh

DIR=$(CDPATH= cd "$(dirname "$0")" && pwd -P) || exit 1
exec "$DIR/runat.sh" threaded "$@"
