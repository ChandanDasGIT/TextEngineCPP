#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "../tabs/tabmanager.h"

// The tab strip of a TERMINAL front end: how many columns each tab takes, which tabs fit, the arrows and the "+"
// button, which control sits under a column, and dragging tabs. All positions are in character columns, so a QML
// front end does not need this file: it draws its own tabs from TabManager (count, title, modified, select ...).
// The strip only reads the tabs; it changes them only to reorder them while a tab is dragged.

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

class TabBar {
public:
    // Columns are 0-based. `cols` is the width of the bar.
    const TabBarLayout& layout(TabManager& tabs, int cols);     // also scrolls the strip so the current tab is visible
    TabHit hit(TabManager& tabs, int x, int cols);
    void scrollBar(TabManager& tabs, int delta);                // move the strip by whole tabs (does not change the selection)

    // ---- drag and drop ----
    bool beginDrag(TabManager& tabs, int x, int cols);          // press on a tab: remembers which tab and where it was grabbed
    bool dragTo(TabManager& tabs, int x, int cols);             // pointer moved: moves the tab to the slot under it, true if the order changed
    void endDrag() { m_dragId = 0; }
    bool dragging() const { return m_dragId != 0; }
    size_t draggedIndex(const TabManager& tabs) const;          // index of the tab being dragged (tabs.count() if none)

private:
    size_t m_scroll = 0;                   // first tab shown in the bar
    bool m_follow = true;                  // keep the current tab in view
    unsigned long long m_seenChanges = 0;        // TabManager::changeCount() at the last layout: if it moved, follow the current tab again
    TabBarLayout m_layout;

    int m_dragId = 0;                      // id of the tab being dragged (0 = no drag)
    int m_dragGrab = 0;                    // columns between the tab's left edge and where it was grabbed
};
