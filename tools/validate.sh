#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
FONT_PATH="$ROOT_DIR/managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"

DEFAULT_BOARD=esp32_2432s028_ili9341

usage() {
        cat <<'EOF'
Usage: sh tools/validate.sh [all|host|firmware] [board]

    all       Run the full local validation flow for the default board.
    host      Run host-side test harnesses.
    firmware  Regenerate checked-in assets, verify they are up to date, and build firmware.

Boards:
    esp32_2432s028_ili9341      ESP32-2432S028 2.8-inch board with ILI9341 LCD.
    esp32_2432s028_st7789       ESP32-2432S028 2.8-inch board with ST7789 LCD.
    esp32_32e_st7789            ESP32-32E 3.2-inch board with ST7789 LCD.
    jc4827w543                  JC4827W543 4.3-inch board with experimental support.
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
        esp32_2432s028_ili9341)
            BOARD_LABEL="ESP32-2432S028 / ILI9341"
            BOARD_TARGET="esp32"
            BOARD_BUILD_DIR="$ROOT_DIR/build-esp32_2432s028_ili9341"
            BOARD_DEFAULTS_REL="sdkconfig.board-esp32_2432s028_ili9341.defaults"
            ;;
        esp32_2432s028_st7789)
            BOARD_LABEL="ESP32-2432S028 / ST7789"
            BOARD_TARGET="esp32"
            BOARD_BUILD_DIR="$ROOT_DIR/build-esp32_2432s028_st7789"
            BOARD_DEFAULTS_REL="sdkconfig.board-esp32_2432s028_st7789.defaults"
            ;;
        esp32_32e_st7789)
            BOARD_LABEL="ESP32-32E / ST7789"
            BOARD_TARGET="esp32"
            BOARD_BUILD_DIR="$ROOT_DIR/build-esp32_32e_st7789"
            BOARD_DEFAULTS_REL="sdkconfig.board-esp32_32e_st7789.defaults"
            ;;
        jc4827w543)
            BOARD_LABEL="JC4827W543 / NV3041A / GT911"
            BOARD_TARGET="esp32s3"
            BOARD_BUILD_DIR="$ROOT_DIR/build-jc4827w543"
            BOARD_DEFAULTS_REL="sdkconfig.board-jc4827w543.defaults"
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
    idf.py -C "$ROOT_DIR" \
        -B "$BOARD_BUILD_DIR" \
        -DSDKCONFIG="$BOARD_SDKCONFIG" \
        -DSDKCONFIG_DEFAULTS="$BOARD_DEFAULTS" \
        set-target "$BOARD_TARGET"

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