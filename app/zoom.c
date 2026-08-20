#include "app.h"
#include "preferences.h"

/* Writer zoom levels (point deltas from FONT_SIZE, 18pt). 12/14/18/24pt
   have a real Times bitmap -- confirmed by reading the FOND resource
   directly rather than assuming. The 30pt level has no native bitmap
   (24pt is the largest this font has) and renders as a scaled
   enlargement of the 24pt bitmap instead -- a known, accepted tradeoff
   for going bigger. */
static short kWriterZoomLevels[] = { -6, -4, 0, 6, 12 };

/* Markdown zoom levels (point deltas from gPrefs.markdownFontSize, 9pt
   by default) -- deliberately a separate table from Writer's, not the
   same offsets applied to a different baseline: those offsets
   (-6/-4/0/6/12) come out to {3,5,9,15,21} against a 9pt baseline, and
   3-5pt text isn't usable at all. Unlike the Times table's own comment
   above, this hasn't been verified by reading Monaco's actual FOND
   resource -- no font resource data was available to check it against
   directly, so this is a reasoned starting point, not a confirmed one:
   {7,8,9,12,18}, chosen so the baseline (9pt) sits at index
   kZoomBaselineIndex like every other zoom table in this app, with 12pt
   being Monaco's other well-known classic bitmap size one step up, and
   modest single-point steps below the baseline since there's much less
   room to shrink from 9pt than Writer's own 18pt starting point has.
   Worth a real look once built and adjusting this one array literal if
   any step doesn't actually read well. */
static short kMarkdownZoomLevels[] = { -2, -1, 0, 3, 9 };

short CurrentWriterFontSize(void)
{
    return FONT_SIZE + kWriterZoomLevels[gWriterZoomIndex];
}

short CurrentMarkdownFontSize(void)
{
    return gPrefs.markdownFontSize + kMarkdownZoomLevels[gMarkdownZoomIndex];
}

static void LoadOneZoomPref(short prefID, short *index)
{
    Handle prefH = GetResource(kZoomPrefType, prefID);

    if (prefH != NULL) {
        HLock(prefH);
        *index = *(short *) *prefH;
        HUnlock(prefH);
        ReleaseResource(prefH);
        if (*index < 0 || *index >= kNumZoomLevels)
            *index = kZoomBaselineIndex;
    }
}

void LoadZoomPref(void)
{
    gWriterZoomIndex = gPrefs.writerZoomIndex;
    if (gWriterZoomIndex < 0 || gWriterZoomIndex >= kNumZoomLevels)
        gWriterZoomIndex = kZoomBaselineIndex;

    LoadOneZoomPref(kMarkdownZoomPrefID, &gMarkdownZoomIndex);
}

static void SaveOneZoomPref(short prefID, short index)
{
    Handle prefH = GetResource(kZoomPrefType, prefID);

    if (prefH != NULL) {
        HLock(prefH);
        *(short *) *prefH = index;
        HUnlock(prefH);
        ChangedResource(prefH);
        WriteResource(prefH);
        ReleaseResource(prefH);
    }
}

/*
    Remaps any run whose size matches one of the OLD base/heading sizes
    to the corresponding NEW size, in place -- used for Writer zoom, so
    it never re-parses markdown and can't clobber unsynced edits in
    whichever buffer isn't currently canonical. Markdown mode has no
    equivalent: it's one uniform run at all times, so ClearStyles's own
    full reset (already keyed to CurrentMarkdownFontSize) is sufficient
    there -- see ApplyMarkdownZoomIndex below.
*/
static void RescaleStyles(TEHandle te, short oldBase, short newBase)
{
    long len = (**te).teLength;
    long i = 0;
    short savedStart = (**te).selStart;
    short savedEnd = (**te).selEnd;

    while (i < len) {
        TextStyle st;
        short lh, fa;
        long runStart = i;
        short oldSize;
        short newSize;

        TEGetStyle((short) i, &st, &lh, &fa, te);
        oldSize = st.tsSize;

        while (i < len) {
            TextStyle st2;

            TEGetStyle((short) i, &st2, &lh, &fa, te);
            if (st2.tsSize != oldSize)
                break;
            i++;
        }

        if (oldSize == oldBase) newSize = newBase;
        else if (oldSize == oldBase + 12) newSize = newBase + 12;
        else if (oldSize == oldBase + 8) newSize = newBase + 8;
        else if (oldSize == oldBase + 4) newSize = newBase + 4;
        else newSize = oldSize + (newBase - oldBase);

        if (newSize != oldSize) {
            TextStyle ts;

            ts.tsSize = newSize;
            TESetSelect((short) runStart, (short) i, te);
            TESetStyle(doSize, &ts, true, te);
        }
    }

    TESetSelect(savedStart, savedEnd, te);
}

/*
    gWriterZoomIndex/gMarkdownZoomIndex stay single app-wide preferences
    (see app.h) -- multiple open documents would all share one zoom
    setting per view, matching MULTI_WINDOW_DESIGN.md §10's note that
    only the live rescale should be scoped to a document, not the
    preference value. That rescale applies to the front document only
    -- once more than one document is open, changing zoom while
    document A is front does NOT retroactively rescale document B's
    already-built TE for this view (it picks up the new index next
    time that view is (re)built for it -- BuildHiddenView on a switch
    into Writer mode, ClearStyles on a switch into Markdown mode, both
    already keyed to the current zoom index for their own view) -- an
    accepted gap, not something this pass covers fixing.

    Deliberately touches only its own view's TE, not the other one:
    previously (when the two shared one index) every zoom action also
    eagerly resynced the inactive view's TE so it wouldn't drift. That
    eager resync is unnecessary now that the two zoom indices are
    independent -- there's nothing to drift, since each view already
    rebuilds itself from its own current zoom index on every switch
    into it (see above), and touching the inactive TE here would just
    be extra work for no visible effect.

    No longer file-local (app.h now declares it) -- DoPreferences
    (preferences.c) calls this too, so an edited Writer zoom value from
    the Preferences window's Save button takes effect on the front
    document immediately rather than only on next launch, reusing this
    function's own rescale/persist/redraw logic rather than duplicating
    it. Still doesn't NULL-check doc itself, matching the established
    DoZoom/DoZoomReset convention (every caller checks FrontDocument()
    != NULL before calling in, not this function) -- DoPreferences's
    own call site does the same.
*/
void ApplyWriterZoomIndex(short newIndex)
{
    DocumentPtr doc = FrontDocument();
    short oldBase;
    short newBase;

    if (newIndex < 0 || newIndex >= kNumZoomLevels || newIndex == gWriterZoomIndex)
        return;

    oldBase = CurrentWriterFontSize();
    gWriterZoomIndex = newIndex;
    newBase = CurrentWriterFontSize();

    RescaleStyles(doc->hiddenTE, oldBase, newBase);
    gPrefs.writerZoomIndex = gWriterZoomIndex;
    SavePreferences();
    AdjustScrollbar();
    InvalRect(&doc->window->portRect);
}

static void ApplyMarkdownZoomIndex(short newIndex)
{
    DocumentPtr doc = FrontDocument();

    if (newIndex < 0 || newIndex >= kNumZoomLevels || newIndex == gMarkdownZoomIndex)
        return;

    gMarkdownZoomIndex = newIndex;

    /* Markdown mode is one uniform Monaco run at all times (no
       heading-style runs to preserve the way Writer mode's
       RescaleStyles does) -- a full ClearStyles at the new size,
       already the existing "None"/style-reset primitive for this
       view, is simpler and exactly as correct as a targeted rescale
       would be here. */
    ClearStyles();
    SaveOneZoomPref(kMarkdownZoomPrefID, gMarkdownZoomIndex);
    AdjustScrollbar();
    InvalRect(&doc->window->portRect);
}

/*
    Zoom In/Out/Default Size act on whichever view is currently active
    for the front document -- Writer zoom while looking at styled
    prose, Markdown zoom while looking at raw source. This is the
    "independent" half of the two sizes: what Zoom does depends on
    what's on screen, not on a separate per-view menu.
*/
void DoZoom(short direction)
{
    DocumentPtr doc = FrontDocument();

    if (doc == NULL)
        return;

    if (doc->hideMarkdown)
        ApplyWriterZoomIndex(gWriterZoomIndex + direction);
    else
        ApplyMarkdownZoomIndex(gMarkdownZoomIndex + direction);
}

void DoZoomReset(void)
{
    DocumentPtr doc = FrontDocument();

    if (doc == NULL)
        return;

    if (doc->hideMarkdown)
        ApplyWriterZoomIndex(kZoomBaselineIndex);
    else
        ApplyMarkdownZoomIndex(kZoomBaselineIndex);
}
