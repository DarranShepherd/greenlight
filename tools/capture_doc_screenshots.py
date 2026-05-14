#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import binascii
import pathlib
import re
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError as exc:  # pragma: no cover - dependency error path
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


BEGIN_RE = re.compile(r"^GLSHOT BEGIN (?P<name>[A-Za-z0-9._-]+) (?P<width>\d+) (?P<height>\d+) (?P<format>[A-Za-z0-9_]+) (?P<stride>\d+) (?P<size>\d+)$")
DATA_RE = re.compile(r"^GLSHOT DATA (?P<offset>\d+) (?P<data>[A-Za-z0-9+/=]+)$")
END_RE = re.compile(r"^GLSHOT END (?P<name>[A-Za-z0-9._-]+)$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Capture Greenlight documentation screenshots over serial.")
    parser.add_argument("--port", required=True, help="Serial device path, for example /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--output-dir", default="docs/generated/screenshots", help="Directory for generated PNG files")
    parser.add_argument("--expect", type=int, default=4, help="Expected number of screenshots before exiting")
    parser.add_argument("--idle-timeout", type=float, default=3.0, help="Stop after this many idle seconds once at least one screenshot is captured")
    parser.add_argument("--startup-timeout", type=float, default=30.0, help="Fail if no screenshot header arrives within this many seconds")
    parser.add_argument("--reset-on-connect", action="store_true", help="Reset the ESP32 after opening the serial port")
    return parser.parse_args()


def reset_esp32(ser: serial.Serial) -> None:
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    time.sleep(0.1)
    ser.reset_input_buffer()


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + chunk_type
        + payload
        + struct.pack(">I", binascii.crc32(chunk_type + payload) & 0xFFFFFFFF)
    )


def rgb565_to_png_bytes(width: int, height: int, stride: int, payload: bytes) -> bytes:
    rows = bytearray()

    for row in range(height):
        row_start = row * stride
        row_bytes = payload[row_start : row_start + (width * 2)]
        if len(row_bytes) < width * 2:
            raise ValueError("short RGB565 row payload")

        rows.append(0)
        for pixel in range(0, width * 2, 2):
            value = row_bytes[pixel] | (row_bytes[pixel + 1] << 8)
            red = ((value >> 11) & 0x1F) * 255 // 31
            green = ((value >> 5) & 0x3F) * 255 // 63
            blue = (value & 0x1F) * 255 // 31
            rows.extend((red, green, blue))

    compressed = zlib.compress(bytes(rows), level=9)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"".join(
        [
            b"\x89PNG\r\n\x1a\n",
            png_chunk(b"IHDR", header),
            png_chunk(b"IDAT", compressed),
            png_chunk(b"IEND", b""),
        ]
    )


def argb2222_to_png_bytes(width: int, height: int, stride: int, payload: bytes) -> bytes:
    rows = bytearray()

    for row in range(height):
        row_start = row * stride
        row_bytes = payload[row_start : row_start + width]
        if len(row_bytes) < width:
            raise ValueError("short ARGB2222 row payload")

        rows.append(0)
        for value in row_bytes:
            blue = (value & 0x03) * 255 // 3
            green = ((value >> 2) & 0x03) * 255 // 3
            red = ((value >> 4) & 0x03) * 255 // 3
            rows.extend((red, green, blue))

    compressed = zlib.compress(bytes(rows), level=9)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"".join(
        [
            b"\x89PNG\r\n\x1a\n",
            png_chunk(b"IHDR", header),
            png_chunk(b"IDAT", compressed),
            png_chunk(b"IEND", b""),
        ]
    )


def process_line(line: str, current: dict[str, object] | None, output_dir: pathlib.Path) -> tuple[dict[str, object] | None, int]:
    begin_match = BEGIN_RE.match(line)
    if begin_match:
        if current is not None:
            raise SystemExit(f"Received nested screenshot header while capturing {current['name']}")
        current = {
            "name": begin_match.group("name"),
            "width": int(begin_match.group("width")),
            "height": int(begin_match.group("height")),
            "format": begin_match.group("format"),
            "stride": int(begin_match.group("stride")),
            "size": int(begin_match.group("size")),
            "segments": {},
        }
        print(f"Capturing {current['name']}...")
        return current, 0

    data_match = DATA_RE.match(line)
    if data_match:
        if current is None:
            return current, 0

        offset = int(data_match.group("offset"))
        segment = base64.b64decode(data_match.group("data"), validate=True)
        existing = current["segments"].get(offset)
        if existing is not None and existing != segment:
            raise SystemExit(f"Conflicting duplicate chunk at offset {offset} for {current['name']}")
        current["segments"][offset] = segment
        return current, 0

    end_match = END_RE.match(line)
    if end_match:
        if current is None:
            return current, 0
        if end_match.group("name") != current["name"]:
            raise SystemExit(f"Screenshot footer mismatch: expected {current['name']}, got {end_match.group('name')}")
        if current["format"] not in {"RGB565", "ARGB2222"}:
            raise SystemExit(f"Unsupported screenshot format: {current['format']}")

        payload = bytearray()
        next_offset = 0
        for offset in sorted(current["segments"]):
            if offset != next_offset:
                raise SystemExit(
                    f"Screenshot data gap for {current['name']}: expected offset {next_offset}, got {offset}"
                )

            segment = current["segments"][offset]
            payload.extend(segment)
            next_offset += len(segment)

        if len(payload) != current["size"]:
            raise SystemExit(
                f"Screenshot size mismatch for {current['name']}: expected {current['size']} bytes, got {len(payload)}"
            )

        if current["format"] == "RGB565":
            png_bytes = rgb565_to_png_bytes(current["width"], current["height"], current["stride"], bytes(payload))
        else:
            png_bytes = argb2222_to_png_bytes(current["width"], current["height"], current["stride"], bytes(payload))
        output_path = output_dir / f"{current['name']}.png"
        output_path.write_bytes(png_bytes)
        print(f"Wrote {output_path}")
        return None, 1

    return current, 0


def main() -> int:
    args = parse_args()
    output_dir = pathlib.Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    captures = 0
    last_activity = time.monotonic()
    start_time = last_activity
    current = None
    buffer = bytearray()

    with serial.Serial(args.port, args.baud, timeout=0.5) as ser:
        if args.reset_on_connect:
            reset_esp32(ser)

        while True:
            chunk = ser.read(ser.in_waiting or 1)

            if chunk:
                last_activity = time.monotonic()
                buffer.extend(chunk)

            processed_line = False
            while True:
                newline_index = buffer.find(b"\n")
                if newline_index < 0:
                    break

                raw_line = bytes(buffer[: newline_index + 1])
                del buffer[: newline_index + 1]

                line = raw_line.decode("utf-8", errors="replace").rstrip("\r\n")
                processed_line = True
                if not line:
                    continue

                current, completed = process_line(line, current, output_dir)
                captures += completed
                if captures >= args.expect:
                    break

            if captures >= args.expect:
                break

            if processed_line:
                continue

            if not chunk:
                if captures > 0 and (time.monotonic() - last_activity) >= args.idle_timeout:
                    break
                if captures == 0 and (time.monotonic() - start_time) >= args.startup_timeout:
                    raise SystemExit("Timed out waiting for GLSHOT stream. Start the capture script, then reset the board.")

        if current is not None:
            raise SystemExit(f"Serial stream ended mid-capture for {current['name']}")

    print(f"Captured {captures} screenshot(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())