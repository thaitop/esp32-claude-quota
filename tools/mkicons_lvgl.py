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

# Tile glyphs are drawn in a 28-unit space, which is the size the artwork was
# first laid out at. UNIT rescales that to whatever TILE is now, so growing the
# tiles does not mean re-deriving every coordinate below.
GLYPH_SPACE = 28

# Navbar tile. Grown from 28: at that size the five tiles left 38px of air
# between them, wider than the tiles themselves, and the row read as sparse.
TILE = 34
TILE_R = 8  # ~22% of the side, which is a squircle rather than a circle

# The header wears the same artwork smaller. Drawn at its own size rather than
# scaled down by LVGL at draw time: lv_image_set_scale() transforms about a
# pivot inside the source and clips to the object's box, which sliced the top
# off every header icon on the device while looking correct in the source. It
# is also cheaper -- a scaled image is redrawn through the transform path on
# every refresh, and these never change size.
HEADER = 26

# Rescales the 28-unit space the artwork was laid out in to whatever size the
# icon is being drawn at, so growing or shrinking a tile does not mean
# re-deriving every coordinate below.
def unit(size: int) -> float:
    return SS * size / GLYPH_SPACE

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

# Weather condition glyphs. Drawn at the size the Weather screen shows them,
# in colour rather than as white masters: the sun is amber and the cloud is
# blue on every one of them, so there is nothing for a recolor style to decide.
WX = 48
C_WX_SUN = "#FFB020"
C_WX_MOON = "#CDBCEC"
C_WX_CLOUD = "#3CB4FF"
C_WX_CLOUD_DIM = "#7C8B9E"
C_WX_DROP = "#3CB4FF"
C_WX_SNOW = "#FFFFFF"
C_WX_BOLT = "#FFD400"


def canvas(w: int, h: int) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def finish(img: Image.Image, w: int, h: int) -> Image.Image:
    return img.resize((w, h), Image.LANCZOS)


def tile_base(colour: str, size: int = TILE) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    img, draw = canvas(size, size)
    # The corner radius scales with the tile, so a smaller copy is the same
    # squircle rather than a rounder or squarer one.
    draw.rounded_rectangle(
        [0, 0, size * SS - 1, size * SS - 1],
        radius=TILE_R * SS * size / TILE,
        fill=colour,
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


def icon_claude(size: int = TILE) -> Image.Image:
    img, draw = tile_base(C_CLAUDE_BG, size)
    s = unit(size)
    sparkle(draw, 16 * s, 16 * s, 9 * s, C_CLAUDE_FG)
    sparkle(draw, 8.5 * s, 8.5 * s, 4.5 * s, C_CLAUDE_FG2)
    return finish(img, size, size)


def icon_weekly(size: int = TILE) -> Image.Image:
    img, draw = tile_base(C_WEEK_BG, size)
    s = unit(size)
    for x, top in ((7, 16), (12.5, 8), (18, 12)):
        draw.rounded_rectangle(
            [x * s, top * s, (x + 3.5) * s, 21 * s], radius=1.5 * s, fill=C_WEEK_FG
        )
    return finish(img, size, size)


def icon_weather(size: int = TILE) -> Image.Image:
    img, draw = tile_base(C_WEATHER_BG, size)
    s = unit(size)
    draw.ellipse([15 * s, 5 * s, 24 * s, 14 * s], fill=C_WEATHER_SUN)
    # Cloud: three overlapping discs with a slab across their base.
    draw.ellipse([5 * s, 12 * s, 15 * s, 22 * s], fill=C_WEATHER_FG)
    draw.ellipse([12 * s, 14 * s, 21 * s, 23 * s], fill=C_WEATHER_FG)
    draw.rounded_rectangle(
        [6 * s, 17 * s, 21 * s, 22 * s], radius=2.5 * s, fill=C_WEATHER_FG
    )
    return finish(img, size, size)


def icon_crypto(size: int = TILE) -> Image.Image:
    img, draw = tile_base(C_CRYPTO_BG, size)
    s = unit(size)
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
    return finish(img, size, size)


def icon_setting(size: int = TILE) -> Image.Image:
    img, draw = tile_base(C_SETTING_BG, size)
    s = unit(size)
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
    return finish(img, size, size)


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


# The two glyphs below are drawn in white on transparent. LVGL can recolor an
# image wholesale through a style, so a single white master serves the clock in
# whichever ramp colour its card is showing, and the wifi arcs in whichever
# colour the connection state calls for. Baking the colour in would mean one
# copy per state in flash.
GLYPH_W = "#FFFFFF"


def icon_clock() -> Image.Image:
    size = 14
    img, draw = canvas(size, size)
    s = SS
    r = 6 * s
    cx = cy = 7 * s
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=GLYPH_W, width=int(1.4 * s))
    draw.line([cx, cy, cx, cy - 3.4 * s], fill=GLYPH_W, width=int(1.4 * s))
    draw.line([cx, cy, cx + 2.6 * s, cy], fill=GLYPH_W, width=int(1.4 * s))
    return finish(img, size, size)


def icon_wifi() -> Image.Image:
    w, h = 22, 16
    img, draw = canvas(w, h)
    s = SS
    cx = 11 * s
    base = 14 * s
    # Three arcs struck from a common origin, then a dot at the origin. Drawn
    # as arcs rather than as a filled fan so the gaps stay even as they scale.
    for i, radius in enumerate((4.5, 8, 11.5)):
        draw.arc(
            [cx - radius * s, base - radius * s, cx + radius * s, base + radius * s],
            start=210,
            end=330,
            fill=GLYPH_W,
            width=int(1.6 * s),
        )
    draw.ellipse([cx - 1.3 * s, base - 1.3 * s, cx + 1.3 * s, base + 1.3 * s],
                 fill=GLYPH_W)
    return finish(img, w, h)


# --- weather conditions ----------------------------------------------------
#
# WMO's code space is a hundred values deep; model.cpp collapses it into the
# seven conditions below, times day and night for the two that differ. Drawing
# one glyph per code would be sixty pictures nobody can tell apart at 48px.


def _sun(draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float, rays: bool):
    from math import cos, radians, sin

    if rays:
        for i in range(8):
            angle = radians(i * 45)
            inner, outer = r * 1.35, r * 1.95
            dx, dy = cos(angle), sin(angle)
            draw.line(
                [cx + dx * inner, cy + dy * inner, cx + dx * outer, cy + dy * outer],
                fill=C_WX_SUN,
                width=int(2.2 * SS),
            )
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=C_WX_SUN)


def _moon(draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float):
    """A crescent: a disc with a second disc erased out of it.

    ImageDraw writes raw channel values into an RGBA image rather than
    compositing, so filling with a zero alpha genuinely cuts the bite out.
    """
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=C_WX_MOON)
    bite = r * 0.86
    draw.ellipse(
        [cx - bite + r * 0.62, cy - bite - r * 0.28,
         cx + bite + r * 0.62, cy + bite - r * 0.28],
        fill=(0, 0, 0, 0),
    )


def _cloud(draw: ImageDraw.ImageDraw, cx: float, cy: float, w: float, colour: str):
    """Three overlapping discs on a slab, the same construction as the tile."""
    h = w * 0.62
    draw.ellipse([cx - w * 0.50, cy - h * 0.55, cx - w * 0.02, cy + h * 0.42], fill=colour)
    draw.ellipse([cx - w * 0.12, cy - h * 0.80, cx + w * 0.40, cy + h * 0.35], fill=colour)
    draw.ellipse([cx + w * 0.12, cy - h * 0.35, cx + w * 0.52, cy + h * 0.45], fill=colour)
    draw.rounded_rectangle(
        [cx - w * 0.48, cy - h * 0.05, cx + w * 0.50, cy + h * 0.45],
        radius=h * 0.25,
        fill=colour,
    )


def _wx_canvas():
    return canvas(WX, WX)


def wx_clear_day() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    _sun(draw, 24 * s, 24 * s, 11 * s, rays=True)
    return finish(img, WX, WX)


def wx_clear_night() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    _moon(draw, 24 * s, 24 * s, 15 * s)
    return finish(img, WX, WX)


def wx_partly_day() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    _sun(draw, 31 * s, 16 * s, 8 * s, rays=True)
    _cloud(draw, 22 * s, 31 * s, 30 * s, C_WX_CLOUD)
    return finish(img, WX, WX)


def wx_partly_night() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    _moon(draw, 31 * s, 15 * s, 10 * s)
    _cloud(draw, 22 * s, 31 * s, 30 * s, C_WX_CLOUD)
    return finish(img, WX, WX)


def wx_cloudy() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    # The back cloud is dimmed so the two read as depth rather than as one
    # lumpy shape.
    _cloud(draw, 29 * s, 19 * s, 24 * s, C_WX_CLOUD_DIM)
    _cloud(draw, 21 * s, 29 * s, 32 * s, C_WX_CLOUD)
    return finish(img, WX, WX)


def wx_fog() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    _cloud(draw, 24 * s, 19 * s, 32 * s, C_WX_CLOUD_DIM)
    # Staggered bars, because three equal ones read as a hamburger menu.
    for y, (x0, x1) in zip((33, 39, 45), ((9, 39), (13, 43), (9, 35))):
        draw.rounded_rectangle(
            [x0 * s, (y - 1.4) * s, x1 * s, (y + 1.4) * s],
            radius=1.4 * s,
            fill=C_WX_CLOUD,
        )
    return finish(img, WX, WX)


def _streaks(draw: ImageDraw.ImageDraw, colour: str):
    s = SS
    for x in (15, 24, 33):
        draw.line(
            [x * s, 32 * s, (x - 3) * s, 43 * s], fill=colour, width=int(2.6 * SS)
        )


def wx_rain() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    _cloud(draw, 24 * s, 18 * s, 32 * s, C_WX_CLOUD)
    _streaks(draw, C_WX_DROP)
    return finish(img, WX, WX)


def wx_snow() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    _cloud(draw, 24 * s, 18 * s, 32 * s, C_WX_CLOUD_DIM)
    # Three-armed asterisks. Six arms turn to mush once resampled to 48px.
    from math import cos, radians, sin

    for cx, cy in ((14, 37), (24, 41), (34, 37)):
        for i in range(3):
            angle = radians(i * 60 + 30)
            dx, dy = cos(angle) * 4 * s, sin(angle) * 4 * s
            draw.line(
                [cx * s - dx, cy * s - dy, cx * s + dx, cy * s + dy],
                fill=C_WX_SNOW,
                width=int(1.8 * SS),
            )
    return finish(img, WX, WX)


def wx_storm() -> Image.Image:
    img, draw = _wx_canvas()
    s = SS
    _cloud(draw, 24 * s, 18 * s, 32 * s, C_WX_CLOUD_DIM)
    draw.polygon(
        [
            (26 * s, 29 * s),
            (16 * s, 39 * s),
            (22 * s, 39 * s),
            (19 * s, 47 * s),
            (31 * s, 35 * s),
            (24 * s, 35 * s),
            (28 * s, 29 * s),
        ],
        fill=C_WX_BOLT,
    )
    return finish(img, WX, WX)


# --- coin badges -----------------------------------------------------------
#
# One badge per tracked coin, drawn at the size the Crypto screen's hero shows
# them. The screen's title scales the same asset down to 26 rather than taking
# a second, smaller copy: the marks are discs with a centred glyph, which is the
# one shape that survives being resampled by an arbitrary factor.
#
# Brand colours, not the palette. A reader recognises Bitcoin's orange and
# Ethereum's blue-violet before they read the letters, and recolouring them to
# fit the product would throw that recognition away for consistency nobody
# asked for.
COIN = 48
C_BTC = "#F7931A"
C_ETH = "#627EEA"
C_BNB = "#F3BA2F"
COIN_FG = "#FFFFFF"


def _disc(draw: ImageDraw.ImageDraw, colour: str, size: int):
    draw.ellipse([0, 0, size * SS - 1, size * SS - 1], fill=colour)


def _diamond(draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float, colour: str):
    draw.polygon(
        [(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], fill=colour
    )


def coin_btc(size: int = COIN) -> Image.Image:
    img, draw = canvas(size, size)
    s = SS * size / COIN
    _disc(draw, C_BTC, size)
    # Same construction as the navbar tile: Inter carries no Bitcoin sign, so
    # the mark is a capital B with the two stems extended past cap height and
    # baseline -- which is how the real glyph is drawn, and keeps it in the
    # same family as the type beside it.
    font = ImageFont.truetype(str(INTER), max(1, int(30 * s)))
    draw.text((25 * s, 24 * s), "B", font=font, fill=COIN_FG, anchor="mm")
    for x in (19.5, 25.2):
        draw.rectangle([x * s, 9.5 * s, (x + 2.4) * s, 15 * s], fill=COIN_FG)
        draw.rectangle([x * s, 33 * s, (x + 2.4) * s, 38.5 * s], fill=COIN_FG)
    return finish(img, size, size)


def coin_eth(size: int = COIN) -> Image.Image:
    img, draw = canvas(size, size)
    s = SS * size / COIN
    _disc(draw, C_ETH, size)
    # The octahedron seen edge-on: an upper kite and a lower one, with the
    # upper half's lower wedge dimmed. Two solid halves with a gap between them
    # is what the mark reduces to once it is 48px across.
    cx = 24 * s
    draw.polygon([(cx, 9 * s), (34 * s, 24.5 * s), (cx, 30 * s), (14 * s, 24.5 * s)],
                 fill=COIN_FG)
    draw.polygon([(cx, 32 * s), (34 * s, 26.5 * s), (cx, 39 * s), (14 * s, 26.5 * s)],
                 fill=COIN_FG)
    # The crease that separates the two faces of the upper kite. Drawn as a
    # wedge in the disc's own colour rather than as a line, so it stays crisp
    # after the resample.
    draw.polygon([(cx, 9 * s), (34 * s, 24.5 * s), (cx, 24.5 * s)], fill=C_ETH)
    draw.polygon([(cx, 32 * s), (34 * s, 26.5 * s), (cx, 34.5 * s)], fill=C_ETH)
    return finish(img, size, size)


def coin_bnb(size: int = COIN) -> Image.Image:
    img, draw = canvas(size, size)
    s = SS * size / COIN
    _disc(draw, C_BNB, size)
    cx = cy = 24 * s
    # Four satellites around a hub, all squares on their corner. The short bars
    # flanking the hub in the real mark disappear at this size, so they are
    # folded into the satellites' spacing instead.
    for dx, dy in ((0, -11), (11, 0), (0, 11), (-11, 0)):
        _diamond(draw, cx + dx * s, cy + dy * s, 5.4 * s, COIN_FG)
    _diamond(draw, cx, cy, 5.4 * s, COIN_FG)
    return finish(img, size, size)


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
    ("glyph_clock", icon_clock),
    ("glyph_wifi", icon_wifi),
    ("wx_clear_day", wx_clear_day),
    ("wx_clear_night", wx_clear_night),
    ("wx_partly_day", wx_partly_day),
    ("wx_partly_night", wx_partly_night),
    ("wx_cloudy", wx_cloudy),
    ("wx_fog", wx_fog),
    ("wx_rain", wx_rain),
    ("wx_snow", wx_snow),
    ("wx_storm", wx_storm),
    ("coin_btc", coin_btc),
    ("coin_eth", coin_eth),
    ("coin_bnb", coin_bnb),
    # The header copies. Same artwork, drawn at the size it is shown rather
    # than scaled there by LVGL -- see HEADER above for what that cost.
    # Claude's header carries the mascot instead, so there is no small tile
    # for it.
    ("icon_weekly_hdr", lambda: icon_weekly(HEADER)),
    ("icon_weather_hdr", lambda: icon_weather(HEADER)),
    ("icon_crypto_hdr", lambda: icon_crypto(HEADER)),
    ("icon_setting_hdr", lambda: icon_setting(HEADER)),
    ("coin_btc_hdr", lambda: coin_btc(HEADER)),
    ("coin_eth_hdr", lambda: coin_eth(HEADER)),
    ("coin_bnb_hdr", lambda: coin_bnb(HEADER)),
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
