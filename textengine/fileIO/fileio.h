#pragma once
#include <string>
#include <vector>

namespace FileIO {

// Paths are UTF-8 strings. '/' is the separator on every system.
bool exists(const std::string& path);
bool isDirectory(const std::string& path);         // links are followed
bool isAbsolute(const std::string& path);
bool isRoot(const std::string& path);              // "/" (or "C:/" on Windows)
std::string homeDir();                             // $HOME (or %USERPROFILE%), empty if unknown
std::string currentDir();                          // the working folder, empty if unknown
std::string canonicalPath(const std::string& path);   // links resolved; the path as it is if it does not exist
std::string expandPath(const std::string& path);   // "~/x" -> "<home>/x"
std::string baseName(const std::string& path);     // "a/b/c.txt" -> "c.txt"
std::string parentDir(const std::string& path);    // "a/b/c.txt" -> "a/b",  "c.txt" -> ""
std::string joinPath(const std::string& dir, const std::string& name);   // "a" + "b" -> "a/b",  "/" + "b" -> "/b"

// What is in a folder: sub-folders and ordinary files (links are followed), in no particular order.
struct DirEntry {
    std::string name;
    bool dir;
};
bool listDir(const std::string& dir, std::vector<DirEntry>& out, std::string& error);

// "notes.txt" -> "/home/me/notes.txt". The folder part is made absolute (links resolved); the file need not exist.
std::string absolutePath(const std::string& path);

// Creates the folders a file would go in, if they are missing ("a/b/c.txt" makes a and a/b).
bool makeParentDirs(const std::string& path, std::string& error);

// The biggest file readFile() accepts, in bytes. Default 50 MB; 0 = no limit. Bigger files are refused with a message
// instead of freezing the program or running out of memory.
size_t maxFileBytes();
void setMaxFileBytes(size_t bytes);

// Reads a whole file. Returns false (and sets error) for missing, unreadable, folder, binary or too big files.
bool readFile(const std::string& path, std::string& text, std::string& error);

// Writes text to a temporary file and renames it over the target.
bool writeFile(const std::string& path, const std::string& text, std::string& error);

// What the bytes of a text are: "UTF-8" (also for an empty text), "ASCII", "UTF-8 BOM" or "invalid UTF-8".
std::string detectEncoding(const std::string& text);

// Modification time (nanoseconds) and size of a file. False if it does not exist (or is a folder).
bool statFile(const std::string& path, long long& mtimeNs, long long& size);

// A fast 64-bit hash of some bytes (FNV-1a). Used to tell whether a file really changed.
unsigned long long hashBytes(const std::string& bytes);

// What the editor holds for the bytes of a file: '\r' removed. Tabs stay tabs (hadTabs says whether there were any),
// so saving does not change a file's indentation.
std::string normalizeText(const std::string& raw, bool* hadTabs = nullptr);
}