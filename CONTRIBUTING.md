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
```

Use the repository's issue and pull request templates when opening public reports or contributions. For suspected vulnerabilities, follow [SECURITY.md](SECURITY.md) instead of filing a public issue.