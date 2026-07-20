#!/usr/bin/env python3
"""Render the navbar tiles and the header mascot as LVGL image descriptors.

The TFT_eSPI build drew these straight onto the panel with fillRoundRect and
fillTriangle calls. LVGL has no equivalent at the widget level, and an
lv_canvas large enough to hold the navbar would want its own 25KB buffer --
which is most of the draw-buffer budget ADR-0003 sets aside. Baking the art
into flash instead costs nothing at runtime.

Everything is drawn at 4x and resampled down, so the rounded corners and the
diagonal edges of the sparkle come out anti-aliased rather than stepped.

Output format is RGB565A8: a plane of little-endian RGB565 followed by a plane
of 8-bit alpha, which is what LVGL expects for artwork that has to blend
against a dark background at the corners. 3 bytes per pixel, so a 28x28 tile is
2352 bytes and the whole set is under 15KB.

Usage:
    tools/mkicons_lvgl.py            regenerate
    tools/mkicons_lvgl.py --preview  also write PNGs to /tmp for eyeballing
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT_C = ROOT / "firmware" / "src" / "ui" / "ui_icons.c"
OUT_H = ROOT / "firmware" / "src" / "ui" / "ui_icons.h"
INTER = ROOT / "fonts" / "Inter.ttf"

SS = 4  # supersample factor

TILE = 28  # navbar tile, drawn larger than the 18px the old build used
# ~22% of the side. At 8 the corners eat so much of a 28px square that the
# tile reads as a circle rather than as the squircle in the design.
TILE_R = 6

# Mirrors the palette in theme.h. Kept as RGB888 here because PIL works in
# 8/8/8; the pack to RGB565 happens at the end, once, in one place.
C_CLAUDE_BG = "#40230A"
C_CLAUDE_FG = "#F5822E"
C_CLAUDE_FG2 = "#FFB020"
C_WEEK_BG = "#241E44"
C_WEEK_FG = "#8C83FF"
C_WEATHER_BG = "#0E1E3C"
C_WEATHER_SUN = "#FFB020"
C_WEATHER_FG = "#3CB4FF"
C_CRYPTO_BG = "#2A2404"
C_CRYPTO_FG = "#FFD400"
C_SETTING_BG = "#2C2C30"
C_SETTING_FG = "#B4B4BA"
C_MASCOT_TOP = (0xF0, 0x4A, 0x2A)
C_MASCOT_BOTTOM = (0xFF, 0x8A, 0x3D)


def canvas(w: int, h: int) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def finish(img: Image.Image, w: int, h: int) -> Image.Image:
    return img.resize((w, h), Image.LANCZOS)


def tile_base(colour: str) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img, draw = canvas(TILE, TILE)
    draw.rounded_rectangle(
        [0, 0, TILE * SS - 1, TILE * SS - 1], radius=TILE_R * SS, fill=colour
    )
    return img, draw


def sparkle(draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float, colour: str):
    """A four-point star: two opposing triangle pairs sharing a centre."""
    waist = r / 2.6
    draw.polygon(
        [(cx, cy - r), (cx + waist, cy), (cx, cy + r), (cx - waist, cy)], fill=colour
    )
    draw.polygon(
        [(cx - r, cy), (cx, cy - waist), (cx + r, cy), (cx, cy + waist)], fill=colour
    )


def icon_claude() -> Image.Image:
    img, draw = tile_base(C_CLAUDE_BG)
    s = SS
    sparkle(draw, 16 * s, 16 * s, 9 * s, C_CLAUDE_FG)
    sparkle(draw, 8.5 * s, 8.5 * s, 4.5 * s, C_CLAUDE_FG2)
    return finish(img, TILE, TILE)


def icon_weekly() -> Image.Image:
    img, draw = tile_base(C_WEEK_BG)
    s = SS
    for x, top in ((7, 16), (12.5, 8), (18, 12)):
        draw.rounded_rectangle(
            [x * s, top * s, (x + 3.5) * s, 21 * s], radius=1.5 * s, fill=C_WEEK_FG
        )
    return finish(img, TILE, TILE)


def icon_weather() -> Image.Image:
    img, draw = tile_base(C_WEATHER_BG)
    s = SS
    draw.ellipse([15 * s, 5 * s, 24 * s, 14 * s], fill=C_WEATHER_SUN)
    # Cloud: three overlapping discs with a slab across their base.
    draw.ellipse([5 * s, 12 * s, 15 * s, 22 * s], fill=C_WEATHER_FG)
    draw.ellipse([12 * s, 14 * s, 21 * s, 23 * s], fill=C_WEATHER_FG)
    draw.rounded_rectangle(
        [6 * s, 17 * s, 21 * s, 22 * s], radius=2.5 * s, fill=C_WEATHER_FG
    )
    return finish(img, TILE, TILE)


def icon_crypto() -> Image.Image:
    img, draw = tile_base(C_CRYPTO_BG)
    s = SS
    # Inter has no Bitcoin sign, so the mark is a capital B from the same face
    # with the two vertical strokes added -- which is how the glyph is built
    # anyway, and keeps it visually related to the rest of the type.
    font = ImageFont.truetype(str(INTER), int(21 * s))
    draw.text((14.5 * s, 14 * s), "B", font=font, fill=C_CRYPTO_FG, anchor="mm")
    # The strokes have to sit over the B's own stems and only just clear the
    # cap height. Longer ones stop reading as a currency mark and start
    # reading as a second letter.
    for x in (10.6, 14.6):
        draw.rectangle([x * s, 3.6 * s, (x + 1.8) * s, 7.5 * s], fill=C_CRYPTO_FG)
        draw.rectangle([x * s, 20.5 * s, (x + 1.8) * s, 24.4 * s], fill=C_CRYPTO_FG)
    return finish(img, TILE, TILE)


def icon_setting() -> Image.Image:
    img, draw = tile_base(C_SETTING_BG)
    s = SS
    cx = cy = 14 * s
    # Eight teeth as rotated squares around the hub reads as a gear at 28px;
    # a true involute profile disappears at this size.
    for i in range(8):
        angle = i * 45
        from math import cos, radians, sin

        dx = cos(radians(angle)) * 8 * s
        dy = sin(radians(angle)) * 8 * s
        draw.ellipse(
            [cx + dx - 2.6 * s, cy + dy - 2.6 * s, cx + dx + 2.6 * s, cy + dy + 2.6 * s],
            fill=C_SETTING_FG,
        )
    draw.ellipse([cx - 7 * s, cy - 7 * s, cx + 7 * s, cy + 7 * s], fill=C_SETTING_FG)
    draw.ellipse(
        [cx - 3 * s, cy - 3 * s, cx + 3 * s, cy + 3 * s], fill=C_SETTING_BG
    )
    return finish(img, TILE, TILE)


# The header mascot, unchanged from theme.h: a blocky robot with two eye slots,
# stubby antennae and four legs. One character per pixel, '#' is opaque.
MASCOT = [
    "...#......#...",
    "...#......#...",
    "..##########..",
    ".############.",
    "##############",
    "##..######..##",
    "##..######..##",
    "##############",
    "##############",
    ".############.",
    ".##.##..##.##.",
    ".##.##..##.##.",
]
MASCOT_SCALE = 2


def icon_mascot() -> Image.Image:
    rows = len(MASCOT)
    cols = len(MASCOT[0])
    w, h = cols * MASCOT_SCALE, rows * MASCOT_SCALE
    # Drawn at final size with no supersampling: it is pixel art, and smoothing
    # the block edges would just make it look like a blurry stamp.
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for r, line in enumerate(MASCOT):
        t = r / (rows - 1)
        colour = tuple(
            int(a + (b - a) * t) for a, b in zip(C_MASCOT_TOP, C_MASCOT_BOTTOM)
        ) + (255,)
        for c, ch in enumerate(line):
            if ch != "#":
                continue
            for dy in range(MASCOT_SCALE):
                for dx in range(MASCOT_SCALE):
                    px[c * MASCOT_SCALE + dx, r * MASCOT_SCALE + dy] = colour
    return img


def pack_rgb565a8(img: Image.Image) -> bytes:
    """RGB565 little-endian plane, then the alpha plane."""
    rgb = bytearray()
    alpha = bytearray()
    for r, g, b, a in list(img.convert("RGBA").getdata()):
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        rgb += struct.pack("<H", value)
        alpha.append(a)
    return bytes(rgb) + bytes(alpha)


def emit(name: str, img: Image.Image) -> str:
    data = pack_rgb565a8(img)
    w, h = img.size
    body = []
    for i in range(0, len(data), 16):
        body.append("    " + " ".join(f"0x{b:02X}," for b in data[i : i + 16]))
    return f"""
static const uint8_t {name}_map[] = {{
{chr(10).join(body)}
}};

const lv_image_dsc_t {name} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = {w},
        .h = {h},
        .stride = {w * 2},
        .reserved_2 = 0,
    }},
    .data_size = sizeof({name}_map),
    .data = {name}_map,
    .reserved = NULL,
}};
"""


IMAGES = [
    ("icon_claude", icon_claude),
    ("icon_weekly", icon_weekly),
    ("icon_weather", icon_weather),
    ("icon_crypto", icon_crypto),
    ("icon_setting", icon_setting),
    ("img_mascot", icon_mascot),
]

BANNER = """// Generated by tools/mkicons_lvgl.py -- do not hand-edit.
// Regenerate after changing any artwork:  tools/mkicons_lvgl.py
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", action="store_true", help="also write PNGs to /tmp")
    args = parser.parse_args()

    OUT_C.parent.mkdir(parents=True, exist_ok=True)

    chunks = [BANNER, '#include "ui_icons.h"\n']
    total = 0
    for name, build in IMAGES:
        img = build()
        chunks.append(emit(name, img))
        size = img.size[0] * img.size[1] * 3
        total += size
        print(f"{name:14} {img.size[0]:>3}x{img.size[1]:<3} {size:>6} bytes")
        if args.preview:
            out = Path("/tmp") / f"{name}.png"
            img.resize((img.size[0] * 6, img.size[1] * 6), Image.NEAREST).save(out)

    OUT_C.write_text("".join(chunks), encoding="utf-8")

    decls = "\n".join(f"extern const lv_image_dsc_t {name};" for name, _ in IMAGES)
    OUT_H.write_text(
        f"""{BANNER}#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {{
#endif

{decls}

#ifdef __cplusplus
}}
#endif
""",
        encoding="utf-8",
    )

    print(f"\n{total} bytes of artwork -> {OUT_C.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
