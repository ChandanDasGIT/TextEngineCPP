#pragma once
#include <cstddef>
#include <string>
#include "../textbuffer/textbuffer.h"

// What the editor engine understands as input. Nothing here knows about terminals, bytes or pixels:
// a terminal front end and a QML front end both turn their own events into these, and the shared
// code (key bindings, find panel, editing) only ever sees these types.
namespace Input {

struct Mods {
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
};

enum class Key {
    Char,        // a character key: see KeyEvent::ch
    Enter, Backspace, Delete, Tab, Escape, Insert,
    Up, Down, Left, Right,
    Home, End, PageUp, PageDown,
    Function     // F1..F12: KeyEvent::ch holds the number
};

// A key press that is a command, not typing.
//  - Char keys: ch is the key WITHOUT shift, so Ctrl+Shift+Z is {Char, 'z', ctrl+shift}. Letters are lower case.
//  - A Char key with no Ctrl and no Alt is typing. The front end sends it as a TextEvent instead
//    (so "Shift+a" arrives as TextEvent "A", and accented or IME input is never lost).
//  - Tab with shift is {Tab, shift}: there is no separate "backtab" key.
struct KeyEvent {
    Key key = Key::Char;
    char32_t ch = 0;
    Mods mods;
};

// Typed characters (UTF-8). A run of several characters is fine.
struct TextEvent {
    std::string text;
};

// A block of pasted text (UTF-8, line breaks already '\n').
struct PasteEvent {
    std::string text;
};

enum class Button { None, Left, Middle, Right };

// Where on the screen a mouse event landed. The front end works this out, because only it knows the layout
// (character cells in a terminal, pixels in QML).
enum class Region {
    Text,        // the text area: pos is the document position under the pointer
    FoldMarker,  // the fold marker next to the line number: pos.line is the line
    Minimap,     // row = 0-based row inside the minimap
    Sticky,      // a pinned line: row = 0-based index of the pinned line
    TabBar,      // x = column inside the tab bar (0-based)
    FindPanel,   // control = the FindPanel::Action value of the control under the pointer
    Browser,     // the file browser is open: x and row hold the 1-based screen column and row (the one place that is still cell based)
    Other        // the status bars or empty space: ignored by the engine
};

struct MouseEvent {
    enum class Kind { Press, Release, Drag, Wheel };
    Kind kind = Kind::Press;
    Button button = Button::None;   // Press, Release and Drag: which button
    Mods mods;
    Region region = Region::Text;
    Position pos;                   // Text and FoldMarker
    int row = 0;                    // Minimap and Sticky
    int x = 0;                      // TabBar column
    int control = 0;                // FindPanel
    int clicks = 1;                 // 1, 2 or 3 on a left Press: counted by the front end (QML has its own count)
    int wheelRows = 0;              // Wheel: rows to scroll, negative = up
};

// One input event of any kind, in the order it happened.
struct Event {
    enum class Type { Key, Text, Paste, Mouse };
    Type type = Type::Key;
    KeyEvent key;
    std::string text;               // Text and Paste
    MouseEvent mouse;

    static Event makeKey(const KeyEvent& k) { Event e; e.type = Type::Key; e.key = k; return e; }
    static Event makeText(const std::string& s) { Event e; e.type = Type::Text; e.text = s; return e; }
    static Event makePaste(const std::string& s) { Event e; e.type = Type::Paste; e.text = s; return e; }
    static Event makeMouse(const MouseEvent& m) { Event e; e.type = Type::Mouse; e.mouse = m; return e; }
};

}
