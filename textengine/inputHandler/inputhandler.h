#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include "../textview/textview.h"
#include "../filebrowser/filebrowser.h"
#include "../inputevents/inputevents.h"

enum class InputMode {
    Edit,       // normal typing
    Find,       // the find bar has the keyboard
    Replace,    // the find bar and the replace bar are open
    Prompt,     // the file browser (open / save as) has the keyboard
};

enum class PromptKind { Open, SaveAs };

// Something the input handler asks the program to do. The handler cannot do these itself, because they belong to the
// program: files, the system clipboard, tabs, scrolling. Every handle...() call adds the requests it caused to
// InputState::requests, in the order they happened. The program takes them all, carries each one out and clears the list.
// (A QML wrapper turns each kind into a signal.)
struct Request {
    enum class Kind {
        SaveFile,           // save the current file under its name
        SaveFileAs,         // text = the path to save to
        OpenFile,           // text = the path to open
        CopyToClipboard,    // text = what to put on the system clipboard
        Quit,
        NewTab, CloseTab, NextTab, PrevTab,
        GotoTab,            // number = tab number, 0-based
        GotoLastTab, ReopenTab, MoveTabLeft, MoveTabRight,
        TabBarMouse,        // a mouse event landed on the tab bar: it is in mouse
        MinimapPress,       // the minimap was pressed: number = row (1 = top of the text area)
        MinimapDrag,        // ... and dragged: number = row
        StickyClick,        // a pinned line was clicked: number = 1 for the first pinned line, 2 for the second ...
        ScrollWheel         // number = rows to scroll, negative = up
    };

    Kind kind = Kind::Quit;
    std::string text;
    int number = 0;
    Input::MouseEvent mouse;

    static Request of(Kind k, int number = 0, std::string text = std::string()) {
        Request r;
        r.kind = k;
        r.number = number;
        r.text = std::move(text);
        return r;
    }
};

// Which "press again to confirm" warning is showing.
enum class Armed { None, Discard, Overwrite };

struct InputState {
    std::string clipboard;
    std::string status = "ready";
    bool running = true;     // owned by the front end: it sets this to false when it has carried out a Quit request
    size_t pageRows = 20;    // text rows on screen, set by the front end, used by Page Up/Down

    InputMode mode = InputMode::Edit;
    std::string findText;
    std::string replaceText;
    bool replaceFieldActive = false;   // in Replace mode: true when typing goes to the replace bar
    bool findCase = false;             // true = case sensitive search
    bool findWord = false;             // whole word
    bool findRegex = false;            // regular expression
    bool findInSel = false;            // search only inside the selection
    bool fieldSelected = false;        // the text of the find / replace field is selected (Ctrl+A)
    bool fieldSelPrev = false;         // the same, as it was when the current event arrived
    bool editorSelected = false;       // text in the editor was selected with the mouse while the find bar is open
    bool editorSelPrev = false;        // the same, as it was when the current event arrived
    bool editorFocus = false;          // the find bar is open, but the text has the keyboard (you clicked on it)

    // ---- file state (set by the front end before every event) ----
    std::string filePath;              // empty = no name yet
    std::string fileName = "[untitled]";
    bool modified = false;             // unsaved changes

    // ---- OUTPUT: requests the front end carries out, in order, then clears ----
    std::vector<Request> requests;
    void post(Request r) { requests.push_back(std::move(r)); }

    // ---- the file browser (open / save as) ----
    PromptKind promptKind = PromptKind::Open;
    FileBrowser browser;

    // ---- "press again to confirm" ----
    Armed armed = Armed::None;         // set by the event just handled
    Armed armedBefore = Armed::None;   // what was showing when this event arrived

    // ---- minimap ----
    bool minimapOn = true;       // Alt+M shows or hides it
    bool minimapDrag = false;    // the left button went down on the minimap and has not been released yet

    // ---- sticky scroll ----
    bool stickyDrag = false;     // the left button went down on a pinned line and has not been released yet
};

// ---- the entry points: one call per event ----
void handleKey(const Input::KeyEvent& key, TextView& view, InputState& state);
void handleText(const std::string& text, TextView& view, InputState& state);
void handlePaste(const std::string& text, TextView& view, InputState& state);
void handleMouse(const Input::MouseEvent& mouse, TextView& view, InputState& state);
void handleEvent(const Input::Event& event, TextView& view, InputState& state);   // calls one of the four above

// Runs the find bar's search again on this view. Call it after switching tabs while the find bar is open.
void refreshFind(TextView& view, InputState& state);