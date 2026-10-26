#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "../textview/textview.h"
#include "../fileIO/fileio.h"
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

// One visible tab in the tab bar. x and width are in terminal columns, starting at 0.
struct TabCell {
    size_t index = 0;
    int x = 0;
    int width = 0;
    std::string title;             // already shortened to fit
    bool active = false;
    bool modified = false;
};

struct TabBarLayout {
    std::vector<TabCell> cells;
    int cols = 0;
    bool leftArrow = false, rightArrow = false;
    int leftX = 0, leftW = 0;
    int rightX = 0, rightW = 0;
    int plusX = -1, plusW = 0;     // the "+" button (plusW == 0: not shown)
};

struct TabHit {
    enum Kind { None, Tab, Close, ArrowLeft, ArrowRight, Plus };
    Kind kind = None;
    size_t index = 0;              // for Tab and Close
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
    size_t newTab();                                            // empty tab next to the current one, selected
    bool openFile(const std::string& path, std::string& message);   // switches if already open; reuses an empty untitled tab

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


    // ---- drag and drop in the tab bar ----
    bool beginDrag(int x, int cols);        // press on a tab: remembers which tab and where it was grabbed
    bool dragTo(int x, int cols);           // pointer moved: moves the tab to the slot under it, true if the order changed
    void endDrag() { m_dragId = 0; }
    bool dragging() const { return m_dragId != 0; }
    size_t draggedIndex() const;            // index of the tab being dragged (count() if none)

    // ---- state ----
    bool modified(size_t i) const;
    size_t modifiedCount() const;
    std::string title(size_t i) const;

    // ---- tab bar (columns are 0-based) ----
    const TabBarLayout& layout(int cols);                       // also scrolls the strip so the current tab is visible
    TabHit hit(int x, int cols);
    void scrollBar(int delta);                                  // move the strip by whole tabs (does not change the selection)

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

    size_t m_scroll = 0;                   // first tab shown in the bar
    bool m_follow = true;                  // keep the current tab in view
    TabBarLayout m_layout;

        int m_dragId = 0;                      // id of the tab being dragged (0 = no drag)
    int m_dragGrab = 0;                    // columns between the tab's left edge and where it was grabbed
};