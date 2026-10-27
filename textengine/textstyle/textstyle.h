#pragma once
#include <string>
#include <vector>
#include "../textbuffer/textbuffer.h"

constexpr int kDefaultFontSize = 12;
constexpr int kMinFontSize = 1;
constexpr int kMaxFontSize = 200;

struct CharStyle {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    int fontSize = kDefaultFontSize;
};

// Stores one CharStyle per character, with the same line layout as TextBuffer.
// It only holds formatting data. It does not know about the cursor or selection.
class TextStyle {
public:
    TextStyle();

    // keep in sync with the text: call these whenever the text changes
    void insert(Position at, const std::string& s, CharStyle style);
    void erase(Position from, Position to);

    // like insert(), but every character gets its own style
    // (styles has one entry per non-newline character of s)
    void insertStyled(Position at, const std::string& s, const std::vector<CharStyle>& styles);

    // the styles of the characters in [from, to); newlines have no entry
    std::vector<CharStyle> slice(Position from, Position to) const;

    CharStyle at(size_t line, size_t col) const;

    // apply formatting to a range (from must be <= to)
    void toggleBold(Position from, Position to);
    void toggleItalic(Position from, Position to);
    void toggleUnderline(Position from, Position to);
    void changeFontSize(Position from, Position to, int delta);

private:
    void toggleFlag(Position from, Position to, bool CharStyle::*flag);

    std::vector<std::vector<CharStyle>> m_lines;
};
