#include "wordwrap.h"
#include <algorithm>

std::vector<WordWrap::VisualRow> WordWrap::layout(const TextBuffer& b, size_t width, size_t tabWidth) {
    if (width < 1) width = 1;
    if (tabWidth < 1) tabWidth = 1;
    std::vector<VisualRow> rows;
    for (size_t l = 0; l < b.lineCount(); ++l) {
        const std::string& s = b.line(l);
        size_t start = 0;
        while (true) {
            // walk forward from start while the characters fit into `width` columns (a row always takes at least one)
            size_t limit = start, col = 0;
            while (limit < s.size()) {
                size_t w = s[limit] == '\t' ? tabWidth - col % tabWidth : 1;
                if (col + w > width && limit > start) break;
                col += w;
                limit = Utf8::nextBoundary(s, limit);
            }
            if (limit >= s.size()) {                    // the rest of the line fits
                rows.push_back({l, start, s.size(), true, s.size()});
                break;
            }
            size_t end = limit;                         // no space found: hard break after `width` characters
            for (size_t k = limit; k > start; k = Utf8::prevBoundary(s, k)) {
                if (s[k - 1] == ' ') { end = k; break; }   // a space is one byte, so k stays on a boundary
            }
            rows.push_back({l, start, end, false, Utf8::prevBoundary(s, end)});
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
    return {r.line, std::min(r.start + col, r.maxCaret)};   // col is a byte offset on a character boundary
}