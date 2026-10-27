#include "tabbar.h"
#include <algorithm>

namespace {

size_t displayCols(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// shortens a string to at most maxCols columns, ending with "…"
std::string clipCols(const std::string& s, size_t maxCols) {
    if (displayCols(s) <= maxCols) return s;
    std::string out;
    size_t cols = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (cols + 1 >= maxCols) break;                    // keep one column for the "…"
        out.append(s, i, len);
        i += len;
        ++cols;
    }
    return out + "…";
}

}

// ---------------------------------------------------------------- the strip

// Cell layout: " title " + (● or space) + "×" + separator  ->  title width + 5 columns.
const TabBarLayout& TabBar::layout(TabManager& tabs, int cols) {
    const int kArrowW = 3, kPlusW = 3;
    TabBarLayout& L = m_layout;
    L = TabBarLayout();
    L.cols = cols;

    size_t n = tabs.count();
    size_t current = tabs.currentIndex();
    if (tabs.changeCount() != m_seenChanges) m_follow = true;    // the tabs changed: show the current one
    m_seenChanges = tabs.changeCount();

    int plusW = cols >= 12 ? kPlusW : 0;
    int avail = cols - plusW;
    int maxTitle = std::max(1, std::min(18, cols - plusW - 2 * kArrowW - 9));

    std::vector<std::string> titles(n);
    std::vector<int> widths(n);
    for (size_t i = 0; i < n; ++i) {
        titles[i] = clipCols(tabs.title(i), static_cast<size_t>(maxTitle));
        widths[i] = static_cast<int>(displayCols(titles[i])) + 9;   // 2 pad + title + 1 gap + 2 dot + 3 button + 1 separator
    }

    auto fitCount = [&](size_t first, int room) {
        size_t k = first;
        int used = 0;
        while (k < n && used + widths[k] <= room) { used += widths[k]; ++k; }
        return k - first;
    };
    // how many tabs are shown when the strip starts at `first` (always at least one)
    auto visibleCount = [&](size_t first) {
        int room = avail - (first > 0 ? kArrowW : 0);
        size_t c = fitCount(first, room);
        if (first + c < n) c = fitCount(first, room - kArrowW);     // a right arrow takes room too
        return std::max<size_t>(c, 1);
    };

    if (m_scroll >= n) m_scroll = n - 1;
    if (m_follow) {
        if (current < m_scroll) m_scroll = current;
        while (m_scroll < current && current >= m_scroll + visibleCount(m_scroll)) ++m_scroll;
    }
    while (m_scroll > 0 && (m_scroll - 1) + visibleCount(m_scroll - 1) >= n) --m_scroll;   // no empty space on the right

    size_t first = m_scroll;
    size_t c = visibleCount(first);
    int x = 0;
    if (first > 0) {
        L.leftArrow = true;
        L.leftX = 0;
        L.leftW = kArrowW;
        x = kArrowW;
    }
    for (size_t k = first; k < first + c && k < n; ++k) {
        TabCell cell;
        cell.index = k;
        cell.x = x;
        cell.width = widths[k];
        cell.title = titles[k];
        cell.active = k == current;
        cell.modified = tabs.modified(k);
        L.cells.push_back(cell);
        x += widths[k];
    }
    if (first + c < n) {
        L.rightArrow = true;
        L.rightW = kArrowW;
        L.rightX = avail - kArrowW;
    }
    L.plusW = plusW;
    L.plusX = plusW ? cols - plusW : -1;
    return L;
}

TabHit TabBar::hit(TabManager& tabs, int x, int cols) {
    const TabBarLayout& L = layout(tabs, cols);
    TabHit h;
    if (L.leftArrow && x >= L.leftX && x < L.leftX + L.leftW) { h.kind = TabHit::ArrowLeft; return h; }
    if (L.rightArrow && x >= L.rightX && x < L.rightX + L.rightW) { h.kind = TabHit::ArrowRight; return h; }
    if (L.plusW > 0 && x >= L.plusX && x < L.plusX + L.plusW) { h.kind = TabHit::Plus; return h; }
    for (const TabCell& c : L.cells) {
        if (x < c.x || x >= c.x + c.width) continue;
        int off = x - c.x;
        h.index = c.index;
        h.kind = (off >= c.width - 3 && off <= c.width - 2) ? TabHit::Close : TabHit::Tab;   // the ● / × columns
        return h;
    }
    return h;
}

void TabBar::scrollBar(TabManager& tabs, int delta) {
    long long t = static_cast<long long>(m_scroll) + delta;
    long long hi = static_cast<long long>(tabs.count()) - 1;
    m_scroll = static_cast<size_t>(std::max(0LL, std::min(t, hi)));
    m_seenChanges = tabs.changeCount();    // tab changes before this scroll are already dealt with
    m_follow = false;                      // stay where the user scrolled until the next tab change
}

// ---------------------------- drag and drop

size_t TabBar::draggedIndex(const TabManager& tabs) const {
    for (size_t i = 0; i < tabs.count(); ++i)
        if (tabs.at(i).id == m_dragId) return i;
    return tabs.count();
}

bool TabBar::beginDrag(TabManager& tabs, int x, int cols) {
    m_dragId = 0;
    const TabBarLayout& L = layout(tabs, cols);
    for (const TabCell& c : L.cells) {
        if (x < c.x || x >= c.x + c.width) continue;
        m_dragId = tabs.at(c.index).id;
        m_dragGrab = x - c.x;
        return true;
    }
    return false;
}

bool TabBar::dragTo(TabManager& tabs, int x, int cols) {
    size_t from = draggedIndex(tabs);
    if (m_dragId == 0 || from >= tabs.count()) { m_dragId = 0; return false; }
    const TabBarLayout& L = layout(tabs, cols);

    int width = 0;
    for (const TabCell& c : L.cells)
        if (c.index == from) width = c.width;
    if (width == 0) return false;                               // the dragged tab is not visible

    size_t to = from;
    if (L.rightArrow && x >= L.rightX) {                        // pointer on the right arrow: one slot right
        if (from + 1 < tabs.count()) to = from + 1;
    } else if (L.leftArrow && x < L.leftX + L.leftW) {          // pointer on the left arrow: one slot left
        if (from > 0) to = from - 1;
    } else {
        int dragLeft = x - m_dragGrab;
        int dragRight = dragLeft + width;
        for (const TabCell& c : L.cells) {                      // moving right: pass the middle of the next tabs
            if (c.index > from && dragRight > c.x + c.width / 2 + 2) to = c.index;
        }
        if (to == from) {
            for (const TabCell& c : L.cells) {                  // moving left: pass the middle of the previous tabs
                if (c.index < from && dragLeft < c.x + c.width / 2) { to = c.index; break; }
            }
        }
    }
    if (to == from) return false;
    tabs.move(from, to);
    return true;
}
