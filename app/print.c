#include "app.h"
#include "print.h"

/*
    This file's Printing Manager calls (THPrint, PrOpen, PrClose,
    PrintDefault, PrStlDialog, PrJobDialog, and the rest as later
    milestones add them) need no #include beyond app.h's existing
    <Multiverse.h> -- verified against this toolchain's actual header
    generation (autc04/multiversal's CIncludes generator, and
    autc04/Retro68's interfaces-and-libraries.sh, both read directly
    rather than assumed), not just PrintMgr.yaml's declarations in
    isolation:

    - multiversal's CIncludes generator concatenates every manager's
      declarations -- PrintMgr.yaml included -- into a single file,
      Multiverse.h. It does not emit a standalone PrintMgr.h.
    - Retro68 then writes a fixed set of thin compatibility headers
      (Windows.h, TextEdit.h, Menus.h, etc., each just
      #include "Multiverse.h") for a hardcoded list of classic Apple
      header names. "Printing" is not on that list -- #include
      <Printing.h> would fail outright (no such file), not merely
      leave PrOpen/THPrint/etc. undeclared, so it's not used here.
    - app.h already #includes <Multiverse.h> directly (not through one
      of the whitelisted compatibility names), before it includes
      "document.h" -- so every Printing Manager declaration is already
      visible to any file that includes app.h, the same way
      ControlHandle already reaches document.h transitively through
      <Windows.h> per that struct's own comment.

    This was confirmed by reading the actual generator source, not by
    a real build -- this environment can't compile 68k output, so
    worth a real build to double-check, same caveat as every other
    milestone handed off this way.
*/

/*
    Per-document print record, lazily allocated -- see the comment on
    DocumentRecord's printRecord field (document.h) for why this isn't
    built eagerly at CreateNewDocument time. PrOpen/PrClose bracket
    just this initialization, not held open for the app's lifetime --
    every later Pr* call brackets its own PrOpen()/PrClose() pair the
    same way (PRINTING_DESIGN.md §2.1), rather than one held open
    across the whole app session.

    Stays static/file-local per PRINTING_DESIGN.md §2.1's sketch --
    only this file's own Pr* entry points (DoPageSetup, DoPrint,
    EnsurePageBreaks) need it directly.

    An earlier version of this function also ran a throwaway
    PrOpenDoc/PrCloseDoc pair here, meant to resolve §5's flagged
    uncertainty about whether PrintDefault alone populates a usable
    prInfo.rPage. Reverted: real testing (this milestone's first actual
    print) surfaced two bugs -- printed pages coming out empty, and the
    app's caret/typing behaving as if drawing into the wrong place
    until switching view modes -- both squarely explained by that
    throwaway PrOpenDoc/PrCloseDoc pair, not by anything else new this
    milestone touched:

    - This function is also called from DoPageSetup, not just DoPrint
      -- so the throwaway pair ran (and a printer port got opened and
      torn down) on the far more common "just changed paper size"
      path, not only on an actual print. PrOpenDoc hands back a NEW
      current QuickDraw port for page drawing; nothing was restoring
      the port back to the window's afterward, on EITHER path -- so
      simply opening Page Setup once could leave the window's own
      screen drawing/caret-tracking code operating against an already-
      torn-down printer port for the rest of the session. That matches
      the reported symptom exactly (garbled caret/typing, fixed by
      forcing a redraw via a mode switch).
    - Separately, DoPrint's own pagination (EnsurePageBreaks) used to
      run BEFORE its own real PrOpenDoc, meaning it read prInfo.rPage
      from whatever this throwaway cycle happened to leave behind --
      not necessarily matching the geometry PrOpenDoc establishes for
      an actual, real print job. A mismatch there is exactly the kind
      of thing that could push a page's content entirely outside the
      rect TEUpdate actually clips to, drawing nothing -- matching the
      "print came out empty" symptom.

    Both are now fixed at their actual source rather than by keeping
    this speculative addition: DoPrint restores the port itself when
    it's done (see its own comment), and computes pagination AFTER its
    own real PrOpenDoc succeeds, against that same rPage, rather than
    trusting a value established by a separate, earlier, unrelated
    open/close cycle. That leaves this function back to Milestone P1's
    simpler form -- PrintDefault alone, no PrOpenDoc/PrCloseDoc here at
    all -- which was never itself shown to be the problem.
*/
static Boolean EnsurePrintRecord(DocumentPtr doc)
{
    if (doc->printRecord != NULL)
        return true;

    doc->printRecord = (THPrint) NewHandle(sizeof(TPrint));
    if (doc->printRecord == NULL)
        return false;

    PrOpen();
    PrintDefault(doc->printRecord);
    PrClose();
    return true;
}

/*
    File > Page Setup. On a successful Page Setup, invalidates this
    document's cached pagination (§5/InvalidatePagination below) so
    Page View and the next print job both recompute page breaks
    against the new paper size/orientation/scaling rather than stale
    ones from before -- left out of this function in Milestone P1 since
    InvalidatePagination didn't exist yet; wired in now that it does.

    PrValidate before the dialog -- previously missing here and
    everywhere else in this file. Documented Inside Macintosh best
    practice: re-syncs the print record against whichever driver is
    CURRENTLY selected, which matters if doc->printRecord was first
    initialized (PrintDefault, EnsurePrintRecord) against a different
    driver than what's active now -- e.g. the user switched printers
    via the Chooser sometime after this document's record was first
    created. A record can look fully valid (every field populated, no
    PrError anywhere) while still being stale relative to the actual
    current driver; PrValidate is specifically what's supposed to
    catch and fix that, and nothing in this file was calling it.
*/
Boolean DoPageSetup(void)
{
    DocumentPtr doc = FrontDocument();
    Boolean result;

    if (doc == NULL)
        return false;

    if (!EnsurePrintRecord(doc))
        return false;

    PrOpen();
    PrValidate(doc->printRecord);
    result = PrStlDialog(doc->printRecord);
    PrClose();
    SetPort(doc->window);
    InvalRect(&doc->window->portRect);

    if (result)
        InvalidatePagination(doc);

    return result;
}

/*
    The actual pagination computation -- PRINTING_DESIGN.md §5. Walks
    lines, accumulating height since the current page's start via the
    same TEGetHeight-cumulative-from-line-0 technique scrolling.c
    already uses and has already found reliable for this exact kind of
    height query (see ScrollCaretIntoView's own comment on why isolated
    single-line queries aren't used instead), and cuts a new page
    whenever the next line would overflow the page's usable height.

    pageHeight comes from printRecord's prInfo.rPage -- populated by
    the real PrOpenDoc that's already open by the time this runs
    (DoPrint calls this only after its own PrOpenDoc succeeds, exactly
    so this can trust rPage directly rather than re-deriving or
    re-checking it here).

    Precondition this function trusts rather than re-checks: doc-
    >activeTE's nLines/lineStarts/per-line heights already reflect a
    wrap against the PAGE's own width, not the window's -- DoPrint sets
    that up (destRect/viewRect to the page's rect, then TECalText) once
    before calling EnsurePageBreaks, specifically so this function's
    own line-by-line reads line up with what DrawPageContent will
    actually draw. Calling this while te is still wrapped for the
    window (the bug an earlier version of this file had) would compute
    page breaks that don't correspond to real drawn positions.

    Stays static: EnsurePageBreaks below is the actual entry point
    every caller (DoPrint now, Page View once Milestone P5 exists)
    uses -- same "only this file's own callers need the raw
    computation, everyone else goes through the cached wrapper" shape
    as EnsurePrintRecord/DoPageSetup above. Whatever calls
    EnsurePageBreaks is responsible for the same page-width-wrap
    precondition DoPrint establishes -- worth keeping in mind once Page
    View is built and becomes a second caller.
*/
static void ComputePageBreaks(DocumentPtr doc, PageBreaks *out)
{
    TEHandle te = doc->activeTE;
    long pageHeight = (**doc->printRecord).prInfo.rPage.bottom
                       - (**doc->printRecord).prInfo.rPage.top;
    short line = 0;
    long pageStartHeight = 0;

    out->breaks[0] = 0;
    out->count = 1;

    while (line < (**te).nLines && out->count < iPFMaxPgs) {
        long heightHere = TEGetHeight(line + 1, 0, te) - pageStartHeight;

        if (heightHere > pageHeight && line > 0) {
            out->breaks[out->count++] = (**te).lineStarts[line];
            pageStartHeight = TEGetHeight(line, 0, te);
        }
        line++;
    }
}

/*
    The cached, public entry point for pagination -- recomputes only if
    doc->pageBreaksValid is false (set by InvalidatePagination below),
    matching the same lazy-recompute shape as this codebase's existing
    height caches (scrolling.c). Also guarantees doc->printRecord
    exists first: ComputePageBreaks reads prInfo.rPage from it
    unconditionally, so this calls EnsurePrintRecord itself rather than
    trusting every caller to remember to. DoPrint (below) already calls
    EnsurePrintRecord on its own for an unrelated reason (PrJobDialog
    needs it before pagination is even relevant) -- this second call is
    redundant but harmless there (EnsurePrintRecord's own first line
    short-circuits once printRecord exists), and is what makes this
    function safe to call on its own once Page View (Milestone P5)
    starts calling it without going through DoPrint first.
*/
void EnsurePageBreaks(DocumentPtr doc)
{
    if (doc == NULL || doc->pageBreaksValid)
        return;

    if (!EnsurePrintRecord(doc))
        return;

    ComputePageBreaks(doc, &doc->pageBreaks);
    doc->pageBreaksValid = true;
}

/*
    Marks doc's cached pagination stale -- called from DoPageSetup
    above on a successful Page Setup, and from scrolling.c's
    InvalidateHeightCache (every one of ITS OWN call sites, in
    document.c and markdown.c, therefore gets this for free without
    each needing its own separate call here).
*/
void InvalidatePagination(DocumentPtr doc)
{
    if (doc == NULL)
        return;

    doc->pageBreaksValid = false;
}

/*
    Draws one page's slice of doc->activeTE into the printer port --
    the same viewRect/destRect-offset-per-page technique
    PRINTING_DESIGN.md §6.1 describes for Page View's screen rendering,
    just targeting prInfo.rPage instead of a window rect. Draws from
    activeTE regardless of hideMarkdown (both modes print, per §7.2):
    Writer mode's formatting (bold/heading/link runs, requirement 3)
    and Markdown mode's Monaco font (requirement 4, Milestone P1) are
    both already correct on their respective TE record as of the
    styling that's already there -- TEUpdate against the printer port
    draws whichever TE record activeTE currently is exactly as
    correctly as it already draws on screen, no separate per-print
    styling logic needed.

    pageNum is 1-based (matching PrJob's iFstPage/iLstPage convention,
    and DoPrint's own loop below) -- doc->pageBreaks.breaks[pageNum-1]
    is that page's starting character offset, always exactly a line
    start (per ComputePageBreaks), so LineContaining (scrolling.c) --
    ordinarily used the other direction, position-to-line for caret
    placement -- doubles here as offset-to-line, and TEGetHeight from
    there gives that page's starting height in the document's
    continuous (unpaginated) coordinate space.

    Only destRect.top changes here, saved and restored around the
    temporary offset -- destRect's left/right/bottom (and viewRect
    entirely) are left exactly as DoPrint already set them for the
    WHOLE job before this ever runs (see its own comment for why,
    including the TECalText that has to go with them). Reassigning the
    full rect here, per page, was the earlier (wrong) shape of this
    function -- see DoPrint's comment for what that broke.

    The SetPort here is a defensive reassertion, not the primary one --
    DoPrint already makes the printer port current once, before the
    per-page loop even starts, so this is redundant but harmless for
    DoPrint's own call sequence; kept only in case something else calls
    this function directly in the future without doing the same setup
    first.
*/
static void DrawPageContent(DocumentPtr doc, TPPrPort prPort, short pageNum)
{
    TEHandle te = doc->activeTE;
    Rect pageRect = (**doc->printRecord).prInfo.rPage;
    short pageStartOffset = doc->pageBreaks.breaks[pageNum - 1];
    short pageStartLine = LineContaining(te, pageStartOffset);
    long pageStartHeight = TEGetHeight(pageStartLine, 0, te);
    short savedDestTop = (**te).destRect.top;

    SetPort((GrafPtr) prPort);

    (**te).destRect.top = (short) (pageRect.top - pageStartHeight);

    TEUpdate(&pageRect, te);

    (**te).destRect.top = savedDestTop;
}

/*
    File > Print -- PRINTING_DESIGN.md §7.1. PrError() checked after
    every Printing Manager call that can fail, including inside the
    page loop's own condition, per §7.1's own explicit reasoning: the
    standard, necessary way to detect a mid-job cancel (Command-period)
    or driver error and stop cleanly rather than continuing to draw
    pages nobody will see. PrClosePage runs unconditionally after
    PrOpenPage regardless of whether PrOpenPage itself succeeded --
    matches the sketch exactly; PrOpenPage/PrClosePage are meant to
    stay paired even on an error, since PrClosePage is what flushes/
    cleans up whatever state PrOpenPage established.

    firstPage is defensively floored at 1 (not in the original sketch,
    which only clamped lastPage) -- cheap insurance against a driver
    somehow handing back an iFstPage below 1, matching this codebase's
    established habit of guarding even paths not known to be reachable
    in practice.

    Several fixes accumulated here across multiple rounds of real-world
    testing (this environment still can't compile or run 68k output,
    so none of these are self-verified beyond careful re-reading
    against the reported symptoms and, where noted, concrete evidence
    either from this codebase's own established patterns or from a
    diagnostic probe that used to live in DrawPageContent, now removed
    -- see its own git history/PR description if this ever needs
    revisiting):

    - THE fix, confirmed by that diagnostic: doc->activeTE's inPort
      field (TERec, defs/TextEdit.yaml) is now set to the printer port
      for the duration of the job (see the comment at the assignment
      itself, below). TERec carries its OWN cached GrafPtr, separate
      from the QuickDraw global "current port" that SetPort changes --
      set once at te's creation and never touched again by anything
      else in this app, since every other TE operation only ever
      needed the window's own port. TEUpdate consults this field
      itself, not just thePort; nothing else in this file ever touched
      it, which is why plain QuickDraw drawing (a diagnostic probe --
      FrameRect, DrawString) printed correctly on the very first try
      while TEUpdate kept drawing nothing through three earlier rounds
      of fixes, none of which happened to touch this field. Those
      earlier fixes (below) were each addressing something real, just
      not sufficient on their own -- left in place, not reverted.
    - Pagination (InvalidatePagination + EnsurePageBreaks) runs AFTER
      PrOpenDoc succeeds, not before -- forces a fresh computation
      against THIS print job's own just-opened prInfo.rPage rather
      than trusting a value established by an earlier, unrelated
      moment.
    - The printer port is made current once, right after PrOpenDoc
      succeeds, before the per-page loop starts -- not per-page inside
      DrawPageContent, which used to only run after PrOpenPage had
      already executed with the window still current. Matches the
      standard documented print-loop pattern: PrOpenPage/PrClosePage/
      PrCloseDoc are all meant to operate with the printer port already
      current, not just the drawing calls in between them.
    - CONCRETE, not just theorized: doc->activeTE's destRect/viewRect
      are switched to the page's own geometry -- and re-wrapped for it
      via TECalText -- ONCE here, before pagination is even computed,
      rather than per-page inside DrawPageContent the way an earlier
      version of this code did. TEUpdate does not itself re-wrap text
      when destRect's width changes; TECalText does, explicitly -- and
      every other place in this codebase that reassigns destRect
      (document.c's SyncDocumentGeometry and ReHouseDocument, for
      window resizing) already pairs it with a TECalText call for
      exactly that reason. DrawPageContent's old per-page version of
      this reassignment never did, leaving TextEdit trying to draw
      using line-break data still computed for the WINDOW's width
      while destRect claimed the PAGE's -- an internally inconsistent
      state, and a real, demonstrable bug independent of the SetPort-
      ordering one above, not merely a timing question this time.
      Computing pagination only ONCE the wrap is correctly set for the
      page (rather than per-page, or before this switch at all) also
      matters: ComputePageBreaks's own nLines/lineStarts/height reads
      have to reflect the SAME wrap DrawPageContent's actual drawing
      uses, or a page's starting character offset won't correspond to
      the Y position it's drawn at.
    - The window's own destRect/viewRect (and wrap) are restored the
      same way -- TECalText included -- once, after the whole job
      finishes, not per-page either. The port is switched back to the
      window BEFORE this restore's TECalText call, not after:
      TECalText's line-height math depends on font metrics from
      whichever port is current, deliberately the printer's for the
      page-width wrap above (print layout should reflect the
      printer's own metrics), but wrong for restoring the on-screen
      wrap, which needs the window's own metrics.
    - SetPort(doc->window) at the very end, unconditionally, restores
      the port back to the window once the whole job (or a cancelled
      dialog) is done -- PrOpenDoc doesn't restore the caller's
      original port on its own the way a modal dialog does.
    - InvalRect(&doc->window->portRect) after that forces a full
      on-screen repaint the next time through the event loop -- a
      direct application of Pascal's own diagnostic finding that
      switching to another app and back (which triggers exactly this
      kind of repaint) fixes on-screen caret/line corruption
      completely. Confirmed scoped ONLY to that on-screen symptom, not
      the printed output itself -- a repaint after the print job has
      already finished can't reach back and change what was actually
      sent to the printer.
    - PrValidate before PrJobDialog -- previously missing here too
      (see EnsurePrintRecord and DoPageSetup's own comments for the
      full reasoning); re-syncs the print record against whichever
      driver is currently selected before using it for anything.
*/
void DoPrint(void)
{
    DocumentPtr doc = FrontDocument();
    TPPrPort prPort;
    short pageNum;

    if (doc == NULL)
        return;

    if (!EnsurePrintRecord(doc))
        return;

    PrOpen();
    PrValidate(doc->printRecord);
    if (!PrJobDialog(doc->printRecord)) {
        PrClose();
        SetPort(doc->window);
        return;
    }

    prPort = PrOpenDoc(doc->printRecord, NULL, NULL);
    if (PrError() == noErr) {
        TEHandle te = doc->activeTE;
        Rect pageRect = (**doc->printRecord).prInfo.rPage;
        Rect savedViewRect = (**te).viewRect;
        Rect savedDestRect = (**te).destRect;
        GrafPtr savedInPort = (**te).inPort;
        short firstPage;
        short lastPage;

        /* Printer port becomes current for the whole PrOpenDoc..
           PrCloseDoc span, starting here -- before the first
           PrOpenPage, not after it (see this function's own doc
           comment for the reasoning). */
        SetPort((GrafPtr) prPort);

        /* THE actual fix, confirmed by the diagnostic probe that used
           to be right here: TERec has its own inPort field (a GrafPtr,
           defs/TextEdit.yaml), separate from the QuickDraw global
           "current port" that SetPort changes -- set once, at te's
           creation (TENew/TEStyleNew, document.c), to whatever port
           was current then (the window), and never touched again by
           anything else in this app, since every other TE operation
           has only ever needed the window's own port anyway. TEUpdate
           consults inPort itself, not just thePort -- documented
           Inside Macintosh guidance for printing from an existing TE
           record is exactly this: point inPort at the target port
           directly, since there's no "TESetPort" call to do it for
           you. This is what plain QuickDraw drawing (FrameRect,
           DrawString -- which only ever cared about thePort) never
           needed and TEUpdate did, and why three previous fixes
           (rPage timing, SetPort ordering, destRect-width/TECalText,
           PrValidate) all made no difference: none of them touched
           this field. Saved/restored the same way viewRect/destRect
           already are, and set before TECalText below for the same
           reason as those: the page-width wrap's own font-metric
           calculations should also reflect the printer's port, not
           just the actual drawing that follows it. */
        (**te).inPort = (GrafPtr) prPort;

        /* Re-wrap to the page's own width before computing page
           breaks or drawing anything -- see this function's own doc
           comment for why both the wrap and the pagination that
           depends on it have to happen here, once, rather than inside
           DrawPageContent per page. */
        (**te).viewRect = pageRect;
        (**te).destRect = pageRect;
        TECalText(te);

        InvalidatePagination(doc);
        EnsurePageBreaks(doc);

        firstPage = (**doc->printRecord).prJob.iFstPage;
        lastPage = (**doc->printRecord).prJob.iLstPage;

        if (firstPage < 1)
            firstPage = 1;
        if (lastPage > doc->pageBreaks.count)
            lastPage = doc->pageBreaks.count;

        for (pageNum = firstPage; pageNum <= lastPage && PrError() == noErr; pageNum++) {
            PrOpenPage(prPort, NULL);
            if (PrError() == noErr)
                DrawPageContent(doc, prPort, pageNum);
            PrClosePage(prPort);
        }
        PrCloseDoc(prPort);

        /* Switch back to the window's own port BEFORE re-wrapping for
           it, not after -- TECalText's line-height/wrap calculations
           depend on font metrics from whichever port is CURRENT. That
           was deliberately the printer's port moments ago (see the
           wrap above), so the print layout reflects the printer's own
           font metrics; it needs to be the window's port again here,
           or this restore would compute the on-screen wrap using
           printer metrics instead of the screen's. inPort is restored
           here too, same reasoning as viewRect/destRect -- this TE
           record goes back to being the window's in every respect. */
        SetPort(doc->window);
        (**te).inPort = savedInPort;
        (**te).viewRect = savedViewRect;
        (**te).destRect = savedDestRect;
        TECalText(te);
    }
    PrClose();
    SetPort(doc->window);
    InvalRect(&doc->window->portRect);
}
