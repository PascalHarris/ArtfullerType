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
#include "print.h"
#include "preferences.h"

/*
    Confirmed by an actual build error, not assumed this time: this
    toolchain's Events.h does not declare suspendResumeMessage or
    resumeFlag, despite both being standard, stable Event Manager
    constants documented in Inside Macintosh ("Handling Suspend and
    Resume Events", IM: Toolbox Essentials) -- the same chapter the
    osEvt handling in EventLoop below follows. Rather than guess again
    at which header might expose them in this particular toolchain (the
    Controls.h mistake a few rounds back was exactly that kind of
    guess), defined directly: these are fixed, Apple-documented bit
    values, not toolchain-specific behavior, so hardcoding them here
    carries none of the risk an unverified #include did. #ifndef-
    guarded in case some combination of headers this file doesn't
    currently pull in does declare them after all.
*/
#ifndef suspendResumeMessage
#define suspendResumeMessage 0x01
#endif
#ifndef resumeFlag
#define resumeFlag 1
#endif

Boolean gDone = false;
MenuHandle gAppleMenu;
MenuHandle gFileMenu;
MenuHandle gViewMenu;
MenuHandle gEditMenu;
MenuHandle gStyleMenu;
MenuHandle gWindowMenu;
short gWriterZoomIndex = kZoomBaselineIndex;
short gMarkdownZoomIndex = kZoomBaselineIndex;

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
    Refreshes the menu bar. Used throughout this codebase after
    anything that could invalidate it -- menu commands, dismissing a
    modal dialog, etc.

    Also draws a black menu bar with white text, but only while the
    front document is BOTH distraction-free AND in Writer mode
    (hideMarkdown) -- not for hideMarkdown alone, which is what this
    used to key off. Per explicit clarification: the indicator only
    makes sense in Distraction Free's full-screen, everything-else-
    hidden context, where inverting the whole bar (including the Apple
    menu and anything a menu-bar-resident utility draws) is consistent
    with what "distraction free" already means, not a bug -- you've
    already asked to see nothing else. In ordinary windowed mode the
    menu bar now stays fully standard, no exceptions; a windowed-mode
    equivalent is a separate, later piece of work, not attempted here.

    This previously keyed off hideMarkdown alone and inverted only the
    strip under this app's own titles, which had two real problems:
    inverted title text is indistinguishable, by Mac convention, from
    those menus being highlighted/tracked, so every title looked
    permanently "selected" any time Writer mode was active, windowed
    or not -- not a sizing problem the strip's bounds could fix, since
    the visual language itself was the wrong tool for a mode that
    isn't full-screen. And the width computation for that narrower
    strip had a real bug independent of the above: it saved/restored
    the wrong GrafPort's font (the document window's, not the Window
    Manager port's, which is the one actually changed), leaving menu-
    bar text size drifting to whatever font the document last had
    active. Neither problem applies to the simpler, full-width bar
    restored below, which needs no font measurement at all.

    Every icon in the bar (Apple menu, Balloon Help, Application menu)
    inverts along with everything else -- a deliberate choice, not an
    oversight. Preserving true icon colors on color-capable screens
    was attempted and then abandoned: doing it exactly (via each
    icon's own mask) was only achievable for this app's own icon,
    since that's the only one of the three this app has real resource
    access to; the other two would only ever get an approximate,
    rectangle-based guess, and a mismatched mix of one exactly-colored
    icon alongside two crudely-haloed ones looks worse than uniformly
    inverting all three the same way. Simple and consistent, even if
    the icons look a little odd when inverted, beats a mishmash.
*/
void UpdateMenuBarLook(void)
{
    DocumentPtr doc = FrontDocument();

    DrawMenuBar();

    if (doc != NULL && doc->distractionFree && doc->hideMarkdown) {
        GrafPtr savePort;
        GrafPtr wMgrPort;
        Rect bar;

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
    /* Built and inserted first -- InsertMenu(..., 0) appends to the
       end of the current menu list, so whichever menu is inserted
       first ends up leftmost. Titled with the Apple logo character
       ("\p\024", 0x14), not a literal word. AppendResMenu populates
       the rest of the menu with every installed desk accessory --
       that's what makes this "a working Apple menu... as with all
       classic Mac software" rather than just an About box under a
       different icon; a real Apple menu's main job historically is
       desk accessory access, not the About item. */
    gAppleMenu = NewMenu(mApple, "\p\024");
    AppendMenu(gAppleMenu, "\pAbout The Artful Type...;(-");
    AppendResMenu(gAppleMenu, 'DRVR');
    InsertMenu(gAppleMenu, 0);

    gFileMenu = NewMenu(mFile, "\pFile");
    AppendMenu(gFileMenu, "\pNew/N;Open.../O;Close/W;Save/S;Save As...;(-;Page Setup...;Print.../P;(-;Quit/Q");
    InsertMenu(gFileMenu, 0);

    /* No "/" shortcut on Redo -- it would register as a second cmd-key
       equivalent for the same letter as Undo, ambiguous to MenuKey.
       Cmd-Shift-Z for Redo is instead handled directly in EventLoop,
       intercepted before MenuKey ever sees it. */
    gEditMenu = NewMenu(mEdit, "\pEdit");
    AppendMenu(gEditMenu, "\pUndo/Z;Redo;(-;Cut/X;Copy/C;Paste/V;(-;Select All/A;(-;Preferences...");
    InsertMenu(gEditMenu, 0);
    DisableItem(gEditMenu, iUndo);
    DisableItem(gEditMenu, iRedo);

    gStyleMenu = NewMenu(mStyle, "\pStyle");
    AppendMenu(gStyleMenu, "\pBold/B;Italic/I;Code/K;Strikethrough;(-;Heading 1/1;Heading 2/2;Heading 3/3;(-;Link/L;(-;None");
    InsertMenu(gStyleMenu, 0);

    gViewMenu = NewMenu(mView, "\pView");
    AppendMenu(gViewMenu, "\pMarkdown;Writer;(-;Distraction Free;(-;Zoom In/=;Zoom Out/-;Default Size/0");
    InsertMenu(gViewMenu, 0);
    CheckItem(gViewMenu, iWriterView, true);

    /* No items appended here -- RebuildWindowMenu (below) builds this
       menu's contents at runtime, one item per open document, since
       the count and titles change as documents open and close. Called
       for the first time once main() has created the first document;
       empty until then. */
    gWindowMenu = NewMenu(mWindow, "\pWindow");
    InsertMenu(gWindowMenu, 0);

    /* Help menu disabled for now -- About now lives in the Apple menu
       (mApple's iAbout branch in DoMenuCommand); revisit what belongs
       here later. Not deleted, just not built/inserted -- mHelp and
       iAbout stay defined in app.h, cheap to keep, reinstate or
       remove once there's an actual decision about Help content.

    helpMenu = NewMenu(mHelp, "\pHelp");
    AppendMenu(helpMenu, "\pAbout The Artful Type...");
    InsertMenu(helpMenu, 0);
    */

    UpdateMenuBarLook();
}

/*
    Enables/disables File menu items based on document state -- New and
    Open based on whether a document slot is actually free (per
    MULTI_WINDOW_DESIGN.md §7.1: "disable New and Open... rather than
    failing silently" once MAX_DOCUMENTS is reached), Close/Save/Save
    As/Page Setup/Print based on whether there's a document to act on
    at all. That second half didn't matter before "no window open" was
    a reachable state -- there was always at least one document open by
    construction -- but now it's a real, permitted state, and each of
    those items' handlers (file.c/print.c) resolves FrontDocument()
    without a NULL guard, so leaving them enabled with zero documents
    open would crash on click or Cmd-key rather than fail silently.
    Called after every document count change (create or close), same
    as before.
*/
void UpdateFileMenuState(void)
{
    Boolean haveFreeSlot = (FindFreeDocumentSlot() != NULL);
    Boolean haveDocument = (FrontDocument() != NULL);

    if (haveFreeSlot) {
        EnableItem(gFileMenu, iNew);
        EnableItem(gFileMenu, iOpen);
    } else {
        DisableItem(gFileMenu, iNew);
        DisableItem(gFileMenu, iOpen);
    }

    if (haveDocument) {
        EnableItem(gFileMenu, iClose);
        EnableItem(gFileMenu, iSave);
        EnableItem(gFileMenu, iSaveAs);
        EnableItem(gFileMenu, iPageSetup);
        EnableItem(gFileMenu, iPrint);
    } else {
        DisableItem(gFileMenu, iClose);
        DisableItem(gFileMenu, iSave);
        DisableItem(gFileMenu, iSaveAs);
        DisableItem(gFileMenu, iPageSetup);
        DisableItem(gFileMenu, iPrint);
    }
}

/*
    Returns the Nth in-use document slot (1-based, matching Window menu
    item numbering), walking gDocuments in slot order. RebuildWindowMenu
    below builds the menu by walking gDocuments in this exact same
    order, appending one item per in-use slot -- so the Nth item it
    appends always corresponds to what this returns for that same N.
    SyncMenusToFrontDocument re-checks the menu the same way, without a
    full rebuild, using this to map each existing item back to a
    document. Keep all three in sync if any changes.
*/
static DocumentPtr DocumentForWindowMenuItem(short item)
{
    short i;
    short count = 0;

    for (i = 0; i < MAX_DOCUMENTS; i++) {
        if (gDocuments[i].inUse) {
            count++;
            if (count == item)
                return &gDocuments[i];
        }
    }
    return NULL;
}

/*
    Rebuilds the Window menu's contents from scratch: deletes every
    existing item, then appends one per open document (filename, or
    "Untitled"/"Untitled 2"/... for documents with no file yet -- at
    most MAX_DOCUMENTS untitled at once, so a single extra digit always
    suffices, no general number-formatting needed), checking whichever
    one matches FrontDocument(). Called after every document create and
    close per MULTI_WINDOW_DESIGN.md §5.3 -- see call sites in file.c
    (DoNewFile/DoOpenFile) and this file's CloseDocumentInteractive and
    main().

    Item titles come from AppendMenu(gWindowMenu, "\p ") (a placeholder,
    safe: a single space has no meta-character meaning to AppendMenu)
    followed by SetMenuItemText with the real text, rather than
    embedding a filename directly into an AppendMenu string --
    AppendMenu's mini-language treats characters like ; / ! ( < as
    syntax, and HFS filenames are permissive enough that a user's
    actual filename could contain any of them (only ':' is forbidden).
    SetMenuItemText sets the raw text with no such interpretation.
    (This toolchain's real name for the classic SetItem call --
    confirmed against autc04/multiversal's actual definitions after
    SetItem itself turned out not to be exposed here; not guessed.)
*/
void RebuildWindowMenu(void)
{
    DocumentPtr front = FrontDocument();
    short i;
    short itemIndex = 0;
    short untitledCount = 0;

    while (CountMItems(gWindowMenu) > 0)
        DeleteMenuItem(gWindowMenu, 1);

    for (i = 0; i < MAX_DOCUMENTS; i++) {
        DocumentPtr doc = &gDocuments[i];
        Str255 title;

        if (!doc->inUse)
            continue;

        itemIndex++;

        if (doc->haveFile) {
            BlockMove(doc->fileName, title, doc->fileName[0] + 1);
        } else {
            static const unsigned char kUntitled[] = "\pUntitled";
            short len = kUntitled[0];

            BlockMove(kUntitled, title, len + 1);
            untitledCount++;
            if (untitledCount > 1) {
                title[++len] = ' ';
                title[++len] = (unsigned char) ('0' + untitledCount);
                title[0] = (unsigned char) len;
            }
        }

        AppendMenu(gWindowMenu, "\p ");
        SetMenuItemText(gWindowMenu, itemIndex, title);
        CheckItem(gWindowMenu, itemIndex, (doc == front));
    }
}

/*
    Re-derives every menu's front-document-dependent state from
    scratch, rather than trusting whichever call site happened to
    leave it correct. Called at the three points MULTI_WINDOW_DESIGN.md
    §5.3 requires -- after SelectWindow from the Window menu (below, in
    DoMenuCommand), after activateEvt/osEvt bring a window forward
    (ActivateWindowDocument above), and once CloseDocumentInteractive's
    close has settled on whichever document is now front -- plus one
    case the design doc's three don't explicitly name but the same
    reasoning covers: file.c's DoNewFile/DoOpenFile, since a newly
    created document also becomes front and starts from its own
    default state (Markdown/Writer mode, empty undo history), which
    won't generally match whatever the previous front document's menu
    state was.
*/
void SyncMenusToFrontDocument(void)
{
    DocumentPtr doc = FrontDocument();
    short i;

    if (doc == NULL) {
        /* Nothing to act on -- these three menus are entirely
           meaningless without a document (every item in them
           ultimately resolves FrontDocument() with no NULL guard), so
           the whole title is disabled rather than leaving individual
           items enabled with nothing for them to do. Window menu is
           left alone: RebuildWindowMenu already empties it when no
           documents are open, so there's nothing here to check or
           uncheck either way. */
        DisableItem(gEditMenu, 0);
        DisableItem(gStyleMenu, 0);
        DisableItem(gViewMenu, 0);
        return;
    }

    EnableItem(gEditMenu, 0);
    EnableItem(gStyleMenu, 0);
    EnableItem(gViewMenu, 0);

    CheckItem(gViewMenu, iMarkdownView, !doc->hideMarkdown);
    CheckItem(gViewMenu, iWriterView, doc->hideMarkdown);
    CheckItem(gViewMenu, iDistractionFree, doc->distractionFree);

    UpdateEditMenuState();

    for (i = 1; i <= CountMItems(gWindowMenu); i++)
        CheckItem(gWindowMenu, i, (DocumentForWindowMenuItem(i) == doc));
}

/*
    The single entry point for turning Distraction Free on or off for
    one document, maintaining the invariant MULTI_WINDOW_DESIGN.md §8
    states: "frontmost window distraction-free implies every other
    open window hidden." ReHouseDocument (document.c) only rebuilds
    doc's own window -- Milestone 5's whole scope; this wraps it with
    the show/hide orchestration across every OTHER open document,
    which is Milestone 6's actual deliverable. Called from the View
    menu's Distraction Free toggle, from the Window-menu-switch-while-
    DF-active swap (§8.1, below in DoMenuCommand), and from file.c's
    DoNewFile/DoOpenFile exiting DF first (§8.2).

    Establishes the whole invariant from scratch on every call rather
    than computing a minimal diff from whatever the previous state
    was. That means composing two calls back to back (as the §8.1 swap
    does) briefly re-shows a document the second call is about to hide
    again -- redundant Toolbox work, not a correctness issue -- traded
    deliberately for every call site being built from the same simple,
    independently-verifiable building block instead of a hand-optimized
    one-off for the swap case.
*/
void SetDistractionFree(DocumentPtr doc, Boolean toDistractionFree)
{
    short i;

    if (doc == NULL)
        return;

    ReHouseDocument(doc, toDistractionFree);

    for (i = 0; i < MAX_DOCUMENTS; i++) {
        DocumentPtr other = &gDocuments[i];

        if (!other->inUse || other == doc)
            continue;

        if (toDistractionFree)
            HideWindow(other->window);
        else
            ShowWindow(other->window);
    }

    SyncMenusToFrontDocument();
}

/*
    Resolves the document that owns w, not the front document -- fixed
    per MULTI_WINDOW_DESIGN.md §6/Milestone 2, written when only one
    window could exist so DocumentForWindow(w) and FrontDocument()
    always agreed anyway. Genuinely matters as of Milestone 3: a
    background window can now receive an update event while a
    different one is front, and this makes sure that redraws with its
    own document's TE, not whichever happens to be frontmost.

    SetPort(w) is the fix for a real bug found after Milestone 3 shipped:
    every draw call below (EraseRect, TEUpdate, DrawControls) draws into
    whatever port is CURRENT, not automatically into w just because w
    was passed in -- and nothing here, or anywhere before this was
    called, guaranteed the current port was actually w's. It happened
    to work throughout Milestones 1-2 because the event loop's own
    SetPort(doc->window) (using the front document) and w were always
    the same window with only one ever able to exist. Once a second
    window existed, an update event for a background window -- e.g.
    from uncovering it -- would still draw with the port left over from
    whichever window was actually front, so the background window's own
    content never got repainted. Toggling view mode on the affected
    window worked around it only because that toggle's own InvalRect
    happened to fire while that same window was front, which coincidentally
    got the port right.

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

    SetPort(w);
    BeginUpdate(w);
    EraseRect(&w->portRect);
    if (doc != NULL) {
        TEUpdate(&w->portRect, doc->activeTE);
        DrawControls(w);
        if (!doc->distractionFree)
            DrawGrowIcon(w);
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
    Boolean wasDistractionFree;

    if (doc == NULL)
        return;

    SelectWindow(doc->window);
    if (!ConfirmDiscardChanges())
        return;

    wasDistractionFree = doc->distractionFree;

    CloseDocument(doc);
    RebuildWindowMenu();
    UpdateFileMenuState();

    if (wasDistractionFree) {
        /* §8.3 interaction the design doc's own three don't spell out:
           the just-closed document was the one Distraction Free
           window, so every OTHER open document (if any) is currently
           hidden per the §8 invariant -- and FrontWindow()/
           FrontDocument() only ever consider VISIBLE windows, so the
           check below would read NULL even though real documents
           remain, wrongly triggering the "nothing left at all"
           fallback and creating a redundant blank document while the
           actual ones sit invisibly hidden. Bring whichever inUse
           document comes first back to standard visibility instead --
           simple, deterministic first-found choice; nothing tracks
           "most recently used" to prefer one over another. */
        short i;

        for (i = 0; i < MAX_DOCUMENTS; i++) {
            if (gDocuments[i].inUse) {
                ShowWindow(gDocuments[i].window);
                SelectWindow(gDocuments[i].window);
                break;
            }
        }
    }

    /* No "last window closed" fallback -- per explicit request, having
       no window open at all is a permitted, ordinary state, not one
       this app papers over by conjuring a fresh blank document and a
       New/Open dialog on top of it. UpdateFileMenuState (above) and
       SyncMenusToFrontDocument (below) already handle a NULL
       FrontDocument() correctly (File > New/Open/Quit stay live,
       everything document-specific -- Close/Save/Page Setup/Print and
       the whole Edit/Style/View menus -- disables itself), so there's
       nothing left to do here once the window itself is gone. */

    /* Reflects whichever document is front by this point -- another
       already-open one, or none at all if that was the last window. */
    SyncMenusToFrontDocument();
}

/*
    Handles a click in a window's grow icon (inGrow, EventLoop below).
    Only ever reachable for documentProc chrome -- plainDBox (Distraction
    Free) windows have no grow icon at all, so FindWindow never reports
    inGrow for one; the doc->distractionFree guard here is a defensive
    belt-and-suspenders check, not a path this is actually expected to
    take in practice.

    Growing a background window is treated the same way the inContent
    branch below already treats a background content click: select and
    stop, don't act on this event. Necessary here specifically because
    ResizeDocument's own AdjustScrollbar call (scrolling.c) self-
    resolves its target via FrontDocument() like every other document-
    mutating call in this codebase -- growing a non-frontmost window
    without this guard would silently resize whichever document
    happens to be front instead of the one actually dragged.

    GrowWindow live-tracks the drag itself (the rubber-band outline)
    and returns once the mouse button is released; a zero result means
    the user dragged the size to nothing meaningfully different (or
    let go without moving), matching GrowWindow's own documented
    convention, and is treated as a no-op rather than resizing to 0x0.
    kMinWindowWidth/kMinWindowHeight (app.h) floor the drag; the
    screen's own current size (recomputed here, same as
    CreateNewDocument's own default-size logic) caps it -- neither is
    tied to any particular document's content.
*/
static void DoGrow(WindowPtr w, Point startPt)
{
    DocumentPtr doc = DocumentForWindow(w);
    Rect limits;
    long growResult;
    short maxWidth, maxHeight;

    if (doc == NULL || doc->distractionFree)
        return;

    if (w != FrontWindow()) {
        SelectWindow(w);
        return;
    }

    maxWidth = (short) (qd.screenBits.bounds.right - qd.screenBits.bounds.left);
    maxHeight = (short) (qd.screenBits.bounds.bottom - qd.screenBits.bounds.top
                          - MENU_BAR_HEIGHT);

    /* GrowWindow's limit rect repurposes the four Rect fields as
       (minWidth, minHeight, maxWidth, maxHeight) rather than actual
       screen coordinates -- a documented classic Mac oddity, not a
       mistake; SetRect's own (left, top, right, bottom) parameter
       order lines up with that directly. */
    SetRect(&limits, kMinWindowWidth, kMinWindowHeight, maxWidth, maxHeight);

    growResult = GrowWindow(w, startPt, &limits);
    if (growResult != 0)
        ResizeDocument(doc, LoWord(growResult), HiWord(growResult));
}

/*
    Handles a click in a window's title-bar zoom box (inZoomIn/
    inZoomOut, EventLoop below) -- same shape as DoGrow above, same
    reasons for each guard: only ever reachable for zoomDocProc chrome
    in practice (plainDBox/Distraction Free windows have no zoom box,
    so FindWindow never reports inZoomIn/inZoomOut for one; the
    doc->distractionFree check is defensive, not a path expected to be
    taken), and background-window protection for the identical reason
    DoGrow needs it -- ZoomDocument's own geometry sync
    (SyncDocumentGeometry, document.c) ends by calling AdjustScrollbar,
    which resolves its target via FrontDocument() like every other
    document-mutating call in this codebase, so zooming a non-frontmost
    window without this guard would silently resize whichever document
    happens to be front instead of the one actually clicked.

    TrackBox live-tracks the click the way GrowWindow live-tracks a
    grow drag -- highlighting the box while the mouse stays down inside
    it, returning false if the mouse is released outside it (a
    cancelled click) or true if released back inside (a confirmed one).
    Only a confirmed click actually zooms.
*/
static void DoZoomBox(WindowPtr w, Point startPt, short part)
{
    DocumentPtr doc = DocumentForWindow(w);

    if (doc == NULL || doc->distractionFree)
        return;

    if (w != FrontWindow()) {
        SelectWindow(w);
        return;
    }

    if (TrackBox(w, startPt, part))
        ZoomDocument(doc, part);
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
           document is front. ShowWindow first too: unlike that other
           call site (always the already-visible front document, or a
           window just clicked, so necessarily visible), this one walks
           every open document regardless of visibility -- if
           Distraction Free is active elsewhere, some of these could be
           hidden. Whether a plain SelectWindow reliably surfaces a
           hidden window isn't something I could confirm one way or
           the other from the available definitions (trap number and
           signature only, no behavioral documentation) -- ShowWindow
           first removes the question rather than resting on an
           unverified assumption; a harmless no-op for whichever
           documents were already visible. */
        ShowWindow(gDocuments[i].window);
        SelectWindow(gDocuments[i].window);
        if (!ConfirmDiscardChanges())
            return false;
        /* Accepted, narrow edge case, not fixed here: if this loop
           ShowWindow'd a document that was hidden because some OTHER
           document is distraction-free, and the person then cancels
           here, that document stays visible even though the DF
           invariant nominally still holds for whichever document was
           distraction-free. Restoring it would mean tracking which
           windows this loop itself made visible and re-hiding exactly
           those on a cancelled return -- real complexity for a
           multi-condition edge case (must be mid-Quit, mid-DF, and
           cancelled at exactly the right moment). Flagging the
           trade-off rather than silently accepting it or building
           the restore logic unasked. */
    }
    return true;
}

static void DoMenuCommand(long menuResult)
{
    short menuID = HiWord(menuResult);
    short menuItem = LoWord(menuResult);
    DocumentPtr doc = FrontDocument();

    if (menuID == mApple) {
        if (menuItem == iAbout) {
            ShowAboutBox();
        } else {
            /* Anything past the About item is one of the desk
               accessories AppendResMenu (MakeMenu) appended -- fetch
               its name and hand it to the Desk Manager. */
            Str255 name;

            GetMenuItemText(gAppleMenu, menuItem, name);
            OpenDeskAcc(name);
        }
    } else if (menuID == mFile) {
        switch (menuItem) {
            case iNew:   DoNewFile(); break;
            case iOpen:  DoOpenFile(); break;
            case iClose: CloseDocumentInteractive(FrontDocument()); break;
            case iSave:   DoSave(); break;
            case iSaveAs: DoSaveAs(); break;
            case iPageSetup: DoPageSetup(); break;
            case iPrint:     DoPrint(); break;
            case iQuit:
                if (ConfirmDiscardChangesForAllDocuments())
                    gDone = true;
                break;
        }
    } else if (menuID == mEdit) {
        /* Known gap, not addressed here: when a desk accessory is
           frontmost, Edit commands conventionally route through
           SystemEdit() instead of this app's own DoCut/DoCopy/etc.,
           so e.g. cut/copy/paste into Calculator or Note Pad would
           work. Left as-is (Milestone 8) -- worth doing eventually,
           deliberately out of scope for the Apple-menu/desk-accessory
           pass that added SystemTask/SystemEvent/SystemClick above. */
        if (doc != NULL) {
            switch (menuItem) {
                case iUndo:      DoUndo(); break;
                case iRedo:      DoRedo(); break;
                case iCut:       DoCut(); break;
                case iCopy:      DoCopy(); break;
                case iPaste:     DoPaste(); break;
                case iSelectAll: DoSelectAll(); break;
            }
        }
        if (menuItem == iPreferences)
            DoPreferences();
    } else if (menuID == mStyle) {
        if (doc != NULL) {
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
        }
    } else if (menuID == mView) {
        if (doc != NULL) {
            switch (menuItem) {
                case iMarkdownView:    SetViewMode(false); break;
                case iWriterView:      SetViewMode(true); break;
                case iDistractionFree: SetDistractionFree(doc, !doc->distractionFree); break;
                case iZoomIn:          DoZoom(1); break;
                case iZoomOut:         DoZoom(-1); break;
                case iZoomDefault:     DoZoomReset(); break;
            }
        }
    /* Help menu disabled for now (see MakeMenu) -- About lives in the
       Apple menu's iAbout branch above instead. Unreachable since
       mHelp is never inserted into the menu bar anymore, kept rather
       than deleted for the same reason noted in MakeMenu.

    } else if (menuID == mHelp) {
        switch (menuItem) {
            case iAbout: ShowAboutBox(); break;
        }
    */
    } else if (menuID == mWindow) {
        DocumentPtr chosen = DocumentForWindowMenuItem(menuItem);

        if (chosen != NULL && chosen != doc) {
            if (doc != NULL && doc->distractionFree) {
                /* §8.1: a plain SelectWindow isn't enough here -- doc
                   (the current front document) is the one Distraction
                   Free window, and chosen may currently be hidden (per
                   the invariant every other open document is, while
                   one is DF). Swap: re-house doc back to standard
                   first, then re-house chosen into Distraction Free --
                   keeps "frontmost DF implies everyone else hidden"
                   true continuously rather than only at the moment DF
                   was first turned on. */
                SetDistractionFree(doc, false);
                SetDistractionFree(chosen, true);
            } else {
                SelectWindow(chosen->window);
                SyncMenusToFrontDocument();
            }
        }
    }
    HiliteMenu(0);
    /* HiliteMenu un-hilites the clicked title assuming the Menu Manager's
       own standard white-bar/black-text look, which clobbers our inverted
       Writer-mode bar -- reassert it now that the menu has closed. */
    UpdateMenuBarLook();
}

/*
    Shared by activateEvt and the osEvt/suspendResumeMessage handling in
    EventLoop below -- MultiFinder can, and per Inside Macintosh's own
    canonical DoOSEvent example (IM: Toolbox Essentials, "Handling
    Suspend and Resume Events") is documented to, activate/deactivate a
    window purely via osEvt when switching layers -- e.g. clicking into
    another application's window -- with no accompanying activateEvt
    guaranteed. Same SetPort-then-(de)activate sequence either way, so
    both call this instead of duplicating it.
*/
static void ActivateWindowDocument(WindowPtr w, Boolean activating)
{
    DocumentPtr doc;

    if (w == NULL)
        return;

    doc = DocumentForWindow(w);
    SetPort(w);
    if (doc != NULL) {
        if (activating)
            TEActivate(doc->activeTE);
        else
            TEDeactivate(doc->activeTE);
    }

    /* Only on activation, not deactivation -- deactivating a window
       doesn't by itself establish which one (if any) is now front;
       whichever activateEvt/osEvt eventually activates the next one
       will trigger its own sync. */
    if (activating)
        SyncMenusToFrontDocument();
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
               thePort dangling at a freed window's memory.

               doc is no longer dereferenced unconditionally here, the
               way it was before this fix. Reference classic-Mac event
               loops guard FrontWindow()'s result before using it (e.g.
               "if (FrontWindow()) IdleControls(FrontWindow());" in
               published examples) rather than assuming it's always
               non-NULL -- this now matches that standard rather than
               assuming it can't happen. I can't fully verify from here
               the exact mechanism connecting "clicked into another
               application's window" to doc being NULL on some pass
               through this loop -- that needs a real MultiFinder
               environment to trace, which this environment doesn't
               have -- but this guard is correct regardless of the
               precise trigger, and the osEvt handling added below is
               the documented, standard piece this app was missing for
               MultiFinder layer switches generally. */
            if (doc != NULL)
                SetPort(doc->window);
            switch (event.what) {
                case updateEvt:
                    /* A desk accessory's own update is routed to the
                       Desk Manager instead of this app's DoUpdate,
                       which only knows about its own documents --
                       DocumentForWindow(w) would just return NULL for
                       a DA window, and DoUpdate would still call
                       BeginUpdate/EraseRect/EndUpdate on it, which
                       isn't this app's place to do. */
                    if (SystemEvent(&event))
                        break;
                    DoUpdate((WindowPtr) event.message);
                    break;

                case mouseDown:
                    part = FindWindow(event.where, &w);
                    if (part == inSysWindow) {
                        /* A click in a desk accessory's own window --
                           SystemClick handles everything about it
                           (dragging, closing, clicking its controls),
                           not this app's own window handling below. */
                        SystemClick(&event, w);
                    } else if (part == inMenuBar) {
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
                        /* Keeps a later zoom-in returning to the right
                           place, not just the right size -- see
                           RecordWindowUserState's own comment
                           (document.c). No-ops harmlessly for a
                           plainDBox (Distraction Free) window, which
                           has no WStateData to update. */
                        RecordWindowUserState(DocumentForWindow(w));
                    } else if (part == inGrow) {
                        DoGrow(w, event.where);
                    } else if (part == inZoomIn || part == inZoomOut) {
                        DoZoomBox(w, event.where, part);
                    }
                    break;

                case keyDown:
                case autoKey: {
                    char key;
                    Boolean isContentKey;

                    /* If a desk accessory is frontmost and this key is
                       meant for it, SystemEvent handles it entirely --
                       skip this app's own key handling (including
                       menu-key interpretation below) for the whole
                       event, not just the content-typing path. */
                    if (SystemEvent(&event))
                        break;

                    key = event.message & charCodeMask;
                    isContentKey = (key < 0x1C || key > 0x1F);

                    if (event.modifiers & cmdKey) {
                        if (event.what == keyDown) {
                            if ((key == 'z' || key == 'Z') && (event.modifiers & shiftKey))
                                DoRedo();
                            else {
                                UpdateEditMenuState();
                                DoMenuCommand(MenuKey(key));
                            }
                        }
                    } else if (doc != NULL) {
                        /* This app can only ever receive keystrokes
                           while it's the active process, at which point
                           doc shouldn't actually be NULL -- guarded
                           anyway to match the same standard this loop
                           now holds everywhere else to, not because a
                           concrete path to NULL here is known. */
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
                            else
                                MaybeRecolorMarkdown(key);
                        }
                        ScrollCaretIntoView();
                        UpdateScrollbarRange();
                    }
                    break;
                }

                case activateEvt:
                    /* Resolved from event.message (the WindowPtr
                       actually being activated/deactivated), not the
                       front document -- fixed per MULTI_WINDOW_DESIGN.md
                       §6/Milestone 2, written when only one window
                       could exist so event.message always named that
                       same window anyway. Genuinely matters as of
                       Milestone 3: a background window can now be
                       deactivated while a different one activates in
                       the same pass. ActivateWindowDocument (above)
                       does the SetPort-then-(de)activate that used to
                       be inline here -- shared with osEvt below, which
                       needs the identical sequence for MultiFinder
                       layer switches that arrive without a matching
                       activateEvt at all.

                       SystemEvent checked first: if the window being
                       (de)activated is a desk accessory's own, that's
                       its own activation notice to act on (e.g.
                       showing/hiding its own caret), not something to
                       silently no-op past via ActivateWindowDocument's
                       own DocumentForWindow(w) == NULL guard, which
                       only protects this app's TE calls -- it doesn't
                       give the DA a chance to respond to its own
                       activation at all. */
                    if (SystemEvent(&event))
                        break;
                    ActivateWindowDocument((WindowPtr) event.message,
                                            (event.modifiers & activeFlag) != 0);
                    break;

                case osEvt:
                    /* The gap this whole fix is for: clicking into
                       another application's window (Finder, or any
                       other running app under MultiFinder/Process
                       Manager) never reaches this app as a mouseDown --
                       the OS intercepts that click and switches layers
                       itself. The only notice this app gets is osEvt
                       with suspendResumeMessage in the top byte of
                       event.message (Inside Macintosh: Toolbox
                       Essentials, "Handling Suspend and Resume Events"
                       -- the DoOSEvent example there is exactly this
                       case block). Bit 0 (resumeFlag) says which:
                       resuming (this app just became active again) or
                       suspending (something else just did). The other
                       kind of osEvt, mouseMovedMessage, is for cursor-
                       region tracking this app never requested
                       (WaitNextEvent's mouseRgn parameter is NULL
                       above) -- ignoring it here is correct, not an
                       oversight. */
                    if (((unsigned long) event.message >> 24) == suspendResumeMessage) {
                        ActivateWindowDocument(FrontWindow(),
                                                (event.message & resumeFlag) != 0);
                    }
                    break;
            }
        }

        /* Both run every pass through the loop, whether or not
           WaitNextEvent returned an event -- FrontDocument() can
           legitimately be NULL here for the same reason noted above at
           the top of the loop, so TEIdle no longer dereferences it
           unconditionally either. SystemTask gives any open desk
           accessory its own processing time (e.g. so a clock DA keeps
           ticking) -- per Milestone 8, this needs to run every pass
           regardless of events, same as TEIdle already did. */
        SystemTask();
        doc = FrontDocument();
        if (doc != NULL)
            TEIdle(doc->activeTE);
    }
}

int main(void)
{
    short message, count;
    DocumentPtr doc;

    Init();
    LoadPreferences();
    LoadZoomPref();
    MakeMenu();
    doc = CreateNewDocument();
    RebuildWindowMenu();
    UpdateFileMenuState();
    SyncMenusToFrontDocument();

    /* This comment's original justification -- guarding against the
       splash dialog blocking normal event processing -- no longer
       applies to a plain cold launch now that the splash is removed
       from that path below. Left in place anyway: harmless either
       way (BeginUpdate/EndUpdate here just means the normal event
       loop's first pass won't see a redundant updateEvt for the same
       region), and DoStartupOpen (below) can still populate this same
       window with real content afterward, in which case this paints
       the blank state once before that happens -- also harmless, just
       possibly a little redundant. */
    DoUpdate(doc->window);

    CountAppFiles(&message, &count);
    if (count >= 1 && message == appOpen)
        DoStartupOpen();
    /* No splash on a plain cold launch (no startup-opened file) --
       per explicit request, the blank "Untitled" document already
       created and painted above is the whole cold-launch experience
       now, not a New/Open dialog on top of it. ShowSplashScreen()
       itself (splash.c) is untouched, just no longer called from
       here. CloseDocumentInteractive's own "last window closed
       mid-session" fallback (main.c) still calls it -- flagging that
       as a separate, unaddressed question rather than assuming the
       same change belongs there too, since closing your last
       document mid-session seems like a different moment than a cold
       launch, not obviously wanting the same treatment. */

    EventLoop();
    return 0;
}
