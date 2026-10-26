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
class TextBuffer {
public:
    TextBuffer();

    size_t lineCount() const;
    size_t lineLength(size_t line) const;
    const std::string& line(size_t index) const;
    std::string text() const;

    Position clamp(Position p) const;   // forces p to be a valid position
    Position endPosition() const;       // position after the last character

    size_t length() const;                     // total characters, each '\n' counts as one
    size_t offsetOf(Position p) const;         // position -> character offset
    Position positionOf(size_t offset) const;  // character offset -> position (clamped)

    std::string textInRange(Position from, Position to) const;
    Position insert(Position at, const std::string& s);  // returns position after inserted text
    void erase(Position from, Position to);

private:
    std::vector<std::string> m_lines;
};
