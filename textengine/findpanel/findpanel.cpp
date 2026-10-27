#include "findpanel.h"

namespace FindPanel {

std::vector<ButtonDef> buttons(bool replaceOpen) {
    std::vector<ButtonDef> v = {
        {Action::Prev, "↑", false}, {Action::Next, "↓", false}, {Action::SelectAll, "All", false},
        {Action::ToggleCase, "Aa", false}, {Action::ToggleWord, "Word", false}, {Action::ToggleRegex, ".*", false},
        {Action::ToggleSelection, "Sel", false}, {Action::Close, "×", false},
    };
    if (replaceOpen) {
        v.push_back({Action::Replace, "Replace", true});
        v.push_back({Action::ReplaceAll, "Replace All", true});
    }
    return v;
}

bool isOn(const Info& in, Action a) {
    switch (a) {
        case Action::ToggleCase: return in.matchCase;
        case Action::ToggleWord: return in.wholeWord;
        case Action::ToggleRegex: return in.regex;
        case Action::ToggleSelection: return in.inSelection;
        default: return false;
    }
}

std::string counterText(const Info& in) {
    if (!in.error.empty()) return in.error;
    if (in.findText.empty()) return "";
    if (in.count == 0) return "No results";
    return (in.current >= 0 ? std::to_string(in.current + 1) : std::string("?")) + " of " + std::to_string(in.count);
}

bool counterIsError(const Info& in) {
    return !in.error.empty() || (!in.findText.empty() && in.count == 0);
}

std::string fieldText(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\n') o += "↵";
        else if (c == '\t') o += "→";
        else o += c;
    }
    return o;
}

}
