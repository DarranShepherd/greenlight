from pathlib import Path

from cairosvg import svg2png
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SVG_PATH = ROOT / "logo.svg"
PNG_PATH = ROOT / "generated" / "logo.png"
HEADER_PATH = ROOT / "main" / "splash_logo.h"
SOURCE_PATH = ROOT / "main" / "splash_logo.c"
OUTPUT_WIDTH = 120


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
    indexed_image = image.quantize(colors=256)
    width, height = indexed_image.size

    palette = indexed_image.getpalette()[: 256 * 3]
    payload = bytearray()

    for palette_index in range(256):
        base = palette_index * 3
        if base + 2 < len(palette):
            red = palette[base]
            green = palette[base + 1]
            blue = palette[base + 2]
        else:
            red = 0
            green = 0
            blue = 0

        payload.extend((blue, green, red, 0xFF))

    payload.extend(indexed_image.tobytes())

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
        .cf = LV_COLOR_FORMAT_I8,
        .w = {width},
        .h = {height},
        .stride = {width},
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
