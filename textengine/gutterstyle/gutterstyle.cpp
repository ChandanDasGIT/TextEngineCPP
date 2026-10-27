#include "gutterstyle.h"
#include <algorithm>

namespace {

std::vector<std::string> linesOf(const TextBuffer& b) {
    std::vector<std::string> v;
    v.reserve(b.lineCount());
    for (size_t i = 0; i < b.lineCount(); ++i) v.push_back(b.line(i));
    return v;
}

// For every line of `cur`: true if it also exists (in order) in `base`.
std::vector<bool> matchedLines(const std::vector<std::string>& base, const TextBuffer& cur) {
    const size_t curSize = cur.lineCount();
    std::vector<bool> same(curSize, false);

    size_t pre = 0;
    while (pre < base.size() && pre < curSize && base[pre] == cur.line(pre)) { same[pre] = true; ++pre; }

    size_t suf = 0;
    while (suf < base.size() - pre && suf < curSize - pre &&
           base[base.size() - 1 - suf] == cur.line(curSize - 1 - suf)) {
        same[curSize - 1 - suf] = true;
        ++suf;
    }

    size_t n = base.size() - pre - suf;
    size_t m = curSize - pre - suf;
    if (n == 0 || m == 0) return same;
    if (n * m > 4000000) return same;      // too big to compare: the middle counts as changed

    std::vector<unsigned> t((n + 1) * (m + 1), 0);
    auto at = [&](size_t i, size_t j) -> unsigned& { return t[i * (m + 1) + j]; };
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            at(i, j) = base[pre + i] == cur.line(pre + j) ? at(i + 1, j + 1) + 1
                                                     : std::max(at(i + 1, j), at(i, j + 1));

    size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (base[pre + i] == cur.line(pre + j)) { same[pre + j] = true; ++i; ++j; }
        else if (at(i + 1, j) >= at(i, j + 1)) ++i;
        else ++j;
    }
    return same;
}

}

void GutterStyle::reset(const TextBuffer& b) {
    m_original = linesOf(b);
    m_saved = m_original;
    m_rev = 0;                                   // the marks must be worked out again
}

void GutterStyle::markSaved(const TextBuffer& b) {
    m_saved = linesOf(b);
    m_rev = 0;
}

void GutterStyle::update(const TextBuffer& b) {
    if (b.revision() == m_rev) return;            // the text is the one the marks were worked out for
    std::vector<bool> sameSaved = matchedLines(m_saved, b);
    std::vector<bool> sameOrig = matchedLines(m_original, b);

    m_marks.assign(b.lineCount(), ChangeMark::Untouched);
    for (size_t i = 0; i < b.lineCount(); ++i) {
        if (!sameSaved[i]) m_marks[i] = ChangeMark::Unsaved;
        else if (!sameOrig[i]) m_marks[i] = ChangeMark::Saved;
    }
    m_rev = b.revision();
}
