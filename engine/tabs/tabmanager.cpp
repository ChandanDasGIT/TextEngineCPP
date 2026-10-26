#include "tabmanager.h"
#include <algorithm>
#include <climits> // IWYU pragma: keep
#include <cstdlib>

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

std::string canonical(const std::string& p) {
    std::string e = FileIO::expandPath(p);
    char buf[PATH_MAX];
    if (realpath(e.c_str(), buf)) return buf;
    return e;
}

// the folder a file is in: "a/b/c.txt" -> "b"
std::string parentName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "";
    size_t prev = path.find_last_of('/', slash - 1);
    size_t start = prev == std::string::npos ? 0 : prev + 1;
    return path.substr(start, slash - start);
}

}

// ---------------------------------------------------------------- creating tabs

TabManager::TabManager() { m_tabs.push_back(makeTab()); }

int TabManager::nextUntitledNo() const {
    for (int n = 1;; ++n) {
        bool used = false;
        for (const auto& t : m_tabs)
            if (!t->doc.hasPath() && t->untitledNo == n) used = true;
        if (!used) return n;
    }
}

std::unique_ptr<EditorTab> TabManager::makeTab() {
    auto t = std::make_unique<EditorTab>();
    t->id = ++m_nextId;
    t->untitledNo = nextUntitledNo();
    t->gutter.reset(t->view.buffer());
    return t;
}

bool TabManager::isPristine(const EditorTab& t) {
    return !t.doc.hasPath() && t.view.text().empty();
}

void TabManager::refreshCurrent() {
    EditorTab& t = current();
    t.modifiedCache = t.doc.modified(t.view);
}

size_t TabManager::newTab() {
    refreshCurrent();
    m_tabs.insert(m_tabs.begin() + static_cast<long>(m_current) + 1, makeTab());
    ++m_current;
    m_follow = true;
    m_armedClose = 0;
    return m_current;
}

bool TabManager::openFile(const std::string& path, std::string& message) {
    std::string want = canonical(path);
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->doc.hasPath() && canonical(m_tabs[i]->doc.path()) == want) {
            select(i);
            message = "switched to " + title(i);
            return true;
        }
    }

    bool reuse = isPristine(current());
    std::unique_ptr<EditorTab> fresh;
    EditorTab* t;
    if (reuse) t = &current();
    else { fresh = makeTab(); t = fresh.get(); }

    if (!t->doc.open(path, t->view, message)) return false;   // the tab is left as it was
    t->gutter.reset(t->view.buffer());
    t->syntax.setLanguageFor(t->doc.path());
    t->scrollTop = 0;
    t->followCursor = true;
    t->modifiedCache = false;

    if (fresh) {
        refreshCurrent();
        m_tabs.insert(m_tabs.begin() + static_cast<long>(m_current) + 1, std::move(fresh));
        ++m_current;
    }
    m_follow = true;
    m_armedClose = 0;
    return true;
}

// ---------------------------------------------------------------- closing tabs

TabManager::CloseResult TabManager::closeTab(size_t index, bool force) {
    if (index >= m_tabs.size()) return CloseResult::Closed;
    EditorTab& t = *m_tabs[index];

    if (!force && modified(index) && m_armedClose != t.id) {
        m_armedClose = t.id;
        return CloseResult::NeedsConfirm;
    }
    m_armedClose = 0;

    if (t.doc.hasPath()) {
        m_closed.push_back(t.doc.path());
        if (m_closed.size() > 20) m_closed.erase(m_closed.begin());
    }
    if (index == m_current) refreshCurrent();

    m_tabs.erase(m_tabs.begin() + static_cast<long>(index));
    if (m_tabs.empty()) {
        m_tabs.push_back(makeTab());
        m_current = 0;
    } else if (index < m_current) {
        --m_current;
    } else if (m_current >= m_tabs.size()) {
        m_current = m_tabs.size() - 1;                  // closed the last tab: the one before it
    }                                                   // otherwise the right neighbour moves into this slot
    m_follow = true;
    return CloseResult::Closed;
}

bool TabManager::reopenClosed(std::string& message) {
    while (!m_closed.empty()) {
        std::string path = m_closed.back();
        m_closed.pop_back();
        if (!FileIO::exists(path)) continue;            // deleted in the meantime
        return openFile(path, message);
    }
    message = "no closed tab to reopen";
    return false;
}

// ---------------------------------------------------------------- switching

void TabManager::select(size_t i) {
    if (i >= m_tabs.size()) return;
    refreshCurrent();
    m_current = i;
    m_follow = true;
    m_armedClose = 0;
}

void TabManager::next() { select((m_current + 1) % m_tabs.size()); }
void TabManager::prev() { select((m_current + m_tabs.size() - 1) % m_tabs.size()); }
void TabManager::selectLast() { select(m_tabs.size() - 1); }

void TabManager::move(size_t from, size_t to) {
    size_t n = m_tabs.size();
    if (from >= n || to >= n || from == to) return;
    const EditorTab* cur = m_tabs[m_current].get();
    auto t = std::move(m_tabs[from]);
    m_tabs.erase(m_tabs.begin() + static_cast<long>(from));
    m_tabs.insert(m_tabs.begin() + static_cast<long>(to), std::move(t));
    for (size_t i = 0; i < n; ++i)
        if (m_tabs[i].get() == cur) m_current = i;
    m_follow = true;
}

// ---------------------------------------------------------------- state

bool TabManager::modified(size_t i) const {
    const EditorTab& t = *m_tabs[i];
    return i == m_current ? t.doc.modified(t.view) : t.modifiedCache;
}

size_t TabManager::modifiedCount() const {
    size_t n = 0;
    for (size_t i = 0; i < m_tabs.size(); ++i)
        if (modified(i)) ++n;
    return n;
}

std::string TabManager::title(size_t i) const {
    const EditorTab& t = *m_tabs[i];
    if (!t.doc.hasPath()) {
        if (!t.label.empty()) return t.label;
        return t.untitledNo <= 1 ? std::string("untitled") : "untitled " + std::to_string(t.untitledNo);
    }

    std::string name = t.doc.name();
    std::string suffix = t.doc.deletedOnDisk() ? " (deleted)" : "";
    for (size_t j = 0; j < m_tabs.size(); ++j) {
        if (j == i || !m_tabs[j]->doc.hasPath() || m_tabs[j]->doc.name() != name) continue;
        std::string dir = parentName(canonical(t.doc.path()));      // same name twice: add the folder
        return dir.empty() ? name + suffix : name + " · " + dir + suffix;
    }
    return name + suffix;
}

// ---------------------------------------------------------------- tab bar

// Cell layout: " title " + (● or space) + "×" + separator  ->  title width + 5 columns.
const TabBarLayout& TabManager::layout(int cols) {
    const int kArrowW = 3, kPlusW = 3;
    TabBarLayout& L = m_layout;
    L = TabBarLayout();
    L.cols = cols;

    size_t n = m_tabs.size();
    int plusW = cols >= 12 ? kPlusW : 0;
    int avail = cols - plusW;
    int maxTitle = std::max(1, std::min(18, cols - plusW - 2 * kArrowW - 9));

    std::vector<std::string> titles(n);
    std::vector<int> widths(n);
    for (size_t i = 0; i < n; ++i) {
        titles[i] = clipCols(title(i), static_cast<size_t>(maxTitle));
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
        if (m_current < m_scroll) m_scroll = m_current;
        while (m_scroll < m_current && m_current >= m_scroll + visibleCount(m_scroll)) ++m_scroll;
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
        cell.active = k == m_current;
        cell.modified = modified(k);
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

TabHit TabManager::hit(int x, int cols) {
    const TabBarLayout& L = layout(cols);
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

void TabManager::scrollBar(int delta) {
    long long t = static_cast<long long>(m_scroll) + delta;
    long long hi = static_cast<long long>(m_tabs.size()) - 1;
    m_scroll = static_cast<size_t>(std::max(0LL, std::min(t, hi)));
    m_follow = false;                      // stay where the user scrolled until the next tab change
}

// ---------------------------- drag and drop

size_t TabManager::draggedIndex() const {
    for (size_t i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i]->id == m_dragId) return i;
    return m_tabs.size();
}

bool TabManager::beginDrag(int x, int cols) {
    m_dragId = 0;
    const TabBarLayout& L = layout(cols);
    for (const TabCell& c : L.cells) {
        if (x < c.x || x >= c.x + c.width) continue;
        m_dragId = m_tabs[c.index]->id;
        m_dragGrab = x - c.x;
        return true;
    }
    return false;
}

bool TabManager::dragTo(int x, int cols) {
    size_t from = draggedIndex();
    if (m_dragId == 0 || from >= m_tabs.size()) { m_dragId = 0; return false; }
    const TabBarLayout& L = layout(cols);

    int width = 0;
    for (const TabCell& c : L.cells)
        if (c.index == from) width = c.width;
    if (width == 0) return false;                               // the dragged tab is not visible

    size_t to = from;
    if (L.rightArrow && x >= L.rightX) {                        // pointer on the right arrow: one slot right
        if (from + 1 < m_tabs.size()) to = from + 1;
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
    move(from, to);
    return true;
}

// ---------------------------- changes on disk

bool TabManager::reloadTab(size_t index, std::string& message) {
    if (index >= m_tabs.size()) return false;
    EditorTab& t = *m_tabs[index];
    if (!t.doc.reload(t.view, message)) return false;
    t.gutter.reset(t.view.buffer());
    t.modifiedCache = false;
    t.followCursor = true;
    return true;
}

bool TabManager::openDiskCopy(size_t index, std::string& message) {
    if (index >= m_tabs.size()) return false;
    EditorTab& src = *m_tabs[index];
    std::string path = src.doc.path();
    std::string raw, err;
    if (!FileIO::readFile(path, raw, err)) {
        message = "cannot read " + src.doc.name() + ": " + err;
        return false;
    }
    std::string label = src.doc.name() + " (on disk)";
    src.doc.acknowledgeDisk();                          // this version is known now: stop asking

    auto t = makeTab();
    t->doc.loadUntitled(FileIO::normalizeText(raw), t->view);
    t->label = label;
    t->gutter.reset(t->view.buffer());
    t->syntax.setLanguageFor(path);
    t->view.setFoldLanguage(path);
    t->modifiedCache = false;

    m_tabs.insert(m_tabs.begin() + static_cast<long>(index) + 1, std::move(t));
    if (m_current > index) ++m_current;
    select(index + 1);
    message = "opened the version of " + src.doc.name() + " that is on disk in a new tab";
    return true;
}