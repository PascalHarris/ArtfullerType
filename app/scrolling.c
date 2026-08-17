#include "app.h"

/*
    The scrollbar's value is never tracked as an independent counter --
    it's always derived fresh from TextEdit's own destRect vs. viewRect,
    so it can't drift out of sync with where the text actually is. Every
    scroll operation below scrolls by (current real offset - desired
    offset) and then re-reads the real offset afterward, rather than
    trusting an incrementally-adjusted running total.
*/
static short CurrentScrollOffset(TEHandle te)
{
    return (**te).viewRect.top - (**te).destRect.top;
}

static void SyncScrollbarToOffset(DocumentPtr doc)
{
    short newValue = CurrentScrollOffset(doc->activeTE);

    /* SetControlValue always redraws the control, even when the value is
       unchanged -- called every tick, an unguarded call here would redraw
       the scrollbar (and the flicker that comes with it) on every single
       keystroke for no reason. */
    if (newValue != GetControlValue(doc->scrollBar))
        SetControlValue(doc->scrollBar, newValue);
}

/*
    TEGetHeight(nLines, 0, te) and the two calls in ScrollCaretIntoView
    below are cumulative-from-line-0 height sums -- the form that's
    proven reliable (see the comment in ScrollCaretIntoView), but O(n)
    in the document's current line count. Calling that on every single
    keystroke is fine on a fast emulator but visibly slows typing down
    on real 68000 hardware as a document grows. The two small caches now
    living in DocumentRecord (cachedTotalHeightNLines/cachedTotalHeight,
    cachedCaretLine/cachedHeightToLine/cachedHeightToLineNext -- moved
    here from this file's own file-statics as part of the DocumentRecord
    refactor, see document.h) skip the recompute whenever nothing that
    affects the answer has changed since the last call -- the underlying
    TEGetHeight calls and their cumulative-from-0 form are otherwise
    untouched.

    Invalidated via InvalidateHeightCache(): unconditionally from
    AdjustScrollbar (the full/infrequent path covering style changes,
    zoom, mode switches, undo/redo, save/load -- anything that can
    change a line's height without necessarily changing nLines), and
    from DetectInlineMarkdown's live-typing conversions (markdown.c),
    since those happen within the fast per-keystroke path and can also
    change a line's height (heading conversion) without changing
    nLines.
*/
void InvalidateHeightCache(void)
{
    DocumentPtr doc = FrontDocument();

    doc->cachedTotalHeightNLines = -1;
    doc->cachedCaretLine = -1;
}

/*
    Updates the scrollbar's range/visibility only -- no clamping of the
    current position. Used on the typing path, where ScrollCaretIntoView
    already owns getting the position right; re-deriving maxVal from
    TEGetHeight for a line that's actively growing as you type is exactly
    the kind of thing that could disagree with ScrollCaretIntoView's own
    (separately computed) target by a pixel or two, and clamping on that
    discrepancy every keystroke is what was causing a brief upward jump.
*/
void UpdateScrollbarRange(void)
{
    DocumentPtr doc = FrontDocument();
    long textHeight;
    short viewHeight;
    short maxVal;
    Boolean shouldShow;

    if ((**doc->activeTE).nLines == doc->cachedTotalHeightNLines) {
        textHeight = doc->cachedTotalHeight;
    } else {
        textHeight = TEGetHeight((**doc->activeTE).nLines, 0, doc->activeTE);
        doc->cachedTotalHeightNLines = (**doc->activeTE).nLines;
        doc->cachedTotalHeight = textHeight;
    }
    viewHeight = (**doc->activeTE).viewRect.bottom - (**doc->activeTE).viewRect.top;

    maxVal = (textHeight > viewHeight) ? (short) (textHeight - viewHeight) : 0;

    if (maxVal != GetControlMaximum(doc->scrollBar))
        SetControlMaximum(doc->scrollBar, maxVal);

    shouldShow = (maxVal > 0);
    if (shouldShow != doc->scrollBarVisible) {
        if (shouldShow)
            ShowControl(doc->scrollBar);
        else
            HideControl(doc->scrollBar);
        doc->scrollBarVisible = shouldShow;
    }
}

/*
    Full version: also clamps the current scroll position if it now
    exceeds the (possibly shrunk) range. Needed after anything that can
    reduce content height -- Style commands, zoom, load/new, mode switch
    -- but not after plain typing, which only ever grows it.
*/
void AdjustScrollbar(void)
{
    DocumentPtr doc = FrontDocument();
    short maxVal;
    short curOffset;

    InvalidateHeightCache();
    UpdateScrollbarRange();

    maxVal = GetControlMaximum(doc->scrollBar);
    curOffset = CurrentScrollOffset(doc->activeTE);
    if (curOffset > maxVal)
        TEScroll(0, curOffset - maxVal, doc->activeTE);
    else if (curOffset < 0)
        TEScroll(0, curOffset, doc->activeTE);

    SyncScrollbarToOffset(doc);
}

/* lineStarts[] is sorted, so the line containing pos is found with a
   binary search instead of a linear scan -- same result, no behavior
   change, just faster for documents with many lines. */
static short LineContaining(TEHandle te, short pos)
{
    short low = 0;
    short high = (**te).nLines - 1;

    while (low < high) {
        short mid = low + (high - low + 1) / 2;

        if ((**te).lineStarts[mid] <= pos)
            low = mid;
        else
            high = mid - 1;
    }
    return low;
}

void ScrollCaretIntoView(void)
{
    DocumentPtr doc = FrontDocument();
    short caretLine;
    long heightToLine, heightToLineNext;
    short lineTop, lineBottom;
    short viewTop, viewBottom;

    caretLine = LineContaining(doc->activeTE, (**doc->activeTE).selEnd);

    /* Querying a single line's height in isolation (e.g. TEGetHeight
       for just [caretLine, caretLine+1)) comes back unreliable right
       after Enter creates a new, still-empty line -- it hasn't
       "settled" with any content yet. (**te).lineHeight turned out
       to have the same problem, returning a stale/wrong value rather
       than tracking the actual current font size. Avoid isolated
       single-line queries entirely: always sum cumulatively from the
       very start of the document, the same pattern already proven
       reliable in UpdateScrollbarRange's TEGetHeight(nLines, 0, ...).
       Cached below (see InvalidateHeightCache) since this is otherwise
       an O(n) call on every keystroke -- the raw heights are cached
       rather than the final lineTop/lineBottom, since those also
       depend on destRect.top, which changes on scroll. */
    if (caretLine == doc->cachedCaretLine) {
        heightToLine = doc->cachedHeightToLine;
        heightToLineNext = doc->cachedHeightToLineNext;
    } else {
        heightToLine = TEGetHeight(caretLine, 0, doc->activeTE);
        heightToLineNext = TEGetHeight(caretLine + 1, 0, doc->activeTE);
        doc->cachedCaretLine = caretLine;
        doc->cachedHeightToLine = heightToLine;
        doc->cachedHeightToLineNext = heightToLineNext;
    }
    lineTop = (**doc->activeTE).destRect.top + heightToLine;
    lineBottom = (**doc->activeTE).destRect.top + heightToLineNext;

    viewTop = (**doc->activeTE).viewRect.top;
    viewBottom = (**doc->activeTE).viewRect.bottom;

    if (lineBottom > viewBottom)
        TEScroll(0, viewBottom - lineBottom, doc->activeTE);
    else if (lineTop < viewTop)
        TEScroll(0, viewTop - lineTop, doc->activeTE);

    SyncScrollbarToOffset(doc);
}

/*
    ControlActionUPP has a fixed Toolbox-mandated signature -- no room
    for an extra DocumentPtr parameter here the way ordinary functions
    in this refactor get one. Resolved instead from the control itself:
    every ControlHandle already knows its owning window via
    contrlOwner, and DocumentForWindow (document.c) turns that into the
    document that owns it. Correct even once Milestone 3 allows more
    than one document/scrollbar to exist -- this doesn't assume "the"
    front document the way most of this refactor's FrontDocument()
    calls do, since a control's action proc has no reason to assume its
    control belongs to the front window.
*/
static pascal void ScrollAction(ControlHandle control, short part)
{
    DocumentPtr doc = DocumentForWindow((**control).contrlOwner);
    short max, delta, desired;
    short pageSize;

    if (part == 0 || doc == NULL)
        return;

    max = GetControlMaximum(control);
    pageSize = (**doc->activeTE).viewRect.bottom - (**doc->activeTE).viewRect.top;

    switch (part) {
        case inUpButton:   delta = -16; break;
        case inDownButton: delta = 16; break;
        case inPageUp:     delta = -pageSize; break;
        case inPageDown:   delta = pageSize; break;
        default:           delta = 0; break;
    }

    desired = CurrentScrollOffset(doc->activeTE) + delta;
    if (desired < 0) desired = 0;
    if (desired > max) desired = max;

    TEScroll(0, CurrentScrollOffset(doc->activeTE) - desired, doc->activeTE);
    SetControlValue(control, CurrentScrollOffset(doc->activeTE));
}

void DoScrollClick(Point pt)
{
    DocumentPtr doc = FrontDocument();
    ControlHandle control;
    short part;
    short desired;

    part = FindControl(pt, doc->window, &control);
    if (part == 0 || control != doc->scrollBar)
        return;

    if (part == inThumb) {
        TrackControl(doc->scrollBar, pt, NULL);
        desired = GetControlValue(doc->scrollBar);
        TEScroll(0, CurrentScrollOffset(doc->activeTE) - desired, doc->activeTE);
        SyncScrollbarToOffset(doc);
    } else {
        TrackControl(doc->scrollBar, pt, NewControlActionUPP(ScrollAction));
    }
}
