# Getting Started

Use this guide for first-time local setup. Keep [CONTRIBUTING.md](../CONTRIBUTING.md) as the policy and PR-validation reference.

## Supported Setup Paths

Greenlight targets ESP-IDF 6.0.

- Dev container: open the repository in the checked-in `.devcontainer`. It is based on `espressif/idf:v6.0` and makes `idf.py` available automatically.
- Native Linux setup: install ESP-IDF 6.0 locally and make sure `idf.py` is on `PATH` before running the commands below.

## Required Packages

Install the packages needed by the current validation scripts.

For host-only validation:

```sh
sudo apt-get update
sudo apt-get install --yes build-essential libcjson-dev
```

For asset generation and firmware validation:

```sh
sudo apt-get update
sudo apt-get install --yes nodejs npm python3-cairosvg python3-pil
sudo npm install --global lv_font_conv
```

If you are using the dev container, install these packages inside the container before running `sh tools/validate.sh`.

## Common ESP-IDF Commands

Run these from the repository root.

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

If `idf.py -p /dev/ttyUSB0 monitor` fails immediately after flashing, wait for the serial device to re-enumerate and retry.

The plain `idf.py build` flow remains pinned to the default 2.8-inch CYD profile.

Before you flash hardware, choose the board profile that matches the physical unit in front of you:

- `esp32_2432s028_ili9341`: 2.8-inch `ESP32-2432S028` board with ILI9341 LCD.
- `esp32_2432s028_st7789`: 2.8-inch `ESP32-2432S028` board with ST7789 LCD.
- `esp32_32e_st7789`: 3.2-inch `ESP32-32E` board with ST7789 LCD.

If you are unsure which board you have, start with the silkscreen text and LCD behaviour. Use the `2432S028` ILI9341 profile for the Guition-style 2.8-inch board, use the `2432S028` ST7789 profile for the near-identical 2.8-inch board that corrupts with the ILI9341 build, and use the `32E 240x320` profile for the distinct 3.2-inch board family.

## Validation Paths

Use the same script entry points that CI uses.

Host-only validation, no board required:

```sh
sh tools/validate.sh host
```

This compiles and runs the host-side logic tests from `tools/run_host_tests.sh`.

Full local validation before a PR:

```sh
sh tools/validate.sh
```

This runs the host tests, regenerates checked-in assets, verifies that generated files are current, and then runs `idf.py build`.

Firmware-only validation when you are iterating on ESP-IDF code or generated assets:

```sh
sh tools/validate.sh firmware
```

Board-specific firmware validation builds each variant into its own output directory without mutating the default workflow:

```sh
sh tools/validate.sh firmware esp32_2432s028_ili9341
sh tools/validate.sh firmware esp32_2432s028_st7789
sh tools/validate.sh firmware esp32_32e_st7789
```

These commands emit firmware under `build-esp32_2432s028_ili9341/`, `build-esp32_2432s028_st7789/`, and `build-esp32_32e_st7789/` respectively.

For a first USB flash onto real hardware, build the matching variant and flash that image over serial before trying OTA updates. The first USB-installed image establishes the board-aware runtime identity used later by Settings when it selects `variants.<board_id>` from release `metadata.json`.

If you need the raw ESP-IDF invocations, they are:

```sh
idf.py -B build-esp32_2432s028_ili9341 \
	-DSDKCONFIG=build-esp32_2432s028_ili9341/sdkconfig \
	-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board-esp32_2432s028_ili9341.defaults" \
	reconfigure build

idf.py -B build-esp32_2432s028_st7789 \
	-DSDKCONFIG=build-esp32_2432s028_st7789/sdkconfig \
	-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board-esp32_2432s028_st7789.defaults" \
	reconfigure build

idf.py -B build-esp32_32e_st7789 \
	-DSDKCONFIG=build-esp32_32e_st7789/sdkconfig \
	-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board-esp32_32e_st7789.defaults" \
	reconfigure build
```

## Generated Assets

Some firmware assets are generated and checked into the repository.

Regenerate them from the repository root with:

```sh
python3 tools/generate_assets.py
```

This refreshes the embedded splash asset and the custom LVGL subset fonts. CI also regenerates these assets during firmware validation and fails if the committed generated files are stale.

## Debug Build

For a local debug-oriented build with assertions, the perf monitor, and the runtime GDB stub enabled:

```sh
SDKCONFIG=sdkconfig.debug \
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" \
idf.py build
```

## Documentation Screenshots

The public README screenshots are checked into [docs/img](../docs/img). To regenerate documentation captures from hardware, use the dedicated docs screenshot profile instead of the normal firmware build.

Install the extra host dependency first:

```sh
python3 -m pip install pyserial
```

Then build and flash the docs profile:

```sh
idf.py -B build-docs \
	-DSDKCONFIG=sdkconfig.docs-screenshots \
	-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.docs-screenshots.defaults" \
	build flash
```

Capture screenshots from the board:

```sh
python3 tools/capture_doc_screenshots.py --port /dev/ttyUSB0 --reset-on-connect
```

This flow keeps the normal firmware build untouched. It uses a separate `build-docs/` directory, boots into a deterministic documentation mode, renders curated UI states, and writes captured PNGs to `docs/generated/screenshots/` by default.

## Real Hardware Validation

Use real hardware when you need to validate board-specific behavior such as display output, touch input, Wi-Fi onboarding, time sync, or live Octopus API traffic.

Typical flow:

```sh
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
idf.py -p /dev/ttyUSB0 flash monitor
```

For first-time flashing, prefer the explicit board-specific build directory instead of the default `build/` output so you know exactly which image is going onto the device:

```sh
idf.py -B build-esp32_2432s028_ili9341 -p /dev/ttyUSB0 flash monitor
idf.py -B build-esp32_2432s028_st7789 -p /dev/ttyUSB0 flash monitor
idf.py -B build-esp32_32e_st7789 -p /dev/ttyUSB0 flash monitor
```

Use the command that matches the board you identified earlier. If the wrong variant is flashed over USB, display and touch behavior may not match the hardware, and OTA will continue treating the device as that wrong compiled board until the correct image is installed over USB.

Host-only validation is sufficient for parser and refresh-logic regressions, but it does not cover the LVGL UI, peripherals, Wi-Fi, TLS, or serial flashing.

## Target Hardware

The repository currently supports three build-time board profiles:

- `esp32_2432s028_ili9341`: the original 2.8-inch `ESP32-2432S028` board, and the default local build.
- `esp32_2432s028_st7789`: a 2.8-inch `ESP32-2432S028` board with an ST7789 LCD.
- `esp32_32e_st7789`: the 3.2-inch `ESP32-32E` board.

Both profiles target an ESP32-based Cheap Yellow Display class board with:

- 240x320 touchscreen display
- LVGL 9 via `esp_lvgl_port`

Board selection now lives in [main/Kconfig.projbuild](../main/Kconfig.projbuild) and [main/board_profile.c](../main/board_profile.c). If you need to add another board later, add a new board profile and a matching `sdkconfig.board-*.defaults` overlay instead of changing the default developer build.

Tagged GitHub releases publish the corresponding board-specific OTA assets as `firmware-esp32_2432s028_ili9341.bin`, `firmware-esp32_2432s028_st7789.bin`, and `firmware-esp32_32e_st7789.bin`, plus a shared `metadata.json` manifest. Match the USB build you flash first to the release artifact family you expect that device to receive later through OTA.