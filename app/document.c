#include "app.h"
#include "preferences.h"

DocumentRecord gDocuments[MAX_DOCUMENTS];

/* No WStateDataHandle typedef exists in this toolchain's generated
   headers -- WStateData itself is declared (defs/WindowMgr.yaml), but
   not a handle-to-it name, the same gap as zoomDocProc in app.h.
   Defined here, file-local: only BuildWindowChrome/RecordWindowUserState
   below need pointer-level access to a window's WStateData; nothing
   outside this file does. */
typedef WStateData **WStateDataHandle;

/*
    The single "standard" size every document's zoom box grows to --
    deliberately NOT CreateNewDocument's own per-slot staggered bounds
    (see BuildWindowChrome's zoom-setup comment below for why): same
    screen-inset-by-kDefaultWindowMargin math as CreateNewDocument's
    own default sizing, just without the stagger offset, since a
    "standard" size that differs by which document slot you happen to
    be in would be a strange thing for "standard" to mean.
*/
static void ComputeStandardBounds(Rect *out)
{
    SetRect(out, 0, 0,
            (short) ((qd.screenBits.bounds.right - qd.screenBits.bounds.left)
                      - (kDefaultWindowMargin * 2)),
            (short) ((qd.screenBits.bounds.bottom - qd.screenBits.bounds.top
                       - MENU_BAR_HEIGHT) - (kDefaultWindowMargin * 2)));
    OffsetRect(out,
        qd.screenBits.bounds.left + kDefaultWindowMargin,
        qd.screenBits.bounds.top + MENU_BAR_HEIGHT + kDefaultWindowMargin);
}

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
    short bottomInset;
    /* documentProc/zoomDocProc windows have a Window-Manager-reserved
       grow icon in the bottom-right corner (FindWindow reports clicks
       there as inGrow, handled in main.c's EventLoop via
       ResizeDocument below) -- plainDBox (Distraction Free) windows
       have no such region. zoomDocProc keeps the grow box that
       documentProc has (see app.h's zoomDocProc comment: it's
       documentProc's same variation bits plus the zoom-box bit, not a
       different family), so this check stays "not plainDBox" rather
       than naming one specific growable procID. */
    Boolean growable = (proc != plainDBox);

    doc->window = NewWindow(NULL, bounds, "\p", true, proc,
                             (WindowPtr) -1L, visible, 0);
    SetPort(doc->window);

    /*
        Scrollbar flush against the window's right edge and top, the
        standard Mac layout (e.g. TeachText). The 1px overlap on the
        right/top outer edges is the usual classic-Mac convention so
        the control's own frame blends with the window's, rather than
        leaving a visible 1px gap.

        The bottom edge is flush too for plainDBox chrome (no grow
        icon to avoid), but for growable chrome it stops
        SCROLLBAR_WIDTH short of the window bottom -- reusing the
        scrollbar's own width as the inset, the usual size for the
        square corner tile the grow icon occupies, so the corner lines
        up cleanly with both the scrollbar and (once there's a
        horizontal scrollbar, if this app ever gets one) itself.
        Without this inset the down arrow sits exactly in the grow
        icon's hot zone and never receives clicks, since FindWindow
        claims that whole corner as inGrow before this app's own
        content-click handling ever runs.

        Text viewRect is then inset from the scrollbar's left edge on
        the right (not from the window's raw right edge), so text
        never runs underneath it. Left/top use the small MARGIN_H/
        MARGIN_TOP text margins (see app.h). Bottom uses the LARGER of
        MARGIN_BOTTOM and (for growable chrome) the same
        SCROLLBAR_WIDTH inset the scrollbar itself uses -- a real bug
        lived here until this fix: bottom used to be inset by
        MARGIN_BOTTOM alone (8px) regardless of chrome, leaving 8px of
        the grow icon's reserved 16px-tall corner strip still inside
        the text viewRect. Text drawn there wasn't visually corrupting
        the *scrollbar* control itself (the two never overlap in X),
        but sat inside that reserved corner where nothing ever
        proactively erases it except a full-window update
        (DrawGrowIcon, main.c's DoUpdate) -- a TEScroll (which doesn't
        trigger DrawGrowIcon at all) just carried those stray pixels
        along with the rest of the scrolled content, exactly matching
        the reported "fragments of text scroll with the scrollbar"
        symptom. DrawGrowIcon repainting on top of it was never a real
        fix for that gap, only for the one small 13x13 icon glyph
        within it.
    */
    bottomInset = growable ? SCROLLBAR_WIDTH : MARGIN_BOTTOM;
    if (bottomInset < MARGIN_BOTTOM)
        bottomInset = MARGIN_BOTTOM;

    sbRect.right = doc->window->portRect.right + 1;
    sbRect.left = sbRect.right - SCROLLBAR_WIDTH;
    sbRect.top = doc->window->portRect.top - 1;
    sbRect.bottom = doc->window->portRect.bottom + 1
                     - (growable ? SCROLLBAR_WIDTH : 0);
    doc->scrollBar = NewControl(doc->window, &sbRect, "\p", false, 0, 0, 0, scrollBarProc, 0);
    doc->scrollBarVisible = false;

    *outViewRect = doc->window->portRect;
    outViewRect->left += MARGIN_H;
    outViewRect->right -= (SCROLLBAR_WIDTH + MARGIN_H);
    outViewRect->top += MARGIN_TOP;
    outViewRect->bottom -= bottomInset;

    /*
        Zoom box setup: the Window Manager allocates doc->window's
        WStateData record automatically as part of NewWindow for any
        zoom-box procID (standard, ROM-level Window Manager behavior,
        not something this toolchain's generator declares one way or
        the other -- there's nothing to grep for in a trap-table
        definition; noted here as the one piece of this feature I
        could confirm only against long-standing documented Toolbox
        behavior, not against this specific toolchain's own source,
        the way zoomDocProc's numeric value itself was above). Guarded
        with a NULL check regardless, both here and at every other
        point this app touches dataHandle, in case that assumption
        turns out wrong on this particular runtime.

        userState (the size/position a zoom-in returns to) starts as
        the window's own just-created bounds; RecordWindowUserState
        (below) keeps it current across every later manual grow or
        drag. stdState (what a zoom-out grows to) is a single fixed
        "standard" size shared by every document -- ComputeStandardBounds
        below, deliberately NOT CreateNewDocument's own per-slot
        staggered bounds, since every document zooming out to a
        different, offset size would be a strange standard for
        "standard" to mean.
    */
    if (growable) {
        WStateDataHandle wsH = (WStateDataHandle) ((WindowPeek) doc->window)->dataHandle;

        if (wsH != NULL) {
            (**wsH).userState = *bounds;
            ComputeStandardBounds(&(**wsH).stdState);
        }
    }
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

    BuildWindowChrome(doc, &bounds, zoomDocProc, true, &viewRect);

    GetFNum(gPrefs.markdownFontName, &fontNum);
    TextFont(fontNum);
    TextSize(CurrentMarkdownFontSize());
    doc->te = TEStyleNew(&viewRect, &viewRect);

    GetFNum("\pTimes", &fontNum);
    TextFont(fontNum);
    TextSize(CurrentWriterFontSize());
    doc->hiddenTE = TEStyleNew(&viewRect, &viewRect);

    doc->hideMarkdown = !gPrefs.defaultMarkdownMode;
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

    doc->printRecord = NULL;
    doc->pageBreaksValid = false;

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
    AdjustScrollbar();

    if (gPrefs.defaultDistractionFree)
        SetDistractionFree(doc, true);

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

    /* DisposeWindow also disposes doc->scrollBar -- same as
       CloseDocument (this file): a Control Manager control is owned
       by its window and goes with it. A real bug lived here until
       this fix: an explicit DisposeControl(doc->scrollBar) used to
       run immediately before this DisposeWindow call, double-freeing
       the scrollbar's Control Record (DisposeWindow disposes it too,
       since it's still attached at that point) -- heap corruption,
       not merely a logic error, which is why the reported symptom
       (window title font changing, system-wide, persisting after
       quit) looked completely unrelated to Distraction Free. Also:
       DisposeWindow while doc->window is the current port leaves
       thePort dangling until BuildWindowChrome's own
       SetPort(doc->window) runs -- that happens as literally its
       first act, so this stays safe, but nothing may be inserted
       between these two calls that touches the port. */
    DisposeWindow(doc->window);

    BuildWindowChrome(doc, &newBounds,
                       toDistractionFree ? plainDBox : zoomDocProc,
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
    Resizes doc's window in place after the user drags its grow icon --
    only ever called for zoomDocProc (document-view) windows, since
    plainDBox (Distraction Free) windows have no grow icon and never
    generate an inGrow event for main.c's EventLoop to call this from.

    Deliberately does NOT go through BuildWindowChrome the way
    ReHouseDocument does: that helper disposes and recreates the whole
    window and scrollbar, appropriate for a chrome-TYPE change (a rare
    action where the momentary flash and change of WindowPtr identity
    don't matter), but wrong here -- GrowWindow (main.c) has already
    live-tracked the drag and returned a final size, so this only needs
    to apply that size to the window and its existing scrollbar/TE
    records in place, via SizeWindow/MoveControl/SizeControl rather
    than DisposeWindow+NewWindow+NewControl.

    Same scrollbar-geometry math as BuildWindowChrome's growable branch
    (flush right/top, inset SCROLLBAR_WIDTH off the bottom for the grow
    icon) and the same viewRect-margin math as its outViewRect -- kept
    in sync by hand rather than factored out, since BuildWindowChrome's
    version has to *create* a control (NewControl) while this one has
    to *move* an existing one (MoveControl/SizeControl), which are
    different enough calls that sharing one helper would need its own
    create-vs-move branch anyway; not worth it for two small rects.

    The part after doc->window's frame is already at its final size --
    everything from SetPort onward -- is shared with ZoomDocument
    below (ZoomWindow resizes the window itself exactly as SizeWindow
    does here, just via a different Toolbox call and a different
    source for the target size), factored out as SyncDocumentGeometry
    so the two don't duplicate this math a second time.
*/
static void SyncDocumentGeometry(DocumentPtr doc)
{
    Rect sbRect, viewRect;
    short savedSelStart, savedSelEnd;

    savedSelStart = (**doc->activeTE).selStart;
    savedSelEnd = (**doc->activeTE).selEnd;

    SetPort(doc->window);

    sbRect.right = doc->window->portRect.right + 1;
    sbRect.left = sbRect.right - SCROLLBAR_WIDTH;
    sbRect.top = doc->window->portRect.top - 1;
    sbRect.bottom = doc->window->portRect.bottom + 1 - SCROLLBAR_WIDTH;
    MoveControl(doc->scrollBar, sbRect.left, sbRect.top);
    SizeControl(doc->scrollBar, (short) (sbRect.right - sbRect.left),
                (short) (sbRect.bottom - sbRect.top));

    /* Same SCROLLBAR_WIDTH-for-the-grow-icon bottom inset as
       BuildWindowChrome, unconditionally here -- this function (like
       ResizeDocument/ZoomDocument, its only two callers) is only ever
       reached for growable/zoomDocProc chrome, so there's no plainDBox
       case to branch on the way BuildWindowChrome has to. Same real
       bug this fixes too: MARGIN_BOTTOM alone used to leave 8px of the
       grow icon's reserved corner inside the text viewRect. */
    viewRect = doc->window->portRect;
    viewRect.left += MARGIN_H;
    viewRect.right -= (SCROLLBAR_WIDTH + MARGIN_H);
    viewRect.top += MARGIN_TOP;
    viewRect.bottom -= SCROLLBAR_WIDTH;

    (**doc->te).viewRect = viewRect;
    (**doc->te).destRect = viewRect;
    (**doc->hiddenTE).viewRect = viewRect;
    (**doc->hiddenTE).destRect = viewRect;
    TECalText(doc->te);
    TECalText(doc->hiddenTE);
    TESetSelect(savedSelStart, savedSelEnd, doc->activeTE);

    InvalidateHeightCache();
    AdjustScrollbar();
    InvalRect(&doc->window->portRect);
}

void ResizeDocument(DocumentPtr doc, short newWidth, short newHeight)
{
    if (doc == NULL)
        return;

    SizeWindow(doc->window, newWidth, newHeight, true);
    SyncDocumentGeometry(doc);

    /* A manual grow always updates userState, whether or not the
       window happened to be at its zoomed/standard size beforehand --
       the standard classic Mac convention (Inside Macintosh's own
       zoom-box sample code does the same): growing effectively
       establishes a new custom size going forward, so the NEXT zoom-in
       click restores THIS size, not whatever the window's size was
       before this grow. */
    RecordWindowUserState(doc);
}

/*
    The zoom-box counterpart to DoGrow/ResizeDocument above -- called
    from main.c's DoZoomBox once TrackBox confirms the click landed
    (and was released) on the zoom box. part is whichever of
    inZoomIn/inZoomOut FindWindow/TrackBox reported; ZoomWindow uses it
    directly to pick userState or stdState as the target, so this
    doesn't need its own "which direction" bookkeeping the way some
    apps track a separate zoomed/unzoomed flag for -- the click itself
    already answers that.

    EraseRect on the OLD portRect before ZoomWindow, not after: if this
    zoom is shrinking the window, nothing else erases the pixels
    outside the new, smaller bounds -- the standard Inside Macintosh
    ordering for exactly this reason. front=true matches DoZoomBox's
    own w != FrontWindow() guard, which already ensures this is only
    ever called for the frontmost window.

    Deliberately does NOT call RecordWindowUserState -- unlike
    ResizeDocument's manual grow, a zoom action must leave userState
    alone so the round trip stays reversible: zoom out, then zoom back
    in, returns to exactly the size/position this zoom started from,
    not to whatever the zoom itself just produced.
*/
void ZoomDocument(DocumentPtr doc, short part)
{
    if (doc == NULL)
        return;

    SetPort(doc->window);
    EraseRect(&doc->window->portRect);
    ZoomWindow(doc->window, part, true);
    SyncDocumentGeometry(doc);
}

/*
    Records doc->window's current bounds as its WStateData userState --
    called after every manual grow (ResizeDocument above) and drag
    (main.c's inDrag handling) so a later zoom-in restores wherever the
    user actually left the window, not a stale size/position from
    whenever it was first created or last zoomed.

    portRect is in LOCAL (window-relative) coordinates -- WStateData's
    rects need to be in GLOBAL (screen) coordinates, the same space
    ZoomWindow repositions the window in, so this converts via
    LocalToGlobal on both corners first. Same technique
    ReHouseDocument's own standardBounds capture (above in this file)
    already uses for the identical reason.

    No-ops (rather than crashing) if dataHandle is NULL -- plainDBox
    (Distraction Free) windows have no WStateData at all, and this is
    called unconditionally after every drag regardless of chrome type,
    so that's the ordinary case for a DF window, not an error.
*/
void RecordWindowUserState(DocumentPtr doc)
{
    WStateDataHandle wsH;

    if (doc == NULL)
        return;

    wsH = (WStateDataHandle) ((WindowPeek) doc->window)->dataHandle;
    if (wsH != NULL) {
        Rect globalBounds = doc->window->portRect;

        LocalToGlobal((Point *) &globalBounds.top);
        LocalToGlobal((Point *) &globalBounds.bottom);
        (**wsH).userState = globalBounds;
    }
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
    /* Guarded: a document that never opened Page Setup or Print never
       allocated a print record (EnsurePrintRecord, print.c) in the
       first place. */
    if (doc->printRecord != NULL)
        DisposeHandle((Handle) doc->printRecord);
    /* DisposeWindow also disposes doc->scrollBar -- a Control Manager
       control is owned by its window and goes with it; no separate
       DisposeControl needed or correct here. */
    DisposeWindow(doc->window);

    doc->window = NULL;
    doc->te = NULL;
    doc->hiddenTE = NULL;
    doc->activeTE = NULL;
    doc->scrollBar = NULL;
    doc->printRecord = NULL;
    doc->inUse = false;
}
