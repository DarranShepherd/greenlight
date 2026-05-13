from pathlib import Path

from cairosvg import svg2png
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SVG_PATH = ROOT / "logo.svg"
PNG_PATH = ROOT / "generated" / "logo.png"
HEADER_PATH = ROOT / "main" / "splash_logo.h"
SOURCE_PATH = ROOT / "main" / "splash_logo.c"
OUTPUT_WIDTH = 120


def rgb888_to_rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def emit_bytes(data: bytes, indent: str = "    ", per_line: int = 12) -> str:
    lines = []
    for offset in range(0, len(data), per_line):
        chunk = data[offset : offset + per_line]
        lines.append(indent + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def main() -> None:
    PNG_PATH.parent.mkdir(parents=True, exist_ok=True)

    svg2png(url=str(SVG_PATH), write_to=str(PNG_PATH), output_width=OUTPUT_WIDTH)

    image = Image.open(PNG_PATH).convert("RGB")
    width, height = image.size

    payload = bytearray()
    for red, green, blue in image.getdata():
        pixel = rgb888_to_rgb565(red, green, blue)
        payload.append(pixel & 0xFF)
        payload.append((pixel >> 8) & 0xFF)

    header_text = """#pragma once

#include <lvgl.h>

extern const lv_image_dsc_t splash_logo;
"""

    source_text = f"""#include \"splash_logo.h\"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_SPLASH_LOGO
#define LV_ATTRIBUTE_IMAGE_SPLASH_LOGO
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_SPLASH_LOGO uint8_t splash_logo_map[] = {{
{emit_bytes(payload)}
}};

const lv_image_dsc_t splash_logo = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = {width},
        .h = {height},
        .stride = {width * 2},
    }},
    .data_size = sizeof(splash_logo_map),
    .data = splash_logo_map,
}};
"""

    HEADER_PATH.write_text(header_text)
    SOURCE_PATH.write_text(source_text)

    print(f"Wrote {HEADER_PATH.relative_to(ROOT)} and {SOURCE_PATH.relative_to(ROOT)} ({width}x{height})")


if __name__ == "__main__":
    main()
