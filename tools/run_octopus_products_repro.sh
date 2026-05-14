#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/.host-test-bin"

mkdir -p "$BUILD_DIR"

cc -std=c11 -Wall -Wextra -Werror \
    -I"$ROOT_DIR/tools/host_stubs" \
    -I"$ROOT_DIR/main" \
    "$ROOT_DIR/tools/repro_octopus_products.c" \
    -o "$BUILD_DIR/repro_octopus_products" \
    $(pkg-config --cflags --libs libcurl libcjson)

"$BUILD_DIR/repro_octopus_products"