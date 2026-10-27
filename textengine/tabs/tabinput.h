#pragma once
#include <sys/types.h>
#include <string>
#include <vector>

// A tab command found in the raw terminal input.
struct TabAction {
    enum Kind { New, Close, Next, Prev, Goto, GotoLast, Reopen, Quit, BarClick, BarMiddle, BarWheel,
                MoveLeft, MoveRight, BarDrag, BarRelease };
    Kind kind = New;
    int a = 0;   // Goto: tab number (0-based). BarClick / BarMiddle: column (0-based). BarWheel: +1 or -1.
};

// The input either goes on to the normal input handler (data) or is a tab command (action).
struct InputPiece {
    std::string data;
    bool isAction = false;
    TabAction action;
};

namespace TabInput {

// Cuts raw terminal bytes into pieces, in order, so the keys typed before a tab command still reach the
// right tab. Recognised: Ctrl+T, Ctrl+W, Ctrl+Q, Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PageUp/Down, Alt+1..9, Alt+T.
// tabKeys:  false = only Ctrl+Q is taken (use while a bar or the file browser has the keyboard).
// barRows:  screen rows used by the tab bar. Mouse events are moved up by this many rows so the text
//           handler sees row 1 as the first text row; presses on the bar itself become actions.
// dragging: true while a tab is being dragged. Mouse motion and release then become BarDrag / BarRelease.
std::vector<InputPiece> split(const char* buf, ssize_t n, bool tabKeys, int barRows, bool dragging = false);

}