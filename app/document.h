#ifndef ARTFULTYPE_DOCUMENT_H
#define ARTFULTYPE_DOCUMENT_H

#include <Windows.h>
#include <TextEdit.h>
/* No <Controls.h> -- it isn't a real standalone header in this toolchain
   (confirmed: the original codebase used ControlHandle throughout
   main.c/scrolling.c and never included it). ControlHandle for the
   scrollBar field below comes in transitively via <Windows.h> above,
   which app.h always includes before document.h -- same as the
   original code relied on. */

/*
    MULTI_WINDOW_DESIGN.md's Milestone 0 decision checklist hadn't
    actually been filled in when this milestone was implemented -- the
    three values below are the design doc's own recommended defaults
    (see its §9 memory-budget table), not a confirmed choice. Revisit
    at Milestone 7 (memory-budget validation on real/emulated hardware)
    if these turn out wrong in practice; nothing downstream depends on
    the exact numbers, just on these three names existing.
*/
#define MAX_DOCUMENTS    4
#define MAX_UNDO_LEVELS  8
#define MAX_LINKS        32

/*
    Undo/redo snapshots store the *canonical markdown text* regardless
    of which mode is active, not the active TE's raw buffer -- the
    hidden (Writer-mode) TE's styling (bold/heading/link runs) has no
    simple "get it all, restore it all" API in classic styled
    TextEdit, but canonical markdown text already round-trips styling
    correctly through the existing BuildHiddenView/SyncHiddenToCanonical
    machinery. So: push a snapshot by syncing to canonical first (if in
    Writer mode) and copying the canonical TE's text; restore one by
    replacing the canonical TE's text and, if in Writer mode, rebuilding
    the hidden TE from it. Both syncing and rebuilding are full-document
    operations, but they only happen at undo/redo-relevant moments
    (pushes are coalesced per typing run, not per keystroke), never per
    character.

    Undo history is intentionally cleared on every view-mode switch and
    on new/open -- simpler and more predictable than trying to make
    snapshots meaningful across two independently-edited buffers.
*/
typedef struct {
    Handle textH;
    long length;
    short selStart, selEnd;
} UndoSnapshot;

/*
    Link URLs in Writer mode live here, keyed by a small ID (1-based;
    0 means "no link"). The ID rides along in each run's otherwise-unused
    tsColor.red -- TextEdit already tracks style-run boundaries through
    every insert/delete, so the ID (and therefore the URL) follows the
    linked text automatically with no manual range bookkeeping. Reset
    (linkCount = 0) at the start of every BuildHiddenView, since that's
    a full reparse of the canonical TE and re-derives whichever links
    currently exist.

    All of the fields below were, before this milestone, bare globals
    in app.h/main.c (see MULTI_WINDOW_DESIGN.md §3 for the full list and
    rationale). Fields the design doc's §3 draft included that aren't
    here yet -- distractionFree, standardBounds -- are Milestone 5's
    concern (window chrome); this milestone explicitly doesn't touch
    that, so they're left out rather than added unused.
*/
typedef struct DocumentRecord {
    Boolean inUse;

    WindowPtr window;
    TEHandle te;                /* canonical markdown */
    TEHandle hiddenTE;          /* styled Writer view */
    TEHandle activeTE;          /* == te or hiddenTE */
    ControlHandle scrollBar;
    Boolean scrollBarVisible;

    Boolean haveFile;
    Boolean dirty;
    Str255 fileName;
    short vRefNum;

    Boolean hideMarkdown;       /* Writer (true) vs. Markdown (false) view */

    UndoSnapshot undoStack[MAX_UNDO_LEVELS];
    short undoCount;
    UndoSnapshot redoStack[MAX_UNDO_LEVELS];
    short redoCount;
    Boolean typingRunActive;

    Str255 linkURLs[MAX_LINKS + 1];
    short linkCount;

    /* Moved from scrolling.c's file-static height caches -- per-TE-
       content caches, so leaving them as file-statics would make one
       document's cache silently apply to another's after a window
       switch. See scrolling.c's InvalidateHeightCache/
       UpdateScrollbarRange/ScrollCaretIntoView for how these are used;
       -1 is the "cache invalid" sentinel for both Line fields, same as
       the original file-statics. */
    short cachedTotalHeightNLines;
    long cachedTotalHeight;
    short cachedCaretLine;
    long cachedHeightToLine;
    long cachedHeightToLineNext;
} DocumentRecord, *DocumentPtr;

/*
    A fixed-size array of slots, not a malloc'd linked list -- see
    MULTI_WINDOW_DESIGN.md §3 for why (heap fragmentation risk on
    68k-class hardware with real memory constraints; a bounded worst
    case beats unbounded flexibility here).

    As of Milestone 3, more than one slot can be inUse at a time --
    CreateNewDocument fills the first free one it finds via
    FindFreeDocumentSlot, CloseDocument frees one back up. Milestone
    1's FrontDocument/DocumentForWindow already had this shape (a scan
    over inUse slots) from the start, so neither needed to change when
    this landed.
*/
extern DocumentRecord gDocuments[MAX_DOCUMENTS];

/* document.c */
DocumentPtr FrontDocument(void);
DocumentPtr DocumentForWindow(WindowPtr w);
DocumentPtr FindFreeDocumentSlot(void);
DocumentPtr CreateNewDocument(void);
void CloseDocument(DocumentPtr doc);
void UpdateWindowTitle(DocumentPtr doc);

#endif
