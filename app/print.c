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
    only this file's own Pr* entry points (DoPageSetup now, DoPrint
    from Milestone P3 on) need it directly.
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
    File > Page Setup. PRINTING_DESIGN.md §3's sketch also invalidates
    this document's cached pagination on a successful Page Setup
    (InvalidatePagination(doc)) so Page View and the next print job
    both recompute page breaks against the new geometry -- deliberately
    left out here: pagination doesn't exist yet (pageBreaks/
    pageBreaksValid land on DocumentRecord in Milestone P2, along with
    InvalidatePagination itself), and this milestone's own stated scope
    is "no ... pagination ... yet." Calling it now would be a forward
    reference to a function this milestone never defines. Milestone
    P2's own prompt already calls for wiring InvalidatePagination into
    every existing call site that needs it, including this one -- add
    the call here then, not before.
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
    result = PrStlDialog(doc->printRecord);
    PrClose();

    return result;
}

/*
    File > Print -- not implemented until Milestone P3 (the print job
    loop, PRINTING_DESIGN.md §7). Declared and wired into the File menu
    now (Milestone P1) so the menu string and item numbering are right
    from the start and don't need touching again in P3; this stub is
    what P3 replaces with the real PrJobDialog/PrOpenDoc/.../PrCloseDoc
    loop.
*/
void DoPrint(void)
{
}
