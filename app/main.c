/*
    Milestone 2: a real distraction-free Markdown editor.
    Full-screen window, wide margins, 14pt Times, File menu with
    Save/Open backed by the classic File Manager. Saving straight to
    the BlueSCSI SD card (bypassing this disk's HFS volume) is a
    later milestone -- this still saves onto the boot disk itself.

    (The "Milestone 2" above is this project's own pre-existing
    numbering, from before any of what follows -- unrelated to, and
    not to be confused with, MULTI_WINDOW_DESIGN.md's separate
    Milestone 1/2/3/... numbering below and elsewhere in this codebase.)

    MULTI_WINDOW_DESIGN.md Milestone 1: gWindow/gTE/gHiddenTE/
    gActiveTE/gScrollBar/gScrollBarVisible/gHaveFile/gDirty/gFileName/
    gVRefNum/gHideMarkdown, the undo/redo stacks, gTypingRunActive, and
    gLinkURLs/gLinkCount used to be defined here as bare globals. They
    now live in gDocuments[0] (document.c), reached via FrontDocument().
    No window management changed -- there was still exactly one window
    at this point, created once in a MakeWindow function that lived
    here in main.c -- this milestone was purely the internal restructure
    described in the design doc's §3. (MakeWindow itself is long gone
    as of Milestone 3 below -- generalized into document.c's
    CreateNewDocument, which can run more than once. Left this note as
    it was written rather than editing it to erase that MakeWindow ever
    existed.)

    MULTI_WINDOW_DESIGN.md Milestone 2: the three event-dispatch fixes
    from the design doc's §6 (activateEvt, DoUpdate, mouseDown/inContent
    all now resolve their target document from the actual WindowPtr
    involved, not FrontDocument()) plus the click-to-front behavior in
    mouseDown/inContent. Still no visible behavior change -- still one
    window -- these are latent-bug fixes and forward prep for Milestone
    3, not anything a user could currently trigger a difference from.

    MULTI_WINDOW_DESIGN.md Milestone 3: this is where a second window
    actually becomes possible. Standard-sized documentProc windows
    replace the old full-screen plainDBox default (MakeWindow is gone;
    CreateNewDocument in document.c replaces it and can run more than
    once); File > Close and the window's own close box both work;
    New/Open create an additional document instead of overwriting the
    current one in place, so the ConfirmDiscardChanges guard moved off
    of them onto Close and Quit instead; Quit confirms every open
    document, not just one. See §4.1/§5.1/§6/§7.1/§7.2 of the design
    doc for the reasoning behind each piece.
*/

#include "app.h"

Boolean gDone = false;
MenuHandle gFileMenu;
MenuHandle gViewMenu;
MenuHandle gEditMenu;
short gZoomIndex = kZoomBaselineIndex;

static void Init(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

/*
    Writer mode gets a black menu bar with white text; Markdown mode gets
    the standard look. There's no Menu Manager API for this on classic Mac
    OS (that's a much later Appearance Manager concept) -- on a 1-bit
    display, drawing the normal bar and then XOR-inverting that strip
    achieves the same thing trivially. Must target the Window Manager
    port (global screen coordinates), not whatever window's port happens
    to be current, since the menu bar isn't part of any window.

    Guards against FrontDocument() returning NULL: MakeMenu() below calls
    this once, before main()'s first CreateNewDocument() call has run
    and before any gDocuments slot is populated -- the only point in
    the whole program where there isn't a front document yet.
    Everywhere else this is called from, a window already exists.
*/
void UpdateMenuBarLook(void)
{
    GrafPtr savePort;
    GrafPtr wMgrPort;
    Rect bar;
    DocumentPtr doc = FrontDocument();

    DrawMenuBar();

    if (doc != NULL && doc->hideMarkdown) {
        GetPort(&savePort);
        GetWMgrPort(&wMgrPort);
        SetPort(wMgrPort);

        SetRect(&bar, 0, 0, qd.screenBits.bounds.right, MENU_BAR_HEIGHT);
        InvertRect(&bar);

        SetPort(savePort);
    }
}

static void MakeMenu(void)
{
    MenuHandle styleMenu;
    MenuHandle helpMenu;

    gFileMenu = NewMenu(mFile, "\pFile");
    AppendMenu(gFileMenu, "\pNew/N;Open.../O;Close/W;Save/S;Save As...;(-;Quit/Q");
    InsertMenu(gFileMenu, 0);

    /* No "/" shortcut on Redo -- it would register as a second cmd-key
       equivalent for the same letter as Undo, ambiguous to MenuKey.
       Cmd-Shift-Z for Redo is instead handled directly in EventLoop,
       intercepted before MenuKey ever sees it. */
    gEditMenu = NewMenu(mEdit, "\pEdit");
    AppendMenu(gEditMenu, "\pUndo/Z;Redo;(-;Cut/X;Copy/C;Paste/V;(-;Select All/A");
    InsertMenu(gEditMenu, 0);
    DisableItem(gEditMenu, iUndo);
    DisableItem(gEditMenu, iRedo);

    styleMenu = NewMenu(mStyle, "\pStyle");
    AppendMenu(styleMenu, "\pBold/B;Italic/I;Code/K;Strikethrough;(-;Heading 1/1;Heading 2/2;Heading 3/3;(-;Link/L;(-;None");
    InsertMenu(styleMenu, 0);

    gViewMenu = NewMenu(mView, "\pView");
    AppendMenu(gViewMenu, "\pMarkdown;Writer;(-;Zoom In/=;Zoom Out/-;Default Size/0");
    InsertMenu(gViewMenu, 0);
    CheckItem(gViewMenu, iWriterView, true);

    helpMenu = NewMenu(mHelp, "\pHelp");
    AppendMenu(helpMenu, "\pAbout The Artful Type...");
    InsertMenu(helpMenu, 0);

    UpdateMenuBarLook();
}

/*
    Enables/disables File > New and Open based on whether a document
    slot is actually free -- called after every document count change
    (create or close) rather than left to the AppendMenu-time default,
    so it can never drift out of sync with reality. Per
    MULTI_WINDOW_DESIGN.md §7.1: "disable New and Open... rather than
    failing silently" once MAX_DOCUMENTS is reached.
*/
void UpdateFileMenuState(void)
{
    Boolean haveFreeSlot = (FindFreeDocumentSlot() != NULL);

    if (haveFreeSlot) {
        EnableItem(gFileMenu, iNew);
        EnableItem(gFileMenu, iOpen);
    } else {
        DisableItem(gFileMenu, iNew);
        DisableItem(gFileMenu, iOpen);
    }
}

/*
    Resolves the document that owns w, not the front document -- fixed
    per MULTI_WINDOW_DESIGN.md §6/Milestone 2, written when only one
    window could exist so DocumentForWindow(w) and FrontDocument()
    always agreed anyway. Genuinely matters as of Milestone 3: a
    background window can now receive an update event while a
    different one is front, and this makes sure that redraws with its
    own document's TE, not whichever happens to be frontmost.

    BeginUpdate/EndUpdate still run unconditionally -- that's Window
    Manager bookkeeping for w's update region and has nothing to do
    with which document (if any) owns it; only the actual redraw is
    skipped if doc is somehow NULL (shouldn't happen in practice, since
    every window this app creates is registered in gDocuments, but
    cheaper to guard than to assume).
*/
static void DoUpdate(WindowPtr w)
{
    DocumentPtr doc = DocumentForWindow(w);

    BeginUpdate(w);
    EraseRect(&w->portRect);
    if (doc != NULL) {
        TEUpdate(&w->portRect, doc->activeTE);
        DrawControls(w);
    }
    EndUpdate(w);
}

/*
    Closes one specific document, with the save-changes prompt, per
    MULTI_WINDOW_DESIGN.md §5.1. Shared by the File > Close menu item
    and a click on a window's own close box (inGoAway in EventLoop) --
    same confirm-then-dispose sequence either way.

    SelectWindow(doc->window) runs first regardless of whether doc is
    already frontmost: ConfirmDiscardChanges (file.c) always asks about
    the front document, so this is what makes it resolve to doc; it
    also means the save-changes alert visually corresponds to the
    window it's asking about, which stops mattering once only one
    window can ever be open but matters from this milestone on.
*/
static void CloseDocumentInteractive(DocumentPtr doc)
{
    if (doc == NULL)
        return;

    SelectWindow(doc->window);
    if (!ConfirmDiscardChanges())
        return;

    CloseDocument(doc);
    UpdateFileMenuState();

    if (FrontDocument() == NULL) {
        /* Last window just closed. MULTI_WINDOW_DESIGN.md's Milestone 0
           decision checklist recommended falling back to the splash
           screen for this case -- never actually confirmed by Pascal,
           same unconfirmed-default flag as document.h's MAX_DOCUMENTS
           and app.h's kDefaultWindowMargin. Mirrors main()'s own
           startup sequence exactly: a fresh blank document, its first
           paint forced the same way main() has to and for the same
           reason (see the comment on that DoUpdate call in main()),
           then the splash offering New/Open into it. */
        DocumentPtr freshDoc = CreateNewDocument();

        if (freshDoc != NULL) {
            DoUpdate(freshDoc->window);
            ShowSplashScreen();
            UpdateFileMenuState();
        }
    }
}

/*
    Quit (MULTI_WINDOW_DESIGN.md §7.2): confirms every open document,
    stopping (and leaving the app running) if any one is cancelled.
    Documents aren't individually CloseDocument()'d here -- the app is
    about to exit outright if this returns true, and classic Mac OS
    reclaims the whole process heap on quit, so there's nothing an
    explicit per-document dispose would buy over just confirming each
    one and letting main() return.
*/
static Boolean ConfirmDiscardChangesForAllDocuments(void)
{
    short i;

    for (i = 0; i < MAX_DOCUMENTS; i++) {
        if (!gDocuments[i].inUse)
            continue;
        /* Same SelectWindow-first reasoning as CloseDocumentInteractive
           above -- ConfirmDiscardChanges always asks about whichever
           document is front. */
        SelectWindow(gDocuments[i].window);
        if (!ConfirmDiscardChanges())
            return false;
    }
    return true;
}

static void DoMenuCommand(long menuResult)
{
    short menuID = HiWord(menuResult);
    short menuItem = LoWord(menuResult);
    DocumentPtr doc = FrontDocument();

    if (menuID == mFile) {
        switch (menuItem) {
            case iNew:   DoNewFile(); break;
            case iOpen:  DoOpenFile(); break;
            case iClose: CloseDocumentInteractive(FrontDocument()); break;
            case iSave:   DoSave(); break;
            case iSaveAs: DoSaveAs(); break;
            case iQuit:
                if (ConfirmDiscardChangesForAllDocuments())
                    gDone = true;
                break;
        }
    } else if (menuID == mEdit) {
        switch (menuItem) {
            case iUndo:      DoUndo(); break;
            case iRedo:      DoRedo(); break;
            case iCut:       DoCut(); break;
            case iCopy:      DoCopy(); break;
            case iPaste:     DoPaste(); break;
            case iSelectAll: DoSelectAll(); break;
        }
    } else if (menuID == mStyle) {
        doc->dirty = true;
        PushUndoSnapshot();
        doc->typingRunActive = false;
        if (doc->hideMarkdown) {
            switch (menuItem) {
                case iBold:   ToggleFace(bold); break;
                case iItalic: ToggleFace(italic); break;
                case iCode:   ToggleCode(); break;
                case iStrike: break; /* no native strikethrough on classic Mac text styles */
                case iH1:     ToggleHeadingHidden(1); break;
                case iH2:     ToggleHeadingHidden(2); break;
                case iH3:     ToggleHeadingHidden(3); break;
                case iLink:   DoLinkHidden(); break;
                case iNone:   ClearSelectionStyleHidden(); break;
            }
        } else {
            switch (menuItem) {
                case iBold:   WrapSelection("**", "**"); break;
                case iItalic: WrapSelection("*", "*"); break;
                case iCode:   WrapSelection("`", "`"); break;
                case iStrike: WrapSelection("~~", "~~"); break;
                case iH1:     ApplyHeading(1); break;
                case iH2:     ApplyHeading(2); break;
                case iH3:     ApplyHeading(3); break;
                case iLink:   DoLink(); break;
                case iNone:   ClearMarkdownInSelection(); break;
            }
            ClearStyles();
        }
        AdjustScrollbar();
    } else if (menuID == mView) {
        switch (menuItem) {
            case iMarkdownView: SetViewMode(false); break;
            case iWriterView:   SetViewMode(true); break;
            case iZoomIn:       DoZoom(1); break;
            case iZoomOut:      DoZoom(-1); break;
            case iZoomDefault:  DoZoomReset(); break;
        }
    } else if (menuID == mHelp) {
        switch (menuItem) {
            case iAbout: ShowAboutBox(); break;
        }
    }
    HiliteMenu(0);
    /* HiliteMenu un-hilites the clicked title assuming the Menu Manager's
       own standard white-bar/black-text look, which clobbers our inverted
       Writer-mode bar -- reassert it now that the menu has closed. */
    UpdateMenuBarLook();
}

static void EventLoop(void)
{
    EventRecord event;
    WindowPtr w;
    short part;
    DocumentPtr doc;

    while (!gDone) {
        if (WaitNextEvent(everyEvent, &event, 15, NULL)) {
            doc = FrontDocument();
            /* Disposing a dialog/window doesn't restore the caller's port
               -- cheap insurance against any path (found or not) leaving
               thePort dangling at a freed window's memory. */
            SetPort(doc->window);
            switch (event.what) {
                case updateEvt:
                    DoUpdate((WindowPtr) event.message);
                    break;

                case mouseDown:
                    part = FindWindow(event.where, &w);
                    if (part == inMenuBar) {
                        UpdateEditMenuState();
                        DoMenuCommand(MenuSelect(event.where));
                    } else if (part == inContent) {
                        ControlHandle hitControl;
                        DocumentPtr clickDoc;

                        /* Standard Mac convention: a content click in a
                           window that isn't already frontmost brings it
                           forward and stops there -- it is NOT also
                           treated as a click into that window's content
                           in the same event. Written in Milestone 2
                           when only one window could ever exist (so
                           this branch was unreachable in practice);
                           genuinely reachable as of Milestone 3, now
                           that File > New/Open/Close make more than
                           one window possible. */
                        if (w != FrontWindow()) {
                            SelectWindow(w);
                            break;
                        }

                        clickDoc = DocumentForWindow(w);
                        SetPort(w);
                        GlobalToLocal(&event.where);
                        if (clickDoc != NULL) {
                            if (FindControl(event.where, w, &hitControl) != 0 && hitControl == clickDoc->scrollBar)
                                DoScrollClick(event.where);
                            else {
                                clickDoc->typingRunActive = false;
                                TEClick(event.where, (event.modifiers & shiftKey) != 0, clickDoc->activeTE);
                            }
                        }
                    } else if (part == inGoAway) {
                        if (TrackGoAway(w, event.where))
                            CloseDocumentInteractive(DocumentForWindow(w));
                    } else if (part == inDrag) {
                        /* SelectWindow before DragWindow regardless of
                           whether w is already frontmost -- a no-op if
                           it already is, and standard Mac behavior for
                           a background window's title bar: dragging it
                           also brings it forward. */
                        SelectWindow(w);
                        DragWindow(w, event.where, &qd.screenBits.bounds);
                    }
                    break;

                case keyDown:
                case autoKey: {
                    char key = event.message & charCodeMask;
                    Boolean isContentKey = (key < 0x1C || key > 0x1F);

                    if (event.modifiers & cmdKey) {
                        if (event.what == keyDown) {
                            if ((key == 'z' || key == 'Z') && (event.modifiers & shiftKey))
                                DoRedo();
                            else {
                                UpdateEditMenuState();
                                DoMenuCommand(MenuKey(key));
                            }
                        }
                    } else {
                        if (isContentKey) {
                            if (!doc->typingRunActive) {
                                PushUndoSnapshot();
                                doc->typingRunActive = true;
                            }
                        } else {
                            doc->typingRunActive = false;
                        }

                        TEKey(key, doc->activeTE);
                        if (isContentKey) {
                            doc->dirty = true;
                            if (doc->hideMarkdown)
                                DetectInlineMarkdown(key);
                        }
                        ScrollCaretIntoView();
                        UpdateScrollbarRange();
                    }
                    break;
                }

                case activateEvt: {
                    /* Resolved from event.message (the WindowPtr actually
                       being activated/deactivated), not the front
                       document -- fixed per MULTI_WINDOW_DESIGN.md §6/
                       Milestone 2, written when only one window could
                       exist so event.message always named that same
                       window anyway. Genuinely matters as of Milestone
                       3: a background window can now be deactivated
                       while a different one activates in the same
                       pass, and this makes sure each gets its own
                       document's TE (de)activated, not FrontDocument()'s. */
                    DocumentPtr activateDoc = DocumentForWindow((WindowPtr) event.message);

                    if (activateDoc != NULL) {
                        if ((event.modifiers & activeFlag) != 0)
                            TEActivate(activateDoc->activeTE);
                        else
                            TEDeactivate(activateDoc->activeTE);
                    }
                    break;
                }
            }
        }
        TEIdle(FrontDocument()->activeTE);
    }
}

int main(void)
{
    short message, count;
    DocumentPtr doc;

    Init();
    LoadZoomPref();
    MakeMenu();
    doc = CreateNewDocument();
    UpdateFileMenuState();

    /* A newly-created visible window has its whole content area marked
       invalid automatically, but the splash dialog appears before the
       event loop ever gets a chance to dequeue and process that update
       event -- force the real BeginUpdate/TEUpdate/EndUpdate cycle to
       happen now, so the window has gone through one proper paint before
       the user can type anything. Without this, the very first line typed
       (before any other update has occurred) doesn't render reliably. */
    DoUpdate(doc->window);

    CountAppFiles(&message, &count);
    if (count >= 1 && message == appOpen)
        DoStartupOpen();
    else
        ShowSplashScreen();

    EventLoop();
    return 0;
}
