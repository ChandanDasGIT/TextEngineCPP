// Terminal test harness for textengine. It is the "front end": it reads terminal bytes, turns them into
// engine events (through TerminalDecoder), and draws everything the engine only describes
// (gutter, tab bar, minimap, find panel, file browser, dialog).
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "terminaldecoder.h"
#include "textengine/textbuffer/textbuffer.h"
#include "textengine/textview/textview.h"
#include "textengine/inputHandler/inputhandler.h"
#include "textengine/inputevents/inputevents.h"
#include "textengine/gutter/gutter.h"
#include "textengine/gutterstyle/gutterstyle.h"
#include "textengine/fileIO/fileio.h" // IWYU pragma: keep
#include "textengine/wordwrap/wordwrap.h"
#include "textengine/syntaxhighlight/syntaxhighlighter.h"
#include "textengine/minimap/minimap.h"
#include "textengine/stickyscroll/stickyscroll.h"
#include "textengine/tabs/tabmanager.h"
#include "textengine/tabbar/tabbar.h"
#include "textengine/dialog/dialog.h"
#include "textengine/findpanel/findpanel.h"
#include "textengine/filebrowser/filebrowser.h"

static termios g_orig;
static const bool kRealExtraCursors = true;
static const int kTabRows = 2;           // screen rows used by the tab bar (the text starts below it)
static size_t g_viewRows = 20;           // how many text rows fit on screen
static TabManager g_tabs;
static TabBar g_bar;                     // the terminal tab strip (columns, hit testing, drag)
static Dialog g_dialog;                  // the "unsaved changes" box
enum class DialogFor { CloseTab, Quit, DiskChanged, SaveConflict };
static DialogFor g_dialogFor = DialogFor::CloseTab;
static int g_dialogTabId = 0;            // the tab a close dialog is about
static int g_closeAfterSaveId = 0;       // close this tab as soon as its save has finished
static bool g_quitAfterSave = false;     // quit as soon as every tab is saved
static int g_quitTriedId = 0;            // the tab whose save was started last while quitting

static Minimap g_minimap;
static StickyScroll g_sticky;
static size_t g_pinnedRows = 0;            // pinned lines drawn at the top of the text (set while drawing)
static const size_t kStickyJumpGap = 3;    // text rows between the pinned lines and a header you jumped to
static SyntaxHighlighter* g_mmSyntax = nullptr;   // the highlighter of the tab whose minimap is being drawn
static bool g_minimapOn = true;            // follows InputState::minimapOn (Alt+M)
static size_t g_mmGrab = 0;                // minimap rows between the top of the rectangle and where it was grabbed
static size_t g_mmVisibleLines = 1;        // document lines on screen, set while drawing the minimap
static const int kMinimapCols = 13;        // terminal columns it takes: 1 for edited lines, 11 for the picture, 1 for marks
static const size_t kMinimapChars = 88;    // characters of a line that the picture covers

// Where the last frame put things. The mouse code reads these to turn a screen cell into an engine event.
struct PanelHit { int row, x0, x1; FindPanel::Action action; };
struct BoxHit { int row, x0, x1; };
static std::vector<PanelHit> g_panelHits;  // find panel controls (1-based screen cells)
static std::vector<BoxHit> g_dialogBtns;   // dialog buttons, in button order
static int g_panelRows = 0;                // 0, 1 (find) or 2 (find + replace)
static int g_cursorRow = 1, g_cursorCol = 1;   // where the terminal cursor goes for the field that has the keyboard

static void out(const std::string& s) {
    ssize_t r = write(STDOUT_FILENO, s.data(), s.size());
    (void)r;
}

static void restoreTerminal() {
    // undo cursor shape, key reporting modes, mouse reporting, and leave the blank page
    out("\x1b[?2004l\x1b[?25h\x1b[>0;4 q\x1b[0 q\x1b[<u\x1b[>4;0m\x1b[?1006l\x1b[?1002l\x1b[?1000l\x1b[?1049l");
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
}

static void setupTerminal() {
    tcgetattr(STDIN_FILENO, &g_orig);
    atexit(restoreTerminal);
    termios raw = g_orig;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    // blank page + mouse click/drag reporting + Ctrl+letter key reporting (so Ctrl+I != Tab) + blinking bar cursor
    out("\x1b[?1049h\x1b[?1000h\x1b[?1002h\x1b[?1006h\x1b[?2004h\x1b[>1u\x1b[>4;2m\x1b[5 q");
}

static int termCols() {
    winsize ws{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    return ws.ws_col ? ws.ws_col : 80;
}

static int termRows() {
    winsize ws{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    return ws.ws_row ? ws.ws_row : 24;
}

// terminal columns the minimap takes (0 = hidden, or the terminal is too narrow)
static int minimapCols() {
    if (!g_minimapOn) return 0;
    return termCols() >= 60 ? kMinimapCols : 0;
}

static size_t wrapWidth(const TextBuffer& b) {
    int cols = termCols();
    int mm = minimapCols();
    int w = cols - Gutter::width(b.lineCount()) - 1 - (mm > 0 ? mm + 1 : 0);   // -1 keeps room for the cursor at line end
    return w > 1 ? static_cast<size_t>(w) : 1;
}

// ---- characters and screen cells ----
// The engine counts BYTES (always on a character boundary). The terminal counts CELLS: a character is
// 1 cell, a CJK character 2, a combining mark 0. These helpers convert between the two.

static char32_t decodeAt(const std::string& s, size_t i, size_t& len) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (i + n > s.size()) n = 1;
    char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    len = n;
    return cp;
}

static int cellWidth(char32_t cp) {
    if (cp == 0) return 0;
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F)) return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) || (cp >= 0x1F900 && cp <= 0x1F9FF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

static const size_t kTabWidth = 4;     // a tab character is drawn up to the next multiple of this

// cells a character takes when it starts at cell `col` of its row (a tab reaches the next tab stop)
static size_t charCells(char32_t cp, size_t col) {
    return cp == U'\t' ? kTabWidth - col % kTabWidth : static_cast<size_t>(cellWidth(cp));
}

// screen cells taken by the bytes [from, to) of s, when `from` is at cell 0 (the start of a row)
static size_t cellsBetween(const std::string& s, size_t from, size_t to) {
    size_t cells = 0;
    to = std::min(to, s.size());
    while (from < to) {
        size_t len = 1;
        char32_t cp = decodeAt(s, from, len);
        cells += charCells(cp, cells);
        from += len;
    }
    return cells;
}

static size_t displayCols(const std::string& s) { return cellsBetween(s, 0, s.size()); }

// The byte (between start and end) that sits under screen cell `cell`. A cell in the right half of a wide
// character belongs to the next character if it is more than half way. Past the end gives `end`.
static size_t byteAtCell(const std::string& s, size_t start, size_t end, size_t cell) {
    size_t b = start, cells = 0;
    end = std::min(end, s.size());
    while (b < end) {
        size_t len = 1;
        char32_t cp = decodeAt(s, b, len);
        size_t w = charCells(cp, cells);
        if (cell < cells + std::max<size_t>(w, 1)) return (w >= 2 && cell > cells + (w - 1) / 2) ? b + len : b;
        cells += w;
        b += len;
    }
    return end;
}

// text cut or padded to exactly `cols` cells (never cuts a character in half)
static std::string fitCells(const std::string& text, size_t cols) {
    std::string o;
    size_t cells = 0, i = 0;
    while (i < text.size()) {
        size_t len = 1;
        char32_t cp = decodeAt(text, i, len);
        size_t w = static_cast<size_t>(cellWidth(cp));
        if (cells + w > cols) break;
        o.append(text, i, len);
        cells += w;
        i += len;
    }
    o.append(cols - cells, ' ');
    return o;
}

// the end of the text that fits into `cols` cells (for a text field that scrolls to the right)
static std::string tailCells(const std::string& text, size_t cols) {
    size_t total = displayCols(text);
    if (total <= cols) return text;
    size_t i = 0, cells = total;
    while (i < text.size() && cells > (cols > 0 ? cols - 1 : 0)) {
        size_t len = 1;
        char32_t cp = decodeAt(text, i, len);
        cells -= static_cast<size_t>(cellWidth(cp));
        i += len;
    }
    return "…" + text.substr(i);
}

static std::string at(int row, int col) {
    return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H";
}

// One full-width bar on a screen row. `style` is an escape code like "\x1b[7m".
static std::string bar(int row, const std::string& text, int cols, const char* style) {
    return at(row, 1) + style + fitCells(text, static_cast<size_t>(cols)) + "\x1b[0m";
}

// The colour of a syntax token (the engine only says which kind a character is).
static const char* tokenColor(Token t) {
    switch (t) {
        case Token::Keyword:      return "\x1b[38;5;204m";
        case Token::Type:         return "\x1b[38;5;80m";
        case Token::Function:     return "\x1b[38;5;221m";
        case Token::Number:       return "\x1b[38;5;180m";
        case Token::String:       return "\x1b[38;5;114m";
        case Token::Comment:      return "\x1b[38;5;244m";
        case Token::Preprocessor: return "\x1b[38;5;176m";
        case Token::Constant:     return "\x1b[38;5;173m";
        case Token::Variable:     return "\x1b[38;5;117m";
        case Token::Property:     return "\x1b[38;5;110m";
        case Token::Attribute:    return "\x1b[38;5;139m";
        case Token::Class:        return "\x1b[38;5;79m";
        case Token::Identifier:   return "\x1b[38;5;215m";
        case Token::Modifier:     return "\x1b[38;5;203m";
        default:                  return "";
    }
}

// ---- tab bar ----

static std::string drawTabBar(int W) {
    const TabBarLayout& L = g_bar.layout(g_tabs, W);
    const int labelRow = kTabRows;                                 // the title sits on the bottom row
    const size_t dragged = g_bar.dragging() ? g_bar.draggedIndex(g_tabs) : g_tabs.count();

    std::string s = "\x1b[0m";
    for (int r = 1; r <= kTabRows; ++r)                            // bar background
        s += at(r, 1) + "\x1b[48;5;235m" + std::string(static_cast<size_t>(W), ' ') + "\x1b[0m";

    if (L.leftArrow) s += at(labelRow, L.leftX + 1) + "\x1b[48;5;235m\x1b[38;5;250m ‹ \x1b[0m";

    for (const TabCell& c : L.cells) {
        bool grabbed = c.index == dragged;
        bool lit = c.active || grabbed;
        const char* bg = grabbed ? "\x1b[48;5;241m" : c.active ? "\x1b[48;5;238m" : "\x1b[48;5;235m";
        const char* btnBg = grabbed ? "\x1b[48;5;243m" : c.active ? "\x1b[48;5;240m" : "\x1b[48;5;237m";

        for (int r = 1; r <= kTabRows; ++r) {
            s += at(r, c.x + 1) + bg;

            if (r == labelRow) {                                   // "  title  "
                s += c.active ? "\x1b[4m\x1b[58;5;75m\x1b[1;38;5;255m"
                              : grabbed ? "\x1b[1;38;5;255m" : "\x1b[38;5;245m";
                s += "  " + c.title + " ";
                s += "\x1b[24m\x1b[22m";
                s += c.modified ? "\x1b[38;5;208m● " : "  ";       // orange dot = unsaved, 2 columns either way
            } else {
                s += std::string(static_cast<size_t>(c.width - 4), ' ');
            }

            s += btnBg;                                            // the close button: 3 columns, both rows
            if (r == labelRow)
                s += lit ? "\x1b[38;5;255m × " : "\x1b[38;5;250m × ";
            else
                s += "   ";

            s += "\x1b[0m" + std::string(bg) + (lit ? " " : "\x1b[38;5;238m▏") + "\x1b[0m";   // separator
        }
    }

    if (L.rightArrow) s += at(labelRow, L.rightX + 1) + "\x1b[48;5;235m\x1b[38;5;250m › \x1b[0m";
    if (L.plusW > 0) s += at(labelRow, L.plusX + 1) + "\x1b[48;5;235m\x1b[38;5;250m + \x1b[0m";
    return s;
}

// ---- gutter ----
// " 12▾┃": space, line number, fold marker, bar. Exactly Gutter::width(lineCount) cells.
// The bar shows the state of the line: red = edited since the last save, green = edited and saved,
// white = the line the cursor is on, dark grey = untouched. The number is bright on a cursor line.
static std::string drawGutter(const EditorTab& tab, size_t line, bool wrapped, bool cursorLine, size_t lineCount, FoldMark fold) {
    size_t digits = static_cast<size_t>(Gutter::width(lineCount) - 3);
    std::string num = wrapped ? "" : std::to_string(line + 1);
    if (num.size() > digits) num = num.substr(num.size() - digits);

    const char* numColour = cursorLine ? "\x1b[1;38;5;255m" : "\x1b[38;5;240m";
    const char* barColour = cursorLine ? "\x1b[1;38;5;255m" : "\x1b[38;5;238m";
    bool marked = cursorLine;
    switch (tab.gutter.markAt(line)) {
        case GutterStyle::ChangeMark::Unsaved: barColour = "\x1b[1;38;5;160m"; marked = true; break;
        case GutterStyle::ChangeMark::Saved:   barColour = "\x1b[1;38;5;34m"; marked = true; break;
        default: break;
    }
    std::string s = "\x1b[0m ";
    s += numColour;
    s += std::string(digits - num.size(), ' ') + num + "\x1b[0m";
    s += fold == FoldMark::Expanded ? "\x1b[38;5;244m▾" : fold == FoldMark::Folded ? "\x1b[38;5;75m▸" : " ";
    s += barColour;
    s += marked ? "┃" : "│";
    s += "\x1b[0m";
    return s;
}

// ---- file handling ----

// copies what the input handler needs to know about the open file
static void syncDocState(InputState& st, const EditorTab& t) {
    st.filePath = t.doc.path();
    st.fileName = t.doc.name();
    st.modified = t.doc.modified(t.view);
}

// ---- saving ----

static void doSave(EditorTab& tab, std::string& msg) {
    if (tab.doc.save(tab.view, msg)) tab.gutter.markSaved(tab.view.buffer());
}

// carries out a Save / Save as / Open request from the input handler
static void handleFileRequest(const Request& req, InputState& st) {
    using K = Request::Kind;
    EditorTab& tab = g_tabs.current();
    std::string msg;
    if (req.kind == K::SaveFile) {
        if (tab.doc.checkDisk() == Document::DiskState::Changed) {      // someone else changed the file meanwhile
            g_dialogFor = DialogFor::SaveConflict;
            g_dialogTabId = tab.id;
            g_dialog.show("File changed on disk",
                          {tab.doc.name() + " was changed by another program since you opened or saved it.",
                           "",
                           "Saving now will overwrite those changes."},
                          {"Overwrite", "Cancel"}, 1, 1);
            st.status = "file changed on disk - choose Overwrite or Cancel";
            return;
        }
        doSave(tab, msg);
    } else if (req.kind == K::SaveFileAs) {
        if (tab.doc.saveAs(req.text, tab.view, msg)) {
            tab.gutter.markSaved(tab.view.buffer());
            tab.syntax.setLanguageFor(req.text);
            tab.view.setFoldLanguage(req.text);
        }
    } else if (req.kind == K::OpenFile) {
        g_tabs.openFile(req.text, msg);        // opens in a new tab, or switches to it
    }
    st.status = msg;
}

// ---- minimap ----

// Draws the minimap on the right edge: a braille picture of the whole file with the part on screen shaded,
// and one column of marks (cursor, selection, find matches).
static std::string drawMinimap(const TextView& v, size_t firstLine, size_t lastLine, size_t vh, int W, int mmCols,
                               const GutterStyle& gutter) {
    size_t picCols = static_cast<size_t>(mmCols) - 2;          // the first column shows edited lines, the last one the marks
    size_t pxWidth = picCols * 2;                               // a braille character is 2 dots wide and 4 dots tall
    g_minimap.setConfig(MinimapConfig{pxWidth, vh * 4, (kMinimapChars + pxWidth - 1) / pxWidth, 4});
    g_minimap.rebuild(v.buffer(), [](size_t l, size_t c) { return static_cast<unsigned char>(g_mmSyntax->tokenAt(l, c)); });
    g_minimap.setCursorMarks(v.cursors());
    g_minimap.setFindMarks(v.findMatches(), v.findCurrent());
    g_minimap.setChangeMarks([&](size_t l) { return gutter.unsavedAt(l); },
                             [&](size_t l) { return gutter.savedChangeAt(l); });
    g_mmVisibleLines = lastLine - firstLine + 1;
    g_minimap.setViewport(firstLine, g_mmVisibleLines);

    const size_t top = g_minimap.viewportTopRow();
    const size_t bottom = top + g_minimap.viewportRows() - 1;
    static const unsigned dot[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};

    std::string s;
    for (size_t t = 0; t < vh; ++t) {                           // t = terminal row, it covers 4 minimap pixel rows
        size_t r0 = t * 4;
        if (r0 >= g_minimap.rows()) break;
        s += at(static_cast<int>(t) + 1 + kTabRows, W - mmCols + 1);
        bool inView = r0 <= bottom && r0 + 3 >= top;
        s += inView ? "\x1b[48;5;237m" : "\x1b[49m";            // the part on screen has a lighter background

        unsigned char edits = 0;                                // red = unsaved edit, green = edit that was saved
        for (size_t dy = 0; dy < 4; ++dy) edits |= g_minimap.marks(r0 + dy);
        s += edits & MarkChangedUnsaved ? "\x1b[38;5;160m▎"
           : edits & MarkChangedSaved   ? "\x1b[38;5;34m▎" : " ";

        for (size_t cc = 0; cc < picCols; ++cc) {
            unsigned bits = 0;
            unsigned char kind = 0, best = 0;
            for (size_t dy = 0; dy < 4; ++dy)
                for (size_t dx = 0; dx < 2; ++dx) {
                    MinimapCell c = g_minimap.cell(r0 + dy, cc * 2 + dx);
                    if (c.density <= 60) continue;
                    bits |= dot[dy][dx];
                    if (c.density > best) { best = c.density; kind = c.kind; }
                }
            if (bits == 0) { s += ' '; continue; }
            const char* colour = kind ? tokenColor(static_cast<Token>(kind)) : "";
            s += *colour ? colour : "\x1b[38;5;250m";
            unsigned cp = 0x2800 + bits;                        // braille block, written out as UTF-8
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }

        unsigned char marks = 0;
        for (size_t dy = 0; dy < 4; ++dy) marks |= g_minimap.marks(r0 + dy);
        s += marks & MarkFindCurrent ? "\x1b[38;5;214m▐"
           : marks & MarkFind        ? "\x1b[38;5;94m▐"
           : marks & MarkCursor      ? "\x1b[97m▐"
           : marks & MarkSelection   ? "\x1b[36m▐" : " ";
        s += "\x1b[0m";
    }
    return s;
}

// carries out a click or drag on the minimap: scrolls the text
static void handleMinimapEvent(EditorTab& tab, const Request& req) {
    int event = req.kind == Request::Kind::MinimapPress ? 1 : 2;
    if (g_minimap.rows() == 0) return;

    size_t px = static_cast<size_t>(std::max(req.number, 1) - 1) * 4 + 2;   // middle of that terminal row
    size_t target;                                                              // first document line to show
    if (event == 1 && !g_minimap.rowInViewport(px)) {                           // click outside the rectangle: jump there
        target = g_minimap.scrollTopForRow(px, g_mmVisibleLines);
        g_mmGrab = g_minimap.viewportRows() / 2;
    } else {                                                                    // grab the rectangle, or keep dragging it
        if (event == 1) g_mmGrab = px - g_minimap.viewportTopRow();
        target = g_minimap.scrollTopForDrag(px, g_mmGrab, g_mmVisibleLines);
    }

    const TextBuffer& b = tab.view.buffer();
    auto rows = tab.view.folding().visibleRows(WordWrap::layout(b, wrapWidth(b), kTabWidth));
    size_t i = 0;
    while (i + 1 < rows.size() && rows[i].line < target) ++i;                   // document line -> screen row
    tab.scrollTop = i;
    tab.followCursor = false;
}

// the document lines shown in the screen rows top, top+1, ...: what StickyScroll needs when lines are folded or wrapped
static std::vector<size_t> rowLinesFrom(const std::vector<WordWrap::VisualRow>& rows, size_t top) {
    std::vector<size_t> v;
    for (size_t i = top; i < rows.size() && v.size() <= g_sticky.maxLines(); ++i) v.push_back(rows[i].line);
    return v;
}

// carries out a click on a pinned line: scrolls so the header is kStickyJumpGap rows below the pinned lines
// that show then (or below the tab bar if none), and moves the cursor to it
static void handleStickyClick(EditorTab& tab, int clicked) {
    if (clicked <= 0) return;

    const TextBuffer& b = tab.view.buffer();
    auto rows = tab.view.folding().visibleRows(WordWrap::layout(b, wrapWidth(b), kTabWidth));
    if (rows.empty() || tab.scrollTop >= rows.size()) return;

    auto pinsFor = [&](size_t top) {                             // the same rule render() uses
        std::vector<StickyLine> p = g_sticky.compute(rowLinesFrom(rows, top));
        size_t most = g_viewRows > 1 ? g_viewRows - 1 : 0;
        if (p.size() > most) p.resize(most);
        return p;
    };
    std::vector<StickyLine> pins = pinsFor(tab.scrollTop);
    size_t idx = static_cast<size_t>(clicked - 1);
    if (idx >= pins.size()) return;
    size_t line = pins[idx].line;

    size_t ri = 0;                                               // the screen row of that line
    while (ri + 1 < rows.size() && rows[ri].line < line) ++ri;

    // k pinned lines will show above it, so it goes k + gap rows below the top: find the k that fits
    size_t best = 0, bestDiff = 0;
    bool found = false;
    for (size_t k = 0; k <= g_sticky.maxLines(); ++k) {
        size_t need = k + kStickyJumpGap;
        size_t top = ri > need ? ri - need : 0;
        size_t shown = pinsFor(top).size();
        size_t diff = shown > k ? shown - k : k - shown;
        if (!found || diff < bestDiff) { found = true; bestDiff = diff; best = top; }
        if (diff == 0) break;
    }
    tab.scrollTop = best;
    tab.view.moveCursor(Position{line, 0});
}

// ---- find / replace panel ----
// The engine says which controls exist (FindPanel::buttons) and what they show; this places them.
// Find row, then (in Replace mode) the replace row, directly above the two status bars.

static std::string drawFindPanel(const FindPanel::Info& fi, int W, int H, int panelRows) {
    using A = FindPanel::Action;
    g_panelHits.clear();
    std::vector<FindPanel::ButtonDef> defs = FindPanel::buttons(fi.replaceOpen);
    std::string s;

    for (int k = 0; k < panelRows; ++k) {
        const bool replaceRow = k == 1;
        const int row = H - 1 - panelRows + k;                    // the panel sits right above the stats bar
        const A focus = replaceRow ? A::FocusReplace : A::FocusFind;
        const bool active = replaceRow ? fi.replaceActive : !fi.replaceActive;
        const bool selected = replaceRow ? fi.replaceSelected : fi.findSelected;
        const std::string& text = replaceRow ? fi.replaceText : fi.findText;

        // widths of this row's buttons, and of the match counter (find row only)
        size_t btnW = 0;
        for (const auto& d : defs)
            if (d.onReplaceRow == replaceRow) btnW += displayCols(d.label) + 2;
        std::string counter = replaceRow ? "" : FindPanel::counterText(fi);
        size_t counterW = counter.empty() ? 0 : displayCols(counter) + 2;

        const int fieldX = 6;                                     // 1-based column where the field starts
        int fieldW = W - (fieldX - 1) - static_cast<int>(btnW) - static_cast<int>(counterW) - 1;
        if (fieldW < 4) fieldW = 4;

        s += at(row, 1) + "\x1b[48;5;236m\x1b[38;5;245m" + fitCells(replaceRow ? " Rep " : " Find", 5) + "\x1b[0m";

        std::string shown = tailCells(FindPanel::fieldText(text), static_cast<size_t>(fieldW) - 1);
        s += at(row, fieldX) + (active ? "\x1b[48;5;239m" : "\x1b[48;5;237m");
        s += selected ? "\x1b[7m" : "";
        s += fitCells(shown, static_cast<size_t>(fieldW)) + "\x1b[0m";
        g_panelHits.push_back({row, fieldX, fieldX + fieldW - 1, focus});
        if (active) { g_cursorRow = row; g_cursorCol = fieldX + static_cast<int>(displayCols(shown)); }

        int x = fieldX + fieldW;
        if (!counter.empty()) {
            s += at(row, x) + "\x1b[48;5;236m" + (FindPanel::counterIsError(fi) ? "\x1b[38;5;203m" : "\x1b[38;5;250m") +
                 " " + counter + " \x1b[0m";
            x += static_cast<int>(counterW);
        }
        for (const auto& d : defs) {
            if (d.onReplaceRow != replaceRow) continue;
            int w = static_cast<int>(displayCols(d.label)) + 2;
            bool on = FindPanel::isOn(fi, d.action);
            s += at(row, x) + (on ? "\x1b[48;5;25m\x1b[1;38;5;255m" : "\x1b[48;5;238m\x1b[38;5;252m") + " " + d.label + " \x1b[0m";
            g_panelHits.push_back({row, x, x + w - 1, d.action});
            x += w;
        }
        if (x <= W) s += at(row, x) + "\x1b[48;5;236m" + std::string(static_cast<size_t>(W - x + 1), ' ') + "\x1b[0m";
    }
    return s;
}

static FindPanel::Action panelActionAt(int x, int y) {
    for (const PanelHit& h : g_panelHits)
        if (h.row == y && x >= h.x0 && x <= h.x1) return h.action;
    return FindPanel::Action::None;
}

// ---- file browser (Open / Save as): a full-screen list ----
// rows: 1 title, 2 folder, 3 name field, 4.. list, last row hint

static const int kBrowserFirstRow = 4;

static size_t browserListRows(int H) { return H > kBrowserFirstRow + 1 ? static_cast<size_t>(H - kBrowserFirstRow) : 1; }

static std::string drawBrowser(const FileBrowser& fb, int W, int H) {
    std::string s = "\x1b[0m\x1b[2J";
    s += bar(1, " " + fb.title(), W, "\x1b[7m");
    s += bar(2, " " + fb.shownDirectory(), W, "\x1b[38;5;75m");
    std::string label = " " + fb.fieldLabel() + ": ";
    s += at(3, 1) + "\x1b[38;5;245m" + label + "\x1b[0m" + "\x1b[48;5;237m" +
         fitCells(fb.name(), static_cast<size_t>(std::max(1, W - static_cast<int>(displayCols(label))))) + "\x1b[0m";

    const size_t listRows = browserListRows(H);
    if (fb.rowCount() == 0) {
        s += at(kBrowserFirstRow, 1) + "\x1b[38;5;244m " + fb.emptyMessage() + "\x1b[0m";
    }
    for (size_t k = 0; k < listRows; ++k) {
        size_t r = fb.topRow() + k;
        if (r >= fb.rowCount()) break;
        std::string name = fb.rowName(r) + (fb.rowIsDir(r) ? "/" : "");
        bool sel = r == fb.selectedRow();
        const char* style = sel ? (fb.listHasFocus() ? "\x1b[7m" : "\x1b[48;5;238m")
                                : (fb.rowIsDir(r) ? "\x1b[38;5;75m" : "");
        s += at(kBrowserFirstRow + static_cast<int>(k), 1) + style + fitCells(" " + name, static_cast<size_t>(W)) + "\x1b[0m";
    }
    s += bar(H, " " + (fb.error().empty() ? fb.hint() : fb.error()), W, fb.error().empty() ? "\x1b[90m" : "\x1b[38;5;203m");

    g_cursorRow = 3;
    g_cursorCol = static_cast<int>(displayCols(label)) + 1 + static_cast<int>(displayCols(fb.name()));
    return s;
}

// ---- dialog ----

static std::string drawDialog(int W, int H) {
    g_dialogBtns.clear();
    const std::vector<std::string>& lines = g_dialog.lines();
    const std::vector<std::string>& labels = g_dialog.buttons();

    size_t inner = displayCols(g_dialog.title());
    for (const std::string& l : lines) inner = std::max(inner, displayCols(l));
    size_t btnW = 0;
    for (const std::string& b : labels) btnW += displayCols(b) + 4;           // "[ label ]" with a space on each side
    inner = std::max(inner, btnW);
    inner = std::min(inner, static_cast<size_t>(std::max(10, W - 6)));

    const int boxW = static_cast<int>(inner) + 4;
    const int boxH = static_cast<int>(lines.size()) + 6;
    const int r0 = std::max(1, (H - boxH) / 2 + 1);
    const int c0 = std::max(1, (W - boxW) / 2 + 1);
    const char* bg = "\x1b[48;5;236m\x1b[38;5;252m";

    auto line = [&](int row, const std::string& content) {
        return at(row, c0) + bg + "│ " + fitCells(content, inner) + " │\x1b[0m";
    };
    std::string s = "\x1b[0m";
    std::string rule;
    for (int i = 0; i < boxW - 2; ++i) rule += "─";
    s += at(r0, c0) + bg + "┌" + rule + "┐\x1b[0m";
    s += at(r0 + 1, c0) + bg + "│ \x1b[1m" + fitCells(g_dialog.title(), inner) + "\x1b[22m │\x1b[0m";
    s += at(r0 + 2, c0) + bg + "├" + rule + "┤\x1b[0m";
    for (size_t i = 0; i < lines.size(); ++i) s += line(r0 + 3 + static_cast<int>(i), lines[i]);
    int blank = r0 + 3 + static_cast<int>(lines.size());
    s += line(blank, "");

    int btnRow = blank + 1;
    s += line(btnRow, "");
    int x = c0 + 2 + static_cast<int>((inner > btnW ? inner - btnW : 0) / 2);
    for (size_t i = 0; i < labels.size(); ++i) {
        const std::string& l = labels[i];
        bool sel = static_cast<int>(i) == g_dialog.selectedButton();
        size_t first = 1;                                                      // first letter is underlined: type it to choose
        while (first < l.size() && (static_cast<unsigned char>(l[first]) & 0xC0) == 0x80) ++first;
        s += at(btnRow, x) + (sel ? "\x1b[48;5;25m\x1b[1;38;5;255m" : "\x1b[48;5;240m\x1b[38;5;255m") + "  " +
             "\x1b[4m" + l.substr(0, first) + "\x1b[24m" + l.substr(first) + "  \x1b[0m";
        int w = static_cast<int>(displayCols(l)) + 4;
        g_dialogBtns.push_back({btnRow, x, x + w - 1});
        x += w;
    }
    s += at(r0 + boxH - 1, c0) + bg + "└" + rule + "┘\x1b[0m";
    return s;
}

// ---- the picture ----

static void render(EditorTab& tab, const InputState& in) {
    const TextView& v = tab.view;
    const TextBuffer& b = v.buffer();
    const std::vector<Cursor>& cs = v.cursors();
    const std::vector<Range>& matches = v.findMatches();
    size_t mainIdx = v.mainCursorIndex();
    const size_t top = static_cast<size_t>(kTabRows);

    int H = termRows();
    int W = termCols();

    if (in.mode == InputMode::Prompt) {                           // the file browser takes the whole screen
        std::string s = drawBrowser(in.browser, W, H);
        s += at(g_cursorRow, g_cursorCol) + "\x1b[?25h";
        out(s);
        return;
    }

    auto allRows = WordWrap::layout(b, wrapWidth(b), kTabWidth);
    auto rows = v.folding().visibleRows(allRows);      // rows of folded-away lines are left out
    int gw = Gutter::width(b.lineCount());
    tab.gutter.update(b);
    tab.syntax.update(b);
    g_mmSyntax = &tab.syntax;
    g_sticky.setRegionsFromFolds(v.folding().regions(), b);

    // ---- viewport: how many text rows fit, and keep the main cursor inside them ----
    g_panelRows = in.mode == InputMode::Find ? 1 : in.mode == InputMode::Replace ? 2 : 0;
    int reserved = kTabRows + 2 + g_panelRows;
    size_t vh = H > reserved ? static_cast<size_t>(H - reserved) : 1;
    g_viewRows = vh;

    // the pinned header lines for a given first screen row (never more than leave one row of text)
    auto pinsAt = [&](size_t top) {
        std::vector<StickyLine> p;
        if (top < rows.size()) p = g_sticky.compute(rowLinesFrom(rows, top));
        size_t most = vh > 1 ? vh - 1 : 0;
        if (p.size() > most) p.resize(most);
        return p;
    };

    // main cursor: screen row, and screen CELL inside the row (the engine's column is a byte offset)
    auto cellOf = [&](Position p, size_t& r, size_t& cell) {
        size_t byteCol = 0;
        WordWrap::toScreen(rows, p, r, byteCol);
        if (rows.empty()) { cell = 0; return; }
        const WordWrap::VisualRow& vr = rows[r];
        cell = cellsBetween(b.line(vr.line), vr.start, vr.start + byteCol);
    };

    Position cur = v.cursor();
    size_t mrow = 0, mcol = 0;
    cellOf(cur, mrow, mcol);
    if (tab.followCursor) {
        if (mrow < tab.scrollTop) tab.scrollTop = mrow;
        else if (mrow >= tab.scrollTop + vh) tab.scrollTop = mrow - vh + 1;
        // the pinned headers cover the first rows: scroll up until the cursor row is below them
        std::vector<size_t> allRowLines;
        allRowLines.reserve(rows.size());
        for (const WordWrap::VisualRow& vr : rows) allRowLines.push_back(vr.line);
        tab.scrollTop = g_sticky.scrollTopToRevealRow(allRowLines, tab.scrollTop, mrow, vh);
    }
    size_t maxTop = rows.size() > vh ? rows.size() - vh : 0;
    if (tab.scrollTop > maxTop) tab.scrollTop = maxTop;
    size_t endRow = std::min(rows.size(), tab.scrollTop + vh);
    size_t pinned = pinsAt(tab.scrollTop).size();      // how many top rows are covered by pinned headers
    g_pinnedRows = pinned;

    // lines that have a cursor on them (their number is drawn bright)
    std::set<size_t> cursorLines;
    // extra cursors without a selection are drawn as a coloured cell
    std::set<std::pair<size_t, size_t>> caretCells;
    for (size_t i = 0; i < cs.size(); ++i) {
        cursorLines.insert(cs[i].pos.line);
        if (!kRealExtraCursors && i != mainIdx && !cs[i].hasSelection())
            caretCells.insert({cs[i].pos.line, cs[i].pos.col});
    }

    // brackets next to the cursors and their partners
    std::set<std::pair<size_t, size_t>> bracketCells;
    for (const BracketPair& bp : v.bracketMatches()) {
        bracketCells.insert({bp.at.line, bp.at.col});
        bracketCells.insert({bp.partner.line, bp.partner.col});
    }

    std::string s = "\x1b[2J";
    s += drawTabBar(W);

    size_t ci = 0, mi = 0;
    for (size_t ri = tab.scrollTop; ri < endRow; ++ri) {
        const WordWrap::VisualRow& row = rows[ri];
        const std::string& line = b.line(row.line);
        s += at(static_cast<int>(ri - tab.scrollTop) + 1 + kTabRows, 1);

        s += drawGutter(tab, row.line, row.start != 0, cursorLines.count(row.line) > 0, b.lineCount(),
                        row.start == 0 ? v.folding().markAt(row.line) : FoldMark::None);

        size_t c = row.start;
        size_t drawn = 0;                                             // cells drawn on this row so far (tab stops count from here)
        while (c < row.end && Utf8::isContinuation(line[c])) ++c;     // a character that the wrap cut: it was drawn on the row above
        while (c < row.end) {
            size_t nxt = Utf8::nextBoundary(line, c);
            Position p{row.line, c};
            while (ci < cs.size() && !(p < cs[ci].end())) ++ci;
            bool sel = ci < cs.size() && cs[ci].hasSelection() && !(p < cs[ci].start());
            while (mi < matches.size() && !(p < matches[mi].to)) ++mi;
            bool inMatch = mi < matches.size() && !(p < matches[mi].from);
            bool isCurrent = inMatch && static_cast<int>(mi) == v.findCurrent();

            CharStyle st = v.styleAt(row.line, c);
            if (sel) s += "\x1b[7m";
            else if (caretCells.count({row.line, c})) s += "\x1b[45m";
            else if (inMatch) s += isCurrent ? "\x1b[48;5;214m\x1b[38;5;16m\x1b[1m"      // current match: bright orange, bold black text
                                             : "\x1b[48;5;94m\x1b[38;5;252m";            // other matches: dim brown, light text
            else if (bracketCells.count({row.line, c})) s += "\x1b[48;5;240m\x1b[1m";
            if (!sel && !inMatch && !caretCells.count({row.line, c}))
                s += tokenColor(tab.syntax.tokenAt(row.line, c));
            if (st.bold) s += "\x1b[1m";
            if (st.italic) s += "\x1b[3m";
            if (st.underline) s += "\x1b[4m";
            if (st.fontSize > kDefaultFontSize) s += "\x1b[33m";      // bigger size = yellow
            else if (st.fontSize < kDefaultFontSize) s += "\x1b[36m"; // smaller size = cyan
            if (line[c] == '\t') {
                size_t n = kTabWidth - drawn % kTabWidth;
                s.append(n, ' ');
                drawn += n;
            } else {
                size_t len = 1;
                drawn += static_cast<size_t>(cellWidth(decodeAt(line, c, len)));
                s.append(line, c, nxt - c);
            }
            s += "\x1b[0m";
            c = nxt;
        }
        if (row.last && v.folding().isFolded(row.line)) {     // folded header: "…" and the closing text
            std::string ph = "…" + v.folding().placeholder(row.line);
            size_t used = static_cast<size_t>(gw) + cellsBetween(line, row.start, row.end);
            size_t room = static_cast<size_t>(W) > used ? static_cast<size_t>(W) - used : 0;
            if (displayCols(ph) > room) ph = room >= 1 ? "…" : "";
            if (!ph.empty()) {
                bool sel = v.isFoldSelected(row.line);
                s += (sel ? "\x1b[0m\x1b[36;7m" : "\x1b[0m\x1b[36m") + ph + "\x1b[0m";
            }
        }
        if (row.last && caretCells.count({row.line, row.end})) s += "\x1b[45m \x1b[0m";   // extra cursor at line end
    }

    // ---- sticky scroll: pinned header lines over the first text rows ----
    {
        std::vector<StickyLine> pins = pinsAt(tab.scrollTop);
        size_t room = wrapWidth(b);
        const char* bg = "\x1b[48;5;236m";
        for (size_t d = 0; d < pins.size(); ++d) {
            const std::string& hl = b.line(pins[d].line);
            // a thin grey underline on the last pinned row only; use "\x1b[4m\x1b[58;5;240m" for every row instead
            const char* ul = (d + 1 == pins.size()) ? "\x1b[4m\x1b[58;5;240m" : "";
            s += at(static_cast<int>(d) + 1 + static_cast<int>(top), 1);
            s += bg;
            s += drawGutter(tab, pins[d].line, false, false, b.lineCount(), FoldMark::None);
            size_t used = 0, c = 0;
            while (c < hl.size()) {
                size_t len = 1;
                char32_t cp = decodeAt(hl, c, len);
                size_t w = charCells(cp, used);
                if (used + w > room) break;
                s += tokenColor(tab.syntax.tokenAt(pins[d].line, c));
                s += bg;
                s += ul;
                if (hl[c] == '\t') s.append(w, ' ');
                else s.append(hl, c, len);
                used += w;
                c += len;
            }
            s += bg;
            s += ul;
            s.append(room - used, ' ');
            s += "\x1b[0m";
        }
    }

    // ---- minimap on the right edge ----
    int mmCols = minimapCols();
    if (mmCols > 0 && tab.scrollTop < endRow)
        s += drawMinimap(v, rows[tab.scrollTop].line, rows[endRow - 1].line, vh, W, mmCols, tab.gutter);

    // ---- bottom bars: status, statistics ----
    CharStyle ts = v.typingStyle();
    char buf[512];

    snprintf(buf, sizeof buf, " %s%s | Ln %zu, Col %zu | cursors %zu | size %d%s%s%s | %s | ^S save ^O open ^T new ^W close ^Q quit",
             in.fileName.c_str(), in.modified ? " [+]" : "",
             cur.line + 1, Utf8::charCount(b.line(cur.line).substr(0, cur.col)) + 1, cs.size(), ts.fontSize,
             ts.bold ? " B" : "", ts.italic ? " I" : "", ts.underline ? " U" : "", in.status.c_str());
    s += bar(H, buf, W, "\x1b[7m");

    TextStats all = v.stats();
    snprintf(buf, sizeof buf, " %zu words | %zu chars (%zu without spaces) | %zu lines | %zu paragraphs | ~%zu min read",
             all.words, all.characters, all.charactersNoSpaces, all.lines, all.paragraphs, all.readingMinutes);
    std::string statsLine = buf;
    if (v.anySelection()) {
        TextStats sel = v.selectionStats();
        snprintf(buf, sizeof buf, " || selected: %zu chars, %zu words, %zu lines", sel.characters, sel.words, sel.lines);
        statsLine += buf;
    }
    {   // line endings and encoding sit at the right end of the bar, and stay visible when the statistics are long
        std::string right = tab.doc.lineEndingName() + " | " + tab.doc.encodingName() + " ";
        size_t Wz = static_cast<size_t>(W);
        size_t rc = displayCols(right);
        if (Wz > rc + 1) statsLine = fitCells(statsLine, Wz - rc) + right;
    }
    s += bar(H - 1, statsLine, W, "\x1b[0m\x1b[90m");

    // ---- find / replace panel, pinned at the bottom ----
    g_panelHits.clear();
    if (g_panelRows > 0) {
        FindPanel::Info fi;
        fi.findText = in.findText;
        fi.replaceText = in.replaceText;
        fi.replaceOpen = in.mode == InputMode::Replace;
        fi.replaceActive = fi.replaceOpen && in.replaceFieldActive;
        fi.matchCase = in.findCase;
        fi.wholeWord = in.findWord;
        fi.regex = in.findRegex;
        fi.inSelection = in.findInSel && v.findScoped();
        fi.count = v.findMatchCount();
        fi.current = v.findCurrent();
        fi.error = v.findError();
        fi.findSelected = in.fieldSelected && !fi.replaceActive;
        fi.replaceSelected = in.fieldSelected && fi.replaceActive;
        s += drawFindPanel(fi, W, H, g_panelRows);
    }

    // the text has the cursor: normal editing, or the find bar is open but you clicked on the text
    bool editorHasCursor = in.mode == InputMode::Edit ||
                           ((in.mode == InputMode::Find || in.mode == InputMode::Replace) && in.editorFocus);

    // ---- extra cursors: real terminal cursors, same shape as the main one ----
    s += "\x1b[>0;4 q";                                    // remove the ones from the last redraw
    if (kRealExtraCursors && editorHasCursor && !g_dialog.active()) {
        std::string coords;
        for (size_t i = 0; i < cs.size(); ++i) {
            if (i == mainIdx) continue;
            size_t r = 0, c = 0;
            cellOf(cs[i].pos, r, c);
            if (r < tab.scrollTop + pinned || r >= endRow) continue;  // off-screen or under a pinned line, skip
            coords += ";2:" + std::to_string(r - tab.scrollTop + 1 + top) + ":" + std::to_string(c + 1 + static_cast<size_t>(gw));
        }
        if (!coords.empty()) s += "\x1b[>29" + coords + " q";
    }

    if (g_dialog.active()) s += drawDialog(W, H);

    // ---- where the terminal's own cursor goes ----
    bool cursorVisible = mrow >= tab.scrollTop + pinned && mrow < endRow;
    s += (!editorHasCursor || cursorVisible) ? "\x1b[?25h" : "\x1b[?25l";   // hide it while scrolled off-screen
    if (editorHasCursor) {
        snprintf(buf, sizeof buf, "\x1b[%zu;%zuH", (cursorVisible ? mrow - tab.scrollTop + 1 : 1) + top,
                 mcol + 1 + static_cast<size_t>(gw));
    } else {                                               // find / replace panel: set by drawFindPanel
        snprintf(buf, sizeof buf, "\x1b[%d;%dH", g_cursorRow, g_cursorCol);
    }
    s += buf;
    if (g_dialog.active()) s += "\x1b[?25l";               // no text cursor behind the dialog
    out(s);
}

// ---- mouse: terminal cell -> engine MouseEvent ----
// The engine does not know the layout. This is where "a click at column 40, row 7" becomes
// "text position (line 3, byte 12)", "the minimap", "tab bar column 5" or "find panel control Next".

static Input::MouseEvent toEngineMouse(const RawMouse& raw, EditorTab& tab, const InputState& st) {
    using Input::Region;
    using Kind = Input::MouseEvent::Kind;

    Input::MouseEvent m;
    m.kind = raw.kind;
    m.button = raw.button;
    m.mods = raw.mods;
    m.clicks = raw.clicks;
    m.wheelRows = raw.wheelRows;
    m.region = Region::Other;

    const int W = termCols(), H = termRows();
    int x = raw.x, y = raw.y;

    if (st.mode == InputMode::Prompt) {                       // file browser: only the list takes clicks
        int last = kBrowserFirstRow + static_cast<int>(browserListRows(H)) - 1;
        if (y >= kBrowserFirstRow && y <= last) {
            m.region = Region::Browser;
            m.row = (y - kBrowserFirstRow) + static_cast<int>(st.browser.topRow());   // list row, counted from the first entry
            m.x = x;
        }
        return m;
    }

    const int textTop = kTabRows + 1;
    const int textBottom = textTop + static_cast<int>(g_viewRows) - 1;
    const int mmCols = minimapCols();
    const bool drag = raw.kind == Kind::Drag || raw.kind == Kind::Release;

    if (drag) {                                               // a drag keeps selecting even when the pointer leaves the text
        y = std::max(textTop, std::min(y, textBottom));
        if (!st.minimapDrag && mmCols > 0) x = std::min(x, W - mmCols);
    }

    if (y <= kTabRows) { m.region = Region::TabBar; m.x = x - 1; return m; }
    if (y >= H - 1) return m;                                 // status bars
    if (g_panelRows > 0 && y > textBottom) {                  // the find / replace rows
        m.region = Region::FindPanel;
        m.control = static_cast<int>(panelActionAt(x, y));
        return m;
    }
    if (y > textBottom) return m;

    const int textRow = y - textTop;                          // 0-based row on screen
    if (mmCols > 0 && x >= W - mmCols + 1) {
        m.region = Region::Minimap;
        m.row = textRow;
        return m;
    }
    if (mmCols > 0 && drag && st.minimapDrag) {               // dragging the minimap rectangle: the row still counts
        m.region = Region::Minimap;
        m.row = textRow;
        return m;
    }
    if (static_cast<size_t>(textRow) < g_pinnedRows && (!drag || st.stickyDrag)) {
        m.region = Region::Sticky;
        m.row = textRow;
        return m;
    }

    const TextBuffer& b = tab.view.buffer();
    auto rows = tab.view.folding().visibleRows(WordWrap::layout(b, wrapWidth(b), kTabWidth));
    if (rows.empty()) return m;
    size_t ri = tab.scrollTop + static_cast<size_t>(textRow);
    bool belowText = ri >= rows.size();                       // clicked under the last line: go to its end
    if (belowText) ri = rows.size() - 1;
    const WordWrap::VisualRow& vr = rows[ri];
    const std::string& line = b.line(vr.line);

    const int gw = Gutter::width(b.lineCount());
    const int x0 = x - 1;
    if (x0 < gw) {
        bool onMarker = x0 >= gw - 3 && vr.start == 0 && !belowText;
        m.region = onMarker ? Region::FoldMarker : Region::Text;
        m.pos = Position{vr.line, vr.start};
        return m;
    }
    size_t cell = belowText ? static_cast<size_t>(W) : static_cast<size_t>(x0 - gw);
    size_t byteInLine = byteAtCell(line, vr.start, vr.end, cell);
    m.region = Region::Text;
    m.pos = WordWrap::toPosition(rows, ri, byteInLine - vr.start);
    return m;
}

// ---- input ----
// Puts text on the system clipboard through the terminal (OSC 52).
static void copyToSystemClipboard(const std::string& text) {
    if (std::getenv("WAYLAND_DISPLAY")) {                      // set the clipboard directly when wl-copy exists
        FILE* p = popen("wl-copy 2>/dev/null", "w");
        if (p) {
            fwrite(text.data(), 1, text.size(), p);
            if (pclose(p) == 0) return;                        // done: no need for OSC 52
        }
    }
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto u = [](char c) { return static_cast<unsigned>(static_cast<unsigned char>(c)); };
    std::string b;
    size_t i = 0;
    for (; i + 2 < text.size(); i += 3) {
        unsigned v = (u(text[i]) << 16) | (u(text[i + 1]) << 8) | u(text[i + 2]);
        b += tbl[(v >> 18) & 63]; b += tbl[(v >> 12) & 63]; b += tbl[(v >> 6) & 63]; b += tbl[v & 63];
    }
    if (i + 1 == text.size()) {
        unsigned v = u(text[i]) << 16;
        b += tbl[(v >> 18) & 63]; b += tbl[(v >> 12) & 63]; b += "==";
    } else if (i + 2 == text.size()) {
        unsigned v = (u(text[i]) << 16) | (u(text[i + 1]) << 8);
        b += tbl[(v >> 18) & 63]; b += tbl[(v >> 12) & 63]; b += tbl[(v >> 6) & 63]; b += '=';
    }
    out("\x1b]52;c;" + b + "\x07");
}

static size_t indexOfTab(int id) {
    for (size_t i = 0; i < g_tabs.count(); ++i)
        if (g_tabs.at(i).id == id) return i;
    return g_tabs.count();
}

static void requestClose(size_t index, InputState& st);
static void requestQuit(InputState& st);
static void applyAppCommand(const Request& r, InputState& st);
static void handleTabBarEvent(const Input::MouseEvent& e, InputState& st);

// Gives one event to the engine for the current tab, then carries out what the engine asked for.
static void feedEvent(const Input::Event& ev, InputState& state) {
    if (ev.type != Input::Event::Type::Mouse) g_bar.endDrag();   // a key press ends a drag whose release was lost

    EditorTab& tab = g_tabs.current();
    state.pageRows = g_viewRows;
    syncDocState(state, tab);
    state.modified = false;       // the quit and "open anyway" warnings are handled by the tabs, not by the input handler
    state.browser.setVisibleRows(static_cast<int>(browserListRows(termRows())));

    std::vector<Cursor> cursorsBefore = tab.view.cursors();
    handleEvent(ev, tab.view, state);
    state.browser.setVisibleRows(static_cast<int>(browserListRows(termRows())));   // it may have just been opened

    // what the engine asked for. Taken in three rounds: first what only touches this tab, then files, and last the
    // requests that can close or switch tabs (`tab` would be gone after those).
    using K = Request::Kind;
    std::vector<Request> requests;
    requests.swap(state.requests);
    g_minimapOn = state.minimapOn;
    int wheelRows = 0;
    for (const Request& r : requests) {
        switch (r.kind) {
            case K::CopyToClipboard: copyToSystemClipboard(r.text); break;          // the system clipboard too
            case K::MinimapPress: case K::MinimapDrag: handleMinimapEvent(tab, r); break;
            case K::StickyClick: handleStickyClick(tab, r.number); break;
            case K::ScrollWheel: wheelRows += r.number; break;
            default: break;
        }
    }
    if (wheelRows != 0) {
        long long t = static_cast<long long>(tab.scrollTop) + wheelRows;
        tab.scrollTop = t < 0 ? 0 : static_cast<size_t>(t);   // render() clamps the bottom
        tab.followCursor = false;
    } else {
        // bring the view back to the cursor only if this input moved a cursor
        const std::vector<Cursor>& after = tab.view.cursors();
        bool moved = after.size() != cursorsBefore.size();
        for (size_t i = 0; !moved && i < after.size(); ++i)
            moved = after[i].pos.line != cursorsBefore[i].pos.line || after[i].pos.col != cursorsBefore[i].pos.col ||
                    after[i].anchor.line != cursorsBefore[i].anchor.line || after[i].anchor.col != cursorsBefore[i].anchor.col;
        if (moved) tab.followCursor = true;
    }
    for (const Request& r : requests)
        if (r.kind == K::SaveFile || r.kind == K::SaveFileAs || r.kind == K::OpenFile) handleFileRequest(r, state);

    for (const Request& r : requests) {                        // these can close or switch tabs
        if (r.kind == K::TabBarMouse) handleTabBarEvent(r.mouse, state);
        else applyAppCommand(r, state);                        // ignores the kinds that were handled above
    }
}

// ---- unsaved-changes dialog ----

// Starts saving a tab by sending it Ctrl+S. A tab without a file name gets the Save As browser.
static void startSave(size_t index, InputState& st) {
    g_tabs.select(index);
    Input::KeyEvent k;
    k.key = Input::Key::Char;
    k.ch = U's';
    k.mods.ctrl = true;
    feedEvent(Input::Event::makeKey(k), st);
}

// Runs after every input: finishes "save, then close" and "save everything, then quit".
static void advanceSaveFlow(InputState& st) {
    if (st.mode == InputMode::Prompt) return;                    // still waiting for a file name

    if (g_closeAfterSaveId != 0) {
        size_t i = indexOfTab(g_closeAfterSaveId);
        g_closeAfterSaveId = 0;
        if (i < g_tabs.count() && !g_tabs.modified(i)) {         // saved: close it. Cancelled or failed: keep it open
            std::string name = g_tabs.title(i);
            g_tabs.closeTab(i, true);
            st.status = "saved and closed " + name;
        }
    }

    while (g_quitAfterSave) {
        size_t dirty = g_tabs.count();
        for (size_t i = 0; i < g_tabs.count(); ++i)
            if (g_tabs.modified(i)) { dirty = i; break; }
        if (dirty == g_tabs.count()) {                           // everything is saved
            g_quitAfterSave = false;
            st.running = false;
            return;
        }
        int id = g_tabs.at(dirty).id;
        if (id == g_quitTriedId) {                               // we tried this one already: cancelled or failed
            g_quitAfterSave = false;
            st.status = "quit cancelled";
            return;
        }
        g_quitTriedId = id;
        startSave(dirty, st);
        if (st.mode == InputMode::Prompt) return;                // wait for the file name
    }
}

static void requestClose(size_t index, InputState& st) {
    std::string name = g_tabs.title(index);
    if (!g_tabs.modified(index)) {
        g_tabs.closeTab(index, true);
        st.status = "closed " + name;
        return;
    }
    g_dialogFor = DialogFor::CloseTab;
    g_dialogTabId = g_tabs.at(index).id;
    g_dialog.show("Unsaved changes",
                  {"Do you want to save the changes you made to " + name + "?",
                   "",
                   "Your changes will be lost if you don't save them."});
}

static void requestQuit(InputState& st) {
    std::vector<std::string> names;
    for (size_t i = 0; i < g_tabs.count(); ++i)
        if (g_tabs.modified(i)) names.push_back(g_tabs.title(i));
    if (names.empty()) { st.running = false; return; }

    std::vector<std::string> lines;
    lines.push_back(names.size() == 1 ? "1 file has unsaved changes:"
                                      : std::to_string(names.size()) + " files have unsaved changes:");
    for (size_t k = 0; k < names.size() && k < 5; ++k) lines.push_back("  • " + names[k]);
    if (names.size() > 5) lines.push_back("  …and " + std::to_string(names.size() - 5) + " more");
    lines.push_back("");
    lines.push_back("Your changes will be lost if you don't save them.");

    g_dialogFor = DialogFor::Quit;
    g_dialog.show("Unsaved changes", lines);
}

// Looks at the open file: reloads it quietly if it changed and you have no edits, otherwise asks.
// Returns true if something visible happened.
static bool checkExternalChange(InputState& st) {
    if (g_dialog.active() || st.mode != InputMode::Edit) return false;
    EditorTab& tab = g_tabs.current();
    Document::DiskState d = tab.doc.checkDisk();
    if (d == Document::DiskState::Unchanged) return false;

    std::string name = tab.doc.name();
    if (d == Document::DiskState::Deleted) {
        st.status = name + " was deleted on disk - saving will create it again";
        return true;
    }
    if (!tab.doc.modified(tab.view)) {                       // no edits of yours: just show the new version
        std::string msg;
        g_tabs.reloadTab(g_tabs.currentIndex(), msg);
        st.status = msg;
        return true;
    }
    g_dialogFor = DialogFor::DiskChanged;
    g_dialogTabId = tab.id;
    g_dialog.show("File changed on disk",
                  {name + " was changed by another program.",
                   "",
                   "Reload: take the disk version (your unsaved changes are lost).",
                   "Keep mine: keep editing; saving overwrites the disk version.",
                   "Compare: open the disk version in a new tab."},
                  {"Reload", "Keep mine", "Compare"}, 1, 1);
    return true;
}

static void onDialogResult(Dialog::Result r, InputState& st) {
    int choice = g_dialog.lastChoice();

    if (g_dialogFor == DialogFor::DiskChanged) {
        size_t i = indexOfTab(g_dialogTabId);
        if (i >= g_tabs.count()) return;
        std::string name = g_tabs.at(i).doc.name();
        std::string msg;
        if (choice == 0) { g_tabs.reloadTab(i, msg); st.status = msg; }
        else if (choice == 1) {
            g_tabs.at(i).doc.acknowledgeDisk();
            st.status = "keeping your version of " + name + " - saving will overwrite the disk version";
        } else { g_tabs.openDiskCopy(i, msg); st.status = msg; }
        return;
    }

    if (g_dialogFor == DialogFor::SaveConflict) {
        size_t i = indexOfTab(g_dialogTabId);
        if (i >= g_tabs.count()) return;
        if (choice == 0) {
            std::string msg;
            doSave(g_tabs.at(i), msg);
            st.status = msg;
        } else {
            st.status = "save cancelled";
        }
        return;
    }

    if (r == Dialog::Result::Cancel) { st.status = "cancelled"; return; }

    if (g_dialogFor == DialogFor::CloseTab) {
        size_t i = indexOfTab(g_dialogTabId);
        if (i >= g_tabs.count()) return;
        if (r == Dialog::Result::DontSave) {
            std::string name = g_tabs.title(i);
            g_tabs.closeTab(i, true);
            st.status = "closed " + name + " without saving";
        } else {
            g_closeAfterSaveId = g_dialogTabId;
            startSave(i, st);
        }
    } else {
        if (r == Dialog::Result::DontSave) st.running = false;
        else { g_quitAfterSave = true; g_quitTriedId = 0; }
    }
    advanceSaveFlow(st);
}

// ---- tabs ----
// The engine reports tab shortcuts and tab bar mouse events as requests (InputState::requests).
// The tab bar itself (what is where, drag and drop) is TabManager; this carries the requests out.

static void applyAppCommand(const Request& r, InputState& st) {
    using K = Request::Kind;
    const int arg = r.number;
    switch (r.kind) {
        case K::Quit: requestQuit(st); break;
        case K::NewTab: g_tabs.newTab(); st.status = "new tab"; break;
        case K::CloseTab: requestClose(g_tabs.currentIndex(), st); break;
        case K::NextTab: g_tabs.next(); break;
        case K::PrevTab: g_tabs.prev(); break;
        case K::GotoTab:
            if (static_cast<size_t>(arg) < g_tabs.count()) g_tabs.select(static_cast<size_t>(arg));
            else st.status = "no tab " + std::to_string(arg + 1);
            break;
        case K::GotoLastTab: g_tabs.selectLast(); break;
        case K::ReopenTab: {
            std::string msg;
            g_tabs.reopenClosed(msg);
            st.status = msg;
            break;
        }
        case K::MoveTabLeft:
        case K::MoveTabRight: {
            size_t i = g_tabs.currentIndex();
            size_t to = r.kind == K::MoveTabLeft ? (i > 0 ? i - 1 : 0)
                                                    : std::min(i + 1, g_tabs.count() - 1);
            if (to != i) { g_tabs.move(i, to); st.status = "moved tab"; }
            break;
        }
        default: break;
    }
}

static void handleTabBarEvent(const Input::MouseEvent& e, InputState& st) {
    using Kind = Input::MouseEvent::Kind;
    const int W = termCols();

    if (e.kind == Kind::Wheel) { g_bar.scrollBar(g_tabs, e.wheelRows < 0 ? -1 : 1); return; }
    if (e.kind != Kind::Press) return;                       // drags and releases are handled in processInput

    TabHit h = g_bar.hit(g_tabs, e.x, W);
    if (e.button == Input::Button::Middle) {
        if (h.kind == TabHit::Tab || h.kind == TabHit::Close) requestClose(h.index, st);
        return;
    }
    if (e.button != Input::Button::Left) return;
    if (h.kind == TabHit::Tab) { g_tabs.select(h.index); g_bar.beginDrag(g_tabs, e.x, W); }   // select it, and a drag may follow
    else if (h.kind == TabHit::Close) requestClose(h.index, st);
    else if (h.kind == TabHit::ArrowLeft) g_bar.scrollBar(g_tabs, -1);
    else if (h.kind == TabHit::ArrowRight) g_bar.scrollBar(g_tabs, 1);
    else if (h.kind == TabHit::Plus) { g_tabs.newTab(); st.status = "new tab"; }
}

// The find bar stays open when you switch tabs: the tab you left loses its highlights, the new one is searched.
static void onTabSwitched(int leftId, InputState& st) {
    size_t i = indexOfTab(leftId);
    if (i < g_tabs.count()) g_tabs.at(i).view.clearFind();
    refreshFind(g_tabs.current().view, st);
}

// One decoded terminal input: to the dialog if it is open, otherwise to the engine.
static void processInput(const TermInput& t, InputState& st) {
    using Kind = Input::MouseEvent::Kind;

    if (g_dialog.active()) {                                   // the dialog takes all input while it is open
        Dialog::Result r = Dialog::Result::None;
        if (t.type == TermInput::Type::Key) r = g_dialog.handleKey(t.key);
        else if (t.type == TermInput::Type::Text) r = g_dialog.handleText(t.text);
        else if (t.type == TermInput::Type::Mouse && t.mouse.kind == Kind::Press && t.mouse.button == Input::Button::Left) {
            for (size_t i = 0; i < g_dialogBtns.size(); ++i) {
                const BoxHit& h = g_dialogBtns[i];
                if (t.mouse.y == h.row && t.mouse.x >= h.x0 && t.mouse.x <= h.x1) {
                    r = g_dialog.clickButton(static_cast<int>(i));
                    break;
                }
            }
        }
        if (r != Dialog::Result::None) onDialogResult(r, st);
        advanceSaveFlow(st);
        return;
    }

    const int beforeId = g_tabs.current().id;
    Input::Event ev;
    switch (t.type) {
        case TermInput::Type::Key: ev = Input::Event::makeKey(t.key); break;
        case TermInput::Type::Text: ev = Input::Event::makeText(t.text); break;
        case TermInput::Type::Paste: ev = Input::Event::makePaste(t.text); break;
        case TermInput::Type::Mouse: {
            const RawMouse& rm = t.mouse;
            if (g_bar.dragging() && (rm.kind == Kind::Drag || rm.kind == Kind::Release)) {   // moving a tab
                if (rm.kind == Kind::Drag) g_bar.dragTo(g_tabs, rm.x - 1, termCols());
                else g_bar.endDrag();
                return;
            }
            ev = Input::Event::makeMouse(toEngineMouse(rm, g_tabs.current(), st));
            break;
        }
    }
    feedEvent(ev, st);
    if (g_tabs.current().id != beforeId) onTabSwitched(beforeId, st);   // switched, opened or closed a tab
    advanceSaveFlow(st);
}

int main(int argc, char** argv) {
    InputState state;

    // ./run_test a.txt b.cpp  ->  opens each file in its own tab (a name that does not exist yet starts a new file)
    for (int i = 1; i < argc; ++i) {
        std::string msg;
        if (!g_tabs.openFile(argv[i], msg)) {
            fprintf(stderr, "%s\n", msg.c_str());
            return 1;
        }
        state.status = msg;
    }
    if (argc > 2) g_tabs.select(0);

    setupTerminal();
    TerminalDecoder decoder;
    syncDocState(state, g_tabs.current());
    render(g_tabs.current(), state);

    char buf[4096];
    while (state.running) {
        pollfd pfd{};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 1000);                          // wake up every second, to look for changes on disk
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        bool redraw = false;
        if (pr > 0) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0) break;
            for (const TermInput& t : decoder.feed(buf, static_cast<size_t>(n))) {
                if (!state.running) break;
                processInput(t, state);
            }
            redraw = true;
        }

        if (checkExternalChange(state)) redraw = true;         // after a key, and once a second while idle

        if (redraw) {
            syncDocState(state, g_tabs.current());
            render(g_tabs.current(), state);
        }
    }
    return 0;
}