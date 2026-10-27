#pragma once
#include <string>
#include "../fileIO/fileio.h"
#include "../textview/textview.h"

// The file that is currently open: its absolute path and the text as it was last saved.
class Document {
public:
    enum class DiskState { Unchanged, Changed, Deleted };

    // Loads the file into the view, or starts an empty new file if it doesn't exist yet.
    // On failure the view is left untouched.
    bool open(const std::string& path, TextView& view, std::string& message);

    bool save(const TextView& view, std::string& message);                       // needs a path
    bool saveAs(const std::string& path, const TextView& view, std::string& message);

    // Looks at how a text is indented: tabs, or spaces (and how many per level). Used when a file is opened,
    // so Tab in that file inserts what the file already uses. No indented lines: 4 spaces.
    static IndentStyle detectIndent(const std::string& text);

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
    bool m_bom = false;                               // file started with a UTF-8 BOM: not shown in the text, written again on save
    std::string m_encoding = "UTF-8";                 // found when the file was opened, checked again on save

    long long m_mtimeNs = 0;                          // the file on disk, as last read or written
    long long m_size = -1;
    unsigned long long m_hash = 0;
    bool m_onDisk = false;                            // the file existed when last read or written
    bool m_deleted = false;                           // it has vanished since
};