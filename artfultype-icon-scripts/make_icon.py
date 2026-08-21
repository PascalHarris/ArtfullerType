#!/usr/bin/env python3
"""
make_icon.py -- convert a 32x32 image into the hex text for ArtfulType's
'ICN#' (128) resource in main.r.

Classic Mac ICN# format: two 128-byte blocks (icon bitmap, then mask),
each 32 rows x 4 bytes/row (32 pixels / 8 bits-per-byte), 1 bit per
pixel, packed most-significant-bit-first. In the bitmap block, bit=1
means black, bit=0 means white. In the mask block, bit=1 means opaque
(the icon's own pixel shows), bit=0 means transparent (the desktop
shows through). This script's decoding/encoding was verified by
round-tripping ArtfulType's own, existing icon data byte-for-byte
before being used on anything new.

Usage:
    python3 make_icon.py input.png [-o output.txt] [--threshold 128] [--resize]

    input.png    Any image PIL can read. Must be exactly 32x32 unless
                 --resize is given.
    -o           Output file for the Rez hex text (default: stdout).
    --threshold  Grayscale cutoff for black/white, 0-255 (default 128).
                 Lower values make more pixels count as black.
    --resize     Resize the input to 32x32 (nearest-neighbor) instead
                 of requiring exact dimensions. For 1-bit icon art,
                 designing at the exact target size and skipping this
                 flag gives more predictable results.

Transparency: if the input image has an alpha channel, pixels with
alpha below --threshold become transparent (mask bit 0). Otherwise
the mask is fully opaque (a plain rectangular icon, no transparency).

Output: paste the printed block directly over the two $"..." blocks
inside the existing `resource 'ICN#' (128) { ... }` in main.r --
the icon bitmap first, then the mask, exactly as they appear now.
"""

import argparse
import sys
from PIL import Image

WIDTH = 32
HEIGHT = 32
ROW_BYTES = WIDTH // 8


def pack_bitmap(get_bit):
    """get_bit(x, y) -> 0 or 1. Returns ROW_BYTES*HEIGHT bytes, MSB-first."""
    out = bytearray(ROW_BYTES * HEIGHT)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if get_bit(x, y):
                out[y * ROW_BYTES + x // 8] |= (0x80 >> (x % 8))
    return bytes(out)


def format_rez_hex(data):
    """Match main.r's own $"..." layout: 8 lines, 16 hex bytes each."""
    hex_str = data.hex().upper()
    assert len(hex_str) == 256, f"expected 256 hex chars (128 bytes), got {len(hex_str)}"
    lines = [hex_str[i:i + 32] for i in range(0, len(hex_str), 32)]
    return "\n".join(f'        ${chr(34)}{line}{chr(34)}' for line in lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="input image (32x32 unless --resize)")
    ap.add_argument("-o", "--output", help="output text file (default: stdout)")
    ap.add_argument("--threshold", type=int, default=128, help="black/white cutoff, 0-255 (default 128)")
    ap.add_argument("--resize", action="store_true", help="resize input to 32x32 instead of requiring exact size")
    args = ap.parse_args()

    img = Image.open(args.input)

    if img.size != (WIDTH, HEIGHT):
        if args.resize:
            img = img.resize((WIDTH, HEIGHT), Image.NEAREST)
        else:
            sys.exit(f"error: input is {img.size[0]}x{img.size[1]}, need exactly "
                      f"{WIDTH}x{HEIGHT} (or pass --resize)")

    has_alpha = "A" in img.getbands()
    rgba = img.convert("RGBA")
    px = rgba.load()

    def icon_bit(x, y):
        r, g, b, a = px[x, y]
        gray = (r + g + b) / 3
        return 1 if gray < args.threshold else 0

    def mask_bit(x, y):
        if not has_alpha:
            return 1  # fully opaque, plain rectangular icon
        a = px[x, y][3]
        return 1 if a >= args.threshold else 0

    icon_data = pack_bitmap(icon_bit)
    mask_data = pack_bitmap(mask_bit)

    out = []
    out.append("/* Icon bitmap (first block of ICN# 128) */")
    out.append(format_rez_hex(icon_data) + ",")
    out.append("")
    out.append("/* Mask (second block of ICN# 128) */")
    out.append(format_rez_hex(mask_data))

    text = "\n".join(out) + "\n"

    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
        print(f"wrote {args.output}", file=sys.stderr)
    else:
        print(text)


if __name__ == "__main__":
    main()
