#include "stickyscroll.h"
#include <algorithm>

// ---------------------------------------------------------------- the regions

void StickyScroll::setRegions(std::vector<ScopeRegion> regions) {
    regions.erase(std::remove_if(regions.begin(), regions.end(),
                                 [](const ScopeRegion& r) { return r.end <= r.start; }),   // one-line scopes cannot be pinned
                  regions.end());
    for (ScopeRegion& r : regions)
        if (r.header > r.start) r.header = r.start;
    std::sort(regions.begin(), regions.end(), [](const ScopeRegion& a, const ScopeRegion& b) {
        return a.start != b.start ? a.start < b.start : a.end > b.end;                      // outer scopes first
    });

    m_nodes.clear();
    std::vector<size_t> open;                                // the scopes that contain the current region
    for (const ScopeRegion& r : regions) {
        while (!open.empty() && m_nodes[open.back()].r.end < r.start) open.pop_back();
        size_t parent = kNone, depth = 0;
        if (!open.empty()) {
            const Node& top = m_nodes[open.back()];
            if (top.r.start == r.start) continue;            // two scopes open on the same line: keep the outer one
            if (r.end > top.r.end) continue;                 // overlaps without being inside: skip it
            parent = open.back();
            depth = top.depth + 1;
        }
        m_nodes.push_back(Node{r, parent, depth});
        open.push_back(m_nodes.size() - 1);
    }
}

std::vector<ScopeRegion> StickyScroll::regions() const {
    std::vector<ScopeRegion> out;
    out.reserve(m_nodes.size());
    for (const Node& n : m_nodes) out.push_back(n.r);
    return out;
}

// ---------------------------------------------------------------- which lines to pin

size_t StickyScroll::innermostAt(size_t line) const {
    size_t lo = 0, hi = m_nodes.size();                      // how many scopes start before `line`
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (m_nodes[mid].r.start < line) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return kNone;
    size_t j = lo - 1;                                       // the last one that started before `line`
    while (j != kNone && m_nodes[j].r.end < line) j = m_nodes[j].parent;   // it is over: take the scope around it
    return j;
}

std::vector<StickyLine> StickyScroll::compute(size_t firstLine, double fraction) const {
    std::vector<size_t> rowLines;
    for (size_t i = 0; i <= m_max; ++i) rowLines.push_back(firstLine + i);
    return compute(rowLines, fraction);
}

std::vector<StickyLine> StickyScroll::compute(const std::vector<size_t>& rowLines, double fraction) const {
    std::vector<StickyLine> out;
    if (m_nodes.empty() || m_max == 0 || rowLines.empty()) return out;
    fraction = std::min(1.0, std::max(0.0, fraction));

    // Row d of the screen shows line rowLines[d]. If that line belongs to a scope whose header is
    // above it, row d is covered by that scope's header (the one at depth d).
    size_t prev = kNone;
    for (size_t d = 0; d < m_max && d < rowLines.size(); ++d) {
        size_t c = innermostAt(rowLines[d]);
        if (c == kNone || m_nodes[c].depth < d) break;
        size_t pick = c;
        while (m_nodes[pick].depth > d) pick = m_nodes[pick].parent;
        if (d > 0 && m_nodes[pick].parent != prev) break;
        prev = pick;

        const Node& n = m_nodes[pick];
        StickyLine s;
        s.line = n.r.header;
        s.scopeStart = n.r.start;
        s.scopeEnd = n.r.end;
        s.depth = d;
        // The header cannot go lower than the last line of its scope: it slides up as that line comes near.
        // `before` = how many screen rows come before the scope's last line (folded lines take no row).
        size_t before = 0;
        while (before < rowLines.size() && rowLines[before] < n.r.end) ++before;
        double push = static_cast<double>(d) - static_cast<double>(before) + fraction;
        s.pushUp = std::min(1.0, std::max(0.0, push));
        out.push_back(s);
    }
    return out;
}

size_t StickyScroll::scrollTopToReveal(size_t line) const {
    size_t lowest = line > m_max ? line - m_max : 0;
    for (size_t t = line;; --t) {
        if (t + compute(t).size() <= line) return t;         // the line is not under the pinned rows
        if (t == lowest) return t;
    }
}

size_t StickyScroll::scrollTopToRevealRow(const std::vector<size_t>& rowLines, size_t top, size_t cursorRow,
                                          size_t viewRows) const {
    if (rowLines.empty()) return 0;
    if (top > cursorRow) top = cursorRow;
    size_t most = viewRows > 1 ? viewRows - 1 : 0;           // at least one row of text stays visible
    std::vector<size_t> rows;
    for (;;) {
        rows.clear();                                        // the rows from `top` that decide what is pinned
        for (size_t i = top; i < rowLines.size() && rows.size() <= m_max; ++i) rows.push_back(rowLines[i]);
        size_t pins = std::min(compute(rows).size(), most);
        if (top == 0 || cursorRow >= top + pins) return top;  // the cursor row is below the pinned lines
        --top;
    }
}

// ---------------------------------------------------------------- headers

size_t StickyScroll::headerLineFor(const TextBuffer& b, size_t startLine) {
    if (startLine >= b.lineCount()) return startLine;
    const std::string& s = b.line(startLine);
    size_t a = s.find_first_not_of(" \t\r");
    size_t z = s.find_last_not_of(" \t\r");
    if (a == std::string::npos || a != z || s[a] != '{') return startLine;   // not a line with only "{"

    for (size_t l = startLine; l > 0;) {                     // the nearest line above that is not blank
        --l;
        if (b.line(l).find_first_not_of(" \t\r") != std::string::npos) return l;
    }
    return startLine;
}