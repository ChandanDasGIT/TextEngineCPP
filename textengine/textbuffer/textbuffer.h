#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct Position {
    size_t line = 0;
    size_t col = 0;
};

inline bool operator==(Position a, Position b) { return a.line == b.line && a.col == b.col; }
inline bool operator!=(Position a, Position b) { return !(a == b); }
inline bool operator<(Position a, Position b) {
    return a.line != b.line ? a.line < b.line : a.col < b.col;
}

// Stores the text as a list of lines. Knows nothing about cursor or selection.

// UTF-8 helpers. A Position's column is a BYTE offset into the line, and it always sits on the start of a
// character (never inside a multi-byte one). These helpers find those boundaries.
namespace Utf8 {

    inline bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

    // True if `col` is the start of a character (or the end of the string).
    inline bool isBoundary(const std::string& s, size_t col) {
        return col >= s.size() || !isContinuation(s[col]);
    }

    // The start of the character that ends at (or contains) col-1. 0 stays 0.
    inline size_t prevBoundary(const std::string& s, size_t col) {
        if (col > s.size()) col = s.size();
        if (col == 0) return 0;
        --col;
        while (col > 0 && isContinuation(s[col])) --col;
        return col;
    }

    // The start of the next character after col. s.size() stays s.size().
    inline size_t nextBoundary(const std::string& s, size_t col) {
        if (col >= s.size()) return s.size();
        ++col;
        while (col < s.size() && isContinuation(s[col])) ++col;
        return col;
    }

    // col moved back to the nearest boundary (no change if it is already on one).
    inline size_t snapBack(const std::string& s, size_t col) {
        if (col > s.size()) return s.size();
        while (col > 0 && isContinuation(s[col])) --col;
        return col;
    }

    // Display column of a byte offset: every character is one column, a tab goes to the next multiple of tabWidth.
    // (The engine does not know how wide a front end draws a CJK character, so it counts that as one column.)
    inline size_t columnOf(const std::string& s, size_t col, size_t tabWidth) {
        if (col > s.size()) col = s.size();
        size_t c = 0;
        for (size_t i = 0; i < col; i = nextBoundary(s, i)) c += s[i] == '\t' ? tabWidth - c % tabWidth : 1;
        return c;
    }
    // The byte offset that sits under a display column (the start of a tab if the column is inside it);
    // the end of the line if the line is shorter.
    inline size_t byteAtColumn(const std::string& s, size_t column, size_t tabWidth) {
        size_t i = 0, c = 0;
        while (i < s.size()) {
            size_t w = s[i] == '\t' ? tabWidth - c % tabWidth : 1;
            if (c + w > column) return i;
            c += w;
            i = nextBoundary(s, i);
        }
        return s.size();
    }

    // Number of characters (code points) in s.
    inline size_t charCount(const std::string& s) {
    size_t n = 0;
    for (char c : s) {
        if (!isContinuation(c)) ++n;
    }
    return n;
}

}

class TextBuffer {
public:
    TextBuffer();

    size_t lineCount() const;
    size_t lineLength(size_t line) const;
    const std::string& line(size_t index) const;
    std::string text() const;

    Position clamp(Position p) const;   // forces p to be a valid position (inside the text, on a character boundary)

    // One character left / right of p, moving across line breaks. Stay put at the start / end of the text.
    Position stepLeft(Position p) const;
    Position stepRight(Position p) const;
    Position endPosition() const;       // position after the last character

    size_t length() const;                     // total characters, each '\n' counts as one
    size_t offsetOf(Position p) const;         // position -> character offset
    Position positionOf(size_t offset) const;  // character offset -> position (clamped)

    std::string textInRange(Position from, Position to) const;
    Position insert(Position at, const std::string& s);  // returns position after inserted text
    void erase(Position from, Position to);

    // A number that changes whenever the text changes (insert, erase, or a new buffer). Two buffers, or one buffer
    // at two times, never share a number unless their text is the same, so a front end can remember the last
    // value it drew and redraw only when it differs. It is only for comparing: do not rely on its size or order.
    unsigned long long revision() const { return m_revision; }

private:
    std::vector<std::string> m_lines;
    unsigned long long m_revision;
};
