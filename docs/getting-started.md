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

- `cyd_28_2432s028r`: original 2.8-inch CYD.
- `ipistbit_32_st7789`: iPistBit-branded 3.2-inch CYD.

If you are unsure which board you have, start with the screen size and branding. Use the 2.8-inch profile for the smaller original CYD boards, and use the iPistBit profile for the larger 3.2-inch boards sold as iPistBit units.

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
sh tools/validate.sh firmware cyd_28_2432s028r
sh tools/validate.sh firmware ipistbit_32_st7789
```

These commands emit firmware under `build-cyd_28_2432s028r/` and `build-ipistbit_32_st7789/` respectively.

For a first USB flash onto real hardware, build the matching variant and flash that image over serial before trying OTA updates. The first USB-installed image establishes the board-aware runtime identity used later by Settings when it selects `variants.<board_id>` from release `metadata.json`.

If you need the raw ESP-IDF invocations, they are:

```sh
idf.py -B build-cyd_28_2432s028r \
	-DSDKCONFIG=build-cyd_28_2432s028r/sdkconfig \
	-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board-cyd_28_2432s028r.defaults" \
	reconfigure build

idf.py -B build-ipistbit_32_st7789 \
	-DSDKCONFIG=build-ipistbit_32_st7789/sdkconfig \
	-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board-ipistbit_32_st7789.defaults" \
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
idf.py -B build-cyd_28_2432s028r -p /dev/ttyUSB0 flash monitor
idf.py -B build-ipistbit_32_st7789 -p /dev/ttyUSB0 flash monitor
```

Use the command that matches the board you identified earlier. If the wrong variant is flashed over USB, display and touch behavior may not match the hardware, and OTA will continue treating the device as that wrong compiled board until the correct image is installed over USB.

Host-only validation is sufficient for parser and refresh-logic regressions, but it does not cover the LVGL UI, peripherals, Wi-Fi, TLS, or serial flashing.

## Target Hardware

The repository currently supports two build-time board profiles:

- `cyd_28_2432s028r`: the original 2.8-inch CYD, and the default local build.
- `ipistbit_32_st7789`: the iPistBit 3.2-inch CYD.

Both profiles target an ESP32-based Cheap Yellow Display class board with:

- 240x320 touchscreen display
- LVGL 9 via `esp_lvgl_port`

Board selection now lives in [main/Kconfig.projbuild](../main/Kconfig.projbuild) and [main/board_profile.c](../main/board_profile.c). If you need to add another board later, add a new board profile and a matching `sdkconfig.board-*.defaults` overlay instead of changing the default developer build.

Tagged GitHub releases publish the corresponding board-specific OTA assets as `firmware-cyd28.bin` and `firmware-ipistbit32.bin`, plus a shared `metadata.json` manifest. Match the USB build you flash first to the release artifact family you expect that device to receive later through OTA.