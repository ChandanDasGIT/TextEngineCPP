#include "fileio.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>      // only for flushing a saved file to the disk (see writeFile)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

// Paths travel through the engine as UTF-8 strings with '/' as the separator, on every system.
fs::path toPath(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

size_t g_maxFileBytes = 50u * 1024u * 1024u;

std::string megabytes(unsigned long long bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, bytes < 100ull * 1024 * 1024 ? "%.1f MB" : "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

std::string toString(const fs::path& p) {
#if defined(__cpp_char8_t)
    std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
#else
    return p.generic_u8string();
#endif
}

}

namespace FileIO {

bool exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(toPath(path), ec);
}

bool isDirectory(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(toPath(path), ec);
}

bool isAbsolute(const std::string& path) {
    return toPath(path).is_absolute();
}

bool isRoot(const std::string& path) {
    fs::path p = toPath(path);
    return !p.empty() && p == p.root_path();
}

std::string homeDir() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) home = std::getenv("USERPROFILE");     // Windows
    return home && *home ? toString(toPath(home)) : std::string();
}

std::string currentDir() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    return ec ? std::string() : toString(p);
}

std::string canonicalPath(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::canonical(toPath(path), ec);               // resolves links; fails if it does not exist
    return ec ? path : toString(p);
}

std::string expandPath(const std::string& path) {
    if (path == "~" || path.rfind("~/", 0) == 0) {
        std::string home = homeDir();
        if (!home.empty()) return home + path.substr(1);
    }
    return path;
}

std::string baseName(const std::string& path) {
    return toString(toPath(path).filename());
}

std::string parentDir(const std::string& path) {
    return toString(toPath(path).parent_path());
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    return dir.back() == '/' ? dir + name : dir + "/" + name;
}

std::string absolutePath(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    fs::path p = toPath(path);
    fs::path dir = p.parent_path();
    if (dir.empty()) dir = ".";
    fs::path d = fs::weakly_canonical(fs::absolute(dir, ec), ec);   // links resolved as far as the folder exists
    if (ec) {
        ec.clear();
        fs::path a = fs::absolute(p, ec);
        return ec ? path : toString(a);
    }
    fs::path base = p.filename();
    return base.empty() ? toString(d) : toString(d / base);
}

bool makeParentDirs(const std::string& path, std::string& error) {
    fs::path dir = toPath(path).parent_path();
    if (dir.empty()) return true;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { error = ec.message(); return false; }
    if (!fs::is_directory(dir, ec)) { error = "it is not a folder: " + toString(dir); return false; }
    return true;
}

bool listDir(const std::string& dir, std::vector<DirEntry>& out, std::string& error) {
    std::error_code ec;
    fs::directory_iterator it(toPath(dir), ec), end;
    if (ec) { error = ec.message(); return false; }
    out.clear();
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e2;
        fs::file_status st = it->status(e2);                    // follows links: a link to a folder is a folder
        if (e2) continue;
        bool isDir = fs::is_directory(st);
        if (isDir || fs::is_regular_file(st)) out.push_back(DirEntry{toString(it->path().filename()), isDir});
    }
    return true;
}

size_t maxFileBytes() { return g_maxFileBytes; }
void setMaxFileBytes(size_t bytes) { g_maxFileBytes = bytes; }

bool readFile(const std::string& path, std::string& text, std::string& error) {
    std::error_code ec;
    fs::file_status st = fs::status(toPath(path), ec);
    if (ec || !fs::exists(st)) { error = ec ? ec.message() : "no such file"; return false; }
    if (fs::is_directory(st)) { error = "it is a folder"; return false; }

    std::string tooBig = "file is bigger than the limit of " + megabytes(g_maxFileBytes);
    if (g_maxFileBytes != 0) {
        uintmax_t size = fs::file_size(toPath(path), ec);
        if (!ec && size > g_maxFileBytes) {
            error = "file is " + megabytes(size) + "; the limit is " + megabytes(g_maxFileBytes);
            return false;
        }
    }

    std::ifstream f(toPath(path), std::ios::binary);
    if (!f) { error = std::strerror(errno); return false; }

    text.clear();
    char chunk[65536];
    while (f.read(chunk, sizeof chunk) || f.gcount() > 0) {
        text.append(chunk, static_cast<size_t>(f.gcount()));
        if (g_maxFileBytes != 0 && text.size() > g_maxFileBytes) {      // it grew while we were reading
            text.clear();
            error = tooBig;
            return false;
        }
    }
    if (f.bad()) { error = "cannot read the file"; return false; }
    if (text.find('\0') != std::string::npos) { error = "it looks like a binary file"; return false; }
    return true;
}

bool writeFile(const std::string& path, const std::string& text, std::string& error) {
    std::error_code ec;
    fs::path target = toPath(path);
    fs::path real = fs::canonical(target, ec);                  // follow symlinks, so the link itself stays a link
    if (!ec) target = real;
    ec.clear();

    fs::path tmp = target;
    tmp += ".tmp~";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { error = std::strerror(errno); return false; }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.flush();
        if (!f) { error = "cannot write the file"; fs::remove(tmp, ec); return false; }
    }

#if defined(__unix__) || defined(__APPLE__)
    int fd = ::open(tmp.c_str(), O_WRONLY);                     // make sure the data is on the disk before it replaces the old file
    if (fd >= 0) { (void)::fsync(fd); ::close(fd); }
#endif

    fs::file_status old = fs::status(target, ec);
    if (!ec && fs::exists(old)) fs::permissions(tmp, old.permissions(), ec);   // keep permissions
    ec.clear();

    fs::rename(tmp, target, ec);
    if (ec) {
        std::string msg = ec.message();
        fs::remove(tmp, ec);
        error = msg;
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
    std::error_code ec;
    fs::path p = toPath(path);
    if (!fs::exists(p, ec) || fs::is_directory(p, ec)) return false;
    fs::file_time_type t = fs::last_write_time(p, ec);
    if (ec) return false;
    uintmax_t sz = fs::file_size(p, ec);
    if (ec) return false;
    // the starting point of the clock differs between systems; the value is only ever compared with an earlier one
    mtimeNs = static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
    size = static_cast<long long>(sz);
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
        if (c == '\t' && hadTabs) *hadTabs = true;
        clean += c;
    }
    return clean;
}

}