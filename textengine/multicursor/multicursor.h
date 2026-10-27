#pragma once
#include <cstddef>
#include <vector>
#include "../textbuffer/textbuffer.h"

// One caret with an optional selection.
struct Cursor {
    Position pos;          // where the caret is
    Position anchor;       // other end of the selection (same as pos when nothing is selected)
    size_t goalCol = 0;    // remembered byte column (kept for older code; up/down uses the three fields below)

    // Up / Down remember a DISPLAY column, so the caret stays under the same spot across lines with
    // accents, tabs and so on. It is valid only while the caret is still where the last Up / Down put it.
    size_t vertGoal = 0;
    Position vertPos;
    bool vertValid = false;

    bool hasSelection() const { return pos != anchor; }
    Position start() const { return anchor < pos ? anchor : pos; }   // earlier end
    Position end() const { return anchor < pos ? pos : anchor; }     // later end
};

namespace MultiCursor {

// Sorts the cursors by position and merges the ones that overlap or touch.
// `main` is the index of the main cursor; it is updated to follow it.
void normalize(std::vector<Cursor>& cursors, size_t& main);

}
