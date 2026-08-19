#include "app.h"
#include "preferences.h"

Preferences gPrefs;

#define kPrefsFileName "\pArtfulType Preferences"

/* Verified via Inside Macintosh (Toolbox Essentials, "Finder Interface
   Reference, Finding Directories, FindFolder"): "vRefNum -- The volume
   reference number (or the constant kOnSystemDisk for the startup
   disk)..." -- a real, stable, documented constant, but not one this
   toolchain's own generated headers define (checked directly against
   multiversal's defs, same gap zoomDocProc/WStateDataHandle were in
   earlier work on this app) -- defined locally rather than trusting an
   undefined identifier to compile. */
#define kOnSystemDisk -1

/*
    Named color table -- section 3's 16 names, each mapped to a
    NamedColor and a starting-point RGBColor. Kept private to this
    file for now: nothing outside preferences.c needs to resolve a
    NamedColor to an actual drawing color yet (that's later
    milestones' job, once dark mode/color mode rendering exists), so
    there's no public accessor here -- just what LoadPreferences/
    SavePreferences need to parse and write the *Color lines.

    RGBColor confidence varies sharply by entry, per
    PREFERENCES_DESIGN.md section 3: the first eight are the
    fully-saturated corners of the RGB cube (mathematically
    unambiguous), the other eight are placeholders pending real
    defaults after hands-on testing, not verified/final values.
*/
typedef struct {
    const char *name;
    NamedColor color;
    RGBColor rgb;
} ColorTableEntry;

static const ColorTableEntry kColorTable[] = {
    { "blackColor",   kColorBlack,      {0, 0, 0} },
    { "whiteColor",   kColorWhite,      {65535, 65535, 65535} },
    { "redColor",     kColorRed,        {65535, 0, 0} },
    { "greenColor",   kColorGreen,      {0, 65535, 0} },
    { "blueColor",    kColorBlue,       {0, 0, 65535} },
    { "cyanColor",    kColorCyan,       {0, 65535, 65535} },
    { "magentaColor", kColorMagenta,    {65535, 0, 65535} },
    { "yellowColor",  kColorYellow,     {65535, 65535, 0} },
    { "darkGray",     kColorDarkGray,   {21845, 21845, 21845} },
    { "midGray",      kColorMidGray,    {32768, 32768, 32768} },
    { "lightGray",    kColorLightGray,  {49152, 49152, 49152} },
    { "lightBlue",    kColorLightBlue,  {26214, 43690, 65535} },
    { "pink",         kColorPink,       {65535, 43690, 43690} },
    { "lightGreen",   kColorLightGreen, {43690, 65535, 43690} },
    { "brown",        kColorBrown,      {39321, 26214, 13107} },
    { "darkBlue",     kColorDarkBlue,   {0, 0, 39321} }
};

#define kNumColors (sizeof(kColorTable) / sizeof(kColorTable[0]))

static Boolean NameToColor(const char *name, long len, NamedColor *outColor)
{
    short i;

    for (i = 0; i < (short) kNumColors; i++) {
        if ((long) strlen(kColorTable[i].name) == len &&
            strncmp(kColorTable[i].name, name, (size_t) len) == 0) {
            *outColor = kColorTable[i].color;
            return true;
        }
    }
    return false;
}

static const char *ColorToName(NamedColor color)
{
    short i;

    for (i = 0; i < (short) kNumColors; i++) {
        if (kColorTable[i].color == color)
            return kColorTable[i].name;
    }
    return kColorTable[0].name; /* shouldn't happen; fall back to black */
}

/*
    Small, self-contained integer parse/format helpers -- deliberately
    not NumToString/StringToNum (Pascal-string based) or sprintf
    (unverified in this toolchain, and nothing elsewhere in this
    codebase uses it): everything else in this file works with raw
    (pointer, length) substrings from the file's own byte buffer
    rather than Pascal strings, so these match that shape directly
    instead of adding a string-representation conversion in the
    middle.
*/
static Boolean ParseInt(const char *s, long len, long *outValue)
{
    long value = 0;
    long i = 0;
    Boolean negative = false;
    Boolean sawDigit = false;

    if (len > 0 && s[0] == '-') {
        negative = true;
        i = 1;
    }
    for (; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false; /* stray non-digit -- malformed, per-line skip */
        value = value * 10 + (s[i] - '0');
        sawDigit = true;
    }
    if (!sawDigit)
        return false;

    *outValue = negative ? -value : value;
    return true;
}

static short AppendInt(char *buf, long value)
{
    char digits[16];
    short numDigits = 0;
    short i;
    short pos = 0;
    Boolean negative = false;

    if (value < 0) {
        negative = true;
        value = -value;
    }
    if (value == 0) {
        digits[numDigits++] = '0';
    } else {
        while (value > 0) {
            digits[numDigits++] = (char) ('0' + (value % 10));
            value /= 10;
        }
    }
    if (negative)
        buf[pos++] = '-';
    for (i = (short) (numDigits - 1); i >= 0; i--)
        buf[pos++] = digits[i];
    return pos;
}

/*
    Copies a (pointer, length) substring into a Str255, truncating to
    255 if somehow longer -- markdownFontName's own storage.
*/
static void CopyToPascalString(const char *src, long len, Str255 dst)
{
    if (len > 255)
        len = 255;
    dst[0] = (unsigned char) len;
    BlockMove((Ptr) src, (Ptr) (dst + 1), len);
}

/*
    DIAGNOSTIC ONLY -- not a fix. Temporary instrumentation to find out
    exactly where SavePreferences (and the button handling leading up
    to it) actually stops on real hardware, given a reported hang with
    no other visible symptom: no crash, no screen corruption, the
    cursor keeps tracking normally, only the dialog itself stops
    responding to clicks. A beep-based version of this was tried first
    and turned out impractical for exactly that reason -- nothing marks
    the moment a hang actually starts, so there's no reliable point to
    stop counting beeps against.

    Draws label directly into the Preferences window's own content
    area, overwriting whatever was there before -- assumes the current
    port is still that window throughout, true for this whole call
    chain since nothing between here and DoPreferences's own
    SetPort(dlg) touches the current port. Only the most recent
    checkpoint reached is ever visible, so whatever text is showing at
    the moment of a hang directly answers "how far did it get" by
    simply looking at the screen, with no timing involved. Remove every
    call to this, and this function itself, once the hang is actually
    located.
*/
static void DiagnosticCheckpoint(const char *label)
{
    Rect r;
    Str255 s;
    long len = (long) strlen(label);

    SetRect(&r, 10, 250, 440, 268);
    EraseRect(&r);
    MoveTo(12, 262);
    CopyToPascalString(label, len, s);
    DrawString(s);
}

/*
    Defaults, per PREFERENCES_DESIGN.md section 4 -- used to populate
    gPrefs before any parsing happens (LoadPreferences), so a missing
    file, or any individual line that fails to parse, simply leaves
    that field at its default rather than needing special-cased
    fallback logic per field.
*/
static void SetDefaultPreferences(void)
{
    gPrefs.version = 1;

    gPrefs.defaultDistractionFree = false;
    gPrefs.defaultMarkdownMode = false;

    CopyToPascalString("Monaco", 6, gPrefs.markdownFontName);
    gPrefs.markdownFontSize = 9;

    gPrefs.writerZoomIndex = kZoomBaselineIndex;

    gPrefs.markdownDarkMode = false;
    gPrefs.markdownColorMode = false;

    gPrefs.lineEnding = kLineEndingMac;

    gPrefs.backgroundColor = kColorWhite;
    gPrefs.headingColor = kColorLightBlue;
    gPrefs.linkColor = kColorDarkBlue;
    gPrefs.emphasisColor = kColorBrown;
    gPrefs.codeColor = kColorGreen;
}

/*
    Locates the preferences file's target folder -- PREFERENCES_DESIGN.md
    section 2. FindFolder (System 7's Alias Manager) is gated behind a
    Gestalt(gestaltFindFolderAttr, ...) check first, never called
    unconditionally -- an unimplemented trap on a System 6 Plus/SE (this
    app's own stated minimum target) generates a bus error, not a
    graceful failure, the same class of risk already corrected once
    this session over Color QuickDraw.

    Pre-System-7 fallback via SysEnvirons -- Inside Macintosh (Operating
    System Utilities, "The System Environment Record") documents
    sysVRefNum explicitly as "the working directory reference number of
    the folder... containing the open System file," i.e. the System
    Folder itself, not merely the startup volume, and HCreate/HOpen are
    separately documented to accept a working directory reference
    number directly as their own vRefNum parameter with dirID 0.

    Honest status: a real hang has been reported on a genuine SE
    running System 6.0.8, specifically when SavePreferences (HCreate,
    actually creating the file for the first time) exercises this
    fallback -- LoadPreferences's own HOpen call through the exact
    same values doesn't hang, though since LoadPreferences is designed
    to fall back to defaults silently on any failure, that only shows
    HOpen doesn't hang here, not that these values are actually
    correct. A first attempt used this direct passthrough; a second
    attempt added GetWDInfo to resolve sysVRefNum to an explicit
    (vRefNum, dirID) pair first, reasoning that HCreate creating a file
    for the first time might be a genuinely different, more sensitive
    path than HOpen's read attempt -- that didn't fix it either, so
    this reverts to the simpler, direct form rather than keep adding
    unverified complexity to a mechanism that's now failed twice.
    DiagnosticCheckpoint calls below (see that function's own comment)
    are temporary -- not a fix, a way to find out exactly where this
    actually stops on real hardware, since two reasoned guesses in a
    row haven't located it. Remove them once that's known.
*/
static Boolean FindPrefsFileLocation(short *outVRefNum, long *outDirID)
{
    long response;

    if (Gestalt(gestaltFindFolderAttr, &response) == noErr) {
        short foundVRefNum;
        long foundDirID;

        if (FindFolder(kOnSystemDisk, kPreferencesFolderType, true,
                        &foundVRefNum, &foundDirID) == noErr) {
            *outVRefNum = foundVRefNum;
            *outDirID = foundDirID;
            return true;
        }
    }

    DiagnosticCheckpoint("5 trying System Folder fallback");

    {
        SysEnvRec envRec;

        if (SysEnvirons(curSysEnvVers, &envRec) == noErr) {
            DiagnosticCheckpoint("6 SysEnvirons OK");

            *outVRefNum = envRec.sysVRefNum;
            *outDirID = 0;
            return true;
        }
    }

    return false;
}

/*
    Applies one already-split (key, value) pair to gPrefs -- called
    once per non-comment, non-blank line by LoadPreferences's own
    parsing loop below. Unrecognized keys, and values that don't parse
    per that key's own type, are silently skipped -- per section 3's
    own per-line-tolerance note, a hand-edited text file is exactly
    the kind of thing a typo can creep into, and one bad line
    shouldn't take down every other correctly-set preference.
*/
static void ApplyPreferenceLine(const char *key, long keyLen,
                                 const char *value, long valueLen)
{
    long n;

    #define KEY_IS(str) (keyLen == (long) strlen(str) && strncmp(key, str, (size_t) keyLen) == 0)
    #define VALUE_IS(str) (valueLen == (long) strlen(str) && strncmp(value, str, (size_t) valueLen) == 0)

    if (KEY_IS("defaultDistractionFree")) {
        if (VALUE_IS("true")) gPrefs.defaultDistractionFree = true;
        else if (VALUE_IS("false")) gPrefs.defaultDistractionFree = false;
    } else if (KEY_IS("defaultMarkdownMode")) {
        if (VALUE_IS("true")) gPrefs.defaultMarkdownMode = true;
        else if (VALUE_IS("false")) gPrefs.defaultMarkdownMode = false;
    } else if (KEY_IS("markdownFontName")) {
        if (valueLen > 0)
            CopyToPascalString(value, valueLen, gPrefs.markdownFontName);
    } else if (KEY_IS("markdownFontSize")) {
        if (ParseInt(value, valueLen, &n) && n > 0 && n <= 255)
            gPrefs.markdownFontSize = (short) n;
    } else if (KEY_IS("writerZoomIndex")) {
        if (ParseInt(value, valueLen, &n) && n >= 0 && n < kNumZoomLevels)
            gPrefs.writerZoomIndex = (short) n;
    } else if (KEY_IS("markdownDarkMode")) {
        if (VALUE_IS("true")) gPrefs.markdownDarkMode = true;
        else if (VALUE_IS("false")) gPrefs.markdownDarkMode = false;
    } else if (KEY_IS("markdownColorMode")) {
        if (VALUE_IS("true")) gPrefs.markdownColorMode = true;
        else if (VALUE_IS("false")) gPrefs.markdownColorMode = false;
    } else if (KEY_IS("lineEnding")) {
        if (VALUE_IS("mac")) gPrefs.lineEnding = kLineEndingMac;
        else if (VALUE_IS("unix")) gPrefs.lineEnding = kLineEndingUnix;
        else if (VALUE_IS("windows")) gPrefs.lineEnding = kLineEndingWindows;
    } else if (KEY_IS("backgroundColor")) {
        NamedColor c;
        if (NameToColor(value, valueLen, &c)) gPrefs.backgroundColor = c;
    } else if (KEY_IS("headingColor")) {
        NamedColor c;
        if (NameToColor(value, valueLen, &c)) gPrefs.headingColor = c;
    } else if (KEY_IS("linkColor")) {
        NamedColor c;
        if (NameToColor(value, valueLen, &c)) gPrefs.linkColor = c;
    } else if (KEY_IS("emphasisColor")) {
        NamedColor c;
        if (NameToColor(value, valueLen, &c)) gPrefs.emphasisColor = c;
    } else if (KEY_IS("codeColor")) {
        NamedColor c;
        if (NameToColor(value, valueLen, &c)) gPrefs.codeColor = c;
    }
    /* else: unrecognized key -- ignored, per section 3. */

    #undef KEY_IS
    #undef VALUE_IS
}

/*
    Parses the whole loaded file buffer, one CR-terminated line at a
    time -- section 3's format. Each line: skip leading whitespace;
    if empty or starting with "//", skip (blank/whole-line comment);
    otherwise split into (key, value) on the first run of whitespace,
    then trim the value at the first "//" (trailing comment) or
    end-of-line, trimming trailing whitespace from the value either
    way.
*/
static void ParsePreferencesBuffer(const char *buf, long len)
{
    long pos = 0;

    while (pos < len) {
        long lineStart, lineEnd;
        long p;
        long keyStart, keyEnd;
        long valueStart, valueEnd;

        lineStart = pos;
        while (pos < len && buf[pos] != '\r')
            pos++;
        lineEnd = pos;
        if (pos < len)
            pos++; /* skip the CR itself */

        p = lineStart;
        while (p < lineEnd && (buf[p] == ' ' || buf[p] == '\t'))
            p++;

        if (p >= lineEnd)
            continue; /* blank line */
        if (p + 1 < lineEnd && buf[p] == '/' && buf[p + 1] == '/')
            continue; /* whole-line comment */

        keyStart = p;
        while (p < lineEnd && buf[p] != ' ' && buf[p] != '\t')
            p++;
        keyEnd = p;

        while (p < lineEnd && (buf[p] == ' ' || buf[p] == '\t'))
            p++;

        valueStart = p;
        valueEnd = lineEnd;
        for (p = valueStart; p < lineEnd - 1; p++) {
            if (buf[p] == '/' && buf[p + 1] == '/') {
                valueEnd = p;
                break;
            }
        }
        while (valueEnd > valueStart &&
               (buf[valueEnd - 1] == ' ' || buf[valueEnd - 1] == '\t'))
            valueEnd--;

        if (keyEnd > keyStart)
            ApplyPreferenceLine(buf + keyStart, keyEnd - keyStart,
                                 buf + valueStart, valueEnd - valueStart);
    }
}

/*
    Loads gPrefs from disk, falling back to defaults (SetDefaultPreferences)
    on any failure -- missing file, can't locate a folder to look in,
    anything -- rather than erroring. Per PREFERENCES_DESIGN.md section 5,
    must run before the first CreateNewDocument() call in main(), since
    the default-window-mode preference needs to be known before that
    document is created, not applied retroactively after.
*/
/*
    Whether a font name corresponds to an actually-installed font --
    used both here (fall back to the default if the stored font was
    deleted since it was last saved) and by DoPreferences below (in
    case a font is deleted mid-session, after LoadPreferences already
    ran at startup). GetFNum alone can't distinguish "this name isn't
    installed, so GetFNum silently defaulted to font 0" from "this
    name really is font 0's own name" -- 0 (the system font,
    typically Chicago) is itself a perfectly valid answer, not an
    error code. The reliable way around that is the round trip this
    does: ask GetFNum for a number, ask GetFontName what THAT number's
    real name is, and compare. A genuinely installed name round-trips
    back to itself; anything else means GetFNum silently fell back to
    the system font instead of actually finding a match.
*/
static Boolean FontIsInstalled(const Str255 name)
{
    short fNum;
    Str255 roundTrip;

    GetFNum(name, &fNum);
    GetFontName(fNum, roundTrip);
    return EqualString(name, roundTrip, false, false);
}

void LoadPreferences(void)
{
    short vRefNum;
    long dirID;
    short refNum;
    long eof;
    Handle bufH;
    OSErr err;

    SetDefaultPreferences();

    if (!FindPrefsFileLocation(&vRefNum, &dirID))
        return;

    err = HOpen(vRefNum, dirID, kPrefsFileName, fsRdPerm, &refNum);
    if (err != noErr)
        return; /* no prefs file yet -- defaults already set above */

    GetEOF(refNum, &eof);
    if (eof <= 0) {
        FSClose(refNum);
        return;
    }

    bufH = NewHandle(eof);
    if (bufH == NULL) {
        FSClose(refNum);
        return;
    }

    HLock(bufH);
    {
        long count = eof;
        FSRead(refNum, &count, *bufH);
        ParsePreferencesBuffer(*bufH, count);
    }
    HUnlock(bufH);
    DisposeHandle(bufH);
    FSClose(refNum);

    /* Robustness per explicit direction: a font name loaded from the
       file that no longer corresponds to an installed font (deleted
       since the file was last saved) falls back to the default
       (Monaco) rather than being kept as a name nothing can resolve. */
    if (!FontIsInstalled(gPrefs.markdownFontName))
        CopyToPascalString("Monaco", 6, gPrefs.markdownFontName);
}

/*
    Writes gPrefs to disk in section 3's exact plain-text format --
    not wired to anything yet (no window, no menu item exist as of
    this milestone), but complete and correct: later milestones that
    do call this depend on every field, including the five hand-edit-
    only *Color ones, round-tripping faithfully rather than being
    silently reset to defaults on save.

    Regenerates the file's full canonical content (comment header
    included) every time -- per section 3's own note, any custom
    comments a user added beyond the app's own generated ones won't
    survive a save; only the *values* round-trip, not arbitrary
    hand-added text.
*/
void SavePreferences(void)
{
    short vRefNum;
    long dirID;
    short refNum;
    OSErr err;
    static char buf[2048];
    long len = 0;

    #define WRITE_STR(s) do { \
        long slen = (long) strlen(s); \
        BlockMove((Ptr) (s), (Ptr) (buf + len), slen); \
        len += slen; \
    } while (0)
    #define WRITE_LINE(s) do { WRITE_STR(s); buf[len++] = '\r'; } while (0)
    #define WRITE_KEYVAL(k, v) do { WRITE_STR(k); buf[len++] = ' '; WRITE_STR(v); buf[len++] = '\r'; } while (0)
    #define WRITE_KEYINT(k, v) do { \
        WRITE_STR(k); \
        buf[len++] = ' '; \
        len += AppendInt(buf + len, (v)); \
        buf[len++] = '\r'; \
    } while (0)

    WRITE_LINE("// ArtfulType preferences -- edit directly with any text editor (e.g.");
    WRITE_LINE("// BBEdit). One preference per line: a camelCase name, then its value,");
    WRITE_LINE("// space-separated. // starts a comment, whole-line or trailing. Blank");
    WRITE_LINE("// lines are ignored. Boolean values are true or false.");
    WRITE_LINE("//");
    WRITE_LINE("// Colour names, for the *Color preferences below -- exactly these strings:");
    WRITE_LINE("//   blackColor, whiteColor, redColor, greenColor, blueColor, cyanColor,");
    WRITE_LINE("//   magentaColor, yellowColor, darkGray, midGray, lightGray, lightBlue,");
    WRITE_LINE("//   pink, lightGreen, brown, darkBlue");
    buf[len++] = '\r';

    WRITE_KEYVAL("defaultDistractionFree", gPrefs.defaultDistractionFree ? "true" : "false");
    WRITE_KEYVAL("defaultMarkdownMode", gPrefs.defaultMarkdownMode ? "true" : "false");

    WRITE_STR("markdownFontName ");
    BlockMove((Ptr) (gPrefs.markdownFontName + 1), (Ptr) (buf + len), gPrefs.markdownFontName[0]);
    len += gPrefs.markdownFontName[0];
    buf[len++] = '\r';

    WRITE_KEYINT("markdownFontSize", gPrefs.markdownFontSize);
    WRITE_KEYINT("writerZoomIndex", gPrefs.writerZoomIndex);
    WRITE_KEYVAL("markdownDarkMode", gPrefs.markdownDarkMode ? "true" : "false");
    WRITE_KEYVAL("markdownColorMode", gPrefs.markdownColorMode ? "true" : "false");

    switch (gPrefs.lineEnding) {
        case kLineEndingUnix:    WRITE_KEYVAL("lineEnding", "unix"); break;
        case kLineEndingWindows: WRITE_KEYVAL("lineEnding", "windows"); break;
        default:                 WRITE_KEYVAL("lineEnding", "mac"); break;
    }

    buf[len++] = '\r';
    WRITE_LINE("// Colours used by colour mode (markdownColorMode) and by dark mode's");
    WRITE_LINE("// background (markdownDarkMode) -- hand-edit only, not shown in the");
    WRITE_LINE("// Preferences window.");
    WRITE_KEYVAL("backgroundColor", ColorToName(gPrefs.backgroundColor));
    WRITE_KEYVAL("headingColor", ColorToName(gPrefs.headingColor));
    WRITE_KEYVAL("linkColor", ColorToName(gPrefs.linkColor));
    WRITE_KEYVAL("emphasisColor", ColorToName(gPrefs.emphasisColor));
    WRITE_KEYVAL("codeColor", ColorToName(gPrefs.codeColor));

    #undef WRITE_STR
    #undef WRITE_LINE
    #undef WRITE_KEYVAL
    #undef WRITE_KEYINT

    /* Defensive: len should never come close to buf's own 2048-byte
       size given everything written above, but this guards against a
       corrupted or unexpectedly large byte count ever reaching
       FSWrite below -- a bug here (e.g. from a future addition to
       this function) would otherwise hand FSWrite a bogus count
       rather than fail visibly at the point of the actual mistake. */
    if (len <= 0 || len > (long) sizeof(buf))
        return;

    if (!FindPrefsFileLocation(&vRefNum, &dirID))
        return;

    DiagnosticCheckpoint("7 location found");

    /* Result deliberately not checked -- matches file.c's own WriteFile
       convention exactly: if this fails because the file already
       exists (the normal case after the first save), that's fine; any
       other real failure means the HOpen just below fails too, and
       that's what's actually checked. */
    HCreate(vRefNum, dirID, kPrefsFileName, 'ArtT', 'PreF');

    DiagnosticCheckpoint("8 HCreate returned");

    err = HOpen(vRefNum, dirID, kPrefsFileName, fsWrPerm, &refNum);
    if (err != noErr)
        return;

    DiagnosticCheckpoint("9 HOpen OK");

    SetEOF(refNum, 0);

    DiagnosticCheckpoint("10 SetEOF returned");

    {
        long count = len;
        FSWrite(refNum, &count, buf);
    }

    DiagnosticCheckpoint("11 FSWrite returned");

    FSClose(refNum);

    DiagnosticCheckpoint("12 FSClose returned, Save done");
}

/*
    Preferences window -- PREFERENCES_DESIGN.md sections 6/7. DITL(140)
    (main.r) defines the item list this operates on; kPrefs*Item below
    are 1-based item numbers matching that list's order exactly -- if
    the DITL is ever reordered, these have to move with it.
*/
#define kPrefsDialogID 140

#define kPrefsSaveItem        1
#define kPrefsCancelItem      2
#define kPrefsWindowedItem    4
#define kPrefsDistractionItem 5
#define kPrefsWriterItem      7
#define kPrefsMarkdownItem    8
#define kPrefsFontNameItem    10
#define kPrefsFontSizeItem    12
#define kPrefsZoomItem        14
#define kPrefsDarkModeItem    15
#define kPrefsColorModeItem   16
#define kPrefsLineEndMacItem  18
#define kPrefsLineEndUnixItem 19
#define kPrefsLineEndWinItem  20

/*
    Whether the current screen can show actual color -- see the
    comment on this function's own declaration (preferences.h) for why
    this is a fresh implementation rather than a reuse of the earlier,
    since-removed ScreenSupportsColorIcons. Two conditions, both
    required: Color QuickDraw has to be present at all -- checked via
    Gestalt rather than just calling GetMainDevice and checking for
    NULL, since an unimplemented trap on a Color-QuickDraw-less system
    doesn't reliably return NULL, it returns whatever garbage was left
    in the result register -- and the CURRENT screen's own depth has
    to be more than 2 bits, the same threshold this session has used
    consistently for "can this screen usefully show more than black
    and white."
*/
Boolean ScreenSupportsColor(void)
{
    long qdVersion;

    if (Gestalt(gestaltQuickdrawVersion, &qdVersion) != noErr)
        return false;
    if (qdVersion < gestalt8BitQD)
        return false;

    return ((**(**GetMainDevice()).gdPMap).pixelSize > 2);
}

/*
    Converts a long to a Str255, reusing AppendInt/CopyToPascalString
    above rather than a separate Pascal-string-specific formatter --
    DRY, and both of those are already proven correct from
    SavePreferences's own use of AppendInt.
*/
static void IntToPascalString(long value, Str255 dst)
{
    char buf[16];
    short len = AppendInt(buf, value);

    CopyToPascalString(buf, len, dst);
}

/*
    Parses a Str255 as an integer, reusing ParseInt above by treating
    the Pascal string's own character data as the (pointer, length)
    substring ParseInt already expects.
*/
static Boolean ParsePascalInt(const Str255 src, long *outValue)
{
    return ParseInt((const char *) (src + 1), src[0], outValue);
}

/*
    Checks exactly one item in a radio-button group (items[0..count-1])
    -- selectedItem -- and unchecks the rest. DITL radio buttons have
    no built-in group behavior of their own; the app is responsible for
    enforcing "only one checked" by hand, both when first showing the
    dialog (pre-filling from gPrefs) and again every time the user
    clicks a different button in the same group during the modal loop
    (DoPreferences below).
*/
static void SetRadioGroup(DialogPtr dlg, const short *items, short count, short selectedItem)
{
    short i;
    short itemType;
    Handle itemH;
    Rect itemRect;

    for (i = 0; i < count; i++) {
        GetDialogItem(dlg, items[i], &itemType, &itemH, &itemRect);
        SetControlValue((ControlHandle) itemH, (items[i] == selectedItem) ? 1 : 0);
    }
}

/*
    The reverse of SetRadioGroup -- which item in the group is
    currently checked, read back when Save is clicked.
*/
static short GetCheckedRadioItem(DialogPtr dlg, const short *items, short count)
{
    short i;
    short itemType;
    Handle itemH;
    Rect itemRect;

    for (i = 0; i < count; i++) {
        GetDialogItem(dlg, items[i], &itemType, &itemH, &itemRect);
        if (GetControlValue((ControlHandle) itemH) != 0)
            return items[i];
    }
    return items[0]; /* shouldn't happen -- some item is always checked */
}

/*
    Resource ID shared by CNTL(200) and MENU(200) in main.r -- the
    Markdown-font popup control (DITL 140 item 10) and its underlying
    menu. popupUseAddResMenu (CNTL 200's own procID) makes the Control
    Manager itself append one item per installed 'FOND' resource to
    this menu automatically, at the moment GetNewDialog creates the
    control -- no AppendResMenu call needed here; that's handled
    entirely by the resource-level configuration. Rebuilt fresh every
    time GetNewDialog runs (a new control instance each call), so the
    list always reflects whatever's actually installed right now.
*/
#define kFontPopupMenuID 200

/*
    Which item number in the font popup's own (auto-populated) menu
    matches a given font name -- used to set the control's value
    (GetControlValue/SetControlValue track the popup's current
    selection by item number, not by name) when pre-filling it from
    scratch.markdownFontName. Falls back to item 1 if nothing matches,
    which shouldn't happen in practice since the caller already runs
    the name through FontIsInstalled first -- a defensive fallback
    rather than a expected path.
*/
static short FindFontMenuItemNumber(MenuHandle menu, const Str255 name)
{
    short count = CountMItems(menu);
    short i;
    Str255 itemName;

    for (i = 1; i <= count; i++) {
        GetMenuItemText(menu, i, itemName);
        if (EqualString(name, itemName, false, false))
            return i;
    }
    return 1;
}

/*
    Edit > Preferences... -- PREFERENCES_DESIGN.md sections 6/7.

    Operates on a scratch copy of gPrefs throughout, per section 6's
    Save/Cancel semantics -- gPrefs itself, and the on-disk file, are
    only touched if Save is clicked; Cancel leaves both completely
    alone. None of the six preferences are wired to actually change app
    behavior yet (that's each of PREFERENCES_DESIGN.md section 8's own
    later milestones) -- this function only reads/writes gPrefs's
    fields and calls SavePreferences, nothing else, matching this
    milestone's own explicit scope. One deliberate exception already
    implied by that scope boundary: gWriterZoomIndex (zoom.c) itself is
    never touched here, even though its value is displayed and can be
    edited -- only scratch.writerZoomIndex/gPrefs.writerZoomIndex are,
    since actually applying an edited zoom value to the live Writer
    view is section 8.3's job, not this milestone's.

    The five *Color preferences are never touched here at all, in
    either direction -- no control in this dialog reads or writes them,
    per section 3/6's "hand-edit only" rule -- so whatever gPrefs
    already held for those five before this dialog opened is exactly
    what gets saved back, unchanged.
*/
void DoPreferences(void)
{
    DialogPtr dlg;
    short itemHit;
    Preferences scratch;
    short itemType;
    Handle itemH;
    Rect itemRect;
    Str255 numStr;
    static const short kWindowItems[] = { kPrefsWindowedItem, kPrefsDistractionItem };
    static const short kViewItems[] = { kPrefsWriterItem, kPrefsMarkdownItem };
    static const short kLineEndItems[] = { kPrefsLineEndMacItem, kPrefsLineEndUnixItem, kPrefsLineEndWinItem };
    Boolean colorCapable;

    scratch = gPrefs;

    dlg = GetNewDialog(kPrefsDialogID, NULL, (WindowPtr) -1L);
    if (dlg == NULL)
        return;

    SetPort(dlg);

    SetRadioGroup(dlg, kWindowItems, 2,
                  scratch.defaultDistractionFree ? kPrefsDistractionItem : kPrefsWindowedItem);
    SetRadioGroup(dlg, kViewItems, 2,
                  scratch.defaultMarkdownMode ? kPrefsMarkdownItem : kPrefsWriterItem);
    {
        short lineEndItem = kPrefsLineEndMacItem;

        if (scratch.lineEnding == kLineEndingUnix) lineEndItem = kPrefsLineEndUnixItem;
        else if (scratch.lineEnding == kLineEndingWindows) lineEndItem = kPrefsLineEndWinItem;
        SetRadioGroup(dlg, kLineEndItems, 3, lineEndItem);
    }

    GetDialogItem(dlg, kPrefsDarkModeItem, &itemType, &itemH, &itemRect);
    SetControlValue((ControlHandle) itemH, scratch.markdownDarkMode ? 1 : 0);

    colorCapable = ScreenSupportsColor();
    GetDialogItem(dlg, kPrefsColorModeItem, &itemType, &itemH, &itemRect);
    SetControlValue((ControlHandle) itemH, scratch.markdownColorMode ? 1 : 0);
    if (!colorCapable) {
        /* Hidden entirely, not merely disabled -- per section 6/8.5's
           explicit rule: a preference that can't actually be set on
           this machine shouldn't appear in this window at all. */
        HideDialogItem(dlg, kPrefsColorModeItem);
    }

    GetDialogItem(dlg, kPrefsFontNameItem, &itemType, &itemH, &itemRect);
    /* Belt-and-suspenders alongside LoadPreferences's own check: that
       one runs once at startup, but a font could in principle be
       deleted later in the same session, before this window opens. */
    if (!FontIsInstalled(scratch.markdownFontName))
        CopyToPascalString("Monaco", 6, scratch.markdownFontName);
    SetControlValue((ControlHandle) itemH,
                     FindFontMenuItemNumber(GetMenu(kFontPopupMenuID), scratch.markdownFontName));

    GetDialogItem(dlg, kPrefsFontSizeItem, &itemType, &itemH, &itemRect);
    IntToPascalString(scratch.markdownFontSize, numStr);
    SetDialogItemText(itemH, numStr);

    GetDialogItem(dlg, kPrefsZoomItem, &itemType, &itemH, &itemRect);
    /* Pre-fill rule per section 6, exactly: the live, current Writer
       zoom if a document is open, since gWriterZoomIndex by now
       already reflects either the loaded-from-prefs value or whatever
       the user has changed it to this session -- the stored/default
       preference value otherwise. */
    IntToPascalString((FrontDocument() != NULL) ? gWriterZoomIndex : scratch.writerZoomIndex, numStr);
    SetDialogItemText(itemH, numStr);

    SelectDialogItemText(dlg, kPrefsFontSizeItem, 0, 32767);

    for (;;) {
        ModalDialog(NULL, &itemHit);

        if (itemHit == kPrefsSaveItem || itemHit == kPrefsCancelItem)
            break;

        if (itemHit == kPrefsWindowedItem || itemHit == kPrefsDistractionItem) {
            SetRadioGroup(dlg, kWindowItems, 2, itemHit);
        } else if (itemHit == kPrefsWriterItem || itemHit == kPrefsMarkdownItem) {
            SetRadioGroup(dlg, kViewItems, 2, itemHit);
        } else if (itemHit == kPrefsLineEndMacItem || itemHit == kPrefsLineEndUnixItem ||
                   itemHit == kPrefsLineEndWinItem) {
            SetRadioGroup(dlg, kLineEndItems, 3, itemHit);
        } else if (itemHit == kPrefsDarkModeItem) {
            GetDialogItem(dlg, kPrefsDarkModeItem, &itemType, &itemH, &itemRect);
            SetControlValue((ControlHandle) itemH, (GetControlValue((ControlHandle) itemH) == 0) ? 1 : 0);
        } else if (itemHit == kPrefsColorModeItem && colorCapable) {
            GetDialogItem(dlg, kPrefsColorModeItem, &itemType, &itemH, &itemRect);
            SetControlValue((ControlHandle) itemH, (GetControlValue((ControlHandle) itemH) == 0) ? 1 : 0);
        }
    }

    if (itemHit == kPrefsSaveItem) {
        DiagnosticCheckpoint("1 Save clicked");

        scratch.defaultDistractionFree =
            (GetCheckedRadioItem(dlg, kWindowItems, 2) == kPrefsDistractionItem);
        scratch.defaultMarkdownMode =
            (GetCheckedRadioItem(dlg, kViewItems, 2) == kPrefsMarkdownItem);

        {
            short checked = GetCheckedRadioItem(dlg, kLineEndItems, 3);

            if (checked == kPrefsLineEndUnixItem) scratch.lineEnding = kLineEndingUnix;
            else if (checked == kPrefsLineEndWinItem) scratch.lineEnding = kLineEndingWindows;
            else scratch.lineEnding = kLineEndingMac;
        }

        GetDialogItem(dlg, kPrefsDarkModeItem, &itemType, &itemH, &itemRect);
        scratch.markdownDarkMode = (GetControlValue((ControlHandle) itemH) != 0);

        if (colorCapable) {
            GetDialogItem(dlg, kPrefsColorModeItem, &itemType, &itemH, &itemRect);
            scratch.markdownColorMode = (GetControlValue((ControlHandle) itemH) != 0);
        } else {
            /* Can't have been turned on through this window (the
               control was hidden), and shouldn't ever be true on a
               screen that can't support it regardless of how it got
               set -- belt-and-suspenders alongside color mode's own
               rendering-side capability check a later milestone adds. */
            scratch.markdownColorMode = false;
        }

        DiagnosticCheckpoint("2 checkboxes/radios OK, reading font");

        GetDialogItem(dlg, kPrefsFontNameItem, &itemType, &itemH, &itemRect);
        GetMenuItemText(GetMenu(kFontPopupMenuID),
                         GetControlValue((ControlHandle) itemH),
                         scratch.markdownFontName);

        DiagnosticCheckpoint("3 font popup read OK");

        GetDialogItem(dlg, kPrefsFontSizeItem, &itemType, &itemH, &itemRect);
        GetDialogItemText(itemH, numStr);
        {
            long n;
            if (ParsePascalInt(numStr, &n) && n > 0 && n <= 255)
                scratch.markdownFontSize = (short) n;
        }

        GetDialogItem(dlg, kPrefsZoomItem, &itemType, &itemH, &itemRect);
        GetDialogItemText(itemH, numStr);
        {
            long n;
            if (ParsePascalInt(numStr, &n) && n >= 0 && n < kNumZoomLevels)
                scratch.writerZoomIndex = (short) n;
        }

        gPrefs = scratch;

        DiagnosticCheckpoint("4 all fields OK, calling SavePreferences");

        SavePreferences();

        DiagnosticCheckpoint("13 SavePreferences returned, all done");
    }

    DisposeDialog(dlg);

    /* Matches this codebase's existing pattern (AskSaveChanges,
       DoOpenFile/DoSaveAs after SFGetFile/SFPutFile) of refreshing the
       menu bar's own look after any modal dialog interaction. */
    UpdateMenuBarLook();
}
