#include "tabmanager.h"
#include <algorithm> // IWYU pragma: keep

namespace {

std::string canonical(const std::string& p) {
    return FileIO::canonicalPath(FileIO::expandPath(p));
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
    m_tabs.push_back(makeTab());                        // new tabs always go to the end
    m_current = m_tabs.size() - 1;
    ++m_changes;
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
        m_tabs.push_back(std::move(fresh));             // new tabs always go to the end
        m_current = m_tabs.size() - 1;
    }
    ++m_changes;
    m_armedClose = 0;
    return true;
}

size_t TabManager::openFiles(const std::vector<std::string>& paths, std::string& message) {
    size_t ok = 0;
    std::string lastOk, failures;
    for (const std::string& path : paths) {
        std::string one;
        if (!FileIO::exists(path)) {
            one = "no such file";
            if (!failures.empty()) failures += "; ";
            failures += FileIO::baseName(path) + ": " + one;
        } else if (openFile(path, one)) {
            ++ok;
            lastOk = one;
        } else {
            if (!failures.empty()) failures += "; ";
            failures += FileIO::baseName(path) + ": " + one;
        }
    }
    if (paths.empty()) message = "nothing to open";
    else if (failures.empty()) message = paths.size() == 1 ? lastOk : "opened " + std::to_string(ok) + " files";
    else if (ok == 0) message = "cannot open " + failures;
    else message = "opened " + std::to_string(ok) + " of " + std::to_string(paths.size()) + " files, failed: " + failures;
    return ok;
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
    ++m_changes;
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
    ++m_changes;
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
    ++m_changes;
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