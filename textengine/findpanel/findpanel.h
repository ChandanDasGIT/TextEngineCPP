#pragma once
#include <cstddef>
#include <string>
#include <vector>

// The find / replace panel (Kate style): what it shows, and which controls it has.
// It keeps no state of its own and knows nothing about screens: the caller passes in what to show,
// and the front end decides where everything goes and how it looks.
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

// The controls. A front end sends one of these as MouseEvent::control when a control is clicked.
enum class Action { None, Prev, Next, SelectAll, ToggleCase, ToggleWord, ToggleRegex, ToggleSelection,
                    Close, Replace, ReplaceAll, FocusFind, FocusReplace };

struct ButtonDef {
    Action action;
    const char* label;             // short text for the button: "Aa", "Word", ".*" ...
    bool onReplaceRow;             // false = beside the find field, true = beside the replace field
};

// The buttons, left to right. The replace row's buttons are only returned when replaceOpen is true.
// If there is no room for all of them, drop buttons from the start of the find row's list first
// (Close, the toggles and the counter matter most).
std::vector<ButtonDef> buttons(bool replaceOpen);

// Is this toggle button switched on? (Case, Word, Regex, Selection; everything else is false.)
bool isOn(const Info& in, Action a);

// The text of the match counter: "3 of 12", "No results", the error, or "" when nothing is searched for.
std::string counterText(const Info& in);
bool counterIsError(const Info& in);     // "No results" and errors are drawn in red

// A field's text as one line: line breaks and tabs are shown as symbols (↵ and →).
std::string fieldText(const std::string& s);

}
