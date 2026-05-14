#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FONT_PATH = ROOT / "managed_components" / "lvgl__lvgl" / "scripts" / "built_in_font" / "Montserrat-Medium.ttf"
LOGO_SCRIPT = ROOT / "tools" / "generate_lvgl_logo.py"
LOGO_MODULES = ("cairosvg", "PIL")

FONT_JOBS = (
    {
        "output": ROOT / "main" / "lv_font_montserrat_20_numeric.c",
        "size": 20,
        "ranges": "0x25,0x30-0x39",
    },
    {
        "output": ROOT / "main" / "lv_font_montserrat_28_numeric.c",
        "size": 28,
        "ranges": "0x2D,0x2E,0x30-0x39,0x47,0x65,0x67-0x69,0x6C,0x6E,0x72,0x74",
    },
)


def run_command(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def can_run_logo_generator(python_executable: str) -> bool:
    command = [python_executable, "-c", "import cairosvg; import PIL"]
    result = subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def iter_logo_python_candidates() -> list[str]:
    candidates = [
        sys.executable,
        os.path.realpath(sys.executable),
        "/usr/bin/python3",
        shutil.which("python3"),
        shutil.which("python"),
    ]
    unique_candidates: list[str] = []
    seen: set[str] = set()
    for candidate in candidates:
        if not candidate:
            continue
        normalized = os.path.abspath(candidate)
        if normalized in seen:
            continue
        seen.add(normalized)
        unique_candidates.append(normalized)
    return unique_candidates


def resolve_logo_python() -> str:
    for candidate in iter_logo_python_candidates():
        if can_run_logo_generator(candidate):
            return candidate

    searched = ", ".join(iter_logo_python_candidates())
    modules = ", ".join(LOGO_MODULES)
    raise RuntimeError(
        f"Unable to find a Python interpreter with {modules} installed. Tried: {searched}"
    )


def normalize_lvgl_include(font_source: Path) -> None:
    text = font_source.read_text()
    old = '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif\n'
    if old in text:
        text = text.replace(old, "#include <lvgl.h>\n", 1)
        font_source.write_text(text)


def generate_font_assets() -> None:
    lv_font_conv = shutil.which("lv_font_conv")
    if lv_font_conv is None:
        raise RuntimeError("lv_font_conv not found on PATH")

    if not FONT_PATH.is_file():
        raise RuntimeError(f"Montserrat font not found at {FONT_PATH}")

    for job in FONT_JOBS:
        command = [
            lv_font_conv,
            "--no-compress",
            "--no-prefilter",
            "--bpp",
            "4",
            "--size",
            str(job["size"]),
            "--font",
            str(FONT_PATH),
            "-r",
            job["ranges"],
            "--format",
            "lvgl",
            "-o",
            str(job["output"]),
            "--force-fast-kern-format",
        ]
        run_command(command)
        normalize_lvgl_include(job["output"])
        print(f"Wrote {job['output'].relative_to(ROOT)}")


def main() -> int:
    logo_python = resolve_logo_python()
    if Path(logo_python).resolve() != Path(sys.executable).resolve():
        print(f"Using {logo_python} for logo generation")
    run_command([logo_python, str(LOGO_SCRIPT)])
    generate_font_assets()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())