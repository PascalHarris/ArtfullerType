#include "app.h"
#include "preferences.h"

short AddLinkURL(const unsigned char *url)
{
    DocumentPtr doc = FrontDocument();

    if (doc->linkCount >= MAX_LINKS)
        return 0;
    doc->linkCount++;
    BlockMove((Ptr) url, (Ptr) doc->linkURLs[doc->linkCount], url[0] + 1);
    return doc->linkCount;
}

typedef struct {
    short start, end, kind, level;
    short linkID;
} StyleOp;

/*
    Heading sizes -- point deltas from CurrentWriterFontSize(), one per
    level (index 0 = Heading 1 ... index 5 = Heading 6). Deliberately a
    plain, hand-editable table rather than a computed formula: the
    original formula (CurrentWriterFontSize() + (4-level)*4) only ever
    covered 3 levels and went negative once extended to 6 (level 5 -> -4,
    level 6 -> -8), meaning lower-priority headings would have rendered
    *smaller* than body text -- backwards from what a heading hierarchy
    should look like. These six values are a starting point, not a
    final answer -- adjust freely; every place that reads this table
    picks up a changed value automatically, no other code to touch.
*/
static short kHeadingSizeDeltas[] = { 14, 12, 10, 8, 6, 4 };

/*
    Color mode's syntax-coloring pass -- PREFERENCES_DESIGN.md section
    8.5. A second pass layered on top of ClearStyles's own uniform
    baseline, not a replacement for it: this only ever changes color on
    the runs it detects, never font or size. No-ops immediately unless
    both markdownColorMode is on AND the screen is color-capable --
    checked here, not left to callers, since a hand-edited preferences
    file (section 3's own hand-editable text format) could set
    markdownColorMode true on a machine the Preferences window's own
    checkbox-hiding never had a chance to gate.

    Mirrors BuildHiddenView's own syntax-recognition rules exactly
    (confirmed by reading that function directly, not assumed) --
    same heading/bold/italic/code/link detection, so color mode agrees
    with Writer mode about what counts as each construct. Deliberately
    NOT a call into BuildHiddenView itself, though: that function's own
    StyleOp ranges are expressed in hiddenTE's stripped coordinate
    space (delimiters removed), but this needs doc->te's raw,
    unstripped space instead, since color mode colors the delimiters
    together with their content, in place, rather than stripping
    anything. Also detects ~~strikethrough~~, which BuildHiddenView
    itself deliberately skips (no native strikethrough text attribute
    on classic Mac to render it with) -- color mode isn't limited by
    that, since color itself is representable regardless.

    Does not call AddLinkURL for detected links -- that call's own
    side effect (populating doc->linkCount/linkURLs) is BuildHiddenView
    /Writer mode's own concern; this pass only needs each link's
    character range to color it, never its URL.

    Resets the whole document's color to plain black before applying
    any syntax-specific color, making this function self-contained and
    correct regardless of caller -- necessary now that it can run
    directly from a live-typing trigger (MaybeRecolorMarkdown), not
    only via ClearStyles's own, already-baseline-setting call. Without
    this reset, a range that WAS colored (e.g. **bold**) but no longer
    matches any construct (more text typed immediately after it, so
    the closing ** no longer ends the run there) would keep its stale
    color indefinitely: this function only ever adds color to ranges
    it currently detects, it never had a way to remove color from a
    range that used to be a construct but isn't one anymore.
*/
void ApplyMarkdownSyntaxColors(void)
{
    DocumentPtr doc = FrontDocument();
    Handle srcH;
    long len;
    long i;
    Boolean inFence = false;
    long fenceStart = 0;
    static StyleOp ops[MAX_STYLE_OPS];
    short opCount;
    short k;
    short savedStart;
    short savedEnd;
    TextStyle baseTs;
    Rect savedViewRect;

    if (!gPrefs.markdownColorMode || !ScreenSupportsColor())
        return;

    /* Same reasoning as BuildHiddenView's own identical precaution:
       not a speedup, just stops this from looking broken while it
       runs on real 68000 hardware. */
    SetCursor(*GetCursor(watchCursor));

    savedStart = (**doc->te).selStart;
    savedEnd = (**doc->te).selEnd;

    /* Same technique SyncHiddenToCanonical already uses around its own
       multi-step rebuild (including its own call into ClearStyles) --
       every TESetStyle call below has redraw=true and would otherwise
       repaint immediately, once for the baseline reset and once per
       detected run, which is exactly what produced a visible flicker
       on every keystroke once this function started running from
       MaybeRecolorMarkdown's own, direct, per-keystroke call. Safe to
       nest if this function is reached through ClearStyles's own,
       separate suppression (e.g. via SyncHiddenToCanonical): each
       layer saves and restores its own "before" state, so an inner
       SuppressDrawing/RestoreDrawing pair here doesn't disturb an
       outer one already in effect. */
    SuppressDrawing(doc->te, &savedViewRect);

    baseTs.tsColor.red = baseTs.tsColor.green = baseTs.tsColor.blue = 0;
    TESetSelect(0, 32767, doc->te);
    TESetStyle(doColor, &baseTs, true, doc->te);

    opCount = 0;
    srcH = (**doc->te).hText;
    len = (**doc->te).teLength;

    HLock(srcH);

    i = 0;
    while (i < len) {
        if (i == 0 || (*srcH)[i - 1] == '\r') {
            if (i + 2 < len && (*srcH)[i] == '`' && (*srcH)[i + 1] == '`' && (*srcH)[i + 2] == '`' &&
                (i + 3 == len || (*srcH)[i + 3] == '\r')) {
                if (!inFence) {
                    inFence = true;
                    fenceStart = i;
                } else {
                    inFence = false;
                    if (opCount < MAX_STYLE_OPS) {
                        ops[opCount].start = (short) fenceStart;
                        ops[opCount].end = (short) (i + 3);
                        ops[opCount].kind = 'F';
                        opCount++;
                    }
                }
                i = (i + 3 < len) ? i + 4 : len;
                continue;
            }

            if (inFence) {
                while (i < len && (*srcH)[i] != '\r')
                    i++;
                continue;
            }

            short level = 0;

            while (level < 6 && i + level < len && (*srcH)[i + level] == '#')
                level++;
            if (level > 0 && i + level < len && (*srcH)[i + level] == ' ') {
                long lineEnd = i + level + 1;

                while (lineEnd < len && (*srcH)[lineEnd] != '\r')
                    lineEnd++;
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) i;
                    ops[opCount].end = (short) lineEnd;
                    ops[opCount].kind = 'H';
                    opCount++;
                }
                i = lineEnd;
                continue;
            }
        }

        if (i + 1 < len && (*srcH)[i] == '~' && (*srcH)[i + 1] == '~') {
            long j = i + 2;

            while (j + 1 < len && !((*srcH)[j] == '~' && (*srcH)[j + 1] == '~'))
                j++;
            if (j + 1 < len) {
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) i;
                    ops[opCount].end = (short) (j + 2);
                    ops[opCount].kind = 'B'; /* emphasisColor -- same as bold/italic */
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if (i + 1 < len && (*srcH)[i] == '*' && (*srcH)[i + 1] == '*') {
            long j = i + 2;

            while (j + 1 < len && !((*srcH)[j] == '*' && (*srcH)[j + 1] == '*'))
                j++;
            if (j + 1 < len) {
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) i;
                    ops[opCount].end = (short) (j + 2);
                    ops[opCount].kind = 'B';
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if (i + 1 < len && (*srcH)[i] == '_' && (*srcH)[i + 1] == '_') {
            long j = i + 2;

            while (j + 1 < len && !((*srcH)[j] == '_' && (*srcH)[j + 1] == '_'))
                j++;
            if (j + 1 < len) {
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) i;
                    ops[opCount].end = (short) (j + 2);
                    ops[opCount].kind = 'U';
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if ((*srcH)[i] == '*') {
            long j = i + 1;

            while (j < len && (*srcH)[j] != '*')
                j++;
            if (j < len) {
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) i;
                    ops[opCount].end = (short) (j + 1);
                    ops[opCount].kind = 'I';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '`') {
            long j = i + 1;

            while (j < len && (*srcH)[j] != '`')
                j++;
            if (j < len) {
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) i;
                    ops[opCount].end = (short) (j + 1);
                    ops[opCount].kind = 'C';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '[') {
            long closeBracket = i + 1;

            while (closeBracket < len && (*srcH)[closeBracket] != ']')
                closeBracket++;
            if (closeBracket < len && closeBracket + 1 < len && (*srcH)[closeBracket + 1] == '(') {
                long closeParen = closeBracket + 2;

                while (closeParen < len && (*srcH)[closeParen] != ')')
                    closeParen++;
                if (closeParen < len) {
                    if (opCount < MAX_STYLE_OPS) {
                        ops[opCount].start = (short) i;
                        ops[opCount].end = (short) (closeParen + 1);
                        ops[opCount].kind = 'L';
                        opCount++;
                    }
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        i++;
    }

    HUnlock(srcH);

    for (k = 0; k < opCount; k++) {
        TextStyle ts;

        switch (ops[k].kind) {
            case 'H': ts.tsColor = NamedColorToRGB(gPrefs.headingColor);  break;
            case 'L': ts.tsColor = NamedColorToRGB(gPrefs.linkColor);     break;
            case 'B': /* fall through -- bold/italic/strikethrough/underline all emphasisColor */
            case 'U': /* fall through */
            case 'I': ts.tsColor = NamedColorToRGB(gPrefs.emphasisColor); break;
            case 'C': /* fall through -- fenced code blocks use the same codeColor as inline code */
            case 'F': ts.tsColor = NamedColorToRGB(gPrefs.codeColor);     break;
            default:  continue;
        }
        TESetSelect(ops[k].start, ops[k].end, doc->te);
        TESetStyle(doColor, &ts, true, doc->te);
    }

    TESetSelect(savedStart, savedEnd, doc->te);

    RestoreDrawing(doc->te, &savedViewRect);

    InitCursor();
}

/*
    Markdown mode's own baseline: plain, uniform black text at the
    current zoom size. Selection is preserved since this gets called
    after Style-menu edits that already placed the caret somewhere
    meaningful. Color mode's own syntax-coloring pass
    (ApplyMarkdownSyntaxColors, above) runs immediately afterward,
    layered on top of this baseline rather than replacing it -- see
    that function's own comment. Wrapped in SuppressDrawing/
    RestoreDrawing for the same reason that function's own comment
    explains: TESetStyle's own redraw=true would otherwise repaint
    the whole document immediately, which is visible as flicker on
    any caller that runs this often (in practice, ApplyMarkdownSyntaxColors
    itself, via a live-typing trigger).
*/
void ClearStyles(void)
{
    DocumentPtr doc = FrontDocument();
    TextStyle ts;
    short fontNum;
    short savedStart = (**doc->te).selStart;
    short savedEnd = (**doc->te).selEnd;
    Rect savedViewRect;

    SuppressDrawing(doc->te, &savedViewRect);

    GetFNum(gPrefs.markdownFontName, &fontNum);
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentMarkdownFontSize();
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;

    TESetSelect(0, 32767, doc->te);
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, doc->te);

    TESetSelect(savedStart, savedEnd, doc->te);

    ApplyMarkdownSyntaxColors();

    RestoreDrawing(doc->te, &savedViewRect);
}

/*
    Builds the hidden (Writer-mode) TE from the canonical TE's markdown
    text, stripping the delimiter characters themselves (**, *, `, [](),
    leading #s) and recording where the surviving text landed so styling
    can be applied afterward, in the stripped buffer's own coordinates.
*/
/*
    A document's te and hiddenTE are both bound to its window (a TE
    record draws into whatever GrafPort was current at TEStyleNew time,
    for its whole lifetime, regardless of which one is "active" later)
    -- so editing the *inactive* record still paints onto the window.
    Moving its viewRect off-screen for the duration of a rebuild makes
    those calls draw nothing, since drawing is clipped to viewRect
    every time.
*/
#define OFFSCREEN_COORD (-32000)

void SuppressDrawing(TEHandle te, Rect *saved)
{
    *saved = (**te).viewRect;
    SetRect(&(**te).viewRect, OFFSCREEN_COORD, OFFSCREEN_COORD,
            OFFSCREEN_COORD + 100, OFFSCREEN_COORD + 100);
}

void RestoreDrawing(TEHandle te, Rect *saved)
{
    (**te).viewRect = *saved;
}

void BuildHiddenView(void)
{
    DocumentPtr doc = FrontDocument();
    Handle srcH;
    long len;
    Handle outH;
    long outLen;
    long i;
    Boolean inFence = false;
    static StyleOp ops[MAX_STYLE_OPS];
    short opCount;
    short fontNum;
    TextStyle ts;
    short k;
    Rect savedViewRect;

    /* Parsing the whole document and applying one TESetStyle call per
       styled span is, on real 68000 hardware, slow enough on a long,
       heavily-styled document to look like the app has hung. A watch
       cursor doesn't make it faster, but it stops it from looking
       broken -- the actual fix for the underlying slowness (lazy/
       incremental styling) is a much bigger, riskier change. */
    SetCursor(*GetCursor(watchCursor));

    opCount = 0;
    doc->linkCount = 0;
    srcH = (**doc->te).hText;
    len = (**doc->te).teLength;
    outH = NewHandle(len + 1);
    outLen = 0;

    HLock(srcH);
    HLock(outH);

    i = 0;
    while (i < len) {
        if (i == 0 || (*srcH)[i - 1] == '\r') {
            if (i + 2 < len && (*srcH)[i] == '`' && (*srcH)[i + 1] == '`' && (*srcH)[i + 2] == '`' &&
                (i + 3 == len || (*srcH)[i + 3] == '\r')) {
                inFence = !inFence;
                i = (i + 3 < len) ? i + 4 : len;
                continue;
            }

            if (inFence) {
                long lineEnd = i;
                long outStart = outLen;

                while (lineEnd < len && (*srcH)[lineEnd] != '\r') {
                    (*outH)[outLen++] = (*srcH)[lineEnd];
                    lineEnd++;
                }
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'F';
                    opCount++;
                }
                i = lineEnd;
                continue;
            }

            short level = 0;

            while (level < 6 && i + level < len && (*srcH)[i + level] == '#')
                level++;
            if (level > 0 && i + level < len && (*srcH)[i + level] == ' ') {
                long lineStart = i + level + 1;
                long lineEnd = lineStart;
                long outStart = outLen;

                while (lineEnd < len && (*srcH)[lineEnd] != '\r') {
                    (*outH)[outLen++] = (*srcH)[lineEnd];
                    lineEnd++;
                }
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'H';
                    ops[opCount].level = level;
                    opCount++;
                }
                i = lineEnd;
                continue;
            }

            if ((*srcH)[i] == '>' && i + 1 < len && (*srcH)[i + 1] == ' ') {
                long lineStart = i + 2;
                long lineEnd = lineStart;
                long outStart = outLen;

                while (lineEnd < len && (*srcH)[lineEnd] != '\r') {
                    (*outH)[outLen++] = (*srcH)[lineEnd];
                    lineEnd++;
                }
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'Q';
                    opCount++;
                }
                i = lineEnd;
                continue;
            }
        }

        if (i + 1 < len && (*srcH)[i] == '*' && (*srcH)[i + 1] == '*') {
            long j = i + 2;

            while (j + 1 < len && !((*srcH)[j] == '*' && (*srcH)[j + 1] == '*'))
                j++;
            if (j + 1 < len) {
                long outStart = outLen, m;

                for (m = i + 2; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'B';
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if (i + 1 < len && (*srcH)[i] == '_' && (*srcH)[i + 1] == '_') {
            long j = i + 2;

            while (j + 1 < len && !((*srcH)[j] == '_' && (*srcH)[j + 1] == '_'))
                j++;
            if (j + 1 < len) {
                long outStart = outLen, m;

                for (m = i + 2; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'U';
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if ((*srcH)[i] == '*') {
            long j = i + 1;

            while (j < len && (*srcH)[j] != '*')
                j++;
            if (j < len) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'I';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '`') {
            long j = i + 1;

            while (j < len && (*srcH)[j] != '`')
                j++;
            if (j < len) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'C';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '[') {
            long closeBracket = i + 1;

            while (closeBracket < len && (*srcH)[closeBracket] != ']')
                closeBracket++;
            if (closeBracket < len && closeBracket + 1 < len && (*srcH)[closeBracket + 1] == '(') {
                long closeParen = closeBracket + 2;

                while (closeParen < len && (*srcH)[closeParen] != ')')
                    closeParen++;
                if (closeParen < len) {
                    long outStart = outLen, m;
                    Str255 url;
                    long urlLen = closeParen - (closeBracket + 2);

                    for (m = i + 1; m < closeBracket; m++)
                        (*outH)[outLen++] = (*srcH)[m];
                    if (urlLen > 255) urlLen = 255;
                    url[0] = (unsigned char) urlLen;
                    BlockMove(*srcH + closeBracket + 2, url + 1, urlLen);
                    if (opCount < MAX_STYLE_OPS) {
                        ops[opCount].start = (short) outStart;
                        ops[opCount].end = (short) outLen;
                        ops[opCount].kind = 'L';
                        ops[opCount].linkID = AddLinkURL(url);
                        opCount++;
                    }
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        (*outH)[outLen++] = (*srcH)[i];
        i++;
    }

    HUnlock(srcH);
    HUnlock(outH);

    SuppressDrawing(doc->hiddenTE, &savedViewRect);

    TESetSelect(0, 32767, doc->hiddenTE);
    TEDelete(doc->hiddenTE);
    TEInsert(*outH, outLen, doc->hiddenTE);
    DisposeHandle(outH);

    GetFNum("\pTimes", &fontNum);
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentWriterFontSize();
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;
    TESetSelect(0, 32767, doc->hiddenTE);
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, doc->hiddenTE);

    for (k = 0; k < opCount; k++) {
        TextStyle opStyle;

        TESetSelect(ops[k].start, ops[k].end, doc->hiddenTE);
        switch (ops[k].kind) {
            case 'B':
                opStyle.tsFace = bold;
                TESetStyle(doFace, &opStyle, true, doc->hiddenTE);
                break;
            case 'I':
                opStyle.tsFace = italic;
                TESetStyle(doFace, &opStyle, true, doc->hiddenTE);
                break;
            case 'U':
                opStyle.tsFace = underline;
                TESetStyle(doFace, &opStyle, true, doc->hiddenTE);
                break;
            case 'C':
                GetFNum("\pMonaco", &opStyle.tsFont);
                TESetStyle(doFont, &opStyle, true, doc->hiddenTE);
                break;
            case 'L':
                opStyle.tsFace = underline;
                opStyle.tsColor.red = ops[k].linkID;
                opStyle.tsColor.green = 0;
                opStyle.tsColor.blue = 65535;
                TESetStyle(doFace + doColor, &opStyle, true, doc->hiddenTE);
                break;
            case 'H':
                opStyle.tsFace = bold;
                opStyle.tsSize = CurrentWriterFontSize() + kHeadingSizeDeltas[ops[k].level - 1];
                TESetStyle(doFace + doSize, &opStyle, true, doc->hiddenTE);
                break;
            case 'Q':
                GetFNum("\pGeneva", &opStyle.tsFont);
                TESetStyle(doFont, &opStyle, true, doc->hiddenTE);
                break;
            case 'F':
                GetFNum("\pMonaco", &opStyle.tsFont);
                opStyle.tsFace = condense;
                if (CurrentWriterFontSize() > 12) {
                    opStyle.tsSize = 12;
                    TESetStyle(doFont + doFace + doSize, &opStyle, true, doc->hiddenTE);
                } else {
                    TESetStyle(doFont + doFace, &opStyle, true, doc->hiddenTE);
                }
                break;
        }
    }

    TESetSelect(0, 0, doc->hiddenTE);

    RestoreDrawing(doc->hiddenTE, &savedViewRect);

    InitCursor();
}

/*
    Reverse direction: walks the hidden TE's text + style runs and
    re-derives markdown delimiters, rebuilding the canonical TE's text
    from scratch. Headings are detected per-line (bold + a heading-sized
    run at the line's start); everything else is inline bold/italic/
    Monaco-as-code. Link underlines round-trip as "[text](url)" -- the
    url comes from the document's linkURLs, keyed by the run's
    tsColor.red (see AddLinkURL above).
*/
void SyncHiddenToCanonical(void)
{
    DocumentPtr doc = FrontDocument();
    Handle srcH;
    long len;
    Handle outH;
    long outCap;
    long outLen;
    long lineStart;
    Boolean inFence = false;
    short monacoFont;
    short genevaFont;
    Rect savedViewRect;
    long urlSpace;
    short li;

    /* Same reasoning as the watch cursor in BuildHiddenView -- this is
       the reverse direction, called on save, mode switch, and (more
       frequently) at the start of every typing run via PushUndoSnapshot/
       PushRedoSnapshot, so a long, heavily-styled document can make it
       pause noticeably mid-typing too. */
    SetCursor(*GetCursor(watchCursor));

    srcH = (**doc->hiddenTE).hText;
    len = (**doc->hiddenTE).teLength;
    urlSpace = 0;
    for (li = 1; li <= doc->linkCount; li++)
        urlSpace += doc->linkURLs[li][0];
    /* len*2 covers the generic inline-delimiter path (**, _, `, etc.),
       which only ever re-adds characters that were already implicitly
       part of hiddenTE's own styled runs. Fence markers are different:
       BuildHiddenView strips ``` lines entirely, so hiddenTE never
       contains them at all -- every ``` this function emits is genuinely
       new. Worst case is one fence transition per line (a line is at
       minimum just its own \r), each adding up to 4 bytes, hence len*4. */
    outCap = len * 2 + len * 4 + 64 + urlSpace;
    outH = NewHandle(outCap);
    outLen = 0;

    GetFNum("\pMonaco", &monacoFont);
    GetFNum("\pGeneva", &genevaFont);

    HLock(srcH);
    HLock(outH);

    lineStart = 0;
    while (lineStart <= len) {
        long lineEnd = lineStart;
        short headingLevel = 0;
        Boolean isHeading = false;
        Boolean isBlockquote = false;
        Boolean isCodeBlockLine = false;
        Boolean emittedAsFence = false;

        while (lineEnd < len && (*srcH)[lineEnd] != '\r')
            lineEnd++;

        if (lineEnd > lineStart) {
            TextStyle firstStyle;
            short dummyLH, dummyFA;

            TEGetStyle((short) lineStart, &firstStyle, &dummyLH, &dummyFA, doc->hiddenTE);
            if (firstStyle.tsFace & bold) {
                short lvl;

                for (lvl = 1; lvl <= 6; lvl++) {
                    if (firstStyle.tsSize == CurrentWriterFontSize() + kHeadingSizeDeltas[lvl - 1]) {
                        headingLevel = lvl;
                        isHeading = true;
                        break;
                    }
                }
            }
            if (!isHeading && firstStyle.tsFont == genevaFont)
                isBlockquote = true;
            if (!isHeading && !isBlockquote && firstStyle.tsFont == monacoFont &&
                (firstStyle.tsFace & condense) != 0)
                isCodeBlockLine = true;
        }

        if (inFence) {
            if (lineEnd == lineStart || isCodeBlockLine) {
                BlockMove(*srcH + lineStart, *outH + outLen, lineEnd - lineStart);
                outLen += (lineEnd - lineStart);
                emittedAsFence = true;
            } else {
                inFence = false;
                (*outH)[outLen++] = '`';
                (*outH)[outLen++] = '`';
                (*outH)[outLen++] = '`';
                (*outH)[outLen++] = '\r';
                /* This line itself isn't part of the fence -- falls
                   through to the existing heading/blockquote/generic
                   handling below, unchanged. */
            }
        } else if (isCodeBlockLine) {
            inFence = true;
            (*outH)[outLen++] = '`';
            (*outH)[outLen++] = '`';
            (*outH)[outLen++] = '`';
            (*outH)[outLen++] = '\r';
            BlockMove(*srcH + lineStart, *outH + outLen, lineEnd - lineStart);
            outLen += (lineEnd - lineStart);
            emittedAsFence = true;
        }

        if (emittedAsFence) {
            /* Skip the existing heading/blockquote/generic block below
               entirely -- already emitted. */
        } else if (isHeading) {
            short k;

            for (k = 0; k < headingLevel; k++)
                (*outH)[outLen++] = '#';
            (*outH)[outLen++] = ' ';
            BlockMove(*srcH + lineStart, *outH + outLen, lineEnd - lineStart);
            outLen += (lineEnd - lineStart);
        } else if (isBlockquote) {
            (*outH)[outLen++] = '>';
            (*outH)[outLen++] = ' ';
            BlockMove(*srcH + lineStart, *outH + outLen, lineEnd - lineStart);
            outLen += (lineEnd - lineStart);
        } else {
            long i = lineStart;
            Boolean inBold = false, inItalic = false, inCode = false, inLink = false, inUnderline = false;
            Str255 curLinkURL;

            while (i <= lineEnd) {
                Boolean wantBold = false, wantItalic = false, wantCode = false, wantLink = false, wantUnderline = false;
                short linkID = 0;

                if (i < lineEnd) {
                    TextStyle st;
                    short dlh, dfa;

                    TEGetStyle((short) i, &st, &dlh, &dfa, doc->hiddenTE);
                    wantBold = (st.tsFace & bold) != 0;
                    wantItalic = (st.tsFace & italic) != 0;
                    wantCode = (st.tsFont == monacoFont);
                    /* Link and plain underline share the same tsFace bit
                       (underline), distinguished only by color -- a link
                       is always blue (tsColor.blue == 65535, established
                       everywhere a link is styled), plain underline never
                       is, since it deliberately never touches color at
                       all. The two are mutually exclusive by
                       construction: a run is styled as one or the
                       other, never both. */
                    wantLink = (st.tsFace & underline) != 0 && st.tsColor.blue == 65535;
                    wantUnderline = (st.tsFace & underline) != 0 && st.tsColor.blue != 65535;
                    linkID = st.tsColor.red;
                }

                /* Close innermost-first: code, italic, bold, underline,
                   then link (link is the outermost wrapper, [bold
                   link](url)). */
                if (inCode && !wantCode) { (*outH)[outLen++] = '`'; inCode = false; }
                if (inItalic && !wantItalic) { (*outH)[outLen++] = '*'; inItalic = false; }
                if (inBold && !wantBold) {
                    (*outH)[outLen++] = '*';
                    (*outH)[outLen++] = '*';
                    inBold = false;
                }
                if (inUnderline && !wantUnderline) {
                    (*outH)[outLen++] = '_';
                    (*outH)[outLen++] = '_';
                    inUnderline = false;
                }
                if (inLink && !wantLink) {
                    (*outH)[outLen++] = ']';
                    (*outH)[outLen++] = '(';
                    BlockMove(curLinkURL + 1, *outH + outLen, curLinkURL[0]);
                    outLen += curLinkURL[0];
                    (*outH)[outLen++] = ')';
                    inLink = false;
                }

                if (!inLink && wantLink) {
                    (*outH)[outLen++] = '[';
                    inLink = true;
                    if (linkID >= 1 && linkID <= doc->linkCount)
                        BlockMove(doc->linkURLs[linkID], curLinkURL, doc->linkURLs[linkID][0] + 1);
                    else
                        curLinkURL[0] = 0;
                }
                if (!inUnderline && wantUnderline) {
                    (*outH)[outLen++] = '_';
                    (*outH)[outLen++] = '_';
                    inUnderline = true;
                }
                if (!inBold && wantBold) {
                    (*outH)[outLen++] = '*';
                    (*outH)[outLen++] = '*';
                    inBold = true;
                }
                if (!inItalic && wantItalic) { (*outH)[outLen++] = '*'; inItalic = true; }
                if (!inCode && wantCode) { (*outH)[outLen++] = '`'; inCode = true; }

                if (i < lineEnd)
                    (*outH)[outLen++] = (*srcH)[i];
                i++;
            }
        }

        if (lineEnd < len)
            (*outH)[outLen++] = '\r';
        lineStart = lineEnd + 1;
    }

    if (inFence) {
        (*outH)[outLen++] = '`';
        (*outH)[outLen++] = '`';
        (*outH)[outLen++] = '`';
    }

    HUnlock(srcH);
    HUnlock(outH);

    SuppressDrawing(doc->te, &savedViewRect);

    TESetSelect(0, 32767, doc->te);
    TEDelete(doc->te);
    TEInsert(*outH, outLen, doc->te);
    DisposeHandle(outH);

    ClearStyles();

    RestoreDrawing(doc->te, &savedViewRect);

    InitCursor();
}

/*
    Cut/Copy/Paste go through the Scrap Manager directly (ZeroScrap/
    PutScrap/GetScrap) rather than the usual TECut/TECopy/TEPaste +
    TEToScrap/TEFromScrap pattern -- TEToScrap/TEFromScrap are declared
    in this toolchain's headers but have no actual implementation
    linked anywhere (confirmed: linker error, not a typo), so they're
    unusable here.

    Styling still survives a copy within Writer mode, just not via the
    clipboard's own (unavailable) style support: copying a Writer-mode
    selection encodes its styled runs as markdown text (the same
    inline bold/italic/code/link delimiters SyncHiddenToCanonical
    already produces for the whole document, just scoped to a range
    instead of per-line -- so headings specifically aren't
    re-derived, since they're a line-level construct that doesn't
    make sense for an arbitrary sub-range), and pasting back into
    Writer mode parses that text for the same delimiters and applies
    the corresponding styles (mirroring BuildHiddenView's inline
    parsing, again without heading handling). Plain text round-trips
    unchanged either way, including to/from other apps -- a paste
    that happens to contain a literal "*" or "`" from some other
    source will get (mis)interpreted as markdown, an accepted
    trade-off for getting styled copy/paste working at all. Markdown
    mode's copy/paste is untouched -- the selection is already raw
    markdown text, no encoding/decoding needed.
*/
Handle EncodeSelectionAsMarkdown(short start, short end, TEHandle te)
{
    DocumentPtr doc = FrontDocument();
    Handle srcH;
    Handle outH;
    long outCap;
    long outLen;
    long urlSpace;
    short li;
    short monacoFont;
    long i;
    Boolean inBold = false, inItalic = false, inCode = false, inLink = false, inUnderline = false;
    Str255 curLinkURL;

    srcH = (**te).hText;
    urlSpace = 0;
    for (li = 1; li <= doc->linkCount; li++)
        urlSpace += doc->linkURLs[li][0];
    outCap = (long) (end - start) * 2 + 64 + urlSpace;
    outH = NewHandle(outCap);
    outLen = 0;

    GetFNum("\pMonaco", &monacoFont);

    HLock(srcH);
    HLock(outH);

    i = start;
    while (i <= end) {
        Boolean wantBold = false, wantItalic = false, wantCode = false, wantLink = false, wantUnderline = false;
        short linkID = 0;

        if (i < end) {
            TextStyle st;
            short dlh, dfa;

            TEGetStyle((short) i, &st, &dlh, &dfa, te);
            wantBold = (st.tsFace & bold) != 0;
            wantItalic = (st.tsFace & italic) != 0;
            wantCode = (st.tsFont == monacoFont);
            /* Same link/underline split as SyncHiddenToCanonical -- see
               that function's own comment for why. */
            wantLink = (st.tsFace & underline) != 0 && st.tsColor.blue == 65535;
            wantUnderline = (st.tsFace & underline) != 0 && st.tsColor.blue != 65535;
            linkID = st.tsColor.red;
        }

        if (inCode && !wantCode) { (*outH)[outLen++] = '`'; inCode = false; }
        if (inItalic && !wantItalic) { (*outH)[outLen++] = '*'; inItalic = false; }
        if (inBold && !wantBold) {
            (*outH)[outLen++] = '*';
            (*outH)[outLen++] = '*';
            inBold = false;
        }
        if (inUnderline && !wantUnderline) {
            (*outH)[outLen++] = '_';
            (*outH)[outLen++] = '_';
            inUnderline = false;
        }
        if (inLink && !wantLink) {
            (*outH)[outLen++] = ']';
            (*outH)[outLen++] = '(';
            BlockMove(curLinkURL + 1, *outH + outLen, curLinkURL[0]);
            outLen += curLinkURL[0];
            (*outH)[outLen++] = ')';
            inLink = false;
        }

        if (!inLink && wantLink) {
            (*outH)[outLen++] = '[';
            inLink = true;
            if (linkID >= 1 && linkID <= doc->linkCount)
                BlockMove(doc->linkURLs[linkID], curLinkURL, doc->linkURLs[linkID][0] + 1);
            else
                curLinkURL[0] = 0;
        }
        if (!inUnderline && wantUnderline) {
            (*outH)[outLen++] = '_';
            (*outH)[outLen++] = '_';
            inUnderline = true;
        }
        if (!inBold && wantBold) {
            (*outH)[outLen++] = '*';
            (*outH)[outLen++] = '*';
            inBold = true;
        }
        if (!inItalic && wantItalic) { (*outH)[outLen++] = '*'; inItalic = true; }
        if (!inCode && wantCode) { (*outH)[outLen++] = '`'; inCode = true; }

        if (i < end)
            (*outH)[outLen++] = (*srcH)[i];
        i++;
    }

    HUnlock(srcH);
    HUnlock(outH);
    SetHandleSize(outH, outLen);

    return outH;
}

void InsertMarkdownAsStyled(Handle srcH, long srcLen, TEHandle te)
{
    Handle outH;
    long outLen;
    long i;
    static StyleOp ops[MAX_STYLE_OPS];
    short opCount = 0;
    short insertStart;
    short k;
    TextStyle baseStyle;
    short fontNum;

    outH = NewHandle(srcLen + 1);
    outLen = 0;

    HLock(srcH);
    HLock(outH);

    i = 0;
    while (i < srcLen) {
        if (i + 1 < srcLen && (*srcH)[i] == '*' && (*srcH)[i + 1] == '*') {
            long j = i + 2;

            while (j + 1 < srcLen && !((*srcH)[j] == '*' && (*srcH)[j + 1] == '*'))
                j++;
            if (j + 1 < srcLen) {
                long outStart = outLen, m;

                for (m = i + 2; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'B';
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if (i + 1 < srcLen && (*srcH)[i] == '_' && (*srcH)[i + 1] == '_') {
            long j = i + 2;

            while (j + 1 < srcLen && !((*srcH)[j] == '_' && (*srcH)[j + 1] == '_'))
                j++;
            if (j + 1 < srcLen) {
                long outStart = outLen, m;

                for (m = i + 2; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'U';
                    opCount++;
                }
                i = j + 2;
                continue;
            }
        }
        if ((*srcH)[i] == '*') {
            long j = i + 1;

            while (j < srcLen && (*srcH)[j] != '*')
                j++;
            if (j < srcLen) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'I';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '`') {
            long j = i + 1;

            while (j < srcLen && (*srcH)[j] != '`')
                j++;
            if (j < srcLen) {
                long outStart = outLen, m;

                for (m = i + 1; m < j; m++)
                    (*outH)[outLen++] = (*srcH)[m];
                if (opCount < MAX_STYLE_OPS) {
                    ops[opCount].start = (short) outStart;
                    ops[opCount].end = (short) outLen;
                    ops[opCount].kind = 'C';
                    opCount++;
                }
                i = j + 1;
                continue;
            }
        }
        if ((*srcH)[i] == '[') {
            long closeBracket = i + 1;

            while (closeBracket < srcLen && (*srcH)[closeBracket] != ']')
                closeBracket++;
            if (closeBracket < srcLen && closeBracket + 1 < srcLen && (*srcH)[closeBracket + 1] == '(') {
                long closeParen = closeBracket + 2;

                while (closeParen < srcLen && (*srcH)[closeParen] != ')')
                    closeParen++;
                if (closeParen < srcLen) {
                    long outStart = outLen, m;
                    Str255 url;
                    long urlLen = closeParen - (closeBracket + 2);

                    for (m = i + 1; m < closeBracket; m++)
                        (*outH)[outLen++] = (*srcH)[m];
                    if (urlLen > 255) urlLen = 255;
                    url[0] = (unsigned char) urlLen;
                    BlockMove(*srcH + closeBracket + 2, url + 1, urlLen);
                    if (opCount < MAX_STYLE_OPS) {
                        ops[opCount].start = (short) outStart;
                        ops[opCount].end = (short) outLen;
                        ops[opCount].kind = 'L';
                        ops[opCount].linkID = AddLinkURL(url);
                        opCount++;
                    }
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        (*outH)[outLen++] = (*srcH)[i];
        i++;
    }

    HUnlock(srcH);
    HUnlock(outH);

    insertStart = (**te).selStart;
    TEInsert(*outH, outLen, te);
    DisposeHandle(outH);

    /* TEInsert's new text inherits whatever style was at the
       insertion point -- normalize the whole pasted range to plain
       before applying the specific ops parsed above, the same order
       BuildHiddenView uses for the same reason. */
    GetFNum("\pTimes", &fontNum);
    baseStyle.tsFont = fontNum;
    baseStyle.tsFace = normal;
    baseStyle.tsSize = CurrentWriterFontSize();
    baseStyle.tsColor.red = baseStyle.tsColor.green = baseStyle.tsColor.blue = 0;
    TESetSelect(insertStart, (short) (insertStart + outLen), te);
    TESetStyle(doFont + doFace + doSize + doColor, &baseStyle, true, te);

    for (k = 0; k < opCount; k++) {
        TextStyle opStyle;

        TESetSelect((short) (insertStart + ops[k].start), (short) (insertStart + ops[k].end), te);
        switch (ops[k].kind) {
            case 'B':
                opStyle.tsFace = bold;
                TESetStyle(doFace, &opStyle, true, te);
                break;
            case 'I':
                opStyle.tsFace = italic;
                TESetStyle(doFace, &opStyle, true, te);
                break;
            case 'U':
                opStyle.tsFace = underline;
                TESetStyle(doFace, &opStyle, true, te);
                break;
            case 'C':
                GetFNum("\pMonaco", &opStyle.tsFont);
                TESetStyle(doFont, &opStyle, true, te);
                break;
            case 'L':
                opStyle.tsFace = underline;
                opStyle.tsColor.red = ops[k].linkID;
                opStyle.tsColor.green = 0;
                opStyle.tsColor.blue = 65535;
                TESetStyle(doFace + doColor, &opStyle, true, te);
                break;
        }
    }

    TESetSelect((short) (insertStart + outLen), (short) (insertStart + outLen), te);
}

void WrapSelection(char *prefix, char *suffix)
{
    DocumentPtr doc = FrontDocument();
    short selStart, selEnd;
    long selLen, totalLen, textLen;
    short prefixLen, suffixLen;
    Handle textH;
    Handle newH;
    Boolean outerWrapped, innerWrapped;

    selStart = (**doc->te).selStart;
    selEnd = (**doc->te).selEnd;
    selLen = selEnd - selStart;
    textH = (**doc->te).hText;
    textLen = (**doc->te).teLength;

    doc->dirty = true;

    prefixLen = strlen(prefix);
    suffixLen = strlen(suffix);

    HLock(textH);
    outerWrapped =
        (selStart >= prefixLen) &&
        (selEnd + suffixLen <= textLen) &&
        (memcmp(*textH + selStart - prefixLen, prefix, prefixLen) == 0) &&
        (memcmp(*textH + selEnd, suffix, suffixLen) == 0);
    innerWrapped = !outerWrapped &&
        (selLen >= prefixLen + suffixLen) &&
        (memcmp(*textH + selStart, prefix, prefixLen) == 0) &&
        (memcmp(*textH + selEnd - suffixLen, suffix, suffixLen) == 0);
    HUnlock(textH);

    if (outerWrapped) {
        /* markers sit just outside the selection -- strip them (toggle off) */
        newH = NewHandle(selLen);
        HLock(newH);
        HLock(textH);
        BlockMove(*textH + selStart, *newH, selLen);
        HUnlock(textH);

        TESetSelect(selStart - prefixLen, selEnd + suffixLen, doc->te);
        TEDelete(doc->te);
        TEInsert(*newH, selLen, doc->te);
        HUnlock(newH);
        DisposeHandle(newH);

        TESetSelect(selStart - prefixLen, selStart - prefixLen + selLen, doc->te);
        return;
    }

    if (innerWrapped) {
        /* markers are part of the selection itself -- strip them (toggle off) */
        long innerLen = selLen - prefixLen - suffixLen;

        newH = NewHandle(innerLen);
        HLock(newH);
        HLock(textH);
        BlockMove(*textH + selStart + prefixLen, *newH, innerLen);
        HUnlock(textH);

        TEDelete(doc->te);
        TEInsert(*newH, innerLen, doc->te);
        HUnlock(newH);
        DisposeHandle(newH);

        TESetSelect(selStart, selStart + innerLen, doc->te);
        return;
    }

    totalLen = prefixLen + selLen + suffixLen;
    newH = NewHandle(totalLen);
    HLock(newH);
    HLock(textH);
    BlockMove(prefix, *newH, prefixLen);
    BlockMove(*textH + selStart, *newH + prefixLen, selLen);
    BlockMove(suffix, *newH + prefixLen + selLen, suffixLen);
    HUnlock(textH);

    TEDelete(doc->te);
    TEInsert(*newH, totalLen, doc->te);
    HUnlock(newH);
    DisposeHandle(newH);

    TESetSelect(selStart + prefixLen, selStart + prefixLen + selLen, doc->te);
}

void ApplyHeading(short level)
{
    DocumentPtr doc = FrontDocument();
    short selStart;
    short lineStart;
    long textLen;
    Handle textH;
    char prefix[8];
    short i;
    Boolean alreadyHeading;

    doc->dirty = true;

    selStart = (**doc->te).selStart;
    textH = (**doc->te).hText;
    textLen = (**doc->te).teLength;

    lineStart = selStart;
    HLock(textH);
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    HUnlock(textH);

    for (i = 0; i < level; i++)
        prefix[i] = '#';
    prefix[level] = ' ';

    HLock(textH);
    alreadyHeading =
        (lineStart + level + 1 <= textLen) &&
        (memcmp(*textH + lineStart, prefix, level + 1) == 0);
    HUnlock(textH);

    if (alreadyHeading) {
        TESetSelect(lineStart, lineStart + level + 1, doc->te);
        TEDelete(doc->te);
        return;
    }

    TESetSelect(lineStart, lineStart, doc->te);
    TEInsert(prefix, level + 1, doc->te);
}

void ApplyBlockquote(void)
{
    DocumentPtr doc = FrontDocument();
    short selStart;
    short lineStart;
    long textLen;
    Handle textH;
    static char prefix[] = "> ";
    Boolean alreadyBlockquote;

    doc->dirty = true;

    selStart = (**doc->te).selStart;
    textH = (**doc->te).hText;
    textLen = (**doc->te).teLength;

    lineStart = selStart;
    HLock(textH);
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    HUnlock(textH);

    HLock(textH);
    alreadyBlockquote =
        (lineStart + 2 <= textLen) &&
        (memcmp(*textH + lineStart, prefix, 2) == 0);
    HUnlock(textH);

    if (alreadyBlockquote) {
        TESetSelect(lineStart, lineStart + 2, doc->te);
        TEDelete(doc->te);
        return;
    }

    TESetSelect(lineStart, lineStart, doc->te);
    TEInsert(prefix, 2, doc->te);
}

void ApplyCodeBlock(void)
{
    DocumentPtr doc = FrontDocument();
    short selStart, selEnd;
    short lineStart, lineEnd;
    Handle textH;
    long len;
    static char openFence[] = "```\r";
    static char closeFence[] = "\r```";

    doc->dirty = true;

    selStart = (**doc->te).selStart;
    selEnd = (**doc->te).selEnd;
    textH = (**doc->te).hText;
    len = (**doc->te).teLength;

    HLock(textH);
    lineStart = selStart;
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    lineEnd = selEnd;
    while (lineEnd < len && (*textH)[lineEnd] != '\r')
        lineEnd++;
    HUnlock(textH);

    TESetSelect(lineEnd, lineEnd, doc->te);
    TEInsert(closeFence, 4, doc->te);

    TESetSelect(lineStart, lineStart, doc->te);
    TEInsert(openFence, 4, doc->te);
}

void DoLink(void)
{
    DocumentPtr doc = FrontDocument();
    short selStart, selEnd;
    long selLen, totalLen;
    Handle textH;
    Handle newH;
    static char mid[] = "]()";
    short midLen = 3;
    short cursorPos;

    doc->dirty = true;

    selStart = (**doc->te).selStart;
    selEnd = (**doc->te).selEnd;
    selLen = selEnd - selStart;
    textH = (**doc->te).hText;

    totalLen = 1 + selLen + midLen;
    newH = NewHandle(totalLen);
    HLock(newH);
    HLock(textH);
    (*newH)[0] = '[';
    BlockMove(*textH + selStart, *newH + 1, selLen);
    BlockMove(mid, *newH + 1 + selLen, midLen);
    HUnlock(textH);

    TEDelete(doc->te);
    TEInsert(*newH, totalLen, doc->te);
    HUnlock(newH);
    DisposeHandle(newH);

    cursorPos = selStart + selLen + 3;
    TESetSelect(cursorPos, cursorPos, doc->te);
}

/*
    Style commands while in Hide Markdown mode apply real TextStyle
    directly to the hidden TE instead of inserting delimiter text --
    there's no visible syntax to insert. Toggle state is read back from
    the style at the selection start.
*/
static Boolean SelectionHasFace(Style face)
{
    DocumentPtr doc = FrontDocument();
    TextStyle ts;
    short lh, fa;

    TEGetStyle((**doc->hiddenTE).selStart, &ts, &lh, &fa, doc->hiddenTE);
    return (ts.tsFace & face) != 0;
}

void ToggleFace(Style face)
{
    DocumentPtr doc = FrontDocument();
    TextStyle ts;

    ts.tsFace = SelectionHasFace(face) ? normal : face;
    TESetStyle(doFace, &ts, true, doc->hiddenTE);
}

/*
    Underline and link share the same tsFace bit (underline),
    distinguished only by color -- a link is always blue
    (tsColor.blue == 65535), plain underline never touches color at
    all. A dedicated function rather than a direct ToggleFace(underline)
    call: toggling the shared bit naively on a selection that's
    currently a link would strip its visual underline while leaving its
    blue color and internal URL association (tsColor.red, the link ID)
    untouched -- a silently broken, inconsistent state, not a real
    "remove the link" operation. Refuses to touch a link-starting
    selection at all; "None" (ClearSelectionStyleHidden) is the
    existing, correct way to remove a link entirely.
*/
void ToggleUnderlineHidden(void)
{
    DocumentPtr doc = FrontDocument();
    TextStyle ts;
    short lh, fa;

    TEGetStyle((**doc->hiddenTE).selStart, &ts, &lh, &fa, doc->hiddenTE);
    if ((ts.tsFace & underline) != 0 && ts.tsColor.blue == 65535)
        return;

    ts.tsFace = SelectionHasFace(underline) ? normal : underline;
    TESetStyle(doFace, &ts, true, doc->hiddenTE);
}

/* Prompts for a URL; returns true and fills in `url` if OK was clicked. */
static Boolean ShowLinkURLDialog(unsigned char *url)
{
    DocumentPtr doc = FrontDocument();
    DialogPtr dlg;
    short item;
    DialogItemType type;
    Handle itemH;
    Rect box;
    Boolean result;

    dlg = GetNewDialog(kLinkDialog, NULL, (WindowPtr) -1L);
    if (dlg == NULL)
        return false;

    SelectDialogItemText(dlg, iLinkField, 0, 32767);

    do {
        ModalDialog(NULL, &item);
    } while (item != iLinkOK && item != iLinkCancel);

    result = (item == iLinkOK);
    if (result) {
        GetDialogItem(dlg, iLinkField, &type, &itemH, &box);
        GetDialogItemText(itemH, url);
    }

    DisposeDialog(dlg);
    SetPort(doc->window);
    UpdateMenuBarLook();
    return result;
}

/*
    "Link" in Writer mode: prompts for a URL, then applies underline +
    a link ID (see AddLinkURL) to the current selection.
*/
void DoLinkHidden(void)
{
    DocumentPtr doc = FrontDocument();
    Str255 url;

    if ((**doc->hiddenTE).selStart == (**doc->hiddenTE).selEnd)
        return;

    if (ShowLinkURLDialog(url)) {
        TextStyle ts;

        ts.tsFace = underline;
        ts.tsColor.red = AddLinkURL(url);
        ts.tsColor.green = 0;
        ts.tsColor.blue = 65535;
        TESetStyle(doFace + doColor, &ts, true, doc->hiddenTE);
    }
}

void ToggleCode(void)
{
    DocumentPtr doc = FrontDocument();
    TextStyle ts;
    short lh, fa;
    short monacoFont, timesFont;

    GetFNum("\pMonaco", &monacoFont);
    GetFNum("\pTimes", &timesFont);

    TEGetStyle((**doc->hiddenTE).selStart, &ts, &lh, &fa, doc->hiddenTE);
    ts.tsFont = (ts.tsFont == monacoFont) ? timesFont : monacoFont;
    TESetStyle(doFont, &ts, true, doc->hiddenTE);
}

void ToggleHeadingHidden(short level)
{
    DocumentPtr doc = FrontDocument();
    short selStart;
    long lineStart, lineEnd;
    Handle textH;
    long len;
    TextStyle ts;
    short lh, fa;
    Boolean isThisLevel;

    selStart = (**doc->hiddenTE).selStart;
    textH = (**doc->hiddenTE).hText;
    len = (**doc->hiddenTE).teLength;

    HLock(textH);
    lineStart = selStart;
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    lineEnd = lineStart;
    while (lineEnd < len && (*textH)[lineEnd] != '\r')
        lineEnd++;
    HUnlock(textH);

    TEGetStyle((short) lineStart, &ts, &lh, &fa, doc->hiddenTE);
    isThisLevel = (ts.tsFace & bold) && (ts.tsSize == CurrentWriterFontSize() + kHeadingSizeDeltas[level - 1]);

    TESetSelect((short) lineStart, (short) lineEnd, doc->hiddenTE);
    if (isThisLevel) {
        ts.tsFace = normal;
        ts.tsSize = CurrentWriterFontSize();
    } else {
        ts.tsFace = bold;
        ts.tsSize = CurrentWriterFontSize() + kHeadingSizeDeltas[level - 1];
    }
    TESetStyle(doFace + doSize, &ts, true, doc->hiddenTE);
}

void ToggleBlockquoteHidden(void)
{
    DocumentPtr doc = FrontDocument();
    short selStart;
    long lineStart, lineEnd;
    Handle textH;
    long len;
    TextStyle ts;
    short lh, fa;
    short genevaFont, timesFont;
    Boolean isBlockquote;

    selStart = (**doc->hiddenTE).selStart;
    textH = (**doc->hiddenTE).hText;
    len = (**doc->hiddenTE).teLength;

    HLock(textH);
    lineStart = selStart;
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    lineEnd = lineStart;
    while (lineEnd < len && (*textH)[lineEnd] != '\r')
        lineEnd++;
    HUnlock(textH);

    GetFNum("\pGeneva", &genevaFont);
    GetFNum("\pTimes", &timesFont);

    TEGetStyle((short) lineStart, &ts, &lh, &fa, doc->hiddenTE);
    isBlockquote = (ts.tsFont == genevaFont);

    TESetSelect((short) lineStart, (short) lineEnd, doc->hiddenTE);
    ts.tsFont = isBlockquote ? timesFont : genevaFont;
    TESetStyle(doFont, &ts, true, doc->hiddenTE);
}

void ToggleCodeBlockHidden(void)
{
    DocumentPtr doc = FrontDocument();
    short selStart, selEnd;
    long lineStart, lineEnd;
    Handle textH;
    long len;
    TextStyle ts;
    short lh, fa;
    short monacoFont, timesFont;
    Boolean isCodeBlock;

    selStart = (**doc->hiddenTE).selStart;
    selEnd = (**doc->hiddenTE).selEnd;
    textH = (**doc->hiddenTE).hText;
    len = (**doc->hiddenTE).teLength;

    HLock(textH);
    lineStart = selStart;
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;
    lineEnd = selEnd;
    while (lineEnd < len && (*textH)[lineEnd] != '\r')
        lineEnd++;
    HUnlock(textH);

    GetFNum("\pMonaco", &monacoFont);
    GetFNum("\pTimes", &timesFont);

    TEGetStyle((short) lineStart, &ts, &lh, &fa, doc->hiddenTE);
    isCodeBlock = (ts.tsFont == monacoFont && (ts.tsFace & condense) != 0);

    TESetSelect((short) lineStart, (short) lineEnd, doc->hiddenTE);
    if (isCodeBlock) {
        ts.tsFont = timesFont;
        ts.tsFace = normal;
        ts.tsSize = CurrentWriterFontSize();
        TESetStyle(doFont + doFace + doSize, &ts, true, doc->hiddenTE);
    } else {
        ts.tsFont = monacoFont;
        ts.tsFace = condense;
        if (CurrentWriterFontSize() > 12) {
            ts.tsSize = 12;
            TESetStyle(doFont + doFace + doSize, &ts, true, doc->hiddenTE);
        } else {
            TESetStyle(doFont + doFace, &ts, true, doc->hiddenTE);
        }
    }
}

/*
    Sets the style at a zero-length selection (the insertion point) --
    Style TextEdit uses this as the style for whatever gets typed next,
    which is exactly what's needed after closing a live-converted span
    so typing doesn't keep inheriting bold/italic/code indefinitely.
*/
static void SetTypingStyleNormal(short pos)
{
    DocumentPtr doc = FrontDocument();
    TextStyle ts;
    short fontNum;

    GetFNum("\pTimes", &fontNum);
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentWriterFontSize();
    TESetSelect(pos, pos, doc->hiddenTE);
    TESetStyle(doFont + doFace + doSize, &ts, true, doc->hiddenTE);
}

/*
    Markdown mode's own trigger for live color updates -- called after
    every content keystroke while Markdown view is active, the same
    place DetectInlineMarkdown is called for Writer mode. Not the same
    mechanism, though: DetectInlineMarkdown works incrementally,
    operating only on the just-completed span, which is what makes it
    cheap enough to run after every keystroke. ApplyMarkdownSyntaxColors
    has no equivalent incremental mode of its own -- it re-parses the
    whole document every time it runs (the same reason it needed its
    own watch-cursor precaution, mirroring BuildHiddenView's identical
    concern).

    Calls it on every content keystroke, not just ones that complete a
    construct -- an earlier, narrower version only triggered on
    specific "completing" characters (closing star/backtick/tilde/close
    paren/newline), which turned out to be incomplete: text typed
    immediately after a colored construct (e.g. "hello" right after
    **text**) inherits that construct's own color at the moment of
    insertion, via TextEdit's own style-inheritance behavior, and
    nothing corrected it until some later, unrelated trigger character
    happened to appear. Widened to every keystroke by explicit
    direction, since this was called out as the more significant of two
    reported issues. The performance cost of a full rescan on every
    single keystroke, on a large document on real hardware, is real and
    hasn't been verified either way -- worth watching for specifically
    if typing responsiveness degrades on a long document with color
    mode on.
*/
void MaybeRecolorMarkdown(char justTyped)
{
    DocumentPtr doc = FrontDocument();

    if (!gPrefs.markdownColorMode || !ScreenSupportsColor())
        return;

    ApplyMarkdownSyntaxColors();
    InvalRect(&doc->window->portRect);
}

/*
    Live "type the markdown, get the formatting" for Writer mode: called
    after every keystroke. Looks backward from the caret for a delimiter
    pair that the just-typed character completed, and if found, strips
    both delimiters and applies the corresponding style in place.
    Strikethrough has no native classic Mac text style, so it stays
    menu-only; everything else, including links, converts live.
*/
void DetectInlineMarkdown(char justTyped)
{
    DocumentPtr doc = FrontDocument();
    Handle textH;
    long len;
    long caret;
    long lineStart;
    long lineEnd;

    if (justTyped == '\r') {
        SetTypingStyleNormal((**doc->hiddenTE).selEnd);
        return;
    }

    textH = (**doc->hiddenTE).hText;
    len = (**doc->hiddenTE).teLength;
    caret = (**doc->hiddenTE).selEnd;

    HLock(textH);

    lineStart = caret;
    while (lineStart > 0 && (*textH)[lineStart - 1] != '\r')
        lineStart--;

    lineEnd = caret;
    while (lineEnd < len && (*textH)[lineEnd] != '\r')
        lineEnd++;

    if (justTyped == ' ') {
        short level = 0;
        long p = lineStart;

        while (level < 6 && p < caret - 1 && (*textH)[p] == '#') {
            level++;
            p++;
        }
        if (level > 0 && p == caret - 1) {
            TextStyle ts;

            HUnlock(textH);
            TESetSelect((short) lineStart, (short) caret, doc->hiddenTE);
            TEDelete(doc->hiddenTE);
            TESetSelect((short) lineStart, (short) lineStart, doc->hiddenTE);
            ts.tsFace = bold;
            ts.tsSize = CurrentWriterFontSize() + kHeadingSizeDeltas[level - 1];
            TESetStyle(doFace + doSize, &ts, true, doc->hiddenTE);
            InvalidateHeightCache();
            return;
        }

        if ((*textH)[lineStart] == '>' && lineStart == caret - 1) {
            TextStyle ts;
            short genevaFont;

            GetFNum("\pGeneva", &genevaFont);
            HUnlock(textH);
            TESetSelect((short) lineStart, (short) caret, doc->hiddenTE);
            TEDelete(doc->hiddenTE);
            TESetSelect((short) lineStart, (short) lineStart, doc->hiddenTE);
            ts.tsFont = genevaFont;
            TESetStyle(doFont, &ts, true, doc->hiddenTE);
            InvalidateHeightCache();
            return;
        }
    } else if (justTyped == '*') {
        if (caret >= 4 && (*textH)[caret - 2] == '*' && (*textH)[caret - 1] == '*') {
            long p = caret - 4;

            while (p >= lineStart) {
                if ((*textH)[p] == '*' && (*textH)[p + 1] == '*' && p + 2 < caret - 2) {
                    long innerStart = p + 2;
                    long innerEnd = caret - 2;
                    TextStyle ts;

                    HUnlock(textH);
                    TESetSelect((short) innerEnd, (short) caret, doc->hiddenTE);
                    TEDelete(doc->hiddenTE);
                    TESetSelect((short) p, (short) innerStart, doc->hiddenTE);
                    TEDelete(doc->hiddenTE);

                    ts.tsFace = bold;
                    TESetSelect((short) p, (short) (innerEnd - 2), doc->hiddenTE);
                    TESetStyle(doFace, &ts, true, doc->hiddenTE);
                    SetTypingStyleNormal((short) (innerEnd - 2));
                    InvalidateHeightCache();
                    return;
                }
                p--;
            }

            /* No opening ** behind the caret -- the just-typed ** may
               instead be an OPENING delimiter for a closing ** that's
               already sitting later in the line (going back to bold
               text that was typed earlier, closing delimiter first). */
            {
                long q = caret + 1;

                while (q + 1 < lineEnd) {
                    if ((*textH)[q] == '*' && (*textH)[q + 1] == '*') {
                        long innerEnd = q;
                        TextStyle ts;

                        HUnlock(textH);
                        TESetSelect((short) innerEnd, (short) (innerEnd + 2), doc->hiddenTE);
                        TEDelete(doc->hiddenTE);
                        TESetSelect((short) (caret - 2), (short) caret, doc->hiddenTE);
                        TEDelete(doc->hiddenTE);

                        ts.tsFace = bold;
                        TESetSelect((short) (caret - 2), (short) (innerEnd - 2), doc->hiddenTE);
                        TESetStyle(doFace, &ts, true, doc->hiddenTE);
                        SetTypingStyleNormal((short) (caret - 2));
                        InvalidateHeightCache();
                        return;
                    }
                    q++;
                }
            }
        } else if (caret >= 3 && (*textH)[caret - 2] != '*') {
            long p = caret - 2;

            while (p >= lineStart) {
                if ((*textH)[p] == '*' &&
                    (p == lineStart || (*textH)[p - 1] != '*') &&
                    (*textH)[p + 1] != '*' && p + 1 < caret - 1) {
                    long innerStart = p + 1;
                    long innerEnd = caret - 1;
                    TextStyle ts;

                    HUnlock(textH);
                    TESetSelect((short) innerEnd, (short) caret, doc->hiddenTE);
                    TEDelete(doc->hiddenTE);
                    TESetSelect((short) p, (short) innerStart, doc->hiddenTE);
                    TEDelete(doc->hiddenTE);

                    ts.tsFace = italic;
                    TESetSelect((short) p, (short) (innerEnd - 1), doc->hiddenTE);
                    TESetStyle(doFace, &ts, true, doc->hiddenTE);
                    SetTypingStyleNormal((short) (innerEnd - 1));
                    InvalidateHeightCache();
                    return;
                }
                p--;
            }

            /* No opening * behind the caret -- the just-typed * may
               instead be an OPENING italic delimiter for a closing *
               that's already sitting later in the line. */
            {
                long q = caret;

                while (q < lineEnd) {
                    if ((*textH)[q] == '*' &&
                        (*textH)[q - 1] != '*' &&
                        (q + 1 == lineEnd || (*textH)[q + 1] != '*') &&
                        q > caret) {
                        long innerEnd = q;
                        TextStyle ts;

                        HUnlock(textH);
                        TESetSelect((short) innerEnd, (short) (innerEnd + 1), doc->hiddenTE);
                        TEDelete(doc->hiddenTE);
                        TESetSelect((short) (caret - 1), (short) caret, doc->hiddenTE);
                        TEDelete(doc->hiddenTE);

                        ts.tsFace = italic;
                        TESetSelect((short) (caret - 1), (short) (innerEnd - 1), doc->hiddenTE);
                        TESetStyle(doFace, &ts, true, doc->hiddenTE);
                        SetTypingStyleNormal((short) (caret - 1));
                        InvalidateHeightCache();
                        return;
                    }
                    q++;
                }
            }
        }
    } else if (justTyped == '_') {
        if (caret >= 4 && (*textH)[caret - 2] == '_' && (*textH)[caret - 1] == '_') {
            long p = caret - 4;

            while (p >= lineStart) {
                if ((*textH)[p] == '_' && (*textH)[p + 1] == '_' && p + 2 < caret - 2) {
                    long innerStart = p + 2;
                    long innerEnd = caret - 2;
                    TextStyle ts;

                    HUnlock(textH);
                    TESetSelect((short) innerEnd, (short) caret, doc->hiddenTE);
                    TEDelete(doc->hiddenTE);
                    TESetSelect((short) p, (short) innerStart, doc->hiddenTE);
                    TEDelete(doc->hiddenTE);

                    ts.tsFace = underline;
                    TESetSelect((short) p, (short) (innerEnd - 2), doc->hiddenTE);
                    TESetStyle(doFace, &ts, true, doc->hiddenTE);
                    SetTypingStyleNormal((short) (innerEnd - 2));
                    InvalidateHeightCache();
                    return;
                }
                p--;
            }

            /* No opening __ behind the caret -- the just-typed __ may
               instead be an OPENING delimiter for a closing __ that's
               already sitting later in the line (going back to
               underlined text that was typed earlier, closing
               delimiter first). */
            {
                long q = caret + 1;

                while (q + 1 < lineEnd) {
                    if ((*textH)[q] == '_' && (*textH)[q + 1] == '_') {
                        long innerEnd = q;
                        TextStyle ts;

                        HUnlock(textH);
                        TESetSelect((short) innerEnd, (short) (innerEnd + 2), doc->hiddenTE);
                        TEDelete(doc->hiddenTE);
                        TESetSelect((short) (caret - 2), (short) caret, doc->hiddenTE);
                        TEDelete(doc->hiddenTE);

                        ts.tsFace = underline;
                        TESetSelect((short) (caret - 2), (short) (innerEnd - 2), doc->hiddenTE);
                        TESetStyle(doFace, &ts, true, doc->hiddenTE);
                        SetTypingStyleNormal((short) (caret - 2));
                        InvalidateHeightCache();
                        return;
                    }
                    q++;
                }
            }
        }
    } else if (justTyped == '`') {
        long p = caret - 2;

        while (p >= lineStart) {
            if ((*textH)[p] == '`' && p + 1 < caret - 1) {
                long innerStart = p + 1;
                long innerEnd = caret - 1;
                TextStyle ts;

                HUnlock(textH);
                TESetSelect((short) innerEnd, (short) caret, doc->hiddenTE);
                TEDelete(doc->hiddenTE);
                TESetSelect((short) p, (short) innerStart, doc->hiddenTE);
                TEDelete(doc->hiddenTE);

                GetFNum("\pMonaco", &ts.tsFont);
                TESetSelect((short) p, (short) (innerEnd - 1), doc->hiddenTE);
                TESetStyle(doFont, &ts, true, doc->hiddenTE);
                SetTypingStyleNormal((short) (innerEnd - 1));
                InvalidateHeightCache();
                return;
            }
            p--;
        }

        /* No opening ` behind the caret -- the just-typed ` may instead
           be an OPENING code delimiter for a closing ` already sitting
           later in the line. */
        {
            long q = caret;

            while (q < lineEnd) {
                if ((*textH)[q] == '`' && q > caret) {
                    long innerEnd = q;
                    TextStyle ts;

                    HUnlock(textH);
                    TESetSelect((short) innerEnd, (short) (innerEnd + 1), doc->hiddenTE);
                    TEDelete(doc->hiddenTE);
                    TESetSelect((short) (caret - 1), (short) caret, doc->hiddenTE);
                    TEDelete(doc->hiddenTE);

                    GetFNum("\pMonaco", &ts.tsFont);
                    TESetSelect((short) (caret - 1), (short) (innerEnd - 1), doc->hiddenTE);
                    TESetStyle(doFont, &ts, true, doc->hiddenTE);
                    SetTypingStyleNormal((short) (caret - 1));
                    InvalidateHeightCache();
                    return;
                }
                q++;
            }
        }
    } else if (justTyped == ')') {
        long closeParenPos = caret - 1;
        long p = closeParenPos - 1;

        while (p >= lineStart && (*textH)[p] != '(')
            p--;

        if (p >= lineStart && p > lineStart && (*textH)[p - 1] == ']') {
            long openParenPos = p;
            long closeBracketPos = openParenPos - 1;
            long urlStart = openParenPos + 1;
            long urlLen = closeParenPos - urlStart;
            long q = closeBracketPos - 1;

            while (q >= lineStart && (*textH)[q] != '[')
                q--;

            if (q >= lineStart) {
                long openBracketPos = q;
                Str255 url;
                short linkID;
                TextStyle ts;

                if (urlLen < 0) urlLen = 0;
                if (urlLen > 255) urlLen = 255;
                url[0] = (unsigned char) urlLen;
                BlockMove(*textH + urlStart, url + 1, urlLen);

                HUnlock(textH);

                TESetSelect((short) closeBracketPos, (short) caret, doc->hiddenTE);
                TEDelete(doc->hiddenTE);
                TESetSelect((short) openBracketPos, (short) (openBracketPos + 1), doc->hiddenTE);
                TEDelete(doc->hiddenTE);

                linkID = AddLinkURL(url);

                ts.tsFace = underline;
                ts.tsColor.red = linkID;
                ts.tsColor.green = 0;
                ts.tsColor.blue = 65535;
                TESetSelect((short) openBracketPos, (short) (closeBracketPos - 1), doc->hiddenTE);
                TESetStyle(doFace + doColor, &ts, true, doc->hiddenTE);
                SetTypingStyleNormal((short) (closeBracketPos - 1));
                InvalidateHeightCache();
                return;
            }
        }
    }

    HUnlock(textH);
}

/* "None" in Writer mode: just clear the applied style on the selection. */
void ClearSelectionStyleHidden(void)
{
    DocumentPtr doc = FrontDocument();
    TextStyle ts;
    short fontNum;

    if ((**doc->hiddenTE).selStart == (**doc->hiddenTE).selEnd)
        return;

    GetFNum("\pTimes", &fontNum);
    ts.tsFont = fontNum;
    ts.tsFace = normal;
    ts.tsSize = CurrentWriterFontSize();
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;
    TESetStyle(doFont + doFace + doSize + doColor, &ts, true, doc->hiddenTE);
}

/*
    "None" in Markdown mode: strips any matched markdown delimiter pairs
    that fall entirely within the selection. Delimiters that extend
    outside the selection are left alone -- to clear those,
    extend the selection to include them, or toggle the specific Style
    menu item that applied them.
*/
void ClearMarkdownInSelection(void)
{
    DocumentPtr doc = FrontDocument();
    Handle textH;
    short selStart, selEnd;
    Handle outH;
    long outLen;
    long i;
    Boolean inFence = false;

    selStart = (**doc->te).selStart;
    selEnd = (**doc->te).selEnd;
    if (selStart == selEnd)
        return;

    textH = (**doc->te).hText;
    outH = NewHandle(selEnd - selStart + 1);
    outLen = 0;

    HLock(textH);
    HLock(outH);

    i = selStart;
    while (i < selEnd) {
        if (i == 0 || (*textH)[i - 1] == '\r') {
            if (i + 2 < selEnd && (*textH)[i] == '`' && (*textH)[i + 1] == '`' && (*textH)[i + 2] == '`' &&
                (i + 3 == selEnd || (*textH)[i + 3] == '\r')) {
                inFence = !inFence;
                i = (i + 3 < selEnd) ? i + 4 : selEnd;
                continue;
            }

            if (inFence) {
                while (i < selEnd && (*textH)[i] != '\r') {
                    (*outH)[outLen++] = (*textH)[i];
                    i++;
                }
                continue;
            }

            short level = 0;
            long p = i;

            while (level < 6 && p < selEnd && (*textH)[p] == '#') {
                level++;
                p++;
            }
            if (level > 0 && p < selEnd && (*textH)[p] == ' ') {
                i = p + 1;
                continue;
            }

            if ((*textH)[i] == '>' && i + 1 < selEnd && (*textH)[i + 1] == ' ') {
                i += 2;
                continue;
            }
        }

        if (i + 1 < selEnd && (*textH)[i] == '*' && (*textH)[i + 1] == '*') {
            long j = i + 2;

            while (j + 1 < selEnd && !((*textH)[j] == '*' && (*textH)[j + 1] == '*'))
                j++;
            if (j + 1 < selEnd) {
                long k;

                for (k = i + 2; k < j; k++)
                    (*outH)[outLen++] = (*textH)[k];
                i = j + 2;
                continue;
            }
        }
        if ((*textH)[i] == '*') {
            long j = i + 1;

            while (j < selEnd && (*textH)[j] != '*')
                j++;
            if (j < selEnd) {
                long k;

                for (k = i + 1; k < j; k++)
                    (*outH)[outLen++] = (*textH)[k];
                i = j + 1;
                continue;
            }
        }
        if ((*textH)[i] == '`') {
            long j = i + 1;

            while (j < selEnd && (*textH)[j] != '`')
                j++;
            if (j < selEnd) {
                long k;

                for (k = i + 1; k < j; k++)
                    (*outH)[outLen++] = (*textH)[k];
                i = j + 1;
                continue;
            }
        }
        if ((*textH)[i] == '[') {
            long closeBracket = i + 1;

            while (closeBracket < selEnd && (*textH)[closeBracket] != ']')
                closeBracket++;
            if (closeBracket < selEnd && closeBracket + 1 < selEnd && (*textH)[closeBracket + 1] == '(') {
                long closeParen = closeBracket + 2;

                while (closeParen < selEnd && (*textH)[closeParen] != ')')
                    closeParen++;
                if (closeParen < selEnd) {
                    long k;

                    for (k = i + 1; k < closeBracket; k++)
                        (*outH)[outLen++] = (*textH)[k];
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        (*outH)[outLen++] = (*textH)[i];
        i++;
    }

    HUnlock(textH);
    HUnlock(outH);

    TESetSelect(selStart, selEnd, doc->te);
    TEDelete(doc->te);
    TEInsert(*outH, outLen, doc->te);
    DisposeHandle(outH);

    TESetSelect(selStart, (short) (selStart + outLen), doc->te);
}
