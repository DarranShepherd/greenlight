# Contributing

Run the same validation flow locally that GitHub Actions enforces before opening a pull request.

If this is your first time in the repository, start with [docs/getting-started.md](docs/getting-started.md) for environment setup, package installation, common ESP-IDF commands, and the difference between host-only and real-hardware validation.

## Local Validation

Use an ESP-IDF 6.0 environment with the asset-generation dependencies available on `PATH`:

- `python3-cairosvg`
- `python3-pil`
- `nodejs`
- `npm`
- `lv_font_conv`
- `build-essential`
- `libcjson-dev`

Then run the repository validation entry point:

```sh
sh tools/validate.sh
```

See `NOTICE` for asset provenance, third-party font and certificate material, and Ampernomics trademark boundaries.

That command runs the host-side logic harnesses, regenerates checked-in assets, verifies that generated files are up to date, and builds the firmware.

If you only need one slice while iterating locally, the same script supports narrower modes:

```sh
sh tools/validate.sh host
sh tools/validate.sh firmware
sh tools/validate.sh firmware ipistbit_32_st7789
```

Firmware validation defaults to the original 2.8-inch CYD and writes board-specific outputs to `build-cyd_28_2432s028r/` or `build-ipistbit_32_st7789/`. CI builds both variants by calling the same entry point with each board key.

When a change can affect board overlays, display/touch setup, OTA metadata, or release packaging, validate both firmware variants locally instead of only the default build:

```sh
sh tools/validate.sh firmware cyd_28_2432s028r
sh tools/validate.sh firmware ipistbit_32_st7789
```

Those local outputs correspond to the release artifact names used by the tagged OTA workflow:

- `build-cyd_28_2432s028r/greenlight.bin` -> `firmware-cyd28.bin`
- `build-ipistbit_32_st7789/greenlight.bin` -> `firmware-ipistbit32.bin`

Contributors testing real hardware should also keep the first-flash rule in mind: blank devices and boards being recovered over USB must receive the correct board-specific image over serial before OTA is considered a safe maintenance path.

Use the repository's issue and pull request templates when opening public reports or contributions. For suspected vulnerabilities, follow [SECURITY.md](SECURITY.md) instead of filing a public issue.