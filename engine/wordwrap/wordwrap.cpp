#include "wordwrap.h"
#include <algorithm>

std::vector<WordWrap::VisualRow> WordWrap::layout(const TextBuffer& b, size_t width) {
    if (width < 1) width = 1;
    std::vector<VisualRow> rows;
    for (size_t l = 0; l < b.lineCount(); ++l) {
        const std::string& s = b.line(l);
        size_t start = 0;
        while (true) {
            if (s.size() - start <= width) {
                rows.push_back({l, start, s.size(), true});
                break;
            }
            size_t end = start + width;                 // no space found: hard break
            for (size_t k = start + width; k > start; --k) {
                if (s[k - 1] == ' ') { end = k; break; }
            }
            rows.push_back({l, start, end, false});
            start = end;
        }
    }
    return rows;
}

void WordWrap::toScreen(const std::vector<VisualRow>& rows, Position p, size_t& row, size_t& col) {
    for (size_t i = 0; i < rows.size(); ++i) {
        const VisualRow& r = rows[i];
        if (r.line == p.line && p.col >= r.start && (p.col < r.end || r.last)) {
            row = i;
            col = p.col - r.start;
            return;
        }
    }
    row = 0;
    col = 0;
}

Position WordWrap::toPosition(const std::vector<VisualRow>& rows, size_t row, size_t col) {
    const VisualRow& r = rows[std::min(row, rows.size() - 1)];
    size_t len = r.end - r.start;
    size_t maxCol = r.last ? len : (len > 0 ? len - 1 : 0);   // end of a wrapped row belongs to the next row
    return {r.line, r.start + std::min(col, maxCol)};
}
