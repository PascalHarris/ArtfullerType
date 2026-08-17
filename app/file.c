#include "app.h"

/*
    Whether name's last 3 characters are ".md", case-insensitively --
    HFS filenames are case-preserving but not case-sensitive, so
    "NOTES.MD" and "notes.Md" both count as already having the
    extension. Pascal string: name[0] is the length byte, name[1] is
    the first character, name[len] is the last.
*/
static Boolean HasMarkdownExtension(StringPtr name)
{
    short len = name[0];
    short i;
    unsigned char suffix[3];

    if (len < 3)
        return false;

    for (i = 0; i < 3; i++) {
        unsigned char c = name[len - 2 + i];
        if (c >= 'a' && c <= 'z')
            c = (unsigned char) (c - 32);
        suffix[i] = c;
    }
    return (suffix[0] == '.' && suffix[1] == 'M' && suffix[2] == 'D');
}

/*
    Force-appends ".md" if it's not already there -- markdown is the
    only thing this app writes, so a saved file without the extension
    would be a real (if minor) usability trap: Finder wouldn't offer to
    open it with the right kind of app, and this app's own Open dialog
    filters on 'TEXT' anyway so it'd still show up there, but every
    other markdown-aware tool on a real or emulated Mac would miss it.
    SFPutFile's suggested name is already "Untitled.md" (file.c's
    DoSaveAs), but the person saving is always free to type over it and
    drop the extension, so this enforces it regardless of what they
    typed.

    HFS filenames are limited to 31 characters -- truncated to leave
    room for the extension rather than silently overflowing Str255 or
    producing an invalid (too-long) name.
*/
static void EnsureMarkdownExtension(Str255 name)
{
    short len = name[0];

    if (HasMarkdownExtension(name))
        return;

    if (len > 28)
        len = 28;

    name[len + 1] = '.';
    name[len + 2] = 'm';
    name[len + 3] = 'd';
    name[0] = (unsigned char) (len + 3);
}

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
    UpdateWindowTitle(doc);
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
    EnsureMarkdownExtension(doc->fileName);
    doc->vRefNum = reply.vRefNum;
    doc->haveFile = true;
    WriteFile(doc->fileName, doc->vRefNum);
    doc->dirty = false;
    UpdateWindowTitle(doc);
    /* Not one of MULTI_WINDOW_DESIGN.md §5.3's three named call sites
       (Save As neither creates nor closes a document), but the Window
       menu's item for this document displays the same name the title
       bar does -- leaving it unrebuilt here would mean it keeps
       showing "Untitled" (or the old name, if this was a rename)
       until some unrelated document create/close happened to refresh
       it. Same gap, same fix, just triggered by a rename instead of a
       create/close. */
    RebuildWindowMenu();
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

/*
    Creates a new document and opens the picked file into it, rather
    than replacing whatever's in the current document -- per
    MULTI_WINDOW_DESIGN.md §7.1. Because nothing existing is touched
    until a document has actually been created for the new file, there
    is genuinely nothing to lose by cancelling the picker or by
    MAX_DOCUMENTS being reached -- neither path needs (or gets) a
    ConfirmDiscardChanges guard anymore; Close and Quit are the only
    two places that still need one.

    Flagging one consequence of this change that's outside this
    function's own scope to fix: ShowSplashScreen (splash.c) calls this
    directly for its "Open Document" button, and does so against the
    blank document main() already created before the splash even
    appears. After this change, choosing Open from the splash creates a
    SECOND document for the opened file, leaving that original blank
    one open too -- so a first launch that opens a file via the splash
    now ends up with two windows (one empty) instead of one. Whether
    that's worth a special case in splash.c (e.g. closing the startup
    document once a splash-driven Open succeeds) is a product decision
    I haven't made unilaterally; flagging it rather than quietly
    patching a file this milestone wasn't asked to touch.
*/
Boolean DoOpenFile(void)
{
    DocumentPtr doc;
    SFReply reply;
    Point where = {100, 100};
    SFTypeList types;

    types[0] = 'TEXT';

    SFGetFile(where, "\p", NULL, 1, types, NULL, &reply);
    UpdateMenuBarLook();
    if (!reply.good)
        return false;

    doc = CreateNewDocument();
    if (doc == NULL)
        return false; /* MAX_DOCUMENTS reached -- File > Open should
                          already be disabled via UpdateFileMenuState by
                          then; this is a safety net, not a normal path. */

    BlockMove(reply.fName, doc->fileName, reply.fName[0] + 1);
    doc->vRefNum = reply.vRefNum;
    doc->haveFile = true;
    ReadFile(doc->fileName, doc->vRefNum);
    RebuildWindowMenu();
    UpdateFileMenuState();
    SyncMenusToFrontDocument();
    return true;
}

void DoNewFile(void)
{
    DocumentPtr doc = CreateNewDocument();

    if (doc == NULL)
        return; /* MAX_DOCUMENTS reached -- same safety net as DoOpenFile */

    RebuildWindowMenu();
    UpdateFileMenuState();
    SyncMenusToFrontDocument();
}
