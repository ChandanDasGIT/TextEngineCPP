#include "fileio.h"
#include <cerrno>
#include <climits> // IWYU pragma: keep
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace FileIO {

bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string expandPath(const std::string& path) {
    if (path == "~" || path.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (home && *home) return std::string(home) + path.substr(1);
    }
    return path;
}

std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string parentDir(const std::string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return "";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string absolutePath(const std::string& path) {
    if (path.empty()) return path;
    std::string dir, base;
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) { dir = "."; base = path; }
    else { dir = slash == 0 ? "/" : path.substr(0, slash); base = path.substr(slash + 1); }

    char resolved[PATH_MAX];
    if (realpath(dir.c_str(), resolved)) {
        std::string d = resolved;
        if (base.empty()) return d;
        return d == "/" ? "/" + base : d + "/" + base;
    }
    // the folder does not exist yet: put the current folder in front, without cleaning anything up
    if (path[0] == '/') return path;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd)) return std::string(cwd) + "/" + path;
    return path;
}

bool makeParentDirs(const std::string& path, std::string& error) {
    std::string dir = parentDir(path);
    if (dir.empty() || exists(dir)) return true;
    size_t i = 0;
    while (i <= dir.size()) {
        size_t slash = dir.find('/', i);
        if (slash == std::string::npos) slash = dir.size();
        std::string partial = dir.substr(0, slash);
        if (!partial.empty() && !exists(partial) && mkdir(partial.c_str(), 0777) != 0 && errno != EEXIST) {
            error = std::strerror(errno);
            return false;
        }
        i = slash + 1;
    }
    return true;
}

bool readFile(const std::string& path, std::string& text, std::string& error) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) { error = std::strerror(errno); return false; }
    if (S_ISDIR(st.st_mode)) { error = "it is a folder"; return false; }

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = std::strerror(errno); return false; }

    text.clear();
    char chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) text.append(chunk, got);
    bool failed = std::ferror(f) != 0;
    int err = errno;
    std::fclose(f);

    if (failed) { error = std::strerror(err); return false; }
    if (text.find('\0') != std::string::npos) { error = "it looks like a binary file"; return false; }
    return true;
}

bool writeFile(const std::string& path, const std::string& text, std::string& error) {
    std::string target = path;
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved)) target = resolved;   // follow symlinks

    std::string tmp = target + ".tmp~";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { error = std::strerror(errno); return false; }

    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fflush(f) != 0) ok = false;
    if (ok) (void)fsync(fileno(f));
    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        int err = errno;
        std::remove(tmp.c_str());
        error = std::strerror(err);
        return false;
    }

    struct stat st;
    if (stat(target.c_str(), &st) == 0) (void)chmod(tmp.c_str(), st.st_mode & 07777);   // keep permissions

    if (std::rename(tmp.c_str(), target.c_str()) != 0) {
        int err = errno;
        std::remove(tmp.c_str());
        error = std::strerror(err);
        return false;
    }
    return true;
}

std::string detectEncoding(const std::string& text) {
    if (text.empty()) return "UTF-8";
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data());
    size_t n = text.size();
    bool bom = n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF;
    bool ascii = true;
    for (size_t i = 0; i < n;) {
        unsigned char c = p[i];
        if (c < 0x80) { ++i; continue; }
        ascii = false;
        size_t len = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        if (len == 0 || i + len > n) return "invalid UTF-8";
        for (size_t k = 1; k < len; ++k)
            if ((p[i + k] & 0xC0) != 0x80) return "invalid UTF-8";
        if (c == 0xE0 && p[i + 1] < 0xA0) return "invalid UTF-8";     // overlong
        if (c == 0xED && p[i + 1] >= 0xA0) return "invalid UTF-8";     // surrogate
        if (c == 0xF0 && p[i + 1] < 0x90) return "invalid UTF-8";      // overlong
        if (c == 0xF4 && p[i + 1] >= 0x90) return "invalid UTF-8";     // above U+10FFFF
        i += len;
    }
    if (bom) return "UTF-8 BOM";
    return ascii ? "ASCII" : "UTF-8";
}

bool statFile(const std::string& path, long long& mtimeNs, long long& size) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;
    mtimeNs = static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + static_cast<long long>(st.st_mtim.tv_nsec);
    size = static_cast<long long>(st.st_size);
    return true;
}

unsigned long long hashBytes(const std::string& bytes) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string normalizeText(const std::string& raw, bool* hadTabs) {
    std::string clean;
    clean.reserve(raw.size());
    for (char c : raw) {
        if (c == '\r') continue;
        if (c == '\t') { clean += "    "; if (hadTabs) *hadTabs = true; }
        else clean += c;
    }
    return clean;
}

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
    view.loadText(FileIO::normalizeText(raw, &hadTabs));
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
    else message = "opened " + FileIO::baseName(path) + (hadTabs ? " - tabs converted to spaces" : "");
    return true;
}

bool Document::save(const TextView& view, std::string& message) {
    if (m_path.empty()) { message = "no file name"; return false; }

    std::string text = view.text();
    std::string out;
    if (m_crlf) {
        out.reserve(text.size() + text.size() / 20);
        for (char c : text) {
            if (c == '\n') out += "\r\n";
            else out += c;
        }
    } else {
        out = text;
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
    view.loadText(text);
    m_savedText = view.text();
    m_encoding = FileIO::detectEncoding(m_savedText);
    m_onDisk = false;
    m_deleted = false;
    m_hash = 0;
}