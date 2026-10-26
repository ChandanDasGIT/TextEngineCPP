#pragma once
#include <sys/types.h>
#include <cstddef>
#include <string>
#include "../textview/textview.h"
#include "../filebrowser/filebrowser.h"

enum class InputMode {
    Edit,       // normal typing
    Find,       // the find bar has the keyboard
    Replace,    // the find bar and the replace bar are open
    Prompt,     // the file browser (open / save as) has the keyboard
};

enum class PromptKind { Open, SaveAs };

// What the input handler asks test.cpp to do with files.
enum class FileRequest { None, Save, SaveAs, Open };

// Which "press again to confirm" warning is showing.
enum class Armed { None, Quit, Discard, Overwrite };

struct InputState {
    std::string clipboard;
    std::string status = "ready";
    bool running = true;
    size_t wrapWidth = 80;   // text width in columns (set by test.cpp), used to map clicks
    size_t pageRows = 20;    // number of text rows on screen (set by test.cpp), used by Page Up/Down
    size_t scrollTop = 0;    // first visible visual row (set by test.cpp), used to map clicks
    long long lastClickMs = 0;   // time of the last left click, for double and triple click
    Position lastClickPos{};
    int clickCount = 0;
    int wheelRows = 0;    // rows the mouse wheel asked to scroll since the last input (negative = up)

    InputMode mode = InputMode::Edit;
    std::string findText;
    std::string replaceText;
    bool replaceFieldActive = false;   // in Replace mode: true when typing goes to the replace bar
    bool findCase = false;             // true = case sensitive search
    bool findWord = false;             // whole word
    bool findRegex = false;            // regular expression
    bool findInSel = false;            // search only inside the selection
    bool inPaste = false;              // between the start and end markers of a terminal paste
    std::string pasteBuf;              // the pasted text collected so far
    bool fieldSelected = false;        // the text of the find / replace field is selected (Ctrl+A)
    bool fieldSelPrev = false;         // the same, as it was when the current key arrived
    bool editorSelected = false;       // text in the editor was selected with the mouse while the find bar is open
    bool editorSelPrev = false;        // the same, as it was when the current key arrived
    bool editorFocus = false;          // the find bar is open, but the text has the keyboard (you clicked on it)
    bool clipboardDirty = false;       // the clipboard changed: test.cpp copies it to the system clipboard
    
    // ---- screen (set by test.cpp), used to click on the find panel ----
    int screenW = 80, screenH = 24;
    int screenRowOffset = 0;           // rows that were cut off the top of mouse positions (the tab bar)

    // ---- file state (set by test.cpp before every key) ----
    std::string filePath;              // empty = no name yet
    std::string fileName = "[untitled]";
    bool modified = false;             // unsaved changes

    // ---- file requests (set here, carried out by test.cpp) ----
    FileRequest fileRequest = FileRequest::None;
    std::string requestPath;

    // ---- the file browser (open / save as) ----
    PromptKind promptKind = PromptKind::Open;
    FileBrowser browser;

    // ---- "press again to confirm" ----
    Armed armed = Armed::None;         // set by the key just handled
    Armed armedBefore = Armed::None;   // what was showing when this key arrived

        // ---- minimap ----
    bool minimapOn = true;       // Alt+M shows or hides it
    int minimapX = 0;            // screen column of its left edge (1 = first column); 0 = not shown. Set by the program.
    int minimapCols = 0;         // screen columns it takes. Set by the program.
    int minimapRows = 0;         // screen rows it takes, counted from the top. Set by the program.
    bool minimapDrag = false;    // the left button went down on the minimap and has not been released yet
    int minimapEvent = 0;        // set by the mouse handler: 0 nothing, 1 pressed on the minimap, 2 dragging it
    int minimapRow = 0;          // screen row (1 = top) of that press or drag

    // ---- sticky scroll ----
    int stickyRows = 0;          // pinned lines drawn at the top of the text. Set by the program.
    bool stickyDrag = false;     // the left button went down on a pinned line and has not been released yet
    int stickyClickRow = 0;      // set by the mouse handler: 1 = the first pinned line was clicked, 2 = the second ...
};

// Takes raw terminal bytes and calls the matching engine functions.
void handleInput(const char* buf, ssize_t n, TextView& view, InputState& state);

// Runs the find bar's search again on this view. Call it after switching tabs while the find bar is open.
void refreshFind(TextView& view, InputState& state);