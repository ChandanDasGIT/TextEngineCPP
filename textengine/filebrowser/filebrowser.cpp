#include "filebrowser.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include "../fileIO/fileio.h"

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string join(const std::string& dir, const std::string& name) { return FileIO::joinPath(dir, name); }

std::string parentOf(const std::string& dir) {
    std::string p = FileIO::parentDir(dir);
    return p.empty() ? dir : p;
}

std::string lastPart(const std::string& dir) { return FileIO::baseName(dir); }

bool isDirectory(const std::string& path) { return FileIO::isDirectory(path); }

}

// ---------------------------------------------------------------- folder contents

bool FileBrowser::loadDir(const std::string& path) {
    std::string dir = FileIO::canonicalPath(path);
    std::vector<FileIO::DirEntry> found;
    std::string err;
    if (!FileIO::listDir(dir, found, err)) { m_error = err; return false; }

    std::vector<Entry> all;
    all.reserve(found.size() + 1);
    for (const FileIO::DirEntry& e : found) all.push_back(Entry{e.name, e.dir});

    std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;            // folders first
        std::string la = lower(a.name), lb = lower(b.name);
        return la != lb ? la < lb : a.name < b.name;
    });
    if (!FileIO::isRoot(dir)) all.insert(all.begin(), Entry{"..", true});

    m_dir = dir;
    m_all = std::move(all);
    m_error.clear();
    m_sel = 0;
    m_top = 0;
    return true;
}

void FileBrowser::rebuildView() {
    m_view.clear();
    bool hidden = m_showHidden || (!m_name.empty() && m_name[0] == '.');
    std::string f = lower(m_name);
    for (size_t i = 0; i < m_all.size(); ++i) {
        const Entry& e = m_all[i];
        if (e.name == "..") {
            if (m_mode == Mode::SaveAs || m_name.empty()) m_view.push_back(i);
            continue;
        }
        if (!hidden && e.name[0] == '.') continue;
        if (m_mode == Mode::Open && !f.empty() && lower(e.name).find(f) == std::string::npos) continue;
        m_view.push_back(i);
    }
    if (m_view.empty()) m_sel = 0;
    else if (m_sel >= m_view.size()) m_sel = m_view.size() - 1;
    if (m_top > m_sel) m_top = m_sel;
    ensureVisible();
}

void FileBrowser::ensureVisible() {
    size_t rows = static_cast<size_t>(m_visible);
    if (m_sel < m_top) m_top = m_sel;
    else if (m_sel >= m_top + rows) m_top = m_sel - rows + 1;
}

void FileBrowser::selectByName(const std::string& name) {
    for (size_t i = 0; i < m_view.size(); ++i)
        if (m_all[m_view[i]].name == name) { m_sel = i; ensureVisible(); return; }
}

// ---------------------------------------------------------------- start

void FileBrowser::open(Mode mode, const std::string& currentFile) {
    m_mode = mode;
    m_name.clear();
    m_error.clear();
    m_listFocus = false;
    m_lastClickMs = 0;

    std::string dir, base;
    if (!currentFile.empty()) {
        dir = FileIO::parentDir(currentFile);
        base = FileIO::baseName(currentFile);
    }
    if (dir.empty() || !loadDir(dir)) {
        std::string cwd = FileIO::currentDir();
        if (cwd.empty() || !loadDir(cwd)) loadDir(FileIO::homeDir().empty() ? "/" : FileIO::homeDir());
    }
    m_error.clear();

    if (mode == Mode::SaveAs) m_name = base;
    rebuildView();
    if (!base.empty()) selectByName(base);
}

// ---------------------------------------------------------------- keys

std::string FileBrowser::resolveName(const std::string& name) const {
    if (!name.empty() && FileIO::isAbsolute(name)) return name;
    if (name == "~" || name.rfind("~/", 0) == 0) {
        std::string home = FileIO::homeDir();
        if (!home.empty()) return home + name.substr(1);
    }
    return join(m_dir, name);
}

// The name ends with '/': if it is a folder, walk into it and use up the name.
bool FileBrowser::navigateFromName() {
    if (m_name.empty() || m_name.back() != '/') return false;
    std::string p = m_name;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    std::string full;
    if (FileIO::isAbsolute(p)) full = p;
    else if (p == "~" || p.rfind("~/", 0) == 0) {
        std::string home = FileIO::homeDir();
        if (home.empty()) return false;
        full = home + p.substr(1);
    } else full = join(m_dir, p);
    if (!isDirectory(full) || !loadDir(full)) return false;
    m_name.clear();
    return true;
}

void FileBrowser::typeText(const std::string& s) {
    m_error.clear();
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 32 || c == 127) continue;
        m_name += c;
        if (c == '/') navigateFromName();
    }
    m_listFocus = false;
    if (m_mode == Mode::Open) { m_sel = 0; m_top = 0; }
    rebuildView();
}

void FileBrowser::backspace(bool wholeName) {
    m_error.clear();
    if (m_name.empty()) { goUp(); return; }
    if (wholeName) m_name.clear();
    else {
        while (!m_name.empty() && (static_cast<unsigned char>(m_name.back()) & 0xC0) == 0x80) m_name.pop_back();
        if (!m_name.empty()) m_name.pop_back();
    }
    m_listFocus = false;
    if (m_mode == Mode::Open) { m_sel = 0; m_top = 0; }
    rebuildView();
}

void FileBrowser::moveSelection(int delta) {
    m_error.clear();
    if (m_view.empty()) return;
    long s = static_cast<long>(m_sel) + delta;
    s = std::max(0L, std::min(s, static_cast<long>(m_view.size()) - 1));
    m_sel = static_cast<size_t>(s);
    m_listFocus = true;
    ensureVisible();
}

void FileBrowser::selectFirst() { moveSelection(-static_cast<int>(m_view.size())); }
void FileBrowser::selectLast() { moveSelection(static_cast<int>(m_view.size())); }

void FileBrowser::goUp() {
    m_error.clear();
    if (FileIO::isRoot(m_dir)) return;
    std::string child = lastPart(m_dir);
    if (!loadDir(parentOf(m_dir))) return;
    if (m_mode == Mode::Open) m_name.clear();
    m_listFocus = false;
    rebuildView();
    selectByName(child);
}

void FileBrowser::toggleHidden() {
    m_error.clear();
    m_showHidden = !m_showHidden;
    rebuildView();
}

void FileBrowser::enterEntry(const Entry& e, bool clearName) {
    std::string child;
    std::string target;
    if (e.name == "..") { target = parentOf(m_dir); child = lastPart(m_dir); }
    else target = join(m_dir, e.name);
    if (!loadDir(target)) return;
    if (clearName) m_name.clear();
    m_listFocus = false;
    rebuildView();
    if (!child.empty()) selectByName(child);
}

FileBrowser::Result FileBrowser::activate() {
    m_error.clear();
    Result r;
    Entry sel{"", false};
    bool haveSel = !m_view.empty() && m_sel < m_view.size();
    if (haveSel) sel = m_all[m_view[m_sel]];

    if (m_mode == Mode::Open) {
        if (!haveSel) {
            if (m_name.empty()) { r.message = "this folder is empty"; return r; }
            r.kind = Result::Choose;                     // nothing matches: open it as a new file
            r.path = resolveName(m_name);
            return r;
        }
        if (sel.dir) { enterEntry(sel, true); return r; }
        r.kind = Result::Choose;
        r.path = join(m_dir, sel.name);
        return r;
    }

    // Save as
    if (m_listFocus && haveSel) {
        if (sel.dir) { enterEntry(sel, false); return r; }
        m_name = sel.name;                               // an existing file: save over it (the caller asks first)
        m_listFocus = false;
        r.kind = Result::Choose;
        r.path = join(m_dir, sel.name);
        return r;
    }
    if (m_name.empty()) { r.message = "type a file name first"; return r; }
    if (m_name == "." || m_name == "..") { r.message = "that is not a file name"; return r; }
    std::string full = resolveName(m_name);
    if (isDirectory(full)) {                             // the name of a folder: walk into it
        if (loadDir(full)) { m_name.clear(); rebuildView(); }
        return r;
    }
    r.kind = Result::Choose;
    r.path = full;
    return r;
}

size_t FileBrowser::pageRows() const {
    int n = m_visible - 1;
    return n > 1 ? static_cast<size_t>(n) : 1;
}

// ---------------------------------------------------------------- mouse

FileBrowser::Result FileBrowser::click(size_t idx) {
    Result none;
    if (idx >= m_view.size()) return none;

    long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
    bool dbl = m_lastClickMs > 0 && idx == m_lastClickIdx && now - m_lastClickMs <= 400;
    m_lastClickMs = dbl ? 0 : now;
    m_lastClickIdx = idx;
    m_error.clear();

    m_sel = idx;
    m_listFocus = true;
    if (dbl) return activate();
    const Entry& e = m_all[m_view[idx]];
    if (m_mode == Mode::SaveAs && !e.dir) { m_name = e.name; m_listFocus = false; }   // a click on a file fills in its name
    return none;
}

void FileBrowser::scroll(int rows) {
    size_t visible = static_cast<size_t>(m_visible);
    size_t maxTop = m_view.size() > visible ? m_view.size() - visible : 0;
    long t = static_cast<long>(m_top) + rows;
    t = std::max(0L, std::min(t, static_cast<long>(maxTop)));
    m_top = static_cast<size_t>(t);
}

// ---------------------------------------------------------------- what the front end draws

std::string FileBrowser::title() const { return m_mode == Mode::Open ? "Open file" : "Save as"; }

std::string FileBrowser::fieldLabel() const { return m_mode == Mode::Open ? "Filter" : "Name"; }

std::string FileBrowser::shownDirectory() const {
    std::string shown = m_dir;
    std::string h = FileIO::homeDir();
    if (!h.empty()) {
        if (shown == h) shown = "~";
        else if (shown.size() > h.size() && shown.compare(0, h.size(), h) == 0 && shown[h.size()] == '/')
            shown = "~" + shown.substr(h.size());
    }
    if (shown.empty() || shown.back() != '/') shown += "/";
    return shown;
}

std::string FileBrowser::rowName(size_t row) const {
    return row < m_view.size() ? m_all[m_view[row]].name : std::string();
}

bool FileBrowser::rowIsDir(size_t row) const {
    return row < m_view.size() && m_all[m_view[row]].dir;
}

std::string FileBrowser::emptyMessage() const {
    if (!m_view.empty()) return "";
    return m_mode == Mode::Open && !m_name.empty() ? "no match - Enter opens it as a new file" : "nothing here";
}

std::string FileBrowser::hint() const {
    return m_mode == Mode::Open
        ? "Enter open | Backspace up | Alt+H hidden | Esc cancel"
        : "Enter save | Up/Down pick | Backspace up | Alt+H hidden | Esc cancel";
}