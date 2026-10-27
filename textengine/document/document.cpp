#include "document.h"

IndentStyle Document::detectIndent(const std::string& text) {
    IndentStyle style;
    size_t tabLines = 0, spaceLines = 0;
    size_t hist[9] = {0};                 // how often the indent changes by 2..8 spaces between two indented lines
    size_t prev = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        if (end > pos) {
            if (text[pos] == '\t') {
                ++tabLines;
            } else if (text[pos] == ' ') {
                size_t n = 0;
                while (pos + n < end && text[pos + n] == ' ') ++n;
                if (pos + n < end) {                                  // ignore lines that are only spaces
                    ++spaceLines;
                    size_t d = n > prev ? n - prev : prev - n;
                    if (d >= 2 && d <= 8) ++hist[d];
                    prev = n;
                }
            } else {
                prev = 0;
            }
        } 
        pos = end + 1;
    }
    if (tabLines > spaceLines) {
        style.useTabs = true;
        return style;
    }
    size_t best = 4;                                                  // 4 wins a tie, then 2
    for (size_t w : {size_t(4), size_t(2), size_t(3), size_t(8), size_t(6), size_t(5), size_t(7)})
        if (hist[w] > hist[best]) best = w;
    style.width = best;
    return style;
}

// ---------------------------------------------------------------- Document

std::string Document::name() const {
    return m_path.empty() ? "[untitled]" : FileIO::baseName(m_path);
}

bool Document::modified(const TextView& view) const {
    return view.text() != m_savedText;
}

void Document::applyRaw(const std::string& raw, const std::string& path, TextView& view, bool& hadTabs) {
    m_crlf = raw.find("\r\n") != std::string::npos;
    m_encoding = FileIO::detectEncoding(raw);
    m_bom = raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0;      // keep it out of the text, write it back on save
    std::string text = FileIO::normalizeText(m_bom ? raw.substr(3) : raw, &hadTabs);
    view.loadText(text);
    view.setIndent(detectIndent(text));                   // Tab inserts what this file already uses
    view.setFoldLanguage(path);
    m_savedText = view.text();
}

void Document::recordDisk(const std::string& raw) {
    m_hash = FileIO::hashBytes(raw);
    long long mt = 0, sz = 0;
    m_onDisk = FileIO::statFile(m_path, mt, sz);
    if (m_onDisk) { m_mtimeNs = mt; m_size = sz; }
    m_deleted = false;
}

bool Document::open(const std::string& rawPath, TextView& view, std::string& message) {
    std::string path = FileIO::absolutePath(FileIO::expandPath(rawPath));
    if (path.empty()) { message = "no file name"; return false; }

    std::string text, err;
    bool isNew = !FileIO::exists(path);
    if (!isNew && !FileIO::readFile(path, text, err)) {
        message = "cannot open " + path + ": " + err;
        return false;
    }

    bool hadTabs = false;
    applyRaw(text, path, view, hadTabs);
    m_path = path;
    if (isNew) { m_onDisk = false; m_deleted = false; m_hash = 0; m_mtimeNs = 0; m_size = -1; }
    else recordDisk(text);

    if (isNew) message = "new file " + FileIO::baseName(path) + " (created on first save)";
    else message = "opened " + FileIO::baseName(path) + (hadTabs ? " (indented with tabs)" : "");
    return true;
}

bool Document::save(const TextView& view, std::string& message) {
    if (m_path.empty()) { message = "no file name"; return false; }

    std::string text = view.text();
    std::string out = m_bom ? "\xEF\xBB\xBF" : "";
    if (m_crlf) {
        out.reserve(text.size() + text.size() / 20);
        for (char c : text) {
            if (c == '\n') out += "\r\n";
            else out += c;
        }
    } else {
        out += text;
    }

    std::string err;
    std::string dir = FileIO::parentDir(m_path);
    bool newFolder = !dir.empty() && !FileIO::exists(dir);           // saving into a folder that does not exist yet
    if (!FileIO::makeParentDirs(m_path, err)) {
        message = "cannot create the folder for " + m_path + ": " + err;
        return false;
    }
    if (!FileIO::writeFile(m_path, out, err)) {
        message = "cannot save " + m_path + ": " + err;
        return false;
    }
    m_savedText = text;
    recordDisk(out);                                 // our own save must not look like an outside change
    m_encoding = FileIO::detectEncoding(text);       // typing an accent into an ASCII file makes it UTF-8
    if (m_bom && m_encoding != "invalid UTF-8") m_encoding = "UTF-8 BOM";
    message = "saved " + FileIO::baseName(m_path) + (newFolder ? " (new folder created)" : "");
    return true;
}

bool Document::saveAs(const std::string& rawPath, const TextView& view, std::string& message) {
    std::string path = FileIO::absolutePath(FileIO::expandPath(rawPath));
    if (path.empty()) { message = "no file name"; return false; }

    std::string old = m_path;
    m_path = path;
    if (!save(view, message)) { m_path = old; return false; }
    return true;
}

// ---------------------------------------------------------------- changes on disk

Document::DiskState Document::checkDisk() {
    if (m_path.empty()) return DiskState::Unchanged;

    long long mt = 0, sz = 0;
    if (!FileIO::statFile(m_path, mt, sz)) {
        if (m_onDisk && !m_deleted) { m_deleted = true; return DiskState::Deleted; }
        return DiskState::Unchanged;
    }
    if (m_onDisk && !m_deleted && mt == m_mtimeNs && sz == m_size) return DiskState::Unchanged;   // the usual case

    // the time or size differs (or the file appeared / came back): look at the content
    std::string raw, err;
    if (!FileIO::readFile(m_path, raw, err)) {        // unreadable or binary: nothing we could show
        m_onDisk = true; m_deleted = false; m_mtimeNs = mt; m_size = sz; m_hash = 0;
        return DiskState::Unchanged;
    }
    if (m_onDisk && FileIO::hashBytes(raw) == m_hash) {    // touched, but the same content
        m_mtimeNs = mt; m_size = sz; m_deleted = false;
        return DiskState::Unchanged;
    }
    return DiskState::Changed;
}

void Document::acknowledgeDisk() {
    std::string raw, err;
    if (FileIO::readFile(m_path, raw, err)) { recordDisk(raw); return; }
    long long mt = 0, sz = 0;
    if (FileIO::statFile(m_path, mt, sz)) {
        m_mtimeNs = mt; m_size = sz; m_onDisk = true; m_hash = 0; m_deleted = false;
    }
}

bool Document::reload(TextView& view, std::string& message) {
    std::string raw, err;
    if (!FileIO::readFile(m_path, raw, err)) {
        message = "cannot reload " + name() + ": " + err;
        acknowledgeDisk();                            // do not ask again about this version
        return false;
    }
    Position p = view.cursor();
    bool hadTabs = false;
    applyRaw(raw, m_path, view, hadTabs);
    recordDisk(raw);
    view.moveCursor(p);
    message = "reloaded " + name() + " from disk";
    return true;
}

void Document::loadUntitled(const std::string& text, TextView& view) {
    m_path.clear();
    m_crlf = false;
    m_bom = false;
    view.loadText(text);
    m_savedText = view.text();
    m_encoding = FileIO::detectEncoding(m_savedText);
    m_onDisk = false;
    m_deleted = false;
    m_hash = 0;
}