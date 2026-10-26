#pragma once
#include <string>
#include <vector>

// The find / replace panel pinned at the bottom (Kate style): layout, drawing and click targets.
// It keeps no state of its own. The caller passes in what to show.
namespace FindPanel {

struct Info {
    std::string findText, replaceText;
    bool replaceOpen = false;      // the replace row is shown
    bool replaceActive = false;    // typing goes to the replace field
    bool matchCase = false, wholeWord = false, regex = false, inSelection = false;
    size_t count = 0;              // number of matches
    int current = -1;              // index of the current match, or -1
    std::string error;             // e.g. "invalid regex"
    bool findSelected = false;     // the text of the find field is highlighted as selected
    bool replaceSelected = false;  // the same for the replace field
};

enum class Action { None, Prev, Next, SelectAll, ToggleCase, ToggleWord, ToggleRegex, ToggleSelection,
                    Close, Replace, ReplaceAll, FocusFind, FocusReplace };

struct Button {
    Action action;
    int row, x, w;                 // 1-based screen position
    std::string label;
};

struct Layout {
    int findRow = 0;
    int replaceRow = 0;            // 0 = not shown
    int fieldX = 11, fieldW = 10;  // text fields (same for both rows)
    int counterX = 0, counterW = 12;
    std::vector<Button> buttons;
};

// The panel sits just above the two bars at the bottom of a W x H screen.
Layout layout(int W, int H, bool replaceOpen);

// Which control is at screen column x, row y (1-based).
Action hit(const Layout& L, int x, int y);

// The panel as terminal output. cursorRow / cursorCol (1-based) receive where the text cursor belongs.
std::string draw(const Info& in, int W, int H, int& cursorRow, int& cursorCol);

}