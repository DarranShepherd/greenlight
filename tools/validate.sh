#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
FONT_PATH="$ROOT_DIR/managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"

DEFAULT_BOARD=cyd_28_2432s028r

usage() {
        cat <<'EOF'
Usage: sh tools/validate.sh [all|host|firmware] [board]

    all       Run the full local validation flow for the default board.
    host      Run host-side test harnesses.
    firmware  Regenerate checked-in assets, verify they are up to date, and build firmware.

Boards:
    cyd_28_2432s028r     Default 2.8-inch CYD build.
    ipistbit_32_st7789   iPistBit 3.2-inch CYD build.
EOF
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

ensure_git_safe_directory() {
    require_command git

    git_probe_output=$(git -C "$ROOT_DIR" rev-parse --show-toplevel 2>&1) && return

    case "$git_probe_output" in
        *"detected dubious ownership in repository at"*)
            git config --global --add safe.directory "$ROOT_DIR"
            git -C "$ROOT_DIR" rev-parse --show-toplevel >/dev/null
            ;;
        *)
            printf '%s\n' "$git_probe_output" >&2
            return 1
            ;;
    esac
}

ensure_font_source() {
    if [ -f "$FONT_PATH" ]; then
        return
    fi

    echo "==> Populating managed components"
    require_command idf.py
    idf.py -C "$ROOT_DIR" reconfigure
}

resolve_board() {
    board=${1:-$DEFAULT_BOARD}

    case "$board" in
        cyd_28_2432s028r)
            BOARD_LABEL="CYD 2.8 2432S028R"
            BOARD_BUILD_DIR="$ROOT_DIR/build-cyd_28_2432s028r"
            BOARD_DEFAULTS_REL="sdkconfig.board-cyd_28_2432s028r.defaults"
            ;;
        ipistbit_32_st7789)
            BOARD_LABEL="iPistBit 3.2 ST7789"
            BOARD_BUILD_DIR="$ROOT_DIR/build-ipistbit_32_st7789"
            BOARD_DEFAULTS_REL="sdkconfig.board-ipistbit_32_st7789.defaults"
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown board: $board" >&2
            usage >&2
            exit 1
            ;;
    esac

    BOARD_SDKCONFIG="$BOARD_BUILD_DIR/sdkconfig"
    BOARD_DEFAULTS="sdkconfig.defaults;$BOARD_DEFAULTS_REL"
}

run_host() {
    echo "==> Running host-side tests"
    sh "$ROOT_DIR/tools/run_host_tests.sh"
}

run_firmware() {
    resolve_board "$1"
    ensure_git_safe_directory

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

    echo "==> Building firmware for $BOARD_LABEL"
    require_command idf.py

    rm -f "$BOARD_SDKCONFIG" "$BOARD_SDKCONFIG.old"
    if [ -n "${PROJECT_VER:-}" ]; then
        idf.py -C "$ROOT_DIR" \
            -B "$BOARD_BUILD_DIR" \
            -DPROJECT_VER="$PROJECT_VER" \
            -DSDKCONFIG="$BOARD_SDKCONFIG" \
            -DSDKCONFIG_DEFAULTS="$BOARD_DEFAULTS" \
            reconfigure build
    else
        idf.py -C "$ROOT_DIR" \
            -B "$BOARD_BUILD_DIR" \
            -DSDKCONFIG="$BOARD_SDKCONFIG" \
            -DSDKCONFIG_DEFAULTS="$BOARD_DEFAULTS" \
            reconfigure build
    fi
}

mode=${1:-all}
board=${2:-$DEFAULT_BOARD}

case "$mode" in
    all)
        run_host
        run_firmware "$board"
        ;;
    host)
        run_host
        ;;
    firmware)
        run_firmware "$board"
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac