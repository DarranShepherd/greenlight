#!/usr/bin/env python3

import argparse
import json
import pathlib
import re
import subprocess
import sys


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate OTA release metadata.json for board-specific firmware artifacts."
    )
    parser.add_argument("--version", required=True, help="Release version without the leading tag prefix, for example 0.9.0")
    parser.add_argument("--tag", required=True, help="GitHub release tag, for example v0.9.0")
    parser.add_argument("--repository", required=True, help="GitHub owner/repo used in release asset URLs")
    parser.add_argument("--variant", action="append", required=True, metavar="BOARD_ID=PATH", help="Board ID and built firmware path")
    parser.add_argument("--output", required=True, help="Destination metadata.json path")
    return parser.parse_args()


def version_code_from_string(version: str) -> int:
    text = version[1:] if version.startswith("v") else version
    parts = text.split(".")
    if len(parts) > 3:
        raise ValueError(f"version has too many components: {version}")

    numbers = [int(part) for part in parts]
    while len(numbers) < 3:
        numbers.append(0)

    major, minor, patch = numbers[:3]
    return major * 10000 + minor * 100 + patch


def parse_variant_specs(specs: list[str]) -> list[tuple[str, pathlib.Path]]:
    variants: list[tuple[str, pathlib.Path]] = []
    for spec in specs:
        if "=" not in spec:
            raise ValueError(f"invalid --variant value: {spec}")
        board_id, firmware_path = spec.split("=", 1)
        board_id = board_id.strip()
        path = pathlib.Path(firmware_path).resolve()
        if not board_id:
            raise ValueError(f"invalid board ID in --variant value: {spec}")
        if not path.is_file():
            raise FileNotFoundError(f"firmware binary not found for {board_id}: {path}")
        variants.append((board_id, path))
    return variants


def read_image_validation_hash(firmware_path: pathlib.Path) -> str:
    result = subprocess.run(
        [sys.executable, "-m", "esptool", "image-info", str(firmware_path)],
        check=True,
        capture_output=True,
        text=True,
    )
    output = f"{result.stdout}\n{result.stderr}"
    match = re.search(r"Validation hash:\s*([0-9a-fA-F\s]+?)\s*\(valid\)", output, re.MULTILINE)
    if match is not None:
        digest = re.sub(r"\s+", "", match.group(1)).lower()
        if SHA256_RE.fullmatch(digest):
            return digest
        raise ValueError(f"invalid validation hash for {firmware_path}: {digest}")
    raise ValueError(f"could not find validation hash in esptool output for {firmware_path}")


def github_release_asset_url(repository: str, tag: str, asset_name: str) -> str:
    return f"https://github.com/{repository}/releases/download/{tag}/{asset_name}"


def main() -> int:
    args = parse_args()
    variants = parse_variant_specs(args.variant)
    variant_entries: dict[str, dict[str, str]] = {}

    for board_id, firmware_path in variants:
        asset_name = firmware_path.name
        variant_entries[board_id] = {
            "firmware_url": github_release_asset_url(args.repository, args.tag, asset_name),
            "sha256": read_image_validation_hash(firmware_path),
        }

    metadata = {
        "version": args.version,
        "version_code": version_code_from_string(args.version),
        "variants": variant_entries,
    }

    output_path = pathlib.Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())