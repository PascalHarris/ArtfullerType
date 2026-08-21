#!/usr/bin/env python3
"""
make_splash_image.py -- convert a 128x100 image into the C array literal
for ArtfulType's splash_image.h (the About box / startup splash graphic).

Unlike the app icon, this isn't a classic Mac resource at all -- it's a
plain array of hex bytes, #included directly into a static array in
splash.c. Format: 1 bit per pixel, packed most-significant-bit-first,
16 bytes/row (128 pixels / 8), 100 rows, bit=1 means black. This
script's decoding/encoding was verified by round-tripping ArtfulType's
own, existing splash graphic byte-for-byte before being used on
anything new. There's no mask/transparency here -- splash.c draws this
bitmap with CopyBits directly onto the dialog, opaque throughout.

Usage:
    python3 make_splash_image.py input.png [-o splash_image.h] [--threshold 128] [--resize]

    input.png    Any image PIL can read. Must be exactly 128x100 unless
                 --resize is given.
    -o           Output file (default: stdout). Pass splash_image.h
                 directly to overwrite it in place.
    --threshold  Grayscale cutoff for black/white, 0-255 (default 128).
    --resize     Resize the input to 128x100 (nearest-neighbor) instead
                 of requiring exact dimensions.

Output: a plain array-literal body (no braces, matching how the
existing file is #included inside kSplashImageBits[]'s own {...} in
splash.c) -- 16 hex bytes per line, matching the existing layout
exactly. Overwrite splash_image.h with the output, or pass -o directly.
"""

import argparse
import sys
from PIL import Image

WIDTH = 128
HEIGHT = 100
ROW_BYTES = WIDTH // 8


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="input image (128x100 unless --resize)")
    ap.add_argument("-o", "--output", help="output file, e.g. splash_image.h (default: stdout)")
    ap.add_argument("--threshold", type=int, default=128, help="black/white cutoff, 0-255 (default 128)")
    ap.add_argument("--resize", action="store_true", help="resize input to 128x100 instead of requiring exact size")
    args = ap.parse_args()

    img = Image.open(args.input)

    if img.size != (WIDTH, HEIGHT):
        if args.resize:
            img = img.resize((WIDTH, HEIGHT), Image.NEAREST)
        else:
            sys.exit(f"error: input is {img.size[0]}x{img.size[1]}, need exactly "
                      f"{WIDTH}x{HEIGHT} (or pass --resize)")

    gray = img.convert("L")
    px = gray.load()

    data = bytearray(ROW_BYTES * HEIGHT)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if px[x, y] < args.threshold:
                data[y * ROW_BYTES + x // 8] |= (0x80 >> (x % 8))

    lines = []
    for row in range(HEIGHT):
        row_bytes = data[row * ROW_BYTES:(row + 1) * ROW_BYTES]
        hex_items = ", ".join(f"0x{b:02X}" for b in row_bytes)
        lines.append(f"    {hex_items},")

    text = "\n".join(lines) + "\n"

    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
        print(f"wrote {args.output}", file=sys.stderr)
    else:
        print(text)


if __name__ == "__main__":
    main()
