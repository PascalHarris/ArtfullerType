#include "app.h"

DocumentRecord gDocuments[MAX_DOCUMENTS];

/*
    Deviation from MULTI_WINDOW_DESIGN.md's original sketch: the design
    doc suggested SetWRefCon/GetWRefCon for O(1) window-to-document
    lookup. A linear scan over at most MAX_DOCUMENTS (4) slots is
    simpler, needs no extra per-window bookkeeping to keep in sync, and
    the cost difference is meaningless at this size -- so DocumentForWindow
    just scans gDocuments directly instead. Worth revisiting only if
    MAX_DOCUMENTS grows enough for that to matter, which seems unlikely
    given the memory-budget concerns in MULTI_WINDOW_DESIGN.md §9.
*/
DocumentPtr DocumentForWindow(WindowPtr w)
{
    short i;

    if (w == NULL)
        return NULL;

    for (i = 0; i < MAX_DOCUMENTS; i++) {
        if (gDocuments[i].inUse && gDocuments[i].window == w)
            return &gDocuments[i];
    }
    return NULL;
}

DocumentPtr FrontDocument(void)
{
    return DocumentForWindow(FrontWindow());
}
