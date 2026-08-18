#include "app.h"

DocumentRecord gDocuments[MAX_DOCUMENTS];

/*
    Deviation from MULTI_WINDOW_DESIGN.md's original sketch: the design
    doc suggested SetWRefCon/GetWRefCon for O(1) window-to-document
    lookup. A linear scan over at most MAX_DOCUMENTS (4) slots is
    simpler, needs no extra per-window bookkeeping to keep in sync, and
    the cost difference is meaningless at this size -- so DocumentForWindow
    just scans gDocuments directly instead. Worth revisiting only if
    MAX_DOCUMENTS grows enough for that to matter, which seems unlikely
    given the memory-budget concerns in MULTI_WINDOW_DESIGN.md §9.
*/
DocumentPtr DocumentForWindow(WindowPtr w)
{
    short i;

    if (w == NULL)
        return NULL;

    for (i = 0; i < MAX_DOCUMENTS; i++) {
        if (gDocuments[i].inUse && gDocuments[i].window == w)
            return &gDocuments[i];
    }
    return NULL;
}

DocumentPtr FrontDocument(void)
{
    return DocumentForWindow(FrontWindow());
}

DocumentPtr FindFreeDocumentSlot(void)
{
    short i;

    for (i = 0; i < MAX_DOCUMENTS; i++) {
        if (!gDocuments[i].inUse)
            return &gDocuments[i];
    }
    return NULL;
}

void UpdateWindowTitle(DocumentPtr doc)
{
    if (doc == NULL)
        return;

    if (doc->haveFile)
        SetWTitle(doc->window, doc->fileName);
    else
        SetWTitle(doc->window, "\pUntitled");
}

/*
    Builds (or rebuilds) one document's window and scrollbar, and
    computes the text viewRect for it -- the geometry work shared by
    CreateNewDocument (a brand new document, which then creates fresh
    TE records into this geometry) and ReHouseDocument (an existing
    document swapping chrome, which reuses its existing TE records and
    just re-points them at this new geometry) -- per the note at the
    end of MULTI_WINDOW_DESIGN.md §4.2. Deliberately doesn't touch TE
    records at all: that's exactly the part that differs between the
    two callers, so it stays their job.

    outViewRect is a small deviation from the doc's own 4-parameter
    BuildWindowChrome(doc, bounds, proc, visible) sketch -- that sketch
    never actually shows how the computed viewRect gets back to the
    caller either, since in the doc's pseudocode the "shared" chrome
    work is written inline rather than factored out at all. Both real
    callers need viewRect afterward, so this returns it directly
    rather than making them recompute the same arithmetic from
    doc->window->portRect a second time -- keeps the doc's own stated
    goal (not duplicating the viewRect/scrollbar setup) rather than
    the letter of a 4-arg signature that was illustrative, not a firm
    contract.
*/
static void BuildWindowChrome(DocumentPtr doc, Rect *bounds, short proc,
                               Boolean visible, Rect *outViewRect)
{
    Rect sbRect;

    doc->window = NewWindow(NULL, bounds, "\p", true, proc,
                             (WindowPtr) -1L, visible, 0);
    SetPort(doc->window);

    /*
        Scrollbar first, flush against the window's right edge and
        spanning the full window height -- the standard Mac layout
        (e.g. TeachText). The 1px overlap on all three outer edges
        (right/top/bottom) is the usual classic-Mac convention so the
        control's own frame blends with the window's, rather than
        leaving a visible 1px gap.

        Text viewRect is then inset from the scrollbar's left edge on
        the right (not from the window's raw right edge), so text
        never runs underneath it. Left/top/bottom use the small
        MARGIN_H/MARGIN_TOP/MARGIN_BOTTOM text margins (see app.h).
    */
    sbRect.right = doc->window->portRect.right + 1;
    sbRect.left = sbRect.right - SCROLLBAR_WIDTH;
    sbRect.top = doc->window->portRect.top - 1;
    sbRect.bottom = doc->window->portRect.bottom + 1;
    doc->scrollBar = NewControl(doc->window, &sbRect, "\p", false, 0, 0, 0, scrollBarProc, 0);
    doc->scrollBarVisible = false;

    *outViewRect = doc->window->portRect;
    outViewRect->left += MARGIN_H;
    outViewRect->right -= (SCROLLBAR_WIDTH + MARGIN_H);
    outViewRect->top += MARGIN_TOP;
    outViewRect->bottom -= MARGIN_BOTTOM;
}

/*
    Builds one new document: window, both TE records, scrollbar, and
    every DocumentRecord field given an explicit initial value -- this
    is Milestone 1's MakeWindow (formerly in main.c, always operating
    on gDocuments[0]) generalized to run more than once, picking
    whichever slot FindFreeDocumentSlot finds free rather than always
    the first one. Returns NULL if none is free -- callers (file.c's
    DoNewFile/DoOpenFile) treat that as a no-op; the File menu's New/
    Open should already be disabled by then via UpdateFileMenuState,
    so reaching NULL here in practice would mean that got out of sync,
    not a normal user path.

    Window sizing: fixed default width/height per MULTI_WINDOW_DESIGN.md
    §4.1 as literally described would be wrong on this project's actual
    target hardware -- see the long comment on kDefaultWindowMargin in
    app.h for why this computes the size from qd.screenBits.bounds at
    runtime instead. Each new window is offset diagonally from the
    previous by kWindowStagger so successive New/Open calls don't stack
    exactly on top of each other; the offset is clamped afterward so a
    window can never start partly off-screen even after several
    creations with a small kDefaultWindowMargin.
*/
DocumentPtr CreateNewDocument(void)
{
    DocumentPtr doc = FindFreeDocumentSlot();
    Rect bounds;
    Rect viewRect;
    short fontNum;
    short slotIndex;
    short stagger;
    short winWidth, winHeight;

    if (doc == NULL)
        return NULL;

    slotIndex = (short) (doc - gDocuments);
    stagger = (short) (slotIndex * kWindowStagger);

    winWidth = (short) ((qd.screenBits.bounds.right - qd.screenBits.bounds.left)
                         - (kDefaultWindowMargin * 2));
    winHeight = (short) ((qd.screenBits.bounds.bottom - qd.screenBits.bounds.top
                           - MENU_BAR_HEIGHT) - (kDefaultWindowMargin * 2));

    SetRect(&bounds, 0, 0, winWidth, winHeight);
    OffsetRect(&bounds,
        qd.screenBits.bounds.left + kDefaultWindowMargin + stagger,
        qd.screenBits.bounds.top + MENU_BAR_HEIGHT + kDefaultWindowMargin + stagger);

    /* Slide back on-screen if staggering pushed the right/bottom edge
       past the screen -- simpler and more robust than hand-tuning
       kDefaultWindowMargin/kWindowStagger/MAX_DOCUMENTS to never
       overflow, and stays correct if any of those three change later. */
    if (bounds.right > qd.screenBits.bounds.right)
        OffsetRect(&bounds, qd.screenBits.bounds.right - bounds.right, 0);
    if (bounds.bottom > qd.screenBits.bounds.bottom)
        OffsetRect(&bounds, 0, qd.screenBits.bounds.bottom - bounds.bottom);

    BuildWindowChrome(doc, &bounds, documentProc, true, &viewRect);

    GetFNum("\pTimes", &fontNum);
    TextFont(fontNum);
    TextSize(CurrentFontSize());

    doc->te = TEStyleNew(&viewRect, &viewRect);
    doc->hiddenTE = TEStyleNew(&viewRect, &viewRect);
    doc->hideMarkdown = true;
    doc->activeTE = doc->hideMarkdown ? doc->hiddenTE : doc->te;
    TEActivate(doc->activeTE);

    doc->haveFile = false;
    doc->dirty = false;
    doc->fileName[0] = 0;
    doc->vRefNum = 0;

    doc->undoCount = 0;
    doc->redoCount = 0;
    doc->typingRunActive = false;

    doc->linkCount = 0;

    doc->cachedTotalHeightNLines = -1;
    doc->cachedCaretLine = -1;
    doc->cachedTotalHeight = 0;
    doc->cachedHeightToLine = 0;
    doc->cachedHeightToLineNext = 0;

    doc->distractionFree = false;

    /* Must be set before UpdateWindowTitle -- DocumentForWindow (which
       UpdateWindowTitle doesn't call directly, but the general rule
       throughout this codebase is that a document isn't "real" to any
       lookup until inUse is true) and every other field above it must
       already be in their final initial state first. */
    doc->inUse = true;

    UpdateWindowTitle(doc);

    return doc;
}

/*
    Rebuilds doc's window with different chrome -- standard <-> full-
    screen borderless -- while preserving its content, styling, undo/
    redo history, and link table untouched, per MULTI_WINDOW_DESIGN.md
    §4.2: TE records aren't structurally owned by a window (see the
    comment above SuppressDrawing in markdown.c, which already relies
    on this), so only the window itself, its scrollbar, and the TE
    records' viewRect/destRect need rebuilding -- not the TE records
    themselves.

    Precondition, same shape as CloseDocument's: doc must already be
    FrontDocument() when this is called. InvalidateHeightCache and
    AdjustScrollbar (scrolling.c) self-resolve their target via
    FrontDocument() rather than taking a DocumentPtr -- this holds at
    every current call site (SetDistractionFree, main.c) because
    NewWindow's behind=(WindowPtr)-1L below places doc's rebuilt window
    frontmost immediately, before either of those calls run later in
    this same function.

    Milestone 5 scope: this only ever touches doc's own window. Hiding/
    showing every OTHER open document to maintain "frontmost
    distraction-free implies everyone else hidden" is Milestone 6's
    concern -- SetDistractionFree (main.c) wraps this with that
    orchestration; nothing here needs to know about other documents.
*/
void ReHouseDocument(DocumentPtr doc, Boolean toDistractionFree)
{
    Rect newBounds, viewRect;
    short savedSelStart, savedSelEnd;

    if (doc == NULL)
        return;

    savedSelStart = (**doc->activeTE).selStart;
    savedSelEnd = (**doc->activeTE).selEnd;

    if (toDistractionFree) {
        /* portRect is in local (window-relative) coordinates -- convert
           to global before storing, since the window it's relative to
           is about to be disposed and a local rect means nothing once
           that's gone. LocalToGlobal converts one Point at a time, not
           a Rect directly -- the (Point*)&rect.top / &rect.bottom cast
           works because {top,left} and {bottom,right} are laid out
           exactly like {v,h} in memory, the standard classic Mac idiom
           for converting a Rect's two corners in place. */
        Rect globalBounds = doc->window->portRect;

        LocalToGlobal((Point *) &globalBounds.top);
        LocalToGlobal((Point *) &globalBounds.bottom);
        doc->standardBounds = globalBounds;

        newBounds = qd.screenBits.bounds;
        newBounds.top += MENU_BAR_HEIGHT;
    } else {
        newBounds = doc->standardBounds;
    }

    /* DisposeWindow while doc->window is the current port leaves
       thePort dangling until BuildWindowChrome's own SetPort(doc->window)
       runs -- that happens as literally its first act, so this stays
       safe, but nothing may be inserted between these two calls that
       touches the port. */
    DisposeControl(doc->scrollBar);
    DisposeWindow(doc->window);

    BuildWindowChrome(doc, &newBounds,
                       toDistractionFree ? plainDBox : documentProc,
                       true, &viewRect);

    (**doc->te).viewRect = viewRect;
    (**doc->te).destRect = viewRect;
    (**doc->hiddenTE).viewRect = viewRect;
    (**doc->hiddenTE).destRect = viewRect;
    TECalText(doc->te);
    TECalText(doc->hiddenTE);
    TESetSelect(savedSelStart, savedSelEnd, doc->activeTE);

    doc->distractionFree = toDistractionFree;
    UpdateWindowTitle(doc);
    InvalidateHeightCache();
    AdjustScrollbar();
    InvalRect(&doc->window->portRect);
}

/*
    Precondition: doc must be FrontDocument() when this is called.
    ClearUndoRedoStacks and every other undo/redo helper in this
    codebase self-resolve their target via FrontDocument() rather than
    taking a DocumentPtr (see MULTI_WINDOW_DESIGN.md Milestone 1's
    rationale for that choice) -- freeing doc's own undo/redo snapshot
    Handles here relies on doc still being front at this exact call.
    Every current caller (main.c's CloseDocumentInteractive) already
    guarantees this via SelectWindow(doc->window) immediately before
    calling in.
*/
void CloseDocument(DocumentPtr doc)
{
    if (doc == NULL || !doc->inUse)
        return;

    ClearUndoRedoStacks();

    TEDispose(doc->te);
    TEDispose(doc->hiddenTE);
    /* DisposeWindow also disposes doc->scrollBar -- a Control Manager
       control is owned by its window and goes with it; no separate
       DisposeControl needed or correct here. */
    DisposeWindow(doc->window);

    doc->window = NULL;
    doc->te = NULL;
    doc->hiddenTE = NULL;
    doc->activeTE = NULL;
    doc->scrollBar = NULL;
    doc->inUse = false;
}
