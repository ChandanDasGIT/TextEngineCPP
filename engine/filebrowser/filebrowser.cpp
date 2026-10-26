#include "filebrowser.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool isDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string join(const std::string& dir, const std::string& name) {
    return dir == "/" ? "/" + name : dir + "/" + name;
}

std::string parentOf(const std::string& dir) {
    size_t slash = dir.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return dir.substr(0, slash);
}

std::string lastPart(const std::string& dir) {
    size_t slash = dir.find_last_of('/');
    return slash == std::string::npos ? dir : dir.substr(slash + 1);
}

size_t charLen(unsigned char c) {
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

size_t cols(const std::string& s) {                 // screen columns: one per UTF-8 character
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// cut to `width` columns, then pad with spaces up to `width`
std::string fit(const std::string& s, size_t width) {
    std::string out;
    size_t n = 0, i = 0;
    while (i < s.size() && n < width) {
        size_t len = std::min(charLen(static_cast<unsigned char>(s[i])), s.size() - i);
        out.append(s, i, len);
        i += len;
        ++n;
    }
    out.append(width - n, ' ');
    return out;
}

// the last `n` characters of s
std::string tail(const std::string& s, size_t n) {
    size_t total = cols(s);
    if (total <= n) return s;
    size_t skip = total - n, i = 0;
    while (skip > 0 && i < s.size()) {
        i += std::min(charLen(static_cast<unsigned char>(s[i])), s.size() - i);
        --skip;
    }
    return s.substr(i);
}

std::string repeat(const char* piece, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) s += piece;
    return s;
}

}

// ---------------------------------------------------------------- folder contents

bool FileBrowser::loadDir(const std::string& path) {
    char resolved[PATH_MAX];
    std::string dir = realpath(path.c_str(), resolved) ? std::string(resolved) : path;
    DIR* d = opendir(dir.c_str());
    if (!d) { m_error = std::strerror(errno); return false; }

    std::vector<Entry> all;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        bool isDir = false, ok = false;
        if (e->d_type == DT_DIR) { isDir = true; ok = true; }
        else if (e->d_type == DT_REG) { ok = true; }
        else {                                       // a link, or the file system did not say: look at the target
            struct stat st;
            if (stat(join(dir, n).c_str(), &st) == 0) {
                isDir = S_ISDIR(st.st_mode);
                ok = isDir || S_ISREG(st.st_mode);
            }
        }
        if (ok) all.push_back(Entry{n, isDir});
    }
    closedir(d);

    std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;            // folders first
        std::string la = lower(a.name), lb = lower(b.name);
        return la != lb ? la < lb : a.name < b.name;
    });
    if (dir != "/") all.insert(all.begin(), Entry{"..", true});

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
    size_t rows = static_cast<size_t>(layout().listRows);
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
        size_t slash = currentFile.find_last_of('/');
        if (slash == std::string::npos) base = currentFile;
        else { dir = slash == 0 ? "/" : currentFile.substr(0, slash); base = currentFile.substr(slash + 1); }
    }
    if (dir.empty() || !loadDir(dir)) {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd) || !loadDir(cwd)) loadDir("/");
    }
    m_error.clear();

    if (mode == Mode::SaveAs) m_name = base;
    rebuildView();
    if (!base.empty()) selectByName(base);
}

// ---------------------------------------------------------------- keys

std::string FileBrowser::resolveName(const std::string& name) const {
    if (!name.empty() && name[0] == '/') return name;
    if (name == "~" || name.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (home && *home) return std::string(home) + name.substr(1);
    }
    return join(m_dir, name);
}

// The name ends with '/': if it is a folder, walk into it and use up the name.
bool FileBrowser::navigateFromName() {
    if (m_name.empty() || m_name.back() != '/') return false;
    std::string p = m_name;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    std::string full;
    if (p[0] == '/') full = p;
    else if (p == "~" || p.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (!home || !*home) return false;
        full = std::string(home) + p.substr(1);
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
    if (m_dir == "/") return;
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
    int n = layout().listRows - 1;
    return n > 1 ? static_cast<size_t>(n) : 1;
}

// ---------------------------------------------------------------- mouse

FileBrowser::Result FileBrowser::click(int x, int y) {
    Result none;
    Layout l = layout();
    if (x < l.left || x >= l.left + l.width || y < l.listTop || y >= l.listTop + l.listRows) return none;
    size_t idx = m_top + static_cast<size_t>(y - l.listTop);
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
    size_t visible = static_cast<size_t>(layout().listRows);
    size_t maxTop = m_view.size() > visible ? m_view.size() - visible : 0;
    long t = static_cast<long>(m_top) + rows;
    t = std::max(0L, std::min(t, static_cast<long>(maxTop)));
    m_top = static_cast<size_t>(t);
}

// ---------------------------------------------------------------- drawing

FileBrowser::Layout FileBrowser::layout() const {
    Layout l;
    int c = m_cols > 0 ? m_cols : 80;
    int r = m_rows > 0 ? m_rows : 24;
    l.width = std::min(c - 2, 90);
    if (l.width < 30) l.width = std::min(c, 30);
    l.left = (c - l.width) / 2 + 1;
    l.listRows = std::max(3, std::min(r - 10, 18));
    l.top = r >= 13 ? 2 : 1;
    l.listTop = l.top + 3;
    l.inputRow = l.listTop + l.listRows + 1;
    l.hintRow = l.inputRow + 1;
    return l;
}

std::string FileBrowser::label() const { return m_mode == Mode::Open ? " Filter: " : " Name: "; }

void FileBrowser::cursorPos(int& row, int& col) const {
    Layout l = layout();
    size_t inner = static_cast<size_t>(l.width) - 2;
    size_t labelCols = cols(label());
    size_t avail = inner > labelCols + 1 ? inner - labelCols - 1 : 1;
    row = l.inputRow;
    col = l.left + 1 + static_cast<int>(labelCols + cols(tail(m_name, avail)));
}

std::string FileBrowser::draw() const {
    Layout l = layout();
    const size_t inner = static_cast<size_t>(l.width) - 2;
    std::string s;

    auto at = [&](int row) { return "\x1b[" + std::to_string(row) + ";" + std::to_string(l.left) + "H"; };
    const std::string grey = "\x1b[0m\x1b[90m";
    const std::string reset = "\x1b[0m";
    auto row = [&](int y, const std::string& content) {           // content is exactly `inner` columns wide
        s += at(y) + grey + "│" + reset + content + grey + "│" + reset;
    };
    auto divider = [&](int y) { s += at(y) + grey + "├" + repeat("─", inner) + "┤" + reset; };

    // top border with the title
    std::string title = m_mode == Mode::Open ? "Open file" : "Save as";
    size_t used = 3 + cols(title);
    s += at(l.top) + grey + "┌─ \x1b[1;97m" + title + reset + grey + " " +
         repeat("─", inner > used ? inner - used : 0) + "┐" + reset;

    // the folder, with $HOME shortened to ~
    std::string shown = m_dir;
    const char* home = std::getenv("HOME");
    if (home && *home) {
        std::string h = home;
        if (shown == h) shown = "~";
        else if (shown.size() > h.size() && shown.compare(0, h.size(), h) == 0 && shown[h.size()] == '/')
            shown = "~" + shown.substr(h.size());
    }
    if (shown != "/") shown += "/";
    if (cols(shown) > inner - 2) shown = "…" + tail(shown, inner - 3);
    row(l.top + 1, "\x1b[1m" + fit(" " + shown, inner) + reset);
    divider(l.top + 2);

    // the list
    bool strong = m_mode == Mode::Open || m_listFocus;
    for (int r = 0; r < l.listRows; ++r) {
        size_t idx = m_top + static_cast<size_t>(r);
        if (idx >= m_view.size()) {
            if (m_view.empty() && r == 0)
                row(l.listTop + r, "\x1b[90m" + fit(m_mode == Mode::Open && !m_name.empty() ? "  no match - Enter opens it as a new file"
                                                                                          : "  nothing here", inner) + reset);
            else row(l.listTop + r, std::string(inner, ' '));
            continue;
        }
        const Entry& e = m_all[m_view[idx]];
        std::string text = fit(" " + e.name + (e.dir ? "/" : ""), inner);
        if (idx == m_sel) row(l.listTop + r, std::string(strong ? "\x1b[7m" : "\x1b[100m") + text + reset);
        else if (e.dir) row(l.listTop + r, "\x1b[1;36m" + text + reset);
        else row(l.listTop + r, text);
    }
    divider(l.listTop + l.listRows);

    // the name field
    size_t labelCols = cols(label());
    size_t avail = inner > labelCols + 1 ? inner - labelCols - 1 : 1;
    row(l.inputRow, "\x1b[1m" + label() + reset + fit(tail(m_name, avail), inner - labelCols));

    // hint or error
    if (!m_error.empty()) row(l.hintRow, "\x1b[31m" + fit(" " + m_error, inner) + reset);
    else {
        std::string hint = m_mode == Mode::Open
            ? " Enter open | Backspace up | Alt+H hidden | Esc cancel"
            : " Enter save | Up/Down pick | Backspace up | Alt+H hidden | Esc cancel";
        row(l.hintRow, "\x1b[90m" + fit(hint, inner) + reset);
    }

    s += at(l.hintRow + 1) + grey + "└" + repeat("─", inner) + "┘" + reset;
    return s;
}
