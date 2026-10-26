#pragma once
#include <cstddef>
#include <vector>
#include "../textbuffer/textbuffer.h"

// One caret with an optional selection.
struct Cursor {
    Position pos;          // where the caret is
    Position anchor;       // other end of the selection (same as pos when nothing is selected)
    size_t goalCol = 0;    // remembered column for up/down movement

    bool hasSelection() const { return pos != anchor; }
    Position start() const { return anchor < pos ? anchor : pos; }   // earlier end
    Position end() const { return anchor < pos ? pos : anchor; }     // later end
};

namespace MultiCursor {

// Sorts the cursors by position and merges the ones that overlap or touch.
// `main` is the index of the main cursor; it is updated to follow it.
void normalize(std::vector<Cursor>& cursors, size_t& main);

}
