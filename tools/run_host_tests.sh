#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/.host-test-bin"

mkdir -p "$BUILD_DIR"

cc -std=c11 -Wall -Wextra -Werror \
    -I/usr/include/cjson \
    -I"$ROOT_DIR/tools/host_stubs" \
    -I"$ROOT_DIR/main" \
    "$ROOT_DIR/tools/test_octopus_client.c" \
    -lcjson \
    -o "$BUILD_DIR/test_octopus_client"

cc -std=c11 -Wall -Wextra -Werror \
    -I"$ROOT_DIR/tools/host_stubs" \
    -I"$ROOT_DIR/main" \
    "$ROOT_DIR/tools/test_sync_controller_refresh.c" \
    -o "$BUILD_DIR/test_sync_controller_refresh"

"$BUILD_DIR/test_octopus_client"
"$BUILD_DIR/test_sync_controller_refresh"