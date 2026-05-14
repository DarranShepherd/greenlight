# Contributing

Keep validation minimal for now. The repository currently expects contributors to run the same two checks that GitHub Actions runs, and it intentionally does not require formatting or linting yet.

## Local Validation

Run the firmware build from an ESP-IDF 6.0 environment:

```sh
idf.py build
```

Run the host-side logic harnesses from a Debian or Ubuntu environment with `build-essential` and `libcjson-dev` installed:

```sh
sh tools/run_host_tests.sh
```

If you touch both firmware code and pure logic modules, run both commands before opening a pull request.

Use the repository's issue and pull request templates when opening public reports or contributions. For suspected vulnerabilities, follow [SECURITY.md](SECURITY.md) instead of filing a public issue.