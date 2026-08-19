#include "Finder.r"
#include "Dialogs.r"

resource 'DITL' (130) {
    {
        {80, 204, 100, 284}, Button { enabled, "Save" },
        {80, 114, 100, 194}, Button { enabled, "Cancel" },
        {80, 14, 100, 104}, Button { enabled, "Don't Save" },
        {10, 14, 70, 284}, StaticText { disabled, "Save changes to \"^0\" before continuing?" }
    }
};

resource 'ALRT' (130) {
    {116, 106, 226, 406},
    130,
    {
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1,
        OK, visible, sound1
    },
    alertPositionMainScreen
};

/*
    Preferences window -- PREFERENCES_DESIGN.md sections 6/7. Item
    numbers below (1-based, in listed order) are exactly what
    preferences.c's kPrefs*Item constants expect; if this list is ever
    reordered, those constants have to move with it. Layout is a
    best-effort estimate, not visually verified -- coordinates chosen
    to comfortably fit a 512x342 compact-Mac screen with centerMainScreen
    positioning, but untested; may need real on-screen tuning.

    Column B (Distraction Free / Markdown) is deliberately the same
    left edge (x=275) in both radio-button rows, so the two line up
    vertically -- per explicit request, not just default DITL flow.
    Column widths throughout are generous on purpose: an earlier,
    tighter pass truncated "Windowed" and "Markdown" against actual
    classic system font metrics this doc had no way to measure without
    seeing it rendered.

    Item 10 (Markdown font) is a Button, not an EditText -- its title
    is the currently-selected font name; clicking it pops up a menu of
    actually-installed fonts (DoPreferences, preferences.c) rather than
    asking the user to type/remember one.

    movableDBoxProc: titled ("Preferences"), draggable, no grow box or
    zoom box, no close box (noGoAway) -- Save/Cancel are the only way
    to dismiss it, matching section 6's own Save/Cancel semantics
    rather than adding an ambiguous third way to close the window.
*/
resource 'DITL' (140) {
    {
        {228, 378, 246, 435}, Button { enabled, "Save" },
        {228, 300, 246, 370}, Button { enabled, "Cancel" },

        {12, 16, 30, 170}, StaticText { disabled, "Default window:" },
        {12, 180, 30, 265}, RadioButton { enabled, "Windowed" },
        {12, 275, 30, 435}, RadioButton { enabled, "Distraction Free" },

        {38, 16, 56, 170}, StaticText { disabled, "Default view:" },
        {38, 180, 56, 265}, RadioButton { enabled, "Writer" },
        {38, 275, 56, 435}, RadioButton { enabled, "Markdown" },

        {64, 16, 82, 170}, StaticText { disabled, "Markdown font:" },
        {64, 180, 82, 435}, Button { enabled, "Monaco" },

        {90, 16, 108, 170}, StaticText { disabled, "Font size:" },
        {90, 180, 108, 230}, EditText { enabled, "9" },

        {116, 16, 134, 170}, StaticText { disabled, "Writer zoom (0-4):" },
        {116, 180, 134, 230}, EditText { enabled, "2" },

        {142, 16, 160, 340}, CheckBox { enabled, "Dark mode for Markdown view" },
        {168, 16, 186, 360}, CheckBox { enabled, "Color mode (color Macs only)" },

        {194, 16, 212, 170}, StaticText { disabled, "Line endings:" },
        {194, 180, 212, 240}, RadioButton { enabled, "Mac" },
        {194, 248, 212, 308}, RadioButton { enabled, "Unix" },
        {194, 316, 212, 400}, RadioButton { enabled, "Windows" }
    }
};

resource 'DLOG' (140) {
    {40, 40, 310, 500},
    movableDBoxProc,
    visible,
    noGoAway,
    0,
    140,
    "Preferences",
    centerMainScreen
};

data 'ZLvl' (128) {
    $"0002"
};

data 'ZLvl' (129) {
    $"0002"
};

resource 'DITL' (131) {
    {
        {206, 60, 228, 180}, Button { enabled, "New Document" },
        {206, 200, 228, 320}, Button { enabled, "Open Document" },
        {12, 15, 196, 365}, UserItem { disabled }
    }
};

resource 'DLOG' (131) {
    {61, 66, 301, 446},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    131,
    "",
    noAutoCenter
};

resource 'DITL' (132) {
    {
        {75, 165, 97, 245}, Button { enabled, "OK" },
        {75, 75, 97, 155}, Button { enabled, "Cancel" },
        {15, 20, 33, 300}, StaticText { disabled, "Link URL:" },
        {38, 20, 58, 300}, EditText { enabled, "" }
    }
};

resource 'DLOG' (132) {
    {126, 96, 236, 416},
    dBoxProc,
    visible,
    noGoAway,
    0,
    132,
    "",
    noAutoCenter
};

resource 'DITL' (133) {
    {
        {206, 140, 228, 240}, Button { enabled, "OK" },
        {12, 15, 196, 365}, UserItem { disabled }
    }
};

resource 'DLOG' (133) {
    {61, 66, 301, 446},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    133,
    "",
    noAutoCenter
};

resource 'ICN#' (128) {
    {
        $"00000000000000000000000000000000"
        $"003FFF00004000800180008003FFFFC0"
        $"00200040002FFE8000200080004FFC80"
        $"004001000080010007FFFFE03FFFFFFC"
        $"2E0000742EFFFF743EFFFF7C07FFFFE0"
        $"044444201FFFFFF83FFFFFFC3000000C"
        $"3FFFFFFC1FFFFFF80000000000000000"
        $"00000000000000000000000000000000",
        $"00000000000000000000000000000000"
        $"003FFF00007FFF8001FFFF8003FFFFC0"
        $"003FFFC0003FFF80003FFF80007FFF80"
        $"007FFF0000FFFF000FFFFFF03FFFFFFC"
        $"3FFFFFFC3FFFFFFC3FFFFFFC0FFFFFF0"
        $"0FFFFFF03FFFFFFC3FFFFFFC3FFFFFFC"
        $"3FFFFFFC3FFFFFFC0000000000000000"
        $"00000000000000000000000000000000"
    }
};

resource 'FREF' (128) {
    'APPL', 0, ""
};

resource 'FREF' (129) {
    'TEXT', 0, ""
};

resource 'BNDL' (128) {
    'ArtT', 0,
    {
        'ICN#', {
            0, 128
        },
        'FREF', {
            0, 128,
            1, 129
        }
    }
};
