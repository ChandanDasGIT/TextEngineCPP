#pragma once
#include <cstddef>
#include <vector>
#include "../textbuffer/textbuffer.h"

namespace WordWrap {

// One row on the screen: bytes [start, end) of a logical line. start and end always sit on a UTF-8
// character boundary, so a row never begins or ends in the middle of a character.
struct VisualRow {
    size_t line;
    size_t start;
    size_t end;
    bool last;       // true if this is the last row of its line
    size_t maxCaret; // the furthest byte offset (in the line) a cursor can have on this row:
                     // `end` on the last row; the start of the last character on a wrapped row
                     // (the position at the end of a wrapped row belongs to the next row)
};

// Splits every line into rows of at most `width` columns, breaking after spaces. A column is one character
// (code point, not byte); a tab counts up to the next multiple of `tabWidth`, counted from the start of the row.
// A wide character (CJK) still counts as one column: the engine does not know how wide the front end draws it.
std::vector<VisualRow> layout(const TextBuffer& b, size_t width, size_t tabWidth = 4);

// Text position -> row and column. 0-based; the column is a BYTE offset from the start of the row
// (always on a character boundary). A front end that counts screen cells converts it itself.
void toScreen(const std::vector<VisualRow>& rows, Position p, size_t& row, size_t& col);

// Row and column (a byte offset from the start of the row, on a character boundary) -> text position, clamped to a valid one.
Position toPosition(const std::vector<VisualRow>& rows, size_t row, size_t col);

}
