#!/usr/bin/env python3
"""Generate LVGL fonts from Inter.ttf, as C sources under firmware/src/ui/fonts.

This replaces mkvlw.py, which produced TFT_eSPI's .vlw format. LVGL cannot read
.vlw at all, so the type had to be regenerated rather than ported.

The actual conversion is done by lv_font_conv, which is a Node tool run through
npx -- there is no Python equivalent that produces LVGL's glyph tables. This
wrapper exists to pin the settings that matter and to keep one command for
regenerating every size, rather than five long npx invocations nobody remembers.

Sizes come from the design: the big percentage, the header, body copy, the pill
badges and the reset countdown. The navbar has no labels, so the 11px face that
the TFT_eSPI build carried is not regenerated.

Usage:
    tools/mkfont_lvgl.py            regenerate every size
    tools/mkfont_lvgl.py --check    verify the checked-in files are current
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_TTF = ROOT / "fonts" / "Inter.ttf"
OUT_DIR = ROOT / "firmware" / "src" / "ui" / "fonts"

# ASCII printable, plus the degree sign the Weather screen needs. Thai glyphs
# would add roughly 150KB and LVGL's text engine does not stack Thai vowels and
# tone marks correctly anyway, so the UI is English throughout.
#
# U+00B0 is one glyph and about forty bytes a face. Spelling the temperature
# "30.2 C" instead would have saved that and read like a chemistry answer.
SYMBOLS_RANGE = "0x20-0x7E,0xB0"
SUBSET_CODEPOINTS = list(range(0x20, 0x7F)) + [0xB0]

# 4 bits per pixel: 16 levels of anti-aliasing. 8bpp doubles the flash cost for
# a difference that is not visible on a 240px panel at arm's length.
BPP = 4

# Inter.ttf is a variable font and lv_font_conv only ever renders the default
# instance, which is Regular. A face that wants weight has to be instanced to a
# static TTF at that wght first -- see instance() below.
REGULAR = 400
BOLD = 700

FACES = [
    ("font_inter_36", 36, REGULAR),        # the big figure on Weather and Crypto
    ("font_inter_30_bold", 30, BOLD),      # the Claude percentages
    ("font_inter_27", 27, REGULAR),        # screen titles
    ("font_inter_22_bold", 22, BOLD),      # "Usage" in the Claude header
    ("font_inter_17", 17, REGULAR),        # body copy
    ("font_inter_15", 15, REGULAR),        # pill badges, status word
    ("font_inter_14", 14, REGULAR),        # captions
    ("font_inter_12", 12, REGULAR),        # reset countdown
]


def instance(weight: int, dest: Path) -> Path:
    """Freeze the variable source at one wght, because lv_font_conv cannot.

    The subsetting pass is not an optimisation, it is what keeps kerning alive.
    Instancing leaves the full 2800-glyph GPOS behind, which compiles past the
    64KB an offset can reach, so fontTools wraps every lookup in an Extension
    (type 9). opentype.js -- which is what lv_font_conv reads fonts with -- only
    collects kern lookups of type 2 and silently returns none for type 9, so the
    generated face comes out with no kerning table at all and nothing says why.
    Cutting the font down to the range we actually render puts GPOS back under
    the limit, the lookups stay type 2, and the pairs survive.
    """
    from fontTools import subset, ttLib
    from fontTools.varLib import instancer

    font = ttLib.TTFont(SOURCE_TTF)
    # Every axis gets pinned, not just wght. Leaving one free keeps the font
    # variable, and the leftover gvar then trips the subsetter.
    limits = {axis.axisTag: axis.defaultValue for axis in font["fvar"].axes}
    limits["wght"] = weight
    instancer.instantiateVariableFont(font, limits, inplace=True)

    options = subset.Options(layout_features=["kern"], notdef_outline=True)
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=SUBSET_CODEPOINTS)
    subsetter.subset(font)

    font.save(dest)
    return dest


def convert(name: str, size: int, source: Path, dest: Path) -> None:
    cmd = [
        "npx",
        "--yes",
        "lv_font_conv@1.5.3",
        "--font", str(source),
        "--size", str(size),
        "--bpp", str(BPP),
        "--range", SYMBOLS_RANGE,
        "--format", "lvgl",
        "--lv-include", "lvgl.h",
        "--no-compress",  # decompression costs RAM at draw time; flash is cheap here
        "--force-fast-kern-format",
        "-o", str(dest),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"lv_font_conv failed for {name} at {size}px")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()[:12] if path.exists() else "-"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="regenerate into a temporary directory and report drift without writing",
    )
    args = parser.parse_args()

    if not SOURCE_TTF.exists():
        raise SystemExit(f"missing {SOURCE_TTF}")
    if shutil.which("npx") is None:
        raise SystemExit("npx not found -- lv_font_conv needs Node installed")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    scratch = OUT_DIR / ".check" if args.check else None
    if scratch:
        scratch.mkdir(exist_ok=True)

    weights = sorted({weight for _, _, weight in FACES})
    staging = Path(tempfile.mkdtemp(prefix="inter-instances-"))
    sources = {w: instance(w, staging / f"Inter-{w}.ttf") for w in weights}

    drift = False
    for name, size, weight in FACES:
        final = OUT_DIR / f"{name}.c"
        target = (scratch / f"{name}.c") if scratch else final
        before = digest(final)
        convert(name, size, sources[weight], target)
        after = digest(target)

        if args.check:
            changed = before != after
            drift = drift or changed
            print(f"{name:16} {size:>3}px  {'DRIFT' if changed else 'ok':>5}  {before}")
        else:
            kb = target.stat().st_size / 1024
            print(f"{name:16} {size:>3}px  {kb:7.1f} KB  {after}")

    shutil.rmtree(staging, ignore_errors=True)

    if scratch:
        shutil.rmtree(scratch)
        if drift:
            print("\ncheckedin fonts differ from a fresh conversion", file=sys.stderr)
            return 1
        return 0

    # lv_font_conv emits the definitions but no header, and the faces are used
    # from C++ where a missing declaration is an error rather than a guess.
    decls = "\n".join(f"LV_FONT_DECLARE({name});" for name, _, _ in FACES)
    (OUT_DIR / "ui_fonts.h").write_text(
        f"""// Generated by tools/mkfont_lvgl.py -- do not hand-edit.
#pragma once

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

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
