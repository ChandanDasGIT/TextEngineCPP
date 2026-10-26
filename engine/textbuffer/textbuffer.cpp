#include "textbuffer.h"
#include <algorithm>

TextBuffer::TextBuffer() : m_lines(1) {}

size_t TextBuffer::lineCount() const { return m_lines.size(); }

size_t TextBuffer::lineLength(size_t line) const {
    return line < m_lines.size() ? m_lines[line].size() : 0;
}

const std::string& TextBuffer::line(size_t index) const { return m_lines[index]; }

std::string TextBuffer::text() const {
    std::string out;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (i) out += '\n';
        out += m_lines[i];
    }
    return out;
}

Position TextBuffer::clamp(Position p) const {
    p.line = std::min(p.line, m_lines.size() - 1);
    p.col = std::min(p.col, m_lines[p.line].size());
    return p;
}

Position TextBuffer::endPosition() const {
    return {m_lines.size() - 1, m_lines.back().size()};
}

size_t TextBuffer::length() const {
    size_t n = m_lines.size() - 1;
    for (const std::string& l : m_lines) n += l.size();
    return n;
}

size_t TextBuffer::offsetOf(Position p) const {
    p = clamp(p);
    size_t off = 0;
    for (size_t l = 0; l < p.line; ++l) off += m_lines[l].size() + 1;
    return off + p.col;
}

Position TextBuffer::positionOf(size_t offset) const {
    for (size_t l = 0; l < m_lines.size(); ++l) {
        if (offset <= m_lines[l].size()) return {l, offset};
        offset -= m_lines[l].size() + 1;
    }
    return endPosition();
}

std::string TextBuffer::textInRange(Position from, Position to) const {
    from = clamp(from);
    to = clamp(to);
    if (to < from) std::swap(from, to);
    if (from == to) return "";

    if (from.line == to.line)
        return m_lines[from.line].substr(from.col, to.col - from.col);

    std::string out = m_lines[from.line].substr(from.col);
    for (size_t l = from.line + 1; l < to.line; ++l) {
        out += '\n';
        out += m_lines[l];
    }
    out += '\n';
    out += m_lines[to.line].substr(0, to.col);
    return out;
}

Position TextBuffer::insert(Position at, const std::string& s) {
    at = clamp(at);

    // split s into lines (ignore '\r')
    std::vector<std::string> parts(1);
    for (char c : s) {
        if (c == '\r') continue;
        if (c == '\n') parts.emplace_back();
        else parts.back() += c;
    }

    std::string tail = m_lines[at.line].substr(at.col);
    m_lines[at.line].erase(at.col);
    m_lines[at.line] += parts[0];

    if (parts.size() == 1) {
        Position end{at.line, m_lines[at.line].size()};
        m_lines[at.line] += tail;
        return end;
    }

    m_lines.insert(m_lines.begin() + at.line + 1, parts.begin() + 1, parts.end());
    size_t last = at.line + parts.size() - 1;
    Position end{last, m_lines[last].size()};
    m_lines[last] += tail;
    return end;
}

void TextBuffer::erase(Position from, Position to) {
    from = clamp(from);
    to = clamp(to);
    if (to < from) std::swap(from, to);
    if (from == to) return;

    if (from.line == to.line) {
        m_lines[from.line].erase(from.col, to.col - from.col);
        return;
    }

    m_lines[from.line].erase(from.col);
    m_lines[from.line] += m_lines[to.line].substr(to.col);
    m_lines.erase(m_lines.begin() + from.line + 1, m_lines.begin() + to.line + 1);
}
