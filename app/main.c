/*
    Milestone 2: a real distraction-free Markdown editor.
    Full-screen window, wide margins, 14pt Times, File menu with
    Save/Open backed by the classic File Manager. Saving straight to
    the BlueSCSI SD card (bypassing this disk's HFS volume) is a
    later milestone -- this still saves onto the boot disk itself.

    MULTI_WINDOW_DESIGN.md Milestone 1: gWindow/gTE/gHiddenTE/
    gActiveTE/gScrollBar/gScrollBarVisible/gHaveFile/gDirty/gFileName/
    gVRefNum/gHideMarkdown, the undo/redo stacks, gTypingRunActive, and
    gLinkURLs/gLinkCount used to be defined here as bare globals. They
    now live in gDocuments[0] (document.c), reached via FrontDocument().
    No window management changed -- there is still exactly one window,
    created once in MakeWindow below -- this is purely the internal
    restructure described in the design doc's §3.
*/

#include "app.h"

Boolean gDone = false;
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
    this once, before MakeWindow() has run and before gDocuments[0] is
    populated -- the only point in the whole program where there isn't a
    front document yet. Everywhere else this is called from, a window
    already exists.
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
    MenuHandle fileMenu;
    MenuHandle styleMenu;
    MenuHandle helpMenu;

    fileMenu = NewMenu(mFile, "\pFile");
    AppendMenu(fileMenu, "\pNew/N;Open.../O;Save/S;Save As...;(-;Quit/Q");
    InsertMenu(fileMenu, 0);

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
    Populates gDocuments[0] -- the one document this milestone ever
    creates. Every field DocumentRecord defines gets an explicit initial
    value here rather than relying on gDocuments' BSS zero-init, both
    for readability (this function is now "the complete initial state of
    a document" in one place) and because two fields specifically need a
    non-zero sentinel (cachedTotalHeightNLines/cachedCaretLine = -1,
    matching the original file-statics in scrolling.c -- zero would be a
    valid nLines/line-index value, not a safe default here).
*/
static void MakeWindow(void)
{
    DocumentPtr doc = &gDocuments[0];
    Rect bounds;
    Rect viewRect;
    Rect sbRect;
    short fontNum;

    bounds = qd.screenBits.bounds;
    bounds.top += MENU_BAR_HEIGHT;

    doc->window = NewWindow(NULL, &bounds, "\p", true, plainDBox,
                             (WindowPtr) -1L, false, 0);
    SetPort(doc->window);

    GetFNum("\pTimes", &fontNum);
    TextFont(fontNum);
    TextSize(CurrentFontSize());

    viewRect = doc->window->portRect;
    viewRect.left += MARGIN_H;
    viewRect.right -= MARGIN_H;
    viewRect.top += MARGIN_TOP;
    viewRect.bottom -= MARGIN_BOTTOM;

    doc->te = TEStyleNew(&viewRect, &viewRect);
    doc->hiddenTE = TEStyleNew(&viewRect, &viewRect);
    doc->hideMarkdown = true;
    doc->activeTE = doc->hideMarkdown ? doc->hiddenTE : doc->te;
    TEActivate(doc->activeTE);

    sbRect = viewRect;
    sbRect.left = viewRect.right + (MARGIN_H - SCROLLBAR_WIDTH) / 2;
    sbRect.right = sbRect.left + SCROLLBAR_WIDTH;
    sbRect.top -= 1;
    sbRect.bottom += 1;
    doc->scrollBar = NewControl(doc->window, &sbRect, "\p", false, 0, 0, 0, scrollBarProc, 0);
    doc->scrollBarVisible = false;

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

    /* Must be set last -- everything above establishes the state that
       DocumentForWindow()/FrontDocument() would otherwise expose
       half-initialized the moment inUse goes true. */
    doc->inUse = true;
}

/*
    Resolves the front document rather than the document owning w --
    with only one window/document possible this milestone, those are
    the same thing. Resolving from w specifically (so a background
    window's update event can't redraw using the wrong document's TE)
    is one of the event-dispatch fixes MULTI_WINDOW_DESIGN.md §6
    covers -- deliberately not done yet; that's Milestone 2.
*/
static void DoUpdate(WindowPtr w)
{
    DocumentPtr doc = FrontDocument();

    BeginUpdate(w);
    EraseRect(&w->portRect);
    TEUpdate(&w->portRect, doc->activeTE);
    DrawControls(w);
    EndUpdate(w);
}

static void DoMenuCommand(long menuResult)
{
    short menuID = HiWord(menuResult);
    short menuItem = LoWord(menuResult);
    DocumentPtr doc = FrontDocument();

    if (menuID == mFile) {
        switch (menuItem) {
            case iNew:
                if (ConfirmDiscardChanges())
                    DoNewFile();
                break;
            case iOpen:
                if (ConfirmDiscardChanges())
                    DoOpenFile();
                break;
            case iSave:   DoSave(); break;
            case iSaveAs: DoSaveAs(); break;
            case iQuit:
                if (ConfirmDiscardChanges())
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

                        SetPort(w);
                        GlobalToLocal(&event.where);
                        if (FindControl(event.where, w, &hitControl) != 0 && hitControl == doc->scrollBar)
                            DoScrollClick(event.where);
                        else {
                            doc->typingRunActive = false;
                            TEClick(event.where, (event.modifiers & shiftKey) != 0, doc->activeTE);
                        }
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

                case activateEvt:
                    if ((event.modifiers & activeFlag) != 0)
                        TEActivate(doc->activeTE);
                    else
                        TEDeactivate(doc->activeTE);
                    break;
            }
        }
        TEIdle(FrontDocument()->activeTE);
    }
}

int main(void)
{
    short message, count;

    Init();
    LoadZoomPref();
    MakeMenu();
    MakeWindow();

    /* A newly-created visible window has its whole content area marked
       invalid automatically, but the splash dialog appears before the
       event loop ever gets a chance to dequeue and process that update
       event -- force the real BeginUpdate/TEUpdate/EndUpdate cycle to
       happen now, so the window has gone through one proper paint before
       the user can type anything. Without this, the very first line typed
       (before any other update has occurred) doesn't render reliably. */
    DoUpdate(gDocuments[0].window);

    CountAppFiles(&message, &count);
    if (count >= 1 && message == appOpen)
        DoStartupOpen();
    else
        ShowSplashScreen();

    EventLoop();
    return 0;
}
