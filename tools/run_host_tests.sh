#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/.host-test-bin"

mkdir -p "$BUILD_DIR"

cc -std=c11 -Wall -Wextra -Werror \
    -include "$ROOT_DIR/tools/host_stubs/host_test_compat.h" \
    -I/usr/include/cjson \
    -I"$ROOT_DIR/tools/host_stubs" \
    -I"$ROOT_DIR/main" \
    "$ROOT_DIR/tools/test_octopus_client.c" \
    "$ROOT_DIR/main/octopus_client_internal.c" \
    -lcjson \
    -o "$BUILD_DIR/test_octopus_client"

cc -std=c11 -Wall -Wextra -Werror \
    -include "$ROOT_DIR/tools/host_stubs/host_test_compat.h" \
    -I/usr/include/cjson \
    -I"$ROOT_DIR/tools/host_stubs" \
    -I"$ROOT_DIR/main" \
    "$ROOT_DIR/tools/test_ota_manager_internal.c" \
    "$ROOT_DIR/main/ota_manager_internal.c" \
    -lcjson \
    -o "$BUILD_DIR/test_ota_manager_internal"

cc -std=c11 -Wall -Wextra -Werror -DGREENLIGHT_HOST_TEST \
    -include "$ROOT_DIR/tools/host_stubs/host_test_compat.h" \
    -I"$ROOT_DIR/tools/host_stubs" \
    -I"$ROOT_DIR/main" \
    "$ROOT_DIR/tools/test_sync_controller_refresh.c" \
    "$ROOT_DIR/main/app_state.c" \
    "$ROOT_DIR/main/tariff_model.c" \
    "$ROOT_DIR/main/sync_controller.c" \
    -o "$BUILD_DIR/test_sync_controller_refresh"

"$BUILD_DIR/test_octopus_client"
"$BUILD_DIR/test_ota_manager_internal"
"$BUILD_DIR/test_sync_controller_refresh"