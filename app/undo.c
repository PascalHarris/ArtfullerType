#include "app.h"

static void FreeSnapshot(UndoSnapshot *snap)
{
    if (snap->textH != NULL)
        DisposeHandle(snap->textH);
    snap->textH = NULL;
}

void ClearUndoRedoStacks(void)
{
    DocumentPtr doc = FrontDocument();
    short i;

    for (i = 0; i < doc->undoCount; i++)
        FreeSnapshot(&doc->undoStack[i]);
    doc->undoCount = 0;
    for (i = 0; i < doc->redoCount; i++)
        FreeSnapshot(&doc->redoStack[i]);
    doc->redoCount = 0;
    doc->typingRunActive = false;
}

void UpdateEditMenuState(void)
{
    DocumentPtr doc = FrontDocument();

    EnableItem(gEditMenu, iUndo);
    EnableItem(gEditMenu, iRedo);
    if (doc->undoCount == 0)
        DisableItem(gEditMenu, iUndo);
    if (doc->redoCount == 0)
        DisableItem(gEditMenu, iRedo);
}

/*
    Captures the current document (always as canonical markdown text,
    syncing first if Writer mode is active) onto the undo stack, and
    clears the redo stack -- any new edit invalidates whatever could
    have been redone. Bounded: pushing past MAX_UNDO_LEVELS evicts the
    oldest entry rather than growing unboundedly.
*/
void PushUndoSnapshot(void)
{
    DocumentPtr doc = FrontDocument();
    UndoSnapshot *slot;
    Handle textH;
    long len;
    short i;

    if (doc->hideMarkdown)
        SyncHiddenToCanonical();

    len = (**doc->te).teLength;
    textH = NewHandle(len);
    HLock(textH);
    HLock((**doc->te).hText);
    BlockMove(*(**doc->te).hText, *textH, len);
    HUnlock((**doc->te).hText);
    HUnlock(textH);

    if (doc->undoCount == MAX_UNDO_LEVELS) {
        FreeSnapshot(&doc->undoStack[0]);
        for (i = 0; i < MAX_UNDO_LEVELS - 1; i++)
            doc->undoStack[i] = doc->undoStack[i + 1];
        doc->undoCount--;
    }

    slot = &doc->undoStack[doc->undoCount++];
    slot->textH = textH;
    slot->length = len;
    slot->selStart = (**doc->activeTE).selStart;
    slot->selEnd = (**doc->activeTE).selEnd;

    for (i = 0; i < doc->redoCount; i++)
        FreeSnapshot(&doc->redoStack[i]);
    doc->redoCount = 0;

    UpdateEditMenuState();
}

/* Same idea as PushUndoSnapshot, but onto the redo stack -- called
   right before undoing, so redoing can bring the undone state back. */
static void PushRedoSnapshot(DocumentPtr doc)
{
    UndoSnapshot *slot;
    Handle textH;
    long len;
    short i;

    if (doc->hideMarkdown)
        SyncHiddenToCanonical();

    len = (**doc->te).teLength;
    textH = NewHandle(len);
    HLock(textH);
    HLock((**doc->te).hText);
    BlockMove(*(**doc->te).hText, *textH, len);
    HUnlock((**doc->te).hText);
    HUnlock(textH);

    if (doc->redoCount == MAX_UNDO_LEVELS) {
        FreeSnapshot(&doc->redoStack[0]);
        for (i = 0; i < MAX_UNDO_LEVELS - 1; i++)
            doc->redoStack[i] = doc->redoStack[i + 1];
        doc->redoCount--;
    }

    slot = &doc->redoStack[doc->redoCount++];
    slot->textH = textH;
    slot->length = len;
    slot->selStart = (**doc->activeTE).selStart;
    slot->selEnd = (**doc->activeTE).selEnd;
}

/* Replaces the canonical TE's text with a snapshot and, if Writer mode
   is active, rebuilds the hidden TE from it so styling comes back
   correctly. Doesn't free the snapshot -- the caller (DoUndo/DoRedo)
   owns that. */
static void RestoreSnapshot(DocumentPtr doc, UndoSnapshot *snap)
{
    Rect savedViewRect;

    SuppressDrawing(doc->te, &savedViewRect);
    TESetSelect(0, 32767, doc->te);
    TEDelete(doc->te);
    HLock(snap->textH);
    TEInsert(*snap->textH, snap->length, doc->te);
    HUnlock(snap->textH);
    RestoreDrawing(doc->te, &savedViewRect);

    if (doc->hideMarkdown) {
        BuildHiddenView();
        TESetSelect(snap->selStart, snap->selEnd, doc->hiddenTE);
    } else {
        ClearStyles();
        TESetSelect(snap->selStart, snap->selEnd, doc->te);
    }

    doc->dirty = true;
    doc->typingRunActive = false;
    AdjustScrollbar();
    InvalRect(&doc->window->portRect);
}

void DoUndo(void)
{
    DocumentPtr doc = FrontDocument();
    UndoSnapshot snap;

    if (doc->undoCount == 0)
        return;

    PushRedoSnapshot(doc);

    doc->undoCount--;
    snap = doc->undoStack[doc->undoCount];
    RestoreSnapshot(doc, &snap);
    FreeSnapshot(&snap);

    UpdateEditMenuState();
}

void DoRedo(void)
{
    DocumentPtr doc = FrontDocument();
    UndoSnapshot snap;

    if (doc->redoCount == 0)
        return;

    /* Take the redo entry before pushing onto undo -- PushUndoSnapshot
       unconditionally clears the redo stack (correct for a genuine new
       edit, but redoing isn't one; grab what's needed first). */
    doc->redoCount--;
    snap = doc->redoStack[doc->redoCount];

    PushUndoSnapshot();

    RestoreSnapshot(doc, &snap);
    FreeSnapshot(&snap);

    UpdateEditMenuState();
}

void DoCut(void)
{
    DocumentPtr doc = FrontDocument();
    short selStart, selEnd;
    long selLen;
    Handle scrapText;

    selStart = (**doc->activeTE).selStart;
    selEnd = (**doc->activeTE).selEnd;
    if (selStart == selEnd)
        return;

    if (doc->hideMarkdown) {
        scrapText = EncodeSelectionAsMarkdown(selStart, selEnd, doc->activeTE);
    } else {
        Handle textH = (**doc->activeTE).hText;

        selLen = selEnd - selStart;
        scrapText = NewHandle(selLen);
        HLock(textH);
        HLock(scrapText);
        BlockMove(*textH + selStart, *scrapText, selLen);
        HUnlock(textH);
        HUnlock(scrapText);
    }

    PushUndoSnapshot();

    ZeroScrap();
    HLock(scrapText);
    PutScrap(GetHandleSize(scrapText), 'TEXT', *scrapText);
    HUnlock(scrapText);
    DisposeHandle(scrapText);

    TEDelete(doc->activeTE);

    doc->dirty = true;
    doc->typingRunActive = false;
    AdjustScrollbar();
}

void DoCopy(void)
{
    DocumentPtr doc = FrontDocument();
    short selStart, selEnd;
    long selLen;
    Handle scrapText;

    selStart = (**doc->activeTE).selStart;
    selEnd = (**doc->activeTE).selEnd;
    if (selStart == selEnd)
        return;

    if (doc->hideMarkdown) {
        scrapText = EncodeSelectionAsMarkdown(selStart, selEnd, doc->activeTE);
    } else {
        Handle textH = (**doc->activeTE).hText;

        selLen = selEnd - selStart;
        scrapText = NewHandle(selLen);
        HLock(textH);
        HLock(scrapText);
        BlockMove(*textH + selStart, *scrapText, selLen);
        HUnlock(textH);
        HUnlock(scrapText);
    }

    ZeroScrap();
    HLock(scrapText);
    PutScrap(GetHandleSize(scrapText), 'TEXT', *scrapText);
    HUnlock(scrapText);
    DisposeHandle(scrapText);
}

void DoPaste(void)
{
    DocumentPtr doc = FrontDocument();
    Handle scrapH;
    long offset;
    long len;

    scrapH = NewHandle(0);
    len = GetScrap(scrapH, 'TEXT', &offset);
    if (len <= 0) {
        DisposeHandle(scrapH);
        return;
    }

    PushUndoSnapshot();

    if (doc->hideMarkdown) {
        InsertMarkdownAsStyled(scrapH, len, doc->activeTE);
        DisposeHandle(scrapH);
    } else {
        HLock(scrapH);
        TEInsert(*scrapH, len, doc->activeTE);
        HUnlock(scrapH);
        DisposeHandle(scrapH);
    }

    doc->dirty = true;
    doc->typingRunActive = false;
    AdjustScrollbar();
}

void DoSelectAll(void)
{
    DocumentPtr doc = FrontDocument();

    TESetSelect(0, 32767, doc->activeTE);
    doc->typingRunActive = false;
}
