#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "../textview/textview.h"
#include "../document/document.h"
#include "../gutterstyle/gutterstyle.h"
#include "../syntaxhighlight/syntaxhighlighter.h"

// Everything that belongs to one open document.
struct EditorTab {
    TextView view;
    Document doc;
    GutterStyle gutter;
    SyntaxHighlighter syntax;
    size_t scrollTop = 0;          // first visible visual row
    bool followCursor = true;      // false after a wheel scroll, until the cursor moves
    int id = 0;                    // never reused
    int untitledNo = 0;            // number shown for untitled tabs
    bool modifiedCache = false;    // last known "has unsaved changes" (refreshed while the tab is current)
    std::string label;             // shown instead of "untitled N" (for read-only copies)
};

class TabManager {
public:
    TabManager();                  // starts with one empty untitled tab

    size_t count() const { return m_tabs.size(); }
    size_t currentIndex() const { return m_current; }
    EditorTab& current() { return *m_tabs[m_current]; }
    const EditorTab& current() const { return *m_tabs[m_current]; }
    EditorTab& at(size_t i) { return *m_tabs[i]; }
    const EditorTab& at(size_t i) const { return *m_tabs[i]; }

    // ---- opening and closing ----
    size_t newTab();                                                // empty tab at the end, selected
    bool openFile(const std::string& path, std::string& message);   // switches if already open; reuses an empty untitled tab
    // Drag and drop: opens every path in order. Each new file goes to the end and the last one that
    // opened (or was already open) becomes the current tab. Returns how many worked; message says
    // what happened, including the files that failed.
    size_t openFiles(const std::vector<std::string>& paths, std::string& message);

    enum class CloseResult { Closed, NeedsConfirm };
    // A tab with unsaved changes is only closed on the second call in a row (or with force).
    CloseResult closeTab(size_t index, bool force = false);
    void disarmClose() { m_armedClose = 0; }
    bool reopenClosed(std::string& message);


    // ---- changes on disk ----
    bool reloadTab(size_t index, std::string& message);        // replace the text with the file on disk
    bool openDiskCopy(size_t index, std::string& message);     // the version on disk, in a new tab (and stop asking about it)

    // ---- switching ----
    void select(size_t i);
    void next();
    void prev();
    void selectLast();
    void move(size_t from, size_t to);                          // reorder (for drag and drop)


    // Goes up whenever the selection or the order of the tabs changes (new tab, close, select, move, open).
    // A tab strip compares it with the value it saw last, to know that it should show the current tab again.
    unsigned long long changeCount() const { return m_changes; }

    // ---- state ----
    bool modified(size_t i) const;
    size_t modifiedCount() const;
    std::string title(size_t i) const;

private:
    std::unique_ptr<EditorTab> makeTab();
    int nextUntitledNo() const;
    void refreshCurrent();
    static bool isPristine(const EditorTab& t);

    std::vector<std::unique_ptr<EditorTab>> m_tabs;
    size_t m_current = 0;
    int m_nextId = 0;
    int m_armedClose = 0;                  // id of the tab that is waiting for a second close
    std::vector<std::string> m_closed;     // paths of closed tabs, newest last
    unsigned long long m_changes = 0;      // see changeCount()
};