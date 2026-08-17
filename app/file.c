#include "app.h"

static void RefreshActiveView(DocumentPtr doc)
{
    if (doc->hideMarkdown)
        BuildHiddenView();
    else
        ClearStyles();
}

void SetViewMode(Boolean hideMarkdown)
{
    DocumentPtr doc = FrontDocument();

    if (hideMarkdown == doc->hideMarkdown)
        return;

    ClearUndoRedoStacks();
    UpdateEditMenuState();
    TEDeactivate(doc->activeTE);

    if (hideMarkdown) {
        BuildHiddenView();
        doc->activeTE = doc->hiddenTE;
    } else {
        SyncHiddenToCanonical();
        doc->activeTE = doc->te;
    }

    TEActivate(doc->activeTE);
    doc->hideMarkdown = hideMarkdown;
    CheckItem(gViewMenu, iMarkdownView, !hideMarkdown);
    CheckItem(gViewMenu, iWriterView, hideMarkdown);
    UpdateMenuBarLook();
    AdjustScrollbar();
    InvalRect(&doc->window->portRect);
}

static void WriteFile(StringPtr name, short vRefNum)
{
    DocumentPtr doc = FrontDocument();
    short refNum;
    long count;
    Handle textH = (**doc->te).hText;
    OSErr err;

    Create(name, vRefNum, 'ArtT', 'TEXT');

    err = FSOpen(name, vRefNum, &refNum);
    if (err != noErr)
        return;

    SetEOF(refNum, 0);
    count = (**doc->te).teLength;
    HLock(textH);
    FSWrite(refNum, &count, *textH);
    HUnlock(textH);
    FSClose(refNum);
}

static void ReadFile(StringPtr name, short vRefNum)
{
    DocumentPtr doc = FrontDocument();
    short refNum;
    long count;
    long eof;
    Handle textH;
    OSErr err;

    err = FSOpen(name, vRefNum, &refNum);
    if (err != noErr)
        return;

    GetEOF(refNum, &eof);
    textH = NewHandle(eof);
    HLock(textH);
    count = eof;
    FSRead(refNum, &count, *textH);
    FSClose(refNum);

    TESetSelect(0, 32767, doc->te);
    TEDelete(doc->te);
    TEInsert(*textH, count, doc->te);
    HUnlock(textH);
    DisposeHandle(textH);

    doc->dirty = false;
    ClearUndoRedoStacks();
    UpdateEditMenuState();
    RefreshActiveView(doc);
    AdjustScrollbar();
    InvalRect(&doc->window->portRect);
}

void DoStartupOpen(void)
{
    DocumentPtr doc = FrontDocument();
    short message, count;
    AppFile theFile;

    CountAppFiles(&message, &count);
    if (count < 1 || message != appOpen)
        return;

    GetAppFiles(1, &theFile);
    BlockMove(theFile.fName, doc->fileName, theFile.fName[0] + 1);
    doc->vRefNum = theFile.vRefNum;
    doc->haveFile = true;
    ReadFile(doc->fileName, doc->vRefNum);
    ClrAppFiles(1);
}

Boolean DoSaveAs(void)
{
    DocumentPtr doc = FrontDocument();
    SFReply reply;
    Point where = {100, 100};

    if (doc->hideMarkdown)
        SyncHiddenToCanonical();

    SFPutFile(where, "\pSave document as:", "\pUntitled.md", NULL, &reply);
    UpdateMenuBarLook();
    if (!reply.good)
        return false;

    BlockMove(reply.fName, doc->fileName, reply.fName[0] + 1);
    doc->vRefNum = reply.vRefNum;
    doc->haveFile = true;
    WriteFile(doc->fileName, doc->vRefNum);
    doc->dirty = false;
    return true;
}

Boolean DoSave(void)
{
    DocumentPtr doc = FrontDocument();

    if (!doc->haveFile)
        return DoSaveAs();

    if (doc->hideMarkdown)
        SyncHiddenToCanonical();

    WriteFile(doc->fileName, doc->vRefNum);
    doc->dirty = false;
    return true;
}

static short AskSaveChanges(void)
{
    DocumentPtr doc = FrontDocument();
    short hit;

    if (doc->haveFile)
        ParamText(doc->fileName, "\p", "\p", "\p");
    else
        ParamText("\pUntitled", "\p", "\p", "\p");

    hit = Alert(kSaveChangesAlert, NULL);
    UpdateMenuBarLook();
    return hit;
}

Boolean ConfirmDiscardChanges(void)
{
    DocumentPtr doc = FrontDocument();

    if (!doc->dirty)
        return true;

    switch (AskSaveChanges()) {
        case kSaveBtn:     return DoSave();
        case kDontSaveBtn: return true;
        default:            return false;
    }
}

Boolean DoOpenFile(void)
{
    DocumentPtr doc = FrontDocument();
    SFReply reply;
    Point where = {100, 100};
    SFTypeList types;

    types[0] = 'TEXT';

    SFGetFile(where, "\p", NULL, 1, types, NULL, &reply);
    UpdateMenuBarLook();
    if (!reply.good)
        return false;

    BlockMove(reply.fName, doc->fileName, reply.fName[0] + 1);
    doc->vRefNum = reply.vRefNum;
    doc->haveFile = true;
    ReadFile(doc->fileName, doc->vRefNum);
    return true;
}

void DoNewFile(void)
{
    DocumentPtr doc = FrontDocument();

    TESetSelect(0, 32767, doc->te);
    TEDelete(doc->te);
    doc->haveFile = false;
    doc->dirty = false;
    ClearUndoRedoStacks();
    UpdateEditMenuState();
    RefreshActiveView(doc);
    AdjustScrollbar();
    InvalRect(&doc->window->portRect);
}
