#ifndef ARTFULTYPE_APP_H
#define ARTFULTYPE_APP_H

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <Files.h>
#include <StandardFile.h>
#include <SegLoad.h>
#include <Multiverse.h>
#include <string.h>

#include "document.h"

/*
    Text-to-window-edge margins. These were sized (64/32/24) for the
    original full-screen "distraction free" window, where a wide margin
    was the point -- much too large for a standard-sized window, where
    it just eats most of the content area and (per the scrollbar layout
    in document.c's CreateNewDocument) pushed the scrollbar in from the
    window edge instead of flush against it. Cut to a few pt on every
    side.

    Still a compile-time constant, not a real preference -- genuinely
    should become one (a per-user saved setting, the same way zoom
    already is via kZoomPrefType/kWriterZoomPrefID/kMarkdownZoomPrefID
    in zoom.c), just not part of this pass. Flagging rather than
    building preference storage unasked.
*/
#define MARGIN_H     8
#define MARGIN_TOP   8
#define MARGIN_BOTTOM 8
#define MENU_BAR_HEIGHT 20
#define FONT_SIZE 18
#define SCROLLBAR_WIDTH 16

/*
    Apple menu (Milestone 8). 1 is the conventional Apple-menu ID;
    nothing else currently uses it. Reuses mHelp's iAbout (also 1) for
    its own About item rather than defining a second constant with the
    same value -- item constants are only ever switched on within
    their own menuID's branch in DoMenuCommand, so this doesn't
    collide with anything.
*/
#define mApple   1

#define mFile    128
#define iNew     1
#define iOpen    2
#define iClose   3
#define iSave    4
#define iSaveAs  5
#define iPageSetup 7
#define iPrint     8
#define iQuit      10

/*
    Default document window sizing (Milestone 3). Deliberately NOT a
    fixed kDefaultWindowWidth/Height as literally described in
    MULTI_WINDOW_DESIGN.md §4.1 -- this project's primary target is a
    512x342 compact Mac screen (see the design doc's own memory-budget
    discussion of Mac Plus-class hardware), and a fixed ~480x340
    "default" would leave almost no margin there, defeating the whole
    "standard window, not full-screen" point of this milestone and
    leaving no room to stagger a second window without it running off
    screen. CreateNewDocument (document.c) computes the actual size
    from qd.screenBits.bounds at runtime instead, inset by
    kDefaultWindowMargin on each side -- sensible on the real target
    hardware, and scales up reasonably on anything larger.
*/
#define kDefaultWindowMargin  40
#define kWindowStagger        20

/*
    Grow (resize) limits for document-view windows -- see main.c's
    inGrow handling and document.c's ResizeDocument. Small enough to
    stay usable on the 512x342 target screen this project targets
    (margins + scrollbar + a handful of visible lines), not tied to
    any particular content. The upper bound is the screen itself
    (computed at grow time from qd.screenBits.bounds, same as
    CreateNewDocument's own default-size computation), not a fixed
    constant here.
*/
#define kMinWindowWidth   200
#define kMinWindowHeight  120

#define zoomDocProc 8
#define zoomNoGrow  12

#define mEdit    131
#define iUndo    1
#define iRedo    2
#define iCut     4
#define iCopy    5
#define iPaste   6
#define iSelectAll 8
#define iPreferences 10

#define mStyle   129
#define iBold    1
#define iItalic  2
#define iCode    3
#define iStrike  4
#define iH1      6
#define iH2      7
#define iH3      8
#define iLink    10
#define iNone    12

#define kSaveChangesAlert 130
#define kSaveBtn          1
#define kCancelBtn        2
#define kDontSaveBtn      3

#define kSplashDialog 131
#define iSplashNew    1
#define iSplashOpen   2
#define iSplashTitle  3

#define kLinkDialog  132
#define iLinkOK      1
#define iLinkCancel  2
#define iLinkField   4

#define kAboutDialog 133
#define iAboutOK     1
#define iAboutTitle  2

#define mView        130
#define iMarkdownView 1
#define iWriterView  2
#define iDistractionFree 4
#define iZoomIn      6
#define iZoomOut     7
#define iZoomDefault 8

#define mHelp    132
#define iAbout   1

/*
    Window menu (Milestone 4, MULTI_WINDOW_DESIGN.md §5.3) has no fixed
    item constants -- its items are one per open document, built and
    torn down at runtime by RebuildWindowMenu (main.c) since the count
    changes as documents open and close. 133 doesn't collide with any
    other MENU-type resource ID above (kAboutDialog is also 133, but a
    DLOG, a different resource type -- Mac resource IDs are namespaced
    per type, not global).
*/
#define mWindow  133

#define MAX_STYLE_OPS 512

#define kNumZoomLevels 5
#define kZoomBaselineIndex 2

#define kZoomPrefType       'ZLvl'
#define kWriterZoomPrefID   128
#define kMarkdownZoomPrefID 129

/*
    Global state -- actual storage lives in main.c. Per-document state
    (everything that used to live here as gWindow/gTE/gHiddenTE/
    gActiveTE/gScrollBar/gDirty/etc.) moved into DocumentRecord -- see
    document.h -- and is reached via FrontDocument()/DocumentForWindow(),
    not as bare globals, as of this milestone. What's left here is
    genuinely app-wide: gDone (should the app quit), the shared menu
    handles (menus are app-wide UI, not per-document), and the zoom
    indices (zoom stays a pair of single app-wide preferences by
    design, not per-document settings -- see MULTI_WINDOW_DESIGN.md
    §10's zoom.c note -- but Writer and Markdown zoom are independent
    of each other as of the printing-support pass: styled prose and
    raw monospace source are different use cases with no reason to
    share one size).
*/
extern Boolean gDone;
extern MenuHandle gAppleMenu;
extern MenuHandle gFileMenu;
extern MenuHandle gViewMenu;
extern MenuHandle gEditMenu;
extern MenuHandle gStyleMenu;
extern MenuHandle gWindowMenu;
extern short gWriterZoomIndex;
extern short gMarkdownZoomIndex;

/* main.c */
void UpdateMenuBarLook(void);
void UpdateFileMenuState(void);
void RebuildWindowMenu(void);
void SyncMenusToFrontDocument(void);
void SetDistractionFree(DocumentPtr doc, Boolean toDistractionFree);

/* scrolling.c */
void UpdateScrollbarRange(void);
void AdjustScrollbar(void);
void ScrollCaretIntoView(void);
void DoScrollClick(Point pt);
void InvalidateHeightCache(void);
short LineContaining(TEHandle te, short pos);

/* markdown.c */
void ClearStyles(void);
void SuppressDrawing(TEHandle te, Rect *saved);
void RestoreDrawing(TEHandle te, Rect *saved);
void BuildHiddenView(void);
void SyncHiddenToCanonical(void);
Handle EncodeSelectionAsMarkdown(short start, short end, TEHandle te);
void InsertMarkdownAsStyled(Handle srcH, long srcLen, TEHandle te);
void WrapSelection(char *prefix, char *suffix);
void ApplyHeading(short level);
void DoLink(void);
void ToggleFace(Style face);
void DoLinkHidden(void);
void ToggleCode(void);
void ToggleHeadingHidden(short level);
void DetectInlineMarkdown(char justTyped);
void ClearSelectionStyleHidden(void);
void ClearMarkdownInSelection(void);
short AddLinkURL(const unsigned char *url);

/* undo.c */
void ClearUndoRedoStacks(void);
void UpdateEditMenuState(void);
void PushUndoSnapshot(void);
void DoUndo(void);
void DoRedo(void);
void DoCut(void);
void DoCopy(void);
void DoPaste(void);
void DoSelectAll(void);

/* zoom.c */
short CurrentWriterFontSize(void);
short CurrentMarkdownFontSize(void);
void LoadZoomPref(void);
void DoZoom(short direction);
void DoZoomReset(void);

/* file.c */
void SetViewMode(Boolean hideMarkdown);
void DoStartupOpen(void);
Boolean DoSaveAs(void);
Boolean DoSave(void);
Boolean ConfirmDiscardChanges(void);
Boolean DoOpenFile(void);
void DoNewFile(void);

/* splash.c */
void ShowSplashScreen(void);
void ShowAboutBox(void);

#endif
