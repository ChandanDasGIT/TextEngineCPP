#include "minimap.h"
#include <algorithm>
#include <array>

namespace {
constexpr size_t kKinds = 32;   // token numbers above this are counted as the last one
}

Minimap::Minimap(const MinimapConfig& c) { setConfig(c); }

void Minimap::setConfig(const MinimapConfig& c) {
    MinimapConfig n = c;
    n.width = std::max<size_t>(c.width, 1);
    n.height = std::max<size_t>(c.height, 1);
    n.charsPerPixel = std::max<size_t>(c.charsPerPixel, 1);
    n.tabWidth = std::max<size_t>(c.tabWidth, 1);
    if (n.width == m_cfg.width && n.height == m_cfg.height && n.charsPerPixel == m_cfg.charsPerPixel &&
        n.tabWidth == m_cfg.tabWidth)
        return;                                   // nothing new: keep the picture
    m_cfg = n;
    m_builtRev = 0;
    m_cells.clear();
    m_rows = 0;
    m_lpr = 1;
}

// ---------------------------------------------------------------- the picture

void Minimap::rebuild(const TextBuffer& b, const std::function<unsigned char(size_t, size_t)>& kindAt) {
    if (b.revision() == m_builtRev && b.lineCount() == m_lineCount) return;
    m_lineCount = b.lineCount();
    m_lpr = m_lineCount <= m_cfg.height ? 1 : (m_lineCount + m_cfg.height - 1) / m_cfg.height;
    m_rows = (m_lineCount + m_lpr - 1) / m_lpr;
    m_cells.assign(m_rows * m_cfg.width, MinimapCell{});
    m_lineMarks.resize(m_lineCount, 0);

    struct Tally {
        unsigned ink = 0;                       // characters that are not spaces
        std::array<unsigned, kKinds> kinds{};   // how many of them have each token
    };
    std::vector<Tally> tally(m_cfg.width);

    for (size_t row = 0; row < m_rows; ++row) {
        std::fill(tally.begin(), tally.end(), Tally{});
        size_t first = row * m_lpr;
        size_t last = std::min(first + m_lpr, m_lineCount);

        for (size_t l = first; l < last; ++l) {
            const std::string& s = b.line(l);
            size_t ch = 0;                                  // character number (not byte number)
            for (size_t i = 0; i < s.size(); ++i) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if ((c & 0xC0) == 0x80) continue;           // inside a multi-byte character
                size_t col = ch / m_cfg.charsPerPixel;
                if (col >= m_cfg.width) break;
                if (c == '\t') { ch += m_cfg.tabWidth; continue; }
                if (c != ' ' && c != '\r') {
                    size_t kind = kindAt ? std::min<size_t>(kindAt(l, i), kKinds - 1) : 0;
                    ++tally[col].ink;
                    ++tally[col].kinds[kind];
                }
                ++ch;
            }
        }

        size_t area = (last - first) * m_cfg.charsPerPixel;
        for (size_t col = 0; col < m_cfg.width; ++col) {
            const Tally& t = tally[col];
            if (t.ink == 0) continue;
            size_t best = 0;
            for (size_t k = 0; k < kKinds; ++k)
                if (t.kinds[k] >= t.kinds[best] && t.kinds[k] > 0) best = k;   // on a tie, the coloured one wins
            MinimapCell& cell = m_cells[row * m_cfg.width + col];
            cell.density = static_cast<unsigned char>(std::min<size_t>(255, t.ink * 255 / area));
            cell.kind = static_cast<unsigned char>(best);
        }
    }
    m_builtRev = b.revision();
}

MinimapCell Minimap::cell(size_t row, size_t col) const {
    if (row >= m_rows || col >= m_cfg.width) return MinimapCell{};
    return m_cells[row * m_cfg.width + col];
}

std::vector<unsigned> Minimap::toArgb(const std::function<unsigned(unsigned char)>& colorOf, unsigned background) const {
    const unsigned bg = background & 0xFFFFFFu;
    std::vector<unsigned> out(m_cells.size(), 0xFF000000u | bg);
    for (size_t i = 0; i < m_cells.size(); ++i) {
        const MinimapCell& c = m_cells[i];
        if (c.density == 0) continue;
        const unsigned fg = colorOf(c.kind) & 0xFFFFFFu;
        unsigned rgb = 0;
        for (int shift : {16, 8, 0}) {
            int b = static_cast<int>((bg >> shift) & 0xFF);
            int f = static_cast<int>((fg >> shift) & 0xFF);
            int v = b + (f - b) * c.density / 255;
            rgb |= static_cast<unsigned>(v) << shift;
        }
        out[i] = 0xFF000000u | rgb;
    }
    return out;
}

// ---------------------------------------------------------------- line <-> row

size_t Minimap::rowForLine(size_t line) const {
    if (m_rows == 0) return 0;
    return std::min(line / m_lpr, m_rows - 1);
}

size_t Minimap::firstLineOfRow(size_t row) const {
    if (m_rows == 0) return 0;
    row = std::min(row, m_rows - 1);
    return std::min(row * m_lpr, m_lineCount - 1);
}

// ---------------------------------------------------------------- viewport and mouse

void Minimap::setViewport(size_t firstVisibleLine, size_t visibleLines) {
    m_vFirst = firstVisibleLine;
    m_vVisible = std::max<size_t>(visibleLines, 1);
}

size_t Minimap::viewportTopRow() const { return rowForLine(m_vFirst); }

size_t Minimap::viewportRows() const {
    if (m_rows == 0) return 0;
    size_t first = std::min(m_vFirst, m_lineCount - 1);
    size_t last = std::min(first + m_vVisible, m_lineCount) - 1;
    return rowForLine(last) - rowForLine(first) + 1;
}

bool Minimap::rowInViewport(size_t row) const {
    size_t top = viewportTopRow();
    return row >= top && row < top + viewportRows();
}

size_t Minimap::scrollTopForRow(size_t row, size_t visibleLines) const {
    if (m_rows == 0) return 0;
    visibleLines = std::max<size_t>(visibleLines, 1);
    size_t line = std::min(firstLineOfRow(row) + (m_lpr - 1) / 2, m_lineCount - 1);
    size_t half = visibleLines / 2;
    size_t top = line > half ? line - half : 0;
    size_t maxTop = m_lineCount > visibleLines ? m_lineCount - visibleLines : 0;
    return std::min(top, maxTop);
}

size_t Minimap::scrollTopForDrag(size_t row, size_t grabOffsetRows, size_t visibleLines) const {
    if (m_rows == 0) return 0;
    visibleLines = std::max<size_t>(visibleLines, 1);
    row = std::min(row, m_rows - 1);
    size_t topRow = row > grabOffsetRows ? row - grabOffsetRows : 0;
    size_t maxTop = m_lineCount > visibleLines ? m_lineCount - visibleLines : 0;
    return std::min(firstLineOfRow(topRow), maxTop);
}

// ---------------------------------------------------------------- marks

void Minimap::clearMarks(unsigned char bits) {
    for (unsigned char& m : m_lineMarks) m = static_cast<unsigned char>(m & ~bits);
}

void Minimap::markLine(size_t line, unsigned char bits) {
    if (line < m_lineMarks.size()) m_lineMarks[line] = static_cast<unsigned char>(m_lineMarks[line] | bits);
}

void Minimap::setCursorMarks(const std::vector<Cursor>& cursors) {
    clearMarks(MarkCursor | MarkSelection);
    for (const Cursor& c : cursors) {
        markLine(c.pos.line, MarkCursor);
        if (!c.hasSelection()) continue;
        size_t a = c.start().line;
        size_t z = c.end().line;
        if (z > a && c.end().col == 0) --z;                 // a selection that ends at the start of a line
        for (size_t l = a; l <= z; ++l) markLine(l, MarkSelection);
    }
}

void Minimap::setFindMarks(const std::vector<Range>& matches, int currentIndex) {
    clearMarks(MarkFind | MarkFindCurrent);
    for (size_t i = 0; i < matches.size(); ++i) {
        unsigned char bits = MarkFind;
        if (static_cast<int>(i) == currentIndex) bits = static_cast<unsigned char>(bits | MarkFindCurrent);
        markLine(matches[i].from.line, bits);
    }
}

void Minimap::setChangeMarks(const std::function<bool(size_t)>& unsaved, const std::function<bool(size_t)>& savedChange) {
    clearMarks(MarkChangedUnsaved | MarkChangedSaved);
    for (size_t l = 0; l < m_lineMarks.size(); ++l) {
        if (unsaved && unsaved(l)) markLine(l, MarkChangedUnsaved);                 // red wins over green
        else if (savedChange && savedChange(l)) markLine(l, MarkChangedSaved);
    }
}

unsigned char Minimap::marks(size_t row) const {
    if (row >= m_rows) return 0;
    size_t first = row * m_lpr;
    size_t last = std::min(first + m_lpr, m_lineMarks.size());
    unsigned char m = 0;
    for (size_t l = first; l < last; ++l) m = static_cast<unsigned char>(m | m_lineMarks[l]);
    return m;
}
