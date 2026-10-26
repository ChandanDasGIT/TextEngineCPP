#pragma once
#include <cstddef>
#include <vector>
#include "../textbuffer/textbuffer.h"

namespace WordWrap {

// One row on the screen: characters [start, end) of a logical line.
struct VisualRow {
    size_t line;
    size_t start;
    size_t end;
    bool last;      // true if this is the last row of its line
};

// Splits every line into rows of at most `width` characters, breaking after spaces.
std::vector<VisualRow> layout(const TextBuffer& b, size_t width);

// Text position -> screen row and column (both 0-based, column excludes the gutter).
void toScreen(const std::vector<VisualRow>& rows, Position p, size_t& row, size_t& col);

// Screen row and column -> text position (clamped to a valid one).
Position toPosition(const std::vector<VisualRow>& rows, size_t row, size_t col);

}
