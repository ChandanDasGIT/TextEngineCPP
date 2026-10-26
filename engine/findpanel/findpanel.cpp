#include "findpanel.h"
#include <algorithm>

namespace FindPanel {

namespace {

int cols(const std::string& s) {
    int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// the last maxCols columns of s (never cuts a UTF-8 character)
std::string tailCols(const std::string& s, int maxCols) {
    int n = 0;
    size_t i = s.size();
    while (i > 0) {
        size_t j = i - 1;
        while (j > 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
        if (n + 1 > maxCols) break;
        ++n;
        i = j;
    }
    return s.substr(i);
}

std::string display(const std::string& s) {      // line breaks and tabs are shown as symbols
    std::string o;
    for (char c : s) {
        if (c == '\n') o += "↵";
        else if (c == '\t') o += "→";
        else o += c;
    }
    return o;
}

std::string at(int r, int c) { return "\x1b[" + std::to_string(r) + ";" + std::to_string(c) + "H"; }

struct Def { Action action; const char* label; };

}

Layout layout(int W, int H, bool replaceOpen) {
    Layout L;
    L.findRow = replaceOpen ? H - 3 : H - 2;
    L.replaceRow = replaceOpen ? H - 2 : 0;
    L.fieldX = 11;                                  // after the 10-column label
    const int minField = 12;

    // from the right edge inwards; the ones at the end are dropped first when the screen is narrow
    static const Def findDefs[] = {
        {Action::Close, " × "}, {Action::ToggleSelection, " Sel "}, {Action::ToggleRegex, " .* "},
        {Action::ToggleWord, " Word "}, {Action::ToggleCase, " Aa "}, {Action::SelectAll, " All "},
        {Action::Next, " ↓ "}, {Action::Prev, " ↑ "},
    };
    int right = W - 1;
    int leftmost = W;
    for (const Def& d : findDefs) {
        int w = cols(d.label);
        int x = right - w + 1;
        int counterX = x - 1 - L.counterW;
        if (counterX - L.fieldX - 1 < minField) break;
        L.buttons.push_back({d.action, L.findRow, x, w, d.label});
        right = x - 2;
        leftmost = x;
    }
    L.counterX = leftmost - 1 - L.counterW;
    L.fieldW = std::max(1, L.counterX - L.fieldX - 1);

    if (replaceOpen) {
        static const Def replDefs[] = {{Action::Replace, " Replace "}, {Action::ReplaceAll, " Replace All "}};
        int x = L.fieldX + L.fieldW + 1;
        for (const Def& d : replDefs) {
            int w = cols(d.label);
            if (x + w - 1 > W - 1) break;
            L.buttons.push_back({d.action, L.replaceRow, x, w, d.label});
            x += w + 1;
        }
    }
    return L;
}

Action hit(const Layout& L, int x, int y) {
    for (const Button& b : L.buttons)
        if (y == b.row && x >= b.x && x < b.x + b.w) return b.action;
    if (y == L.findRow && x >= L.fieldX && x < L.fieldX + L.fieldW) return Action::FocusFind;
    if (L.replaceRow != 0 && y == L.replaceRow && x >= L.fieldX && x < L.fieldX + L.fieldW) return Action::FocusReplace;
    return Action::None;
}

std::string draw(const Info& in, int W, int H, int& cursorRow, int& cursorCol) {
    Layout L = layout(W, H, in.replaceOpen);
    const std::string base = "\x1b[0m\x1b[48;5;236m\x1b[38;5;252m";
    const char* red = "\x1b[38;5;203m";
    std::string s;

    auto rowBackground = [&](int r) { s += at(r, 1) + base + std::string(static_cast<size_t>(W), ' ') + "\x1b[0m"; };

    // a text field; returns how many columns of text are shown (where the cursor goes)
    auto field = [&](int r, const std::string& text, bool active, bool selected) {
        std::string shown = tailCols(display(text), std::max(1, L.fieldW - 1));
        int used = cols(shown);
        const std::string bg = active ? "\x1b[48;5;240m" : "\x1b[48;5;238m";
        s += at(r, L.fieldX);
        if (selected && used > 0) s += "\x1b[48;5;75m\x1b[38;5;16m" + shown;
        else s += bg + "\x1b[38;5;255m" + shown;
        s += bg + std::string(static_cast<size_t>(std::max(0, L.fieldW - used)), ' ') + "\x1b[0m";
        return used;
    };

    auto isOn = [&](Action a) {
        switch (a) {
            case Action::ToggleCase: return in.matchCase;
            case Action::ToggleWord: return in.wholeWord;
            case Action::ToggleRegex: return in.regex;
            case Action::ToggleSelection: return in.inSelection;
            default: return false;
        }
    };

    // ---- find row ----
    rowBackground(L.findRow);
    s += at(L.findRow, 1) + base + "\x1b[1m Find:    \x1b[0m";
    int findUsed = field(L.findRow, in.findText, !in.replaceActive, in.findSelected);

    std::string ctext;
    const char* ccolor = "\x1b[38;5;250m";
    if (!in.error.empty()) { ctext = in.error; ccolor = red; }
    else if (!in.findText.empty()) {
        if (in.count == 0) { ctext = "No results"; ccolor = red; }
        else ctext = (in.current >= 0 ? std::to_string(in.current + 1) : std::string("?")) + " of " + std::to_string(in.count);
    }
    if (static_cast<int>(ctext.size()) > L.counterW) ctext.resize(static_cast<size_t>(L.counterW));
    ctext.append(static_cast<size_t>(L.counterW) - ctext.size(), ' ');
    s += at(L.findRow, L.counterX) + base + ccolor + ctext + "\x1b[0m";

    // ---- replace row ----
    int replUsed = 0;
    if (in.replaceOpen) {
        rowBackground(L.replaceRow);
        s += at(L.replaceRow, 1) + base + "\x1b[1m Replace: \x1b[0m";
        replUsed = field(L.replaceRow, in.replaceText, in.replaceActive, in.replaceSelected);
    }

    // ---- buttons ----
    for (const Button& b : L.buttons) {
        s += at(b.row, b.x);
        s += isOn(b.action) ? "\x1b[48;5;75m\x1b[38;5;16m\x1b[1m" : "\x1b[48;5;239m\x1b[38;5;252m";
        s += b.label + "\x1b[0m";
    }

    cursorRow = in.replaceActive ? L.replaceRow : L.findRow;
    cursorCol = L.fieldX + (in.replaceActive ? replUsed : findUsed);
    return s;
}

}