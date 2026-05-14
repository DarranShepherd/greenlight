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

## Real Hardware Validation

Use real hardware when you need to validate board-specific behavior such as display output, touch input, Wi-Fi onboarding, time sync, or live Octopus API traffic.

Typical flow:

```sh
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
idf.py -p /dev/ttyUSB0 flash monitor
```

Host-only validation is sufficient for parser and refresh-logic regressions, but it does not cover the LVGL UI, peripherals, Wi-Fi, TLS, or serial flashing.