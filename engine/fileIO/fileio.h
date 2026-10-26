#pragma once
#include <string>
#include "../textview/textview.h"

namespace FileIO {

bool exists(const std::string& path);
std::string expandPath(const std::string& path);   // "~/x" -> "$HOME/x"
std::string baseName(const std::string& path);     // "a/b/c.txt" -> "c.txt"
std::string parentDir(const std::string& path);    // "a/b/c.txt" -> "a/b",  "c.txt" -> ""

// "notes.txt" -> "/home/me/notes.txt". The folder part is made absolute (links resolved); the file need not exist.
std::string absolutePath(const std::string& path);

// Creates the folders a file would go in, if they are missing ("a/b/c.txt" makes a and a/b).
bool makeParentDirs(const std::string& path, std::string& error);

// Reads a whole file. Returns false (and sets error) for missing, unreadable, folder or binary files.
bool readFile(const std::string& path, std::string& text, std::string& error);

// Writes text to a temporary file and renames it over the target.
bool writeFile(const std::string& path, const std::string& text, std::string& error);

// What the bytes of a text are: "UTF-8" (also for an empty text), "ASCII", "UTF-8 BOM" or "invalid UTF-8".
std::string detectEncoding(const std::string& text);

// Modification time (nanoseconds) and size of a file. False if it does not exist (or is a folder).
bool statFile(const std::string& path, long long& mtimeNs, long long& size);

// A fast 64-bit hash of some bytes (FNV-1a). Used to tell whether a file really changed.
unsigned long long hashBytes(const std::string& bytes);

// What the editor shows for the bytes of a file: '\r' removed, tabs turned into 4 spaces.
std::string normalizeText(const std::string& raw, bool* hadTabs = nullptr);
}

// The file that is currently open: its absolute path and the text as it was last saved.
class Document {
public:
    enum class DiskState { Unchanged, Changed, Deleted };

    // Loads the file into the view, or starts an empty new file if it doesn't exist yet.
    // On failure the view is left untouched.
    bool open(const std::string& path, TextView& view, std::string& message);

    bool save(const TextView& view, std::string& message);                       // needs a path
    bool saveAs(const std::string& path, const TextView& view, std::string& message);

    bool hasPath() const { return !m_path.empty(); }
    const std::string& path() const { return m_path; }
    std::string name() const;                         // file name, or "[untitled]"
    bool modified(const TextView& view) const;        // true if the text differs from the last save
    std::string lineEndingName() const { return m_crlf ? "CRLF" : "LF"; }   // what a save writes
    const std::string& encodingName() const { return m_encoding; }          // see FileIO::detectEncoding

    // ---- changes made to the file by someone else ----
    // Compares the file on disk with what this document last read or wrote. Cheap: only a stat() unless
    // the time or size differs. Changed is reported every time until reload() or acknowledgeDisk() is called;
    // Deleted is reported once.
    DiskState checkDisk();
    bool reload(TextView& view, std::string& message);   // replaces the text with the file on disk, keeps the cursor
    void acknowledgeDisk();                              // "keep mine": the disk version is known, stop asking
    bool deletedOnDisk() const { return m_deleted; }

    // An untitled document that counts as saved (for a read-only copy such as "the version on disk").
    void loadUntitled(const std::string& text, TextView& view);

private:
    void applyRaw(const std::string& raw, const std::string& path, TextView& view, bool& hadTabs);
    void recordDisk(const std::string& raw);          // remember size, time and hash of what is on disk now

    std::string m_path;
    std::string m_savedText;
    bool m_crlf = false;                              // file used Windows line endings
    std::string m_encoding = "UTF-8";                 // found when the file was opened, checked again on save

    long long m_mtimeNs = 0;                          // the file on disk, as last read or written
    long long m_size = -1;
    unsigned long long m_hash = 0;
    bool m_onDisk = false;                            // the file existed when last read or written
    bool m_deleted = false;                           // it has vanished since
};