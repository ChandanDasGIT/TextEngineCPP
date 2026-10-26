#include "gutterstyle.h"
#include <algorithm>
#include "../gutter/gutter.h"

namespace {

std::vector<std::string> linesOf(const TextBuffer& b) {
    std::vector<std::string> v;
    v.reserve(b.lineCount());
    for (size_t i = 0; i < b.lineCount(); ++i) v.push_back(b.line(i));
    return v;
}

// For every line of `cur`: true if it also exists (in order) in `base`.
std::vector<bool> matchedLines(const std::vector<std::string>& base, const std::vector<std::string>& cur) {
    std::vector<bool> same(cur.size(), false);

    size_t pre = 0;
    while (pre < base.size() && pre < cur.size() && base[pre] == cur[pre]) { same[pre] = true; ++pre; }

    size_t suf = 0;
    while (suf < base.size() - pre && suf < cur.size() - pre &&
           base[base.size() - 1 - suf] == cur[cur.size() - 1 - suf]) {
        same[cur.size() - 1 - suf] = true;
        ++suf;
    }

    size_t n = base.size() - pre - suf;
    size_t m = cur.size() - pre - suf;
    if (n == 0 || m == 0) return same;
    if (n * m > 4000000) return same;      // too big to compare: the middle counts as changed

    std::vector<unsigned> t((n + 1) * (m + 1), 0);
    auto at = [&](size_t i, size_t j) -> unsigned& { return t[i * (m + 1) + j]; };
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            at(i, j) = base[pre + i] == cur[pre + j] ? at(i + 1, j + 1) + 1
                                                     : std::max(at(i + 1, j), at(i, j + 1));

    size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (base[pre + i] == cur[pre + j]) { same[pre + j] = true; ++i; ++j; }
        else if (at(i + 1, j) >= at(i, j + 1)) ++i;
        else ++j;
    }
    return same;
}

}

void GutterStyle::reset(const TextBuffer& b) {
    m_original = linesOf(b);
    m_saved = m_original;
}

void GutterStyle::markSaved(const TextBuffer& b) { m_saved = linesOf(b); }

void GutterStyle::update(const TextBuffer& b) {
    std::vector<std::string> cur = linesOf(b);
    std::vector<bool> sameSaved = matchedLines(m_saved, cur);
    std::vector<bool> sameOrig = matchedLines(m_original, cur);

    m_marks.assign(cur.size(), Mark::Untouched);
    for (size_t i = 0; i < cur.size(); ++i) {
        if (!sameSaved[i]) m_marks[i] = Mark::Unsaved;
        else if (!sameOrig[i]) m_marks[i] = Mark::Saved;
    }
}

std::string GutterStyle::draw(size_t line, bool continuation, bool hasCursor, size_t lineCount, FoldMark fold) const {
    std::string s = hasCursor ? "\x1b[0m\x1b[1;97m" : "\x1b[0m\x1b[90m";

    if (!continuation) {
        std::string label = Gutter::label(line, lineCount);       // e.g. " 12 │"
        s += label.substr(0, label.size() - 4);                   // " 12": without the last space and the bar (3 bytes)
    } else {
        s += std::string(static_cast<size_t>(Gutter::width(lineCount) - 2), ' ');
    }

    // fold marker: the column between the number and the bar
    if (fold == FoldMark::Folded) s += "\x1b[0m\x1b[1;36m▸";
    else if (fold == FoldMark::Expanded) s += hasCursor ? "\x1b[0m\x1b[1;97m▾" : "\x1b[0m\x1b[90m▾";
    else s += " ";

        Mark m = line < m_marks.size() ? m_marks[line] : Mark::Untouched;
    if (m == Mark::Unsaved)
        s += hasCursor ? "\x1b[0m\x1b[1;91m" : "\x1b[0m\x1b[38;5;124m";   // bright red / dim red
    else if (m == Mark::Saved)
        s += hasCursor ? "\x1b[0m\x1b[1;92m" : "\x1b[0m\x1b[38;5;28m";    // bright green / dim green
    else
        s += hasCursor ? "\x1b[0m\x1b[1;97m" : "\x1b[0m\x1b[90m";         // white / gray
    s += "│\x1b[0m";
    return s;
}
