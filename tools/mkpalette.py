#!/usr/bin/env python3
"""Emit the RGB565 palette block for firmware/src/theme.h.

Packing 8/8/8 into 5/6/5 by hand is error prone in a way that is invisible
until the board is on a desk: a dark green (#386800) hand-packed as 0x3540 is
actually #31AA00, a glaring lime, and nothing in the build complains. Colours
are therefore declared here as hex and generated.

Usage:
    tools/mkpalette.py            # print the block
    tools/mkpalette.py --check    # verify theme.h matches, exit 1 if not
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

THEME = Path(__file__).resolve().parent.parent / "firmware/src/theme.h"

# (constant, hex, trailing comment)
PALETTE = [
    ("COL_BG", "#000000", "page background"),
    ("COL_CARD", "#141416", "card fill"),
    ("COL_CARD_EDGE", "#26262A", "1px card hairline"),
    ("COL_TEXT", "#FFFFFF", ""),
    ("COL_MUTED", "#9A9AA0", "secondary type"),
    ("COL_TRACK", "#2E2740", "progress bar groove"),
    ("COL_RULE", "#3A3A44", "inactive glyph"),
    ("COL_CHEVRON", "#5A5A62", ""),
    (None, None, None),
    ("COL_PILL_BG", "#3B2E52", "Current badge"),
    ("COL_PILL_TX", "#CDBCEC", ""),
    ("COL_PILL2_BG", "#2C4A10", "Weekly badge"),
    ("COL_PILL2_TX", "#B6E86A", ""),
    (None, None, None),
    ("COL_LIME", "#C7E93B", "usage ramp"),
    ("COL_GREEN", "#7FD93C", ""),
    ("COL_AMBER", "#FFB020", ""),
    ("COL_ORANGE", "#FF8A3D", ""),
    ("COL_RED", "#F04A2A", ""),
    ("COL_ACCENT", "#F5822E", "brand orange"),
    (None, None, None),
    ("COL_NAV_EDGE", "#1E1E22", "tab bar hairline"),
    ("COL_NAV_TX", "#8E8E94", "inactive tab label"),
    (None, None, None),
    ("COL_TAB1_BG", "#40230A", "Claude tile"),
    ("COL_TAB2_BG", "#241E44", "Weekly tile"),
    ("COL_TAB2_FG", "#8C83FF", ""),
    ("COL_TAB3_BG", "#0E1E3C", "Weather tile"),
    ("COL_TAB3_FG", "#3CB4FF", ""),
    ("COL_TAB4_BG", "#2A2404", "Crypto tile"),
    ("COL_TAB4_FG", "#FFD400", ""),
    ("COL_TAB5_BG", "#2C2C30", "Setting tile"),
    ("COL_TAB5_FG", "#B4B4BA", ""),
]


def pack565(hex_colour: str) -> int:
    h = hex_colour.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def render() -> str:
    width = max(len(name) for name, _, _ in PALETTE if name)
    lines = []
    for name, hex_colour, note in PALETTE:
        if name is None:
            lines.append("")
            continue
        comment = hex_colour + (f"  {note}" if note else "")
        lines.append(
            f"constexpr uint16_t {name:<{width}} = 0x{pack565(hex_colour):04X};  // {comment}"
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    block = render()
    if not args.check:
        print(block)
        return 0

    source = THEME.read_text(encoding="utf-8")
    for name, hex_colour, _ in PALETTE:
        if name is None:
            continue
        match = re.search(rf"^constexpr uint16_t {name}\s*=\s*(0x[0-9A-Fa-f]{{4}})", source, re.M)
        if not match:
            print(f"missing: {name}", file=sys.stderr)
            return 1
        if int(match.group(1), 16) != pack565(hex_colour):
            print(
                f"stale: {name} is {match.group(1)}, "
                f"{hex_colour} packs to 0x{pack565(hex_colour):04X}",
                file=sys.stderr,
            )
            return 1
    print("theme.h palette matches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
