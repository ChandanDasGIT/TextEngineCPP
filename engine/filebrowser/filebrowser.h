#pragma once
#include <cstddef>
#include <string>
#include <vector>

// The panel behind Open (Ctrl+O) and Save as (Ctrl+S on a new file, Alt+S).
// It lists one folder at a time. You walk through the folders, then pick a file (Open)
// or type a file name (Save as). The panel only keeps its own state: it never opens or writes files.
class FileBrowser {
public:
    enum class Mode { Open, SaveAs };

    struct Result {
        enum Kind { None, Choose } kind = None;
        std::string path;       // Choose: the full path the user picked
        std::string message;    // None: a short hint for the status bar ("" = nothing to say)
    };

    // Starts the panel. `currentFile` is the open file's path ("" = none): the panel starts in its folder.
    void open(Mode mode, const std::string& currentFile);
    void setScreen(int cols, int rows) { m_cols = cols; m_rows = rows; }

    // ---- keys ----
    void typeText(const std::string& s);   // Open: filters the list.  Save as: the file name.  "dir/" walks into dir.
    void backspace(bool wholeName);        // on an empty name: go up one folder
    void moveSelection(int delta);
    void selectFirst();
    void selectLast();
    void goUp();                           // parent folder
    void toggleHidden();                   // show or hide names that start with '.'
    Result activate();                     // Enter
    size_t pageRows() const;               // rows to jump for Page Up / Page Down

    // ---- mouse (screen column and row, both 1-based) ----
    Result click(int x, int y);            // click selects, double click = Enter
    void scroll(int rows);                 // mouse wheel

    // ---- drawing ----
    std::string draw() const;              // escape codes that draw the panel on top of the screen
    void cursorPos(int& row, int& col) const;   // where the terminal cursor goes (in the name field)

    const std::string& directory() const { return m_dir; }
    const std::string& name() const { return m_name; }

private:
    struct Entry { std::string name; bool dir; };
    struct Layout { int left, width, top, listTop, listRows, inputRow, hintRow; };

    Layout layout() const;
    bool loadDir(const std::string& path);
    void rebuildView();
    void ensureVisible();
    void selectByName(const std::string& name);
    void enterEntry(const Entry& e, bool clearName);
    bool navigateFromName();
    std::string resolveName(const std::string& name) const;
    std::string label() const;

    Mode m_mode = Mode::Open;
    std::string m_dir;                 // the folder being shown (absolute)
    std::string m_name;                // Open: the filter text.  Save as: the file name
    std::string m_error;               // shown instead of the hint, until the next key
    std::vector<Entry> m_all;          // everything in the folder: ".." first, then folders, then files
    std::vector<size_t> m_view;        // indexes into m_all that are shown (hidden files and filter applied)
    size_t m_sel = 0;                  // selected row, an index into m_view
    size_t m_top = 0;                  // first visible row
    bool m_showHidden = false;
    bool m_listFocus = false;          // Save as: true after Up/Down, so Enter acts on the selected row
    int m_cols = 80, m_rows = 24;
    long long m_lastClickMs = 0;
    size_t m_lastClickIdx = 0;
};
