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
    Rect sbRect;
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

    doc->window = NewWindow(NULL, &bounds, "\p", true, documentProc,
                             (WindowPtr) -1L, true, 0);
    SetPort(doc->window);

    GetFNum("\pTimes", &fontNum);
    TextFont(fontNum);
    TextSize(CurrentFontSize());

    /*
        Scrollbar first, flush against the window's right edge and
        spanning the full window height -- the standard Mac layout
        (e.g. TeachText), and what "scroll bars should be tight
        against the window edge" specifically asks for. The 1px
        overlap on all three outer edges (right/top/bottom) is the
        usual classic-Mac convention so the control's own frame blends
        with the window's, rather than leaving a visible 1px gap.

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

    viewRect = doc->window->portRect;
    viewRect.left += MARGIN_H;
    viewRect.right -= (SCROLLBAR_WIDTH + MARGIN_H);
    viewRect.top += MARGIN_TOP;
    viewRect.bottom -= MARGIN_BOTTOM;

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
