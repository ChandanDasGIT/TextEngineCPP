#include "textstyle.h"
#include <algorithm>
#include <utility>

namespace {
template <typename Fn>
void forEachInRange(std::vector<std::vector<CharStyle>>& lines, Position from, Position to, Fn fn) {
    for (size_t l = from.line; l <= to.line && l < lines.size(); ++l) {
        size_t start = (l == from.line) ? from.col : 0;
        size_t end = (l == to.line) ? to.col : lines[l].size();
        end = std::min(end, lines[l].size());
        for (size_t c = start; c < end; ++c) fn(lines[l][c]);
    }
}
}

TextStyle::TextStyle() : m_lines(1) {}

CharStyle TextStyle::at(size_t line, size_t col) const {
    if (line < m_lines.size() && col < m_lines[line].size()) return m_lines[line][col];
    return CharStyle{};
}

void TextStyle::insert(Position at, const std::string& s, CharStyle style) {
    // same splitting rules as TextBuffer::insert ('\r' ignored, '\n' starts a new line)
    std::vector<size_t> counts(1, 0);
    for (char c : s) {
        if (c == '\r') continue;
        if (c == '\n') counts.push_back(0);
        else counts.back()++;
    }

    auto& first = m_lines[at.line];
    std::vector<CharStyle> tail(first.begin() + at.col, first.end());
    first.resize(at.col);
    first.insert(first.end(), counts[0], style);

    if (counts.size() == 1) {
        first.insert(first.end(), tail.begin(), tail.end());
        return;
    }

    std::vector<std::vector<CharStyle>> newLines;
    for (size_t k = 1; k < counts.size(); ++k) newLines.emplace_back(counts[k], style);
    newLines.back().insert(newLines.back().end(), tail.begin(), tail.end());
    m_lines.insert(m_lines.begin() + at.line + 1, newLines.begin(), newLines.end());
}

void TextStyle::insertStyled(Position at, const std::string& s, const std::vector<CharStyle>& styles) {
    insert(at, s, CharStyle{});   // makes room, then every inserted character gets its own style
    size_t line = at.line, col = at.col, k = 0;
    for (char ch : s) {
        if (ch == '\r') continue;
        if (ch == '\n') { ++line; col = 0; continue; }
        if (k < styles.size()) m_lines[line][col] = styles[k];
        ++k;
        ++col;
    }
}

std::vector<CharStyle> TextStyle::slice(Position from, Position to) const {
    std::vector<CharStyle> out;
    if (to < from) std::swap(from, to);
    for (size_t l = from.line; l <= to.line && l < m_lines.size(); ++l) {
        size_t start = (l == from.line) ? from.col : 0;
        size_t end = (l == to.line) ? to.col : m_lines[l].size();
        end = std::min(end, m_lines[l].size());
        for (size_t c = start; c < end; ++c) out.push_back(m_lines[l][c]);
    }
    return out;
}

void TextStyle::erase(Position from, Position to) {
    if (to < from) std::swap(from, to);
    if (from == to) return;

    if (from.line == to.line) {
        auto& l = m_lines[from.line];
        l.erase(l.begin() + from.col, l.begin() + to.col);
        return;
    }

    auto& a = m_lines[from.line];
    a.erase(a.begin() + from.col, a.end());
    auto& b = m_lines[to.line];
    a.insert(a.end(), b.begin() + to.col, b.end());
    m_lines.erase(m_lines.begin() + from.line + 1, m_lines.begin() + to.line + 1);
}

// If every selected character already has the flag, turn it off. Otherwise turn it on for all.
void TextStyle::toggleFlag(Position from, Position to, bool CharStyle::*flag) {
    bool allOn = true;
    forEachInRange(m_lines, from, to, [&](CharStyle& s) { if (!(s.*flag)) allOn = false; });
    forEachInRange(m_lines, from, to, [&](CharStyle& s) { s.*flag = !allOn; });
}

void TextStyle::toggleBold(Position from, Position to)      { toggleFlag(from, to, &CharStyle::bold); }
void TextStyle::toggleItalic(Position from, Position to)    { toggleFlag(from, to, &CharStyle::italic); }
void TextStyle::toggleUnderline(Position from, Position to) { toggleFlag(from, to, &CharStyle::underline); }

void TextStyle::changeFontSize(Position from, Position to, int delta) {
    forEachInRange(m_lines, from, to, [&](CharStyle& s) {
        s.fontSize = std::clamp(s.fontSize + delta, kMinFontSize, kMaxFontSize);
    });
}
