#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
FONT_PATH="$ROOT_DIR/managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"

usage() {
    cat <<'EOF'
Usage: sh tools/validate.sh [all|host|firmware]

  all       Run the full local validation flow.
  host      Run host-side test harnesses.
  firmware  Regenerate checked-in assets, verify they are up to date, and build firmware.
EOF
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

ensure_font_source() {
    if [ -f "$FONT_PATH" ]; then
        return
    fi

    echo "==> Populating managed components"
    require_command idf.py
    idf.py -C "$ROOT_DIR" reconfigure
}

run_host() {
    echo "==> Running host-side tests"
    sh "$ROOT_DIR/tools/run_host_tests.sh"
}

run_firmware() {
    echo "==> Regenerating generated assets"
    require_command python3
    ensure_font_source
    python3 "$ROOT_DIR/tools/generate_assets.py"

    echo "==> Verifying generated assets are up to date"
    git -C "$ROOT_DIR" diff --exit-code -- \
        main/lv_font_montserrat_20_numeric.c \
        main/lv_font_montserrat_28_numeric.c \
        main/splash_logo.c \
        main/splash_logo.h

    echo "==> Building firmware"
    require_command idf.py
    idf.py -C "$ROOT_DIR" build
}

mode=${1:-all}

case "$mode" in
    all)
        run_host
        run_firmware
        ;;
    host)
        run_host
        ;;
    firmware)
        run_firmware
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac