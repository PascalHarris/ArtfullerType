#ifndef ARTFULTYPE_PREFERENCES_H
#define ARTFULTYPE_PREFERENCES_H

/*
    PREFERENCES_DESIGN.md sections 2-5. The preferences file itself is
    plain text in the file's data fork (type 'PreF', creator 'ArtT'),
    hand-editable with any text editor -- section 3 has the exact
    schema (camelCase-name-space-value, // comments, CR line endings,
    the 16 named colors). This header just declares the in-memory
    shape LoadPreferences/SavePreferences (preferences.c) read and
    write that file into/from.
*/

typedef enum {
    kLineEndingMac = 0,     /* CR only */
    kLineEndingUnix = 1,    /* LF only */
    kLineEndingWindows = 2  /* CRLF */
} LineEndingMode;

/*
    One entry per color name in section 3's table. Used for all five
    *Color fields below; preferences.c's own color table (private to
    that file) is what actually maps each of these to a real RGBColor
    and back to its file-format name string. The name<->NamedColor
    direction stays private (only LoadPreferences/SavePreferences ever
    need it, for parsing/writing the file itself) -- the RGBColor
    direction is now public via NamedColorToRGB below, needed by R7/R8's
    dark mode and color mode rendering (markdown.c).
*/
typedef enum {
    kColorBlack, kColorWhite, kColorRed, kColorGreen, kColorBlue,
    kColorCyan, kColorMagenta, kColorYellow, kColorDarkGray,
    kColorMidGray, kColorLightGray, kColorLightBlue, kColorPink,
    kColorLightGreen, kColorBrown, kColorDarkBlue
} NamedColor;

typedef struct {
    short version;                  /* format version -- bump if fields change */

    Boolean defaultDistractionFree; /* new documents start in Distraction Free? */
    Boolean defaultMarkdownMode;    /* new documents start in Markdown (true) or Writer (false)? */

    Str255 markdownFontName;        /* e.g. "\pMonaco" */
    short markdownFontSize;         /* default 9 */

    short writerZoomIndex;          /* same index space as gWriterZoomIndex, zoom.c */

    Boolean markdownDarkMode;
    Boolean markdownColorMode;      /* only meaningful on color-capable screens */

    LineEndingMode lineEnding;

    /* Hand-edit only, per section 3 -- never shown in the Preferences
       window (see DoPreferences below), but still loaded/saved here so
       a Save from that window round-trips these untouched rather than
       silently resetting them to defaults. */
    NamedColor backgroundColor;
    NamedColor headingColor;
    NamedColor linkColor;
    NamedColor emphasisColor;
    NamedColor codeColor;
} Preferences;

extern Preferences gPrefs;

/* preferences.c */
void LoadPreferences(void);
void SavePreferences(void);
void DoPreferences(void);
RGBColor NamedColorToRGB(NamedColor color);

/*
    Whether the current screen can show actual color (Color QuickDraw
    present, current device depth > 2 bits) -- used to hide (not just
    disable) the color-mode checkbox in the Preferences window, per
    section 6/8.5's explicit rule, and will be reused by color mode's
    own rendering once a later milestone implements it. Same
    Gestalt(gestaltQuickdrawVersion, ...)/GetMainDevice technique this
    session already established and verified for an earlier, since-
    abandoned feature (menu-bar icon color preservation) -- that
    specific application was reverted, not this general capability
    check, which is re-implemented fresh here since the earlier one
    was removed along with the rest of that feature.
*/
Boolean ScreenSupportsColor(void);

#endif
