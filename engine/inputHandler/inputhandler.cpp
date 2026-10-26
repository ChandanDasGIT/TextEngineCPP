#include "inputhandler.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../fileIO/fileio.h"
#include "../findpanel/findpanel.h"
#include "../gutter/gutter.h"
#include "../wordwrap/wordwrap.h"

namespace {

struct Mods {
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
};

// Terminal modifier number: 1 + (shift 1, alt 2, ctrl 4).
Mods modsFrom(int mod) {
    Mods m;
    if (mod > 1) {
        int bits = mod - 1;
        m.shift = (bits & 1) != 0;
        m.alt = (bits & 2) != 0;
        m.ctrl = (bits & 4) != 0;
    }
    return m;
}

// While the find bar is open but you clicked on the text, keys work as in normal editing: for the length of one
// key the mode is switched to Edit, and put back afterwards (unless the key closed or changed the bar).
struct EditorKeys {
    InputState& st;
    TextView& view;
    InputMode saved;
    bool on;
    EditorKeys(InputState& s, TextView& v, bool enable)
        : st(s), view(v), saved(s.mode),
          on(enable && s.editorFocus && (s.mode == InputMode::Find || s.mode == InputMode::Replace)) {
        if (on) st.mode = InputMode::Edit;
    }
    ~EditorKeys() {
        if (!on) return;
        if (st.mode == InputMode::Edit && st.editorFocus) st.mode = saved;                    // the bar stays open
        else if (st.mode == InputMode::Prompt) { view.clearFind(); st.editorFocus = false; }  // Save as / Open: the bar goes
        else st.editorFocus = false;                                                          // closed, or Ctrl+F / Ctrl+R
    }
    EditorKeys(const EditorKeys&) = delete;
    EditorKeys& operator=(const EditorKeys&) = delete;
};

// ---------------------------------------------------------------- find bar and file name bar

std::string& activeField(InputState& st) {
    return (st.mode == InputMode::Replace && st.replaceFieldActive) ? st.replaceText : st.findText;
}

bool typingInFindField(const InputState& st) {
    return st.mode == InputMode::Find || (st.mode == InputMode::Replace && !st.replaceFieldActive);
}

FindOptions findOptionsOf(const InputState& st) {
    FindOptions o;
    o.matchCase = st.findCase;
    o.wholeWord = st.findWord;
    o.regex = st.findRegex;
    o.inSelection = st.findInSel;
    return o;
}

void runFind(TextView& view, InputState& st) { view.findSetQuery(st.findText, findOptionsOf(st)); }

std::string escapeRegex(const std::string& s) {
    const std::string special = "\\^$.|?*+()[]{}";
    std::string out;
    for (char c : s) {
        if (special.find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

void openFind(TextView& view, InputState& st, bool withReplace) {
    if (st.mode == InputMode::Edit) {
        view.findOpen();
        st.findInSel = false;
        if (view.hasSelection()) {
            std::string sel = view.selectedText();
            if (sel.find('\n') == std::string::npos) st.findText = st.findRegex ? escapeRegex(sel) : sel;   // start with the selected text
            else st.findInSel = true;                           // several lines selected: search inside them
        }
        st.status = "find: Alt+C case, Alt+W word, Alt+R regex, Alt+L selection, Alt+Enter select all";
    }
    st.mode = withReplace ? InputMode::Replace : InputMode::Find;
    st.replaceFieldActive = false;
    runFind(view, st);
}

void closeFind(TextView& view, InputState& st) {
    st.mode = InputMode::Edit;
    st.editorFocus = false;
    st.replaceFieldActive = false;
    st.findInSel = false;
    view.clearFind();
    st.status = "find closed";
}

void selectAllFromPanel(TextView& view, InputState& st) {
    if (!view.selectAllMatches()) { st.status = "no matches"; return; }
    size_t n = view.cursorCount();
    st.mode = InputMode::Edit;
    st.editorFocus = false;
    st.replaceFieldActive = false;
    st.findInSel = false;
    view.clearFind();
    st.status = std::to_string(n) + " cursors on all matches";
}

void toggleFindOption(char l, TextView& view, InputState& st) {
    if (l == 'c') { st.findCase = !st.findCase; st.status = st.findCase ? "case sensitive" : "ignore case"; }
    else if (l == 'w') { st.findWord = !st.findWord; st.status = st.findWord ? "whole word" : "part of words too"; }
    else if (l == 'r') { st.findRegex = !st.findRegex; st.status = st.findRegex ? "regex on" : "regex off"; }
    else if (l == 'l') { st.findInSel = !st.findInSel; st.status = st.findInSel ? "in selection" : "whole text"; }
    else return;
    runFind(view, st);
    if (st.findInSel && !view.findScoped()) st.status = "select text before opening find to search inside it";
}

// A click on a control of the find panel.
void panelAction(FindPanel::Action a, TextView& view, InputState& st) {
    using A = FindPanel::Action;
    switch (a) {
        case A::Prev: view.findPrev(); st.status = "previous match"; break;
        case A::Next: view.findNext(); st.status = "next match"; break;
        case A::SelectAll: selectAllFromPanel(view, st); break;
        case A::ToggleCase: toggleFindOption('c', view, st); break;
        case A::ToggleWord: toggleFindOption('w', view, st); break;
        case A::ToggleRegex: toggleFindOption('r', view, st); break;
        case A::ToggleSelection: toggleFindOption('l', view, st); break;
        case A::Close: closeFind(view, st); break;
        case A::Replace:
            if (st.mode == InputMode::Replace) st.status = view.replaceCurrent(st.replaceText) ? "replaced" : "next match";
            break;
        case A::ReplaceAll:
            if (st.mode == InputMode::Replace) {
                size_t n = view.replaceAll(st.replaceText);
                st.status = "replaced " + std::to_string(n);
            }
            break;
        case A::FocusFind: st.replaceFieldActive = false; break;
        case A::FocusReplace: if (st.mode == InputMode::Replace) st.replaceFieldActive = true; break;
        case A::None: break;
    }
}

void popChar(std::string& s) {   // removes one whole UTF-8 character
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

void openPrompt(InputState& st, PromptKind kind) {
    st.mode = InputMode::Prompt;
    st.promptKind = kind;
    st.browser.open(kind == PromptKind::SaveAs ? FileBrowser::Mode::SaveAs : FileBrowser::Mode::Open, st.filePath);
    st.status = kind == PromptKind::SaveAs ? "save as" : "open file";
}

// Carries out what the file browser reported: a picked file is opened, or saved to.
void acceptBrowserResult(const FileBrowser::Result& r, InputState& st) {
    if (r.kind != FileBrowser::Result::Choose) {
        if (!r.message.empty()) st.status = r.message;
        return;
    }
    const std::string& path = r.path;

    if (st.promptKind == PromptKind::SaveAs) {
        bool otherFile = path != st.filePath;
        if (otherFile && FileIO::exists(path) && st.armedBefore != Armed::Overwrite) {
            st.armed = Armed::Overwrite;
            st.status = "file exists - press Enter again to overwrite it";
            return;
        }
        st.fileRequest = FileRequest::SaveAs;
    } else {
        st.fileRequest = FileRequest::Open;
    }
    st.requestPath = path;
    st.mode = InputMode::Edit;
}

void submitPrompt(InputState& st) { acceptBrowserResult(st.browser.activate(), st); }

// ---------------------------------------------------------------- keys

// \r and \r\n from the terminal become \n
std::string normalizedPaste(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            o += '\n';
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else {
            o += s[i];
        }
    }
    return o;
}

// Pasted text goes to whatever has the keyboard: the editor, a find / replace field or the file browser.
void pasteText(const std::string& raw, TextView& view, InputState& st) {
    std::string s = normalizedPaste(raw);
    if (s.empty()) return;

    if (st.mode == InputMode::Edit) {
        view.paste(s);
        st.status = "paste";
        return;
    }
    if (st.mode == InputMode::Prompt) {                        // a path has no line breaks
        std::string one;
        for (char c : s)
            if (c != '\n') one += c;
        st.browser.typeText(one);
        return;
    }
    if (st.fieldSelPrev) activeField(st).clear();              // pasting over a selected field replaces it
    activeField(st) += s;                                      // line breaks stay: searches can span lines
    if (typingInFindField(st)) runFind(view, st);
    st.status = "pasted";
}

void typeText(const std::string& s, TextView& view, InputState& st) {
    if (st.mode == InputMode::Edit) {
        view.insertText(s);
        st.status = "type";
        return;
    }
    if (st.mode == InputMode::Prompt) { st.browser.typeText(s); return; }
    if (st.fieldSelPrev) activeField(st).clear();             // typing over a selected field replaces it
    for (char c : s)
        if (c != '\r' && c != '\n') activeField(st) += c;
    if (typingInFindField(st)) runFind(view, st);
}

void backspaceKey(bool word, TextView& view, InputState& st) {
    if (st.mode == InputMode::Edit) {
        if (word) { view.deleteWord(false); st.status = "delete word"; }
        else { view.backspace(); st.status = "backspace"; }
        return;
    }
    if (st.mode == InputMode::Prompt) { st.browser.backspace(word); return; }
    if (st.fieldSelPrev) activeField(st).clear();
    else popChar(activeField(st));
    if (typingInFindField(st)) runFind(view, st);
}

void deleteKey(Mods m, TextView& view, InputState& st) {
    if (st.mode != InputMode::Edit) return;
    if (m.ctrl) { view.deleteWord(true); st.status = "delete word"; }
    else { view.del(); st.status = "delete"; }
}

void enterKey(TextView& view, InputState& st, Mods m) {
    if (st.mode == InputMode::Prompt) { submitPrompt(st); return; }
    if (st.mode == InputMode::Edit) { view.enter(); st.status = "enter"; return; }

    if (m.alt) { selectAllFromPanel(view, st); return; }                // Alt+Enter: a cursor on every match
    if (m.ctrl) {                                                       // Ctrl+Enter: a line break in the field
        if (st.fieldSelPrev) activeField(st).clear();
        activeField(st) += '\n';
        if (typingInFindField(st)) runFind(view, st);
        return;
    }
    if (st.mode == InputMode::Replace && st.replaceFieldActive) {
        st.status = view.replaceCurrent(st.replaceText) ? "replaced" : "next match";
    } else if (m.shift) {
        view.findPrev();
        st.status = "previous match";
    } else {
        view.findNext();
        st.status = "next match";
    }
}
void tabKey(bool shift, TextView& view, InputState& st) {
    if (st.mode == InputMode::Prompt) return;
    if (st.mode == InputMode::Edit) {
        if (shift) { view.shiftTab(); st.status = "outdent"; }
        else { view.tab(); st.status = "tab"; }
    } else if (st.mode == InputMode::Find) {
        st.mode = InputMode::Replace;
        st.replaceFieldActive = true;
    } else {
        st.replaceFieldActive = !st.replaceFieldActive;
    }
}

void escapeKey(TextView& view, InputState& st) {
    if (st.editorFocus) { closeFind(view, st); return; }         // the text has the keyboard, the find bar is still open
    if (st.mode == InputMode::Prompt) {
        st.mode = InputMode::Edit;
        st.status = "cancelled";
    } else if (st.mode != InputMode::Edit) {
        closeFind(view, st);
    } else if (view.cursorCount() > 1) {
        view.clearExtraCursors();
        st.status = "single cursor";
    }
}

void arrowKey(char dir, Mods m, TextView& view, InputState& st) {
    if (st.mode == InputMode::Prompt) {                      // the file browser
        if (dir == 'A' && m.alt) st.browser.goUp();          // Alt+Up: parent folder
        else if (dir == 'A') st.browser.moveSelection(-1);
        else if (dir == 'B') st.browser.moveSelection(1);
        return;
    }
    if (st.mode != InputMode::Edit) {        // in the find bar, Up and Down jump between matches
        if (dir == 'A') { view.findPrev(); st.status = "previous match"; }
        if (dir == 'B') { view.findNext(); st.status = "next match"; }
        return;
    }
    if (dir == 'A' || dir == 'B') {
        if (m.alt && m.ctrl) {                                // Ctrl+Alt+Up/Down: add a cursor
            if (dir == 'A') view.addCursorAbove(); else view.addCursorBelow();
            st.status = "add cursor";
            return;
        }
        if (m.alt && m.shift) {                               // Alt+Shift+Up/Down: duplicate lines
            view.duplicateLines(dir == 'B');
            st.status = "duplicate lines";
            return;
        }
        if (m.alt) {                                          // Alt+Up/Down: move lines
            view.moveLines(dir == 'A');
            st.status = "move lines";
            return;
        }
    }
    switch (dir) {
        case 'A': view.moveUp(m.shift);    st.status = "up"; break;
        case 'B': view.moveDown(m.shift);  st.status = "down"; break;
        case 'C':
            if (m.ctrl) { view.moveWordRight(m.shift); st.status = "word right"; }
            else { view.moveRight(m.shift); st.status = "right"; }
            break;
        case 'D':
            if (m.ctrl) { view.moveWordLeft(m.shift); st.status = "word left"; }
            else { view.moveLeft(m.shift); st.status = "left"; }
            break; break;
        default: break;
    }
}

void homeKey(Mods m, TextView& view, InputState& st) {
    if (st.mode == InputMode::Prompt) { st.browser.selectFirst(); return; }
    if (st.mode != InputMode::Edit) return;
    view.moveHome(m.shift);
    st.status = "home";
}

void endKey(Mods m, TextView& view, InputState& st) {
    if (st.mode == InputMode::Prompt) { st.browser.selectLast(); return; }
    if (st.mode != InputMode::Edit) return;
    view.moveEnd(m.shift);
    st.status = "end";
}

void pageKey(bool down, Mods m, TextView& view, InputState& st) {
    if (st.mode == InputMode::Prompt) {
        int rows = static_cast<int>(st.browser.pageRows());
        st.browser.moveSelection(down ? rows : -rows);
        return;
    }
    if (st.mode != InputMode::Edit) return;
    size_t n = st.pageRows > 1 ? st.pageRows - 1 : 1;   // keep one row of overlap
    for (size_t i = 0; i < n; ++i) {
        if (down) view.moveDown(m.shift);
        else view.moveUp(m.shift);
    }
    st.status = down ? "page down" : "page up";
}

void ctrlLetter(char l, Mods m, TextView& view, InputState& st) {
    if (l == 'q') {                                                      // Ctrl+Q
        if (st.modified && st.armedBefore != Armed::Quit) {
            st.armed = Armed::Quit;
            st.status = "unsaved changes - press Ctrl+Q again to quit";
            return;
        }
        st.running = false;
        return;
    }
    if (st.mode == InputMode::Prompt) return;                            // nothing else works in the file name bar

    if (l == 'f') { openFind(view, st, false); return; }                // Ctrl+F
    if (l == 'r') { openFind(view, st, true); return; }                 // Ctrl+R
        if (st.mode == InputMode::Find || st.mode == InputMode::Replace) {   // keys inside a find / replace field
        std::string& field = activeField(st);
        if (l == 'v') { pasteText(st.clipboard, view, st); return; }
        if (l == 'a') {
            st.fieldSelected = !field.empty();
            st.status = field.empty() ? "field is empty" : "field text selected";
            return;
        }
        if (l == 'c' || l == 'x') {
            if (st.editorSelPrev && view.hasSelection()) {               // text selected in the editor with the mouse
                std::string t = l == 'x' ? view.cut() : view.copy();
                if (!t.empty()) { st.clipboard = t; st.clipboardDirty = true; }
                if (l == 'x') runFind(view, st);                         // the text changed: search again
                else st.editorSelected = true;                           // copying keeps the selection
                st.status = l == 'x' ? "cut" : "copied";
                return;
            }
            if (field.empty()) { st.status = "nothing to copy"; return; }
            st.clipboard = field;
            st.clipboardDirty = true;
            if (l == 'x') {
                field.clear();
                if (typingInFindField(st)) runFind(view, st);
                st.status = "cut";
            } else {
                st.fieldSelected = st.fieldSelPrev;                      // copying keeps the selection
                st.status = "copied";
            }
            return;
        }
        return;
    }
    if (st.mode != InputMode::Edit) return;

    switch (l) {
        case 's':                                                        // Ctrl+S: save (Ctrl+Shift+S: save as)
            if (m.shift || st.filePath.empty()) openPrompt(st, PromptKind::SaveAs);
            else st.fileRequest = FileRequest::Save;
            break;
        case 'o':                                                        // Ctrl+O: open a file
            if (st.modified && st.armedBefore != Armed::Discard) {
                st.armed = Armed::Discard;
                st.status = "unsaved changes - press Ctrl+O again to open anyway";
            } else {
                openPrompt(st, PromptKind::Open);
            }
            break;
        case 'a': view.selectAll(); st.status = "select all"; break;
        case 'c': { //changed this block 9988
            std::string t = view.copy();
            if (!t.empty()) { st.clipboard = t; st.clipboardDirty = true; }   // nothing selected: keep the old clipboard
            st.status = "copy " + std::to_string(t.size()) + " bytes";
            break;
        }
        case 'x': {
            std::string t = view.cut();
            if (!t.empty()) { st.clipboard = t; st.clipboardDirty = true; }
            st.status = "cut";
            break;
        }
        case 'v': view.paste(st.clipboard); st.status = "paste"; break;
        case 'b': view.toggleBold(); st.status = "bold"; break;
        case 'i': view.toggleItalic(); st.status = "italic"; break;
        case 'u': view.toggleUnderline(); st.status = "underline"; break;
        case 'p': view.changeFontSize(1); st.status = "size +"; break;
        case 'n': view.changeFontSize(-1); st.status = "size -"; break;
        case 'z':
            if (m.shift) st.status = view.redo() ? "redo" : "nothing to redo";
            else st.status = view.undo() ? "undo" : "nothing to undo";
            break;
        case 'y': st.status = view.redo() ? "redo" : "nothing to redo"; break;
        case 'd': st.status = view.addNextOccurrence() ? "add next occurrence" : "no more occurrences"; break;
        case 'k': view.deleteLines(); st.status = "delete lines"; break;
        default: break;
    }
}

void altLetter(char l, TextView& view, InputState& st) {
    if (st.mode == InputMode::Prompt) {                                 // Alt+H in the file browser: show or hide hidden files
        if (l == 'h') st.browser.toggleHidden();
        return;
    }
    if (st.mode == InputMode::Edit && l == 'm') {                       // Alt+M: show or hide the minimap
        st.minimapOn = !st.minimapOn;
        st.status = st.minimapOn ? "minimap on" : "minimap off";
        return;
    }
    if (st.mode == InputMode::Edit && l == 's') {                       // Alt+S: save as
        openPrompt(st, PromptKind::SaveAs);
        return;
    }
    if (st.mode == InputMode::Find || st.mode == InputMode::Replace) {    // Alt+C / W / R / L, Alt+Enter
        if (l == '\r' || l == '\n') selectAllFromPanel(view, st);
        else if (l == 'a' && st.mode == InputMode::Replace) {            // Alt+A: replace all
            size_t n = view.replaceAll(st.replaceText);
            st.status = "replaced " + std::to_string(n);
        }
        else toggleFindOption(l, view, st);
    }
}

// A key that arrived as a number (kitty style ESC[key;mod u, xterm style ESC[27;mod;key ~)
void numberedKey(int key, Mods m, TextView& view, InputState& st) {
    if (key >= 'A' && key <= 'Z') { key += 32; m.shift = true; }
    if (key == 'd' && m.ctrl && m.alt && st.mode == InputMode::Edit) {   // Ctrl+Alt+D: a cursor on every occurrence
        st.status = view.selectAllOccurrences() ? std::to_string(view.cursorCount()) + " cursors" : "nothing to select";
        return;
    }
    if (key >= 'a' && key <= 'z') {
        if (m.ctrl) ctrlLetter(static_cast<char>(key), m, view, st);
        else if (m.alt) altLetter(static_cast<char>(key), view, st);
        else typeText(std::string(1, static_cast<char>(m.shift ? std::toupper(key) : key)), view, st);
        return;
    }
    if ((key == '[' || key == ']') && m.ctrl && m.alt) {      // Ctrl+Alt+[ fold, Ctrl+Alt+] unfold (+Shift = all)
        if (st.mode != InputMode::Edit) return;
        bool open = key == '[';
        if (m.shift) {
            if (open) view.foldAll(); else view.unfoldAll();
            st.status = open ? "fold all" : "unfold all";
        } else {
            bool ok = open ? view.foldAtCursor() : view.unfoldAtCursor();
            st.status = ok ? (open ? "fold" : "unfold") : "nothing to fold";
        }
        return;
    }
    switch (key) {
        case 27: escapeKey(view, st); break;
        case 13: enterKey(view, st, m); break;
        case 9: tabKey(m.shift, view, st); break;
        case 8:
        case 127: backspaceKey(m.ctrl, view, st); break;
        default: break;
    }
}

// ---------------------------------------------------------------- mouse

void selectWordAt(TextView& view, Position p) {
    const std::string& line = view.buffer().line(p.line);
    if (line.empty()) { view.moveCursor(p); return; }

    auto cls = [](unsigned char ch) {
        if (ch == ' ' || ch == '\t') return 0;
        return (std::isalnum(ch) || ch == '_' || ch >= 0x80) ? 1 : 2;
    };
    size_t i = p.col < line.size() ? p.col : line.size() - 1;
    int k = cls(static_cast<unsigned char>(line[i]));
    size_t a = i, b = i + 1;
    while (a > 0 && cls(static_cast<unsigned char>(line[a - 1])) == k) --a;
    while (b < line.size() && cls(static_cast<unsigned char>(line[b])) == k) ++b;
    view.select(Position{p.line, a}, Position{p.line, b});
}

void selectLineAt(TextView& view, Position p) {
    const TextBuffer& tb = view.buffer();
    Position from{p.line, 0};
    Position to = p.line + 1 < tb.lineCount() ? Position{p.line + 1, 0}
                                              : Position{p.line, tb.lineLength(p.line)};
    view.select(from, to);
}

bool sameClickSpot(Position a, Position b) {
    if (a.line != b.line) return false;
    size_t d = a.col > b.col ? a.col - b.col : b.col - a.col;
    return d <= 1;
}

// ESC [ < button ; x ; y M (press) or m (release)
void handleMouse(const std::string& params, char fin, TextView& view, InputState& st) {
    int b, x, y;
    if (sscanf(params.c_str() + 1, "%d;%d;%d", &b, &x, &y) != 3) return;
    if (fin == 'm') { st.minimapDrag = false; st.stickyDrag = false; return; }   // a mouse button was released
    if (fin != 'M') return;
    if (st.mode == InputMode::Prompt) {                                 // the file browser is open: it gets the mouse
        int wheel = b & ~(4 | 8 | 16);
        if (wheel == 64) st.browser.scroll(-3);
        else if (wheel == 65) st.browser.scroll(3);
        else if (wheel == 0) acceptBrowserResult(st.browser.click(x, y), st);
        return;
    }
    int base = b & ~(4 | 8 | 16);
    if (st.mode == InputMode::Find || st.mode == InputMode::Replace) {   // the find panel at the bottom
        int sy = y + st.screenRowOffset;                                 // real screen row
        FindPanel::Layout L = FindPanel::layout(st.screenW, st.screenH, st.mode == InputMode::Replace);
        if (sy >= L.findRow) {                                           // the panel and the bars below it
            st.editorSelected = false;
            st.editorFocus = false;                                      // clicking the panel gives the keyboard back to it
            if (base == 0) panelAction(FindPanel::hit(L, x, sy), view, st);
            return;
        }
    }
    if (st.minimapX > 0) {                                              // the minimap is shown
        bool over = x >= st.minimapX && x < st.minimapX + st.minimapCols && y >= 1 && y <= st.minimapRows;
        if (base == 0 && over) {                                        // pressed on the minimap
            st.minimapDrag = true;
            st.minimapEvent = 1;
            st.minimapRow = y;
            st.status = "minimap";
            return;
        }
        if (base == 32 && st.minimapDrag) {                             // dragging it (the mouse may leave the minimap)
            st.minimapEvent = 2;
            st.minimapRow = y;
            return;
        }
    }
    if (st.stickyRows > 0 && base == 0 && y >= 1 && y <= st.stickyRows &&
        x <= Gutter::width(view.buffer().lineCount()) + static_cast<int>(st.wrapWidth)) {   // a pinned line
        st.stickyClickRow = y;                                          // test.cpp scrolls to it
        st.stickyDrag = true;
        st.clickCount = 0;
        if (st.mode != InputMode::Edit) st.editorFocus = true;         // find bar open: the text gets the keyboard
        st.status = "go to pinned line";
        return;
    }
    if (base == 32 && st.stickyDrag) return;                            // dragging after such a click: no selection
    if ((b & ~(4 | 8 | 16)) == 64 || (b & ~(4 | 8 | 16)) == 65) {   // mouse wheel
        st.wheelRows += ((b & ~(4 | 8 | 16)) == 64) ? -3 : 3;       // 3 rows per notch
        st.status = "scroll";
        return;
    }

    const TextBuffer& tb = view.buffer();
    auto rows = view.folding().visibleRows(WordWrap::layout(tb, st.wrapWidth));
    size_t r = std::min<size_t>(static_cast<size_t>(std::max(y - 1, 0)) + st.scrollTop, rows.size() - 1);
    int gw = Gutter::width(tb.lineCount());
    size_t cx = static_cast<size_t>(std::max(x - 1 - gw, 0));
    Position p = WordWrap::toPosition(rows, r, cx);

    bool shift = (b & 4) != 0;
    bool alt = (b & 8) != 0;
    int button = b & ~(4 | 8 | 16);

    if (button == 0) {                       // left button pressed
        // the fold marker and the bar next to it
        bool onMarker = !alt && !shift && x - 1 >= gw - 2 && x - 1 < gw &&
                        rows[r].start == 0 && view.folding().canFold(rows[r].line);
        if (onMarker) {
            view.toggleFoldAt(rows[r].line);
            st.clickCount = 0;
            st.status = "toggle fold";
        } else if (alt) { view.addCursor(p); st.status = "add cursor"; }
        else if (shift) { view.moveCursor(p, true); st.status = "extend selection"; }
        else {
            long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
            bool again = st.clickCount > 0 && now - st.lastClickMs <= 400 && sameClickSpot(p, st.lastClickPos);
            st.clickCount = again ? st.clickCount % 3 + 1 : 1;
            st.lastClickMs = now;
            st.lastClickPos = p;

            if (st.clickCount == 2) { selectWordAt(view, p); st.status = "select word"; }
            else if (st.clickCount == 3) { selectLineAt(view, p); st.status = "select line"; }
            else { view.moveCursor(p); st.status = "click"; }
        }
    } else if (button == 32) {               // moved with the left button held
        view.moveCursor(p, true);
        st.status = "drag select";
    }
    if (st.mode != InputMode::Edit && (button == 0 || button == 32)) {   // find bar open and you clicked on the text:
        st.editorSelected = true;
        st.editorFocus = true;                                           // the text gets the keyboard
    }
}

// ---------------------------------------------------------------- escape sequences

void handleCsi(const std::string& params, char fin, TextView& view, InputState& st) {
    if (!params.empty() && params[0] == '<') { handleMouse(params, fin, view, st); return; }

    // numbers separated by ';' (a ':' starts a sub-field that atoi ignores)
    std::vector<int> nums;
    size_t k = 0;
    while (true) {
        size_t e = params.find(';', k);
        if (e == std::string::npos) e = params.size();
        nums.push_back(std::atoi(params.substr(k, e - k).c_str()));
        if (e >= params.size()) break;
        k = e + 1;
    }

    if (fin == 'u') {                                         // ESC [ key ; mod u
        numberedKey(nums[0], modsFrom(nums.size() > 1 ? nums[1] : 1), view, st);
        return;
    }
    if (fin == '~' && nums[0] == 27 && nums.size() >= 3) {    // ESC [ 27 ; mod ; key ~
        numberedKey(nums[2], modsFrom(nums[1]), view, st);
        return;
    }

    Mods m = modsFrom(nums.size() > 1 ? nums[1] : 1);
    switch (fin) {
        case 'A': case 'B': case 'C': case 'D': arrowKey(fin, m, view, st); break;
        case 'H': homeKey(m, view, st); break;
        case 'F': endKey(m, view, st); break;
        case 'Z': tabKey(true, view, st); break;                  // Shift+Tab
        case '~':
            if (nums[0] == 3) deleteKey(m, view, st);
            else if (nums[0] == 1 || nums[0] == 7) homeKey(Mods{}, view, st);
            else if (nums[0] == 4 || nums[0] == 8) endKey(Mods{}, view, st);
            else if (nums[0] == 5) pageKey(false, m, view, st);   // Page Up
            else if (nums[0] == 6) pageKey(true, m, view, st);    // Page Down
            break;
        default: break;
    }
}

}

void handleInput(const char* buf, ssize_t n, TextView& view, InputState& st) {
    ssize_t i = 0;
    while (i < n && st.running) {
        if (st.inPaste) {                                                     // inside a terminal paste: collect it
            st.pasteBuf.append(buf + i, static_cast<size_t>(n - i));
            i = n;
            size_t e = st.pasteBuf.find("\x1b[201~");
            if (e == std::string::npos) return;                               // the rest comes with the next read
            std::string text = st.pasteBuf.substr(0, e);
            std::string rest = st.pasteBuf.substr(e + 6);
            st.pasteBuf.clear();
            st.inPaste = false;
            { EditorKeys ek(st, view, true); pasteText(text, view, st); }
            if (!rest.empty()) handleInput(rest.data(), static_cast<ssize_t>(rest.size()), view, st);
            return;
        }
        st.armedBefore = st.armed;       // any key cancels a "press again" warning,
        st.armed = Armed::None;          // unless the key itself sets it again
        st.fieldSelPrev = st.fieldSelected;   // any key ends a field selection,
        st.fieldSelected = false;             // unless the key itself (Ctrl+A, Ctrl+C) sets it again
        
        unsigned char c = static_cast<unsigned char>(buf[i]);
        bool isMouse = c == 27 && i + 2 < n && buf[i + 1] == '[' && buf[i + 2] == '<';
        if (!isMouse) {                                   // any key ends the "selected with the mouse" state,
            st.editorSelPrev = st.editorSelected;         // unless the key itself (Ctrl+C) sets it again
            st.editorSelected = false;
        }
        EditorKeys ek(st, view, !isMouse);                // keys go to the text if you clicked on it (mouse events never do)

        if (c == 27) {
            if (i + 1 >= n) { escapeKey(view, st); ++i; continue; }          // the Esc key
            if (buf[i + 1] == 'O') { i += 3; continue; }                      // ESC O x: function keys, ignored
            if (buf[i + 1] != '[') {                                          // ESC + letter: Alt+letter
                altLetter(static_cast<char>(std::tolower(static_cast<unsigned char>(buf[i + 1]))), view, st);
                i += 2;
                continue;
            }
            ssize_t j = i + 2;
            while (j < n && !(buf[j] >= 0x40 && buf[j] <= 0x7E)) ++j;
            if (j >= n) return;
            std::string params(buf + i + 2, buf + j);
            char fin = buf[j];
            i = j + 1;
            if (fin == '~' && params == "200") {                              // start of a terminal paste
                st.inPaste = true;
                st.pasteBuf.clear();
                continue;
            }
            handleCsi(params, fin, view, st);
            continue;
        }

        if (c == 127 || c == 8) { backspaceKey(false, view, st); ++i; }
        else if (c == 13 || c == 10) { enterKey(view, st, Mods{}); ++i; }
        else if (c == 9) { tabKey(false, view, st); ++i; }
        else if (c >= 1 && c <= 26) { ctrlLetter(static_cast<char>('a' + c - 1), Mods{}, view, st); ++i; }
        else if (c >= 32) {                                                   // normal typing
            std::string run;
            while (i < n && static_cast<unsigned char>(buf[i]) >= 32 && buf[i] != 127) run += buf[i++];
            typeText(run, view, st);
        } else {
            ++i;                                                              // ignore other keys
        }
    }
}


void refreshFind(TextView& view, InputState& st) {
    if (st.mode != InputMode::Find && st.mode != InputMode::Replace) return;
    st.findInSel = false;                    // a selection of the other tab means nothing here
    view.findOpen();
    runFind(view, st);
}