Icon and splash-image conversion scripts for ArtfulType
=========================================================

Both require Python 3 and Pillow (pip install Pillow).

Both were verified by round-tripping ArtfulType's own, existing icon
and splash graphic through them and confirming the output matches
the current main.r / splash_image.h byte-for-byte.

make_icon.py -- app icon (ICN# resource in main.r)
----------------------------------------------------
Input: a 32x32 image. If it has an alpha channel, transparent areas
become the icon's mask; otherwise the icon is fully opaque.

    python3 make_icon.py my_icon.png

Prints two hex blocks. Paste them directly over the two existing
$"..." blocks inside `resource 'ICN#' (128) { ... }` in main.r --
icon bitmap first, then mask, in that order.

make_splash_image.py -- About box / splash graphic (splash_image.h)
----------------------------------------------------------------------
Input: a 128x100 image (grayscale/color both fine, thresholded to
black/white -- no transparency, since this graphic is drawn opaque).

    python3 make_splash_image.py my_splash.png -o splash_image.h

Overwrites splash_image.h directly (pass -o to point at wherever
your copy lives). No other file needs to change -- splash.c
#includes this file as-is.

Both scripts:
- --threshold N     adjust the black/white cutoff (0-255, default 128)
- --resize          resize input to the required dimensions instead
                     of requiring an exact match (nearest-neighbor;
                     for 1-bit art, designing at the exact target
                     size usually gives better results than resizing)
- -o FILE           write to a file instead of stdout
