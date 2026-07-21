#!/usr/bin/env python3
"""Rasterise SVG artwork into the LVGL C arrays the firmware compiles in.

Why offline: the ESP32 has ~290KB heap, no PSRAM, and one TLS handshake already
peaks ~45KB (ADR-0003). A runtime SVG engine (ThorVG) would not fit and would
fight LVGL's layer pool. So the *authoring* is vector -- crisp, themeable -- and
what ships is a plain RGB565A8 bitmap with zero runtime cost.

Sources (all permissive licences, vendored under tools/icons/):
  - lucide/   Lucide, ISC   -- stroke icons (stroke="currentColor")
  - brand/    Simple Icons, CC0 -- single-path brand marks (fill inherits)
  - custom/   authored here  -- e.g. the Claude spark Lucide has no glyph for

img_mascot is NOT here: it is the Claude character, which no icon set carries,
so it lives hand-kept in ui_icons_static.c and is never regenerated.

Pipeline per icon: set paint colour -> resvg render at 4x supersample ->
Pillow LANCZOS downscale -> split into an RGB565 colour plane followed by an A8
alpha plane (the LVGL RGB565A8 byte order) -> emit as a C array.

Deps (host only, never flashed):  pip install resvg-py Pillow
Run:                              python3 tools/mkicons_lvgl.py
Regenerates:                      src/ui/ui_icons.c  and  src/ui/ui_icons.h
"""

import io
import os
import re
import sys

try:
    import resvg_py
    from PIL import Image
except ImportError as e:
    sys.exit(f"missing host dep: {e}\n  pip install resvg-py Pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "icons")
# Hand-dropped overrides. A file here named exactly after an icon
# (icon_weather.svg, wx_rain.svg, coin_btc.svg, ...) wins over the vendored
# source. A "_hdr" variant with no file of its own reuses the base drop
# (coin_btc_hdr <- coin_btc), so only ~18 files cover all 29 icons.
DROP = os.path.join(ICONS, "drop")
OUT_C = os.path.join(HERE, "..", "src", "ui", "ui_icons.c")
OUT_H = os.path.join(HERE, "..", "src", "ui", "ui_icons.h")

SS = 4  # supersample factor before the LANCZOS downscale

# Theme palette (mirrors src/ui/theme.h -- keep in sync by eye, it rarely moves).
TEXT = "#FFFFFF"
MUTED = "#9A9AA0"
ACCENT = "#F5822E"
AMBER = "#FFB020"
LILAC = "#CDBCEC"   # PILL_SESSION_TX, reads as moonlight
RAIN = "#5B9BD5"
# Brand marks keep their own colours.
BTC, ETH, BNB = "#F7931A", "#627EEA", "#F3BA2F"

# kind: "stroke" (Lucide/custom -- recolour the stroke) or
#       "fill"   (Simple Icons -- recolour the fill).
# Order here IS the order emitted into ui_icons.c and declared in ui_icons.h.
# img_mascot is spliced into the header at its original position but has no row
# here; it is defined in ui_icons_static.c.
MANIFEST = [
    # name,              src,                         w,  h,  colour, kind
    ("icon_claude",   "custom/anthropic.svg",        34, 34, ACCENT, "stroke"),
    ("icon_weekly",   "lucide/calendar-days.svg",    34, 34, TEXT,   "stroke"),
    ("icon_weather",  "lucide/cloud-sun.svg",        34, 34, TEXT,   "stroke"),
    ("icon_crypto",   "lucide/coins.svg",            34, 34, TEXT,   "stroke"),
    ("icon_stock",    "lucide/trending-up.svg",      34, 34, TEXT,   "stroke"),
    ("icon_setting",  "lucide/settings.svg",         34, 34, TEXT,   "stroke"),

    # Usage-screen header mark. Ships from drop/icon_usage_hdr.svg (the Claude
    # Code logo, full-colour -> kept as-is). The vendored fallback is only a
    # placeholder for when no drop file is present.
    ("icon_usage_hdr", "custom/anthropic.svg",       26, 26, ACCENT, "stroke"),

    ("__mascot__",    None,                          28, 24, None,   "static"),

    # glyph_* are recoloured at runtime (image_recolor), so the baked colour is
    # irrelevant -- only the alpha shape matters. White keeps them legible if a
    # recolor is ever dropped.
    ("glyph_clock",      "lucide/clock.svg",         14, 14, TEXT,   "stroke"),
    ("glyph_wifi",       "lucide/wifi.svg",          22, 16, TEXT,   "stroke"),
    # Weaker-signal variants of the wifi glyph, swapped in by RSSI. Recoloured
    # at runtime like glyph_wifi, so the baked colour is only a fallback.
    ("glyph_wifi_high",  "lucide/wifi-high.svg",     22, 16, TEXT,   "stroke"),
    ("glyph_wifi_low",   "lucide/wifi-low.svg",      22, 16, TEXT,   "stroke"),
    ("glyph_wifi_off",   "lucide/wifi-off.svg",      22, 16, TEXT,   "stroke"),
    ("glyph_brightness", "lucide/sun.svg",           16, 16, TEXT,   "stroke"),

    ("wx_clear_day",    "lucide/sun.svg",            48, 48, AMBER,  "stroke"),
    ("wx_clear_night",  "lucide/moon.svg",           48, 48, LILAC,  "stroke"),
    ("wx_partly_day",   "lucide/cloud-sun.svg",      48, 48, AMBER,  "stroke"),
    ("wx_partly_night", "lucide/cloud-moon.svg",     48, 48, LILAC,  "stroke"),
    ("wx_cloudy",       "lucide/cloud.svg",          48, 48, MUTED,  "stroke"),
    ("wx_fog",          "lucide/cloud-fog.svg",      48, 48, MUTED,  "stroke"),
    ("wx_rain",         "lucide/cloud-rain.svg",     48, 48, RAIN,   "stroke"),
    ("wx_snow",         "lucide/snowflake.svg",      48, 48, TEXT,   "stroke"),
    ("wx_storm",        "lucide/cloud-lightning.svg",48, 48, AMBER,  "stroke"),

    ("coin_btc",  "brand/bitcoin.svg",               48, 48, BTC,    "fill"),
    ("coin_eth",  "brand/ethereum.svg",              48, 48, ETH,    "fill"),
    ("coin_bnb",  "brand/binance.svg",               48, 48, BNB,    "fill"),

    ("icon_weekly_hdr",  "lucide/calendar-days.svg", 26, 26, ACCENT, "stroke"),
    ("icon_weather_hdr", "lucide/cloud-sun.svg",     26, 26, ACCENT, "stroke"),
    ("icon_crypto_hdr",  "lucide/coins.svg",         26, 26, ACCENT, "stroke"),
    ("icon_stock_hdr",   "lucide/trending-up.svg",   26, 26, ACCENT, "stroke"),
    ("icon_setting_hdr", "lucide/settings.svg",      26, 26, ACCENT, "stroke"),

    ("coin_btc_hdr", "brand/bitcoin.svg",            26, 26, BTC,    "fill"),
    ("coin_eth_hdr", "brand/ethereum.svg",           26, 26, ETH,    "fill"),
    ("coin_bnb_hdr", "brand/binance.svg",            26, 26, BNB,    "fill"),

    # Per-ticker row logos for the Stock screen. Drop-only; the vendored path is
    # a placeholder. Brand marks ship as solid black, so "mono" tints them white
    # to read on the dark row card.
    ("logo_aapl", "brand/bitcoin.svg",               20, 20, TEXT,   "mono"),
    ("logo_nvda", "brand/bitcoin.svg",               20, 20, TEXT,   "mono"),
    ("logo_tsla", "brand/bitcoin.svg",               20, 20, TEXT,   "mono"),
    ("logo_goog", "brand/bitcoin.svg",               20, 20, TEXT,   "mono"),
    ("logo_msft", "brand/bitcoin.svg",               20, 20, TEXT,   "mono"),
]


def paint(svg: str, colour: str, kind: str) -> str:
    """Force a single paint colour onto the source SVG."""
    if kind == "stroke":
        return svg.replace('stroke="currentColor"', f'stroke="{colour}"')
    if kind == "mono":
        # Brand marks that hard-code a black fill on every path (so setting fill
        # on <svg> can't reach them). Rewrite the black token itself; on a dark
        # background these have to be tinted or they vanish.
        for black in ("#000000", "#000", "black"):
            svg = svg.replace(f'"{black}"', f'"{colour}"')
            svg = svg.replace(f":{black}", f":{colour}")
        return svg
    # fill: Simple Icons paths inherit fill; set it on the root <svg>.
    return re.sub(r"<svg\b", f'<svg fill="{colour}"', svg, count=1)


def pick_src(name: str, vendored: str):
    """Resolve a source SVG: a drop/ override wins; a _hdr with no drop of its
    own falls back to its base's drop; otherwise the vendored source."""
    for cand in (name, name[:-4] if name.endswith("_hdr") else None):
        if cand:
            p = os.path.join(DROP, cand + ".svg")
            if os.path.isfile(p):
                return p, True
    return os.path.join(ICONS, vendored), False


def viewbox_aspect(svg: str) -> float:
    """Width/height of the SVG's own coordinate box (1.0 if it can't be read)."""
    m = re.search(r'viewBox="[\d.\-]+ [\d.\-]+ ([\d.]+) ([\d.]+)"', svg)
    if m:
        vw, vh = float(m.group(1)), float(m.group(2))
        if vw > 0 and vh > 0:
            return vw / vh
    return 1.0


def render(path: str, is_drop: bool, w: int, h: int, colour: str,
           kind: str) -> Image.Image:
    with open(path, encoding="utf-8") as f:
        svg = f.read()
    # Vendored art is monochrome and always tinted. A dropped SVG is tinted
    # only if it opts in with currentColor -- otherwise its own colours ship
    # as-is, so full-colour hand art survives untouched. kind "mono" overrides
    # that: it is for drops that hard-code black and must be recoloured to show.
    if kind == "mono" or not is_drop or "currentColor" in svg:
        svg = paint(svg, colour, kind)

    # Fit the art into WxH keeping its own aspect, then centre it on a
    # transparent WxH canvas. A square glyph forced into a wide box would
    # otherwise stretch (this is what widened glyph_wifi).
    aspect = viewbox_aspect(svg)
    iw, ih = w, h
    if aspect > w / h:
        ih = max(1, round(w / aspect))
    else:
        iw = max(1, round(h * aspect))
    png = resvg_py.svg_to_bytes(svg_string=svg, width=iw * SS, height=ih * SS)
    art = Image.open(io.BytesIO(bytes(png))).convert("RGBA").resize(
        (iw, ih), Image.LANCZOS)
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    canvas.alpha_composite(art, ((w - iw) // 2, (h - ih) // 2))
    return canvas


def to_rgb565a8(img: Image.Image) -> bytes:
    """LVGL RGB565A8: full RGB565 plane (2 bytes LE) then a full A8 plane."""
    px = img.load()
    w, h = img.size
    colour = bytearray()
    alpha = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            word = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            colour += bytes((word & 0xFF, (word >> 8) & 0xFF))  # little-endian
            alpha.append(a)
    return bytes(colour) + bytes(alpha)


def c_array(name: str, data: bytes) -> str:
    rows = []
    for i in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return (f"static const uint8_t {name}_map[] = {{\n"
            + "\n".join(rows) + f"\n}};\n")


def descriptor(name: str, w: int, h: int) -> str:
    return (
        f"const lv_image_dsc_t {name} = {{\n"
        "    .header = {\n"
        "        .magic = LV_IMAGE_HEADER_MAGIC,\n"
        "        .cf = LV_COLOR_FORMAT_RGB565A8,\n"
        "        .flags = 0,\n"
        f"        .w = {w},\n"
        f"        .h = {h},\n"
        f"        .stride = {w * 2},\n"
        "        .reserved_2 = 0,\n"
        "    },\n"
        f"    .data_size = sizeof({name}_map),\n"
        f"    .data = {name}_map,\n"
        "    .reserved = NULL,\n"
        "};\n"
    )


BANNER = ("// Generated by tools/mkicons_lvgl.py -- do not hand-edit.\n"
          "// Regenerate after changing any artwork:  python3 tools/mkicons_lvgl.py\n")


def main():
    c_parts = [BANNER, '#include "ui_icons.h"\n\n']
    h_externs = []
    for name, src, w, h, colour, kind in MANIFEST:
        if kind == "static":
            h_externs.append("extern const lv_image_dsc_t img_mascot;")
            continue
        path, is_drop = pick_src(name, src)
        img = render(path, is_drop, w, h, colour, kind)
        data = to_rgb565a8(img)
        assert len(data) == w * h * 3, (name, len(data), w * h * 3)
        c_parts.append(c_array(name, data))
        c_parts.append("\n")
        c_parts.append(descriptor(name, w, h))
        c_parts.append("\n")
        h_externs.append(f"extern const lv_image_dsc_t {name};")
        tag = "drop" if is_drop else "vendor"
        print(f"  {name:<18} {w}x{h}  {len(data):>6} bytes  [{tag}] <- "
              f"{os.path.relpath(path, ICONS)}")

    with open(OUT_C, "w") as f:
        f.write("".join(c_parts))

    header = (BANNER + "#pragma once\n\n#include <lvgl.h>\n\n"
              '#ifdef __cplusplus\nextern "C" {\n#endif\n\n'
              + "\n".join(h_externs)
              + '\n\n#ifdef __cplusplus\n}\n#endif\n')
    with open(OUT_H, "w") as f:
        f.write(header)

    print(f"wrote {os.path.relpath(OUT_C)} and {os.path.relpath(OUT_H)}")


if __name__ == "__main__":
    main()
