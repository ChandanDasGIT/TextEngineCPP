#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <cerrno>
#include <poll.h>
#include "engine/textbuffer/textbuffer.h"
#include "engine/textview/textview.h"
#include "engine/inputHandler/inputhandler.h"
#include "engine/gutter/gutter.h"
#include "engine/gutterstyle/gutterstyle.h"
#include "engine/fileIO/fileio.h"
#include "engine/wordwrap/wordwrap.h"
#include "engine/syntaxhighlight/syntaxhighlighter.h"
#include "engine/minimap/minimap.h"
#include "engine/stickyscroll/stickyscroll.h"
#include "engine/tabs/tabmanager.h"
#include "engine/tabs/tabinput.h"
#include "engine/dialog/dialog.h"
#include "engine/findpanel/findpanel.h"

static termios g_orig;
static const bool kRealExtraCursors = true;
static const int kTabRows = 2;           // screen rows used by the tab bar (the text starts below it)
static size_t g_viewRows = 20;           // how many text rows fit on screen
static TabManager g_tabs;
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

// number of screen columns a UTF-8 string takes (counts every byte that starts a character)
static size_t displayCols(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// One full-width bar on a screen row. `style` is an escape code like "\x1b[7m".
static std::string bar(int row, std::string text, int cols, const char* style) {
    if (static_cast<int>(text.size()) > cols) text.resize(static_cast<size_t>(cols));
    else text.append(static_cast<size_t>(cols) - text.size(), ' ');
    char head[32];
    snprintf(head, sizeof head, "\x1b[%d;1H", row);
    return std::string(head) + style + text + "\x1b[0m";
}

// ---- tab bar ----

static std::string drawTabBar(int W) {
    const TabBarLayout& L = g_tabs.layout(W);
    const int labelRow = kTabRows;                                 // the title sits on the bottom row
    const size_t dragged = g_tabs.dragging() ? g_tabs.draggedIndex() : g_tabs.count();
    auto at = [](int row, int x) { return "\x1b[" + std::to_string(row) + ";" + std::to_string(x + 1) + "H"; };

    std::string s = "\x1b[0m";
    for (int r = 1; r <= kTabRows; ++r)                            // bar background
        s += at(r, 0) + "\x1b[48;5;235m" + std::string(static_cast<size_t>(W), ' ') + "\x1b[0m";

    if (L.leftArrow) s += at(labelRow, L.leftX) + "\x1b[48;5;235m\x1b[38;5;250m ‹ \x1b[0m";

    for (const TabCell& c : L.cells) {
        bool grabbed = c.index == dragged;
        bool lit = c.active || grabbed;
        const char* bg = grabbed ? "\x1b[48;5;241m" : c.active ? "\x1b[48;5;238m" : "\x1b[48;5;235m";
        const char* btnBg = grabbed ? "\x1b[48;5;243m" : c.active ? "\x1b[48;5;240m" : "\x1b[48;5;237m";

        for (int r = 1; r <= kTabRows; ++r) {
            s += at(r, c.x) + bg;

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

    if (L.rightArrow) s += at(labelRow, L.rightX) + "\x1b[48;5;235m\x1b[38;5;250m › \x1b[0m";
    if (L.plusW > 0) s += at(labelRow, L.plusX) + "\x1b[48;5;235m\x1b[38;5;250m + \x1b[0m";
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

// carries out Save / Save as / Open requests from the input handler
static void handleFileRequest(InputState& st) {
    FileRequest req = st.fileRequest;
    st.fileRequest = FileRequest::None;
    if (req == FileRequest::None) return;

    EditorTab& tab = g_tabs.current();
    std::string msg;
    if (req == FileRequest::Save) {
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
    } else if (req == FileRequest::SaveAs) {
        if (tab.doc.saveAs(st.requestPath, tab.view, msg)) {
            tab.gutter.markSaved(tab.view.buffer());
            tab.syntax.setLanguageFor(st.requestPath);
            tab.view.setFoldLanguage(st.requestPath);
        }
    } else if (req == FileRequest::Open) {
        g_tabs.openFile(st.requestPath, msg);        // opens in a new tab, or switches to it
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
        char head[32];
        snprintf(head, sizeof head, "\x1b[%zu;%zuH", t + 1 + static_cast<size_t>(kTabRows), static_cast<size_t>(W - mmCols + 1));
        s += head;
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
            const char* colour = kind ? SyntaxHighlighter::ansi(static_cast<Token>(kind)) : "";
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

// tells the input handler where the minimap is, so it can tell mouse clicks on it from clicks in the text
static void syncMinimapState(InputState& st) {
    int W = termCols();
    int mc = minimapCols();
    st.minimapX = mc > 0 ? W - mc + 1 : 0;
    st.minimapCols = mc;
    st.minimapRows = static_cast<int>(g_viewRows);
}

// carries out a click or drag on the minimap: scrolls the text
static void handleMinimapEvent(EditorTab& tab, InputState& st) {
    int event = st.minimapEvent;
    st.minimapEvent = 0;
    if (event == 0 || g_minimap.rows() == 0) return;

    size_t px = static_cast<size_t>(std::max(st.minimapRow, 1) - 1) * 4 + 2;   // middle of that terminal row
    size_t target;                                                              // first document line to show
    if (event == 1 && !g_minimap.rowInViewport(px)) {                           // click outside the rectangle: jump there
        target = g_minimap.scrollTopForRow(px, g_mmVisibleLines);
        g_mmGrab = g_minimap.viewportRows() / 2;
    } else {                                                                    // grab the rectangle, or keep dragging it
        if (event == 1) g_mmGrab = px - g_minimap.viewportTopRow();
        target = g_minimap.scrollTopForDrag(px, g_mmGrab, g_mmVisibleLines);
    }

    const TextBuffer& b = tab.view.buffer();
    auto rows = tab.view.folding().visibleRows(WordWrap::layout(b, wrapWidth(b)));
    size_t i = 0;
    while (i + 1 < rows.size() && rows[i].line < target) ++i;                   // document line -> screen row
    tab.scrollTop = i;
    tab.followCursor = false;
}

// carries out a click on a pinned line: scrolls so the header is kStickyJumpGap rows below the pinned lines
// that show then (or below the tab bar if none), and moves the cursor to it
static void handleStickyClick(EditorTab& tab, InputState& st) {
    int clicked = st.stickyClickRow;
    st.stickyClickRow = 0;
    if (clicked <= 0) return;

    const TextBuffer& b = tab.view.buffer();
    auto rows = tab.view.folding().visibleRows(WordWrap::layout(b, wrapWidth(b)));
    if (rows.empty() || tab.scrollTop >= rows.size()) return;

    auto pinsFor = [&](size_t top) {                             // the same rule render() uses
        std::vector<StickyLine> p = g_sticky.compute(rows[top].line);
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

static void render(EditorTab& tab, const InputState& in) {
    const TextView& v = tab.view;
    const TextBuffer& b = v.buffer();
    const std::vector<Cursor>& cs = v.cursors();
    const std::vector<Range>& matches = v.findMatches();
    size_t mainIdx = v.mainCursorIndex();
    const size_t top = static_cast<size_t>(kTabRows);

    winsize ws{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    int H = ws.ws_row ? ws.ws_row : 24;
    int W = ws.ws_col ? ws.ws_col : 80;

    auto allRows = WordWrap::layout(b, wrapWidth(b));
    auto rows = v.folding().visibleRows(allRows);      // rows of folded-away lines are left out
    int gw = Gutter::width(b.lineCount());
    tab.gutter.update(b);
    tab.syntax.update(b);
    g_mmSyntax = &tab.syntax;
    g_sticky.setRegionsFromFolds(v.folding().regions(), b);

    // ---- viewport: how many text rows fit, and keep the main cursor inside them ----
    int reserved = kTabRows + 2 + (in.mode == InputMode::Find ? 1
                                 : in.mode == InputMode::Replace ? 2 : 0);
    size_t vh = H > reserved ? static_cast<size_t>(H - reserved) : 1;
    g_viewRows = vh;

    // the pinned header lines for a given first screen row (never more than leave one row of text)
    auto pinsAt = [&](size_t top) {
        std::vector<StickyLine> p;
        if (top < rows.size()) p = g_sticky.compute(rows[top].line);
        size_t most = vh > 1 ? vh - 1 : 0;
        if (p.size() > most) p.resize(most);
        return p;
    };

    Position cur = v.cursor();
    size_t mrow = 0, mcol = 0;
    WordWrap::toScreen(rows, cur, mrow, mcol);
    if (tab.followCursor) {
        if (mrow < tab.scrollTop) tab.scrollTop = mrow;
        else if (mrow >= tab.scrollTop + vh) tab.scrollTop = mrow - vh + 1;
        // the pinned headers cover the first rows: scroll up until the cursor row is below them
        while (tab.scrollTop > 0 && mrow < tab.scrollTop + pinsAt(tab.scrollTop).size()) --tab.scrollTop;
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
    s += "\x1b[" + std::to_string(kTabRows + 1) + ";1H";          // the text starts below the tab bar

    size_t ci = 0, mi = 0;
    for (size_t ri = tab.scrollTop; ri < endRow; ++ri) {
        const WordWrap::VisualRow& row = rows[ri];
        const std::string& line = b.line(row.line);

        s += tab.gutter.draw(row.line, row.start != 0, cursorLines.count(row.line) > 0, b.lineCount(),
                             row.start == 0 ? v.folding().markAt(row.line) : FoldMark::None);

        for (size_t c = row.start; c < row.end; ++c) {
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
                s += SyntaxHighlighter::ansi(tab.syntax.tokenAt(row.line, c));
            if (st.bold) s += "\x1b[1m";
            if (st.italic) s += "\x1b[3m";
            if (st.underline) s += "\x1b[4m";
            if (st.fontSize > kDefaultFontSize) s += "\x1b[33m";      // bigger size = yellow
            else if (st.fontSize < kDefaultFontSize) s += "\x1b[36m"; // smaller size = cyan
            s += line[c];
            s += "\x1b[0m";
        }
        if (row.last && v.folding().isFolded(row.line)) {     // folded header: "…" and the closing text
            std::string ph = "…" + v.folding().placeholder(row.line);
            size_t used = static_cast<size_t>(gw) + (row.end - row.start);
            size_t room = static_cast<size_t>(W) > used ? static_cast<size_t>(W) - used : 0;
            if (displayCols(ph) > room) ph = room >= 1 ? "…" : "";
            if (!ph.empty()) {
                bool sel = v.isFoldSelected(row.line);
                s += (sel ? "\x1b[0m\x1b[36;7m" : "\x1b[0m\x1b[36m") + ph + "\x1b[0m";
            }
        }
        if (row.last && caretCells.count({row.line, row.end})) s += "\x1b[45m \x1b[0m";   // extra cursor at line end
        if (ri + 1 < endRow) s += "\r\n";
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
            char pos[32];
            snprintf(pos, sizeof pos, "\x1b[%zu;1H", d + 1 + top);     // `top` = rows used by the tab bar
            s += pos;
            s += bg;
            s += tab.gutter.draw(pins[d].line, false, false, b.lineCount(), FoldMark::None);
            size_t n = std::min(hl.size(), room);
            for (size_t c = 0; c < n; ++c) {
                s += SyntaxHighlighter::ansi(tab.syntax.tokenAt(pins[d].line, c));
                s += bg;
                s += ul;
                s += hl[c] == '\t' ? ' ' : hl[c];
            }
            s += bg;
            s += ul;
            s.append(room - n, ' ');
            s += "\x1b[0m";
        }
    }

    // ---- minimap on the right edge ----
    int mmCols = minimapCols();
    if (mmCols > 0 && tab.scrollTop < endRow)
        s += drawMinimap(v, rows[tab.scrollTop].line, rows[endRow - 1].line, vh, W, mmCols, tab.gutter);

    // ---- bottom bars: status, statistics, find / replace / file name ----
    CharStyle ts = v.typingStyle();
    char buf[512];

    snprintf(buf, sizeof buf, " %s%s | Ln %zu, Col %zu | cursors %zu | size %d%s%s%s | %s | ^S save ^O open ^T new ^W close ^Q quit",
             in.fileName.c_str(), in.modified ? " [+]" : "",
             cur.line + 1, cur.col + 1, cs.size(), ts.fontSize,
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
        if (Wz > right.size() + 1) {
            if (statsLine.size() > Wz - right.size() - 1) statsLine.resize(Wz - right.size() - 1);
            statsLine.append(Wz - right.size() - statsLine.size(), ' ');
            statsLine += right;
        }
    }
    s += bar(H - 1, statsLine, W, "\x1b[0m\x1b[90m");

    // ---- find / replace panel, pinned at the bottom ----
    int panelRow = 1, panelCol = 1;
    if (in.mode == InputMode::Find || in.mode == InputMode::Replace) {
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
        s += FindPanel::draw(fi, W, H, panelRow, panelCol);
    }

    if (in.mode == InputMode::Prompt) s += in.browser.draw();      // the Open / Save as file browser
    if (g_dialog.active()) s += g_dialog.draw(W, H);
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
            WordWrap::toScreen(rows, cs[i].pos, r, c);
            if (r < tab.scrollTop + pinned || r >= endRow) continue;  // off-screen or under a pinned line, skip
            coords += ";2:" + std::to_string(r - tab.scrollTop + 1 + top) + ":" + std::to_string(c + 1 + static_cast<size_t>(gw));
        }
        if (!coords.empty()) s += "\x1b[>29" + coords + " q";
    }

    // ---- where the terminal's own cursor goes ----
    bool cursorVisible = mrow >= tab.scrollTop + pinned && mrow < endRow;
    s += (!editorHasCursor || cursorVisible) ? "\x1b[?25h" : "\x1b[?25l";   // hide it while scrolled off-screen
    if (editorHasCursor) {
        snprintf(buf, sizeof buf, "\x1b[%zu;%zuH", (cursorVisible ? mrow - tab.scrollTop + 1 : 1) + top,
                 mcol + 1 + static_cast<size_t>(gw));
    } else if (in.mode == InputMode::Prompt) {
        int pr = 1, pc = 1;
        in.browser.cursorPos(pr, pc);
        snprintf(buf, sizeof buf, "\x1b[%d;%dH", pr, pc);
    } else {                                               // find / replace panel: position set by FindPanel::draw
        snprintf(buf, sizeof buf, "\x1b[%d;%dH", panelRow, panelCol);
    }
    s += buf;
    if (g_dialog.active()) s += "\x1b[?25l";               // no text cursor behind the dialog
    out(s);
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

// Gives bytes to the normal input handler, for the current tab.
static void feedInput(const std::string& data, InputState& state) {
    // g_quitArmed = false;
    // g_tabs.disarmClose();
    g_tabs.endDrag();                                          // a key press ends a drag whose release was lost

    EditorTab& tab = g_tabs.current();
    state.wrapWidth = wrapWidth(tab.view.buffer());
    state.pageRows = g_viewRows;
    state.scrollTop = tab.scrollTop;
    state.wheelRows = 0;
    {                                                          // the file browser and the find panel need the screen size
        winsize ws{};
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
        state.screenW = ws.ws_col ? ws.ws_col : 80;
        state.screenH = ws.ws_row ? ws.ws_row : 24;
        state.screenRowOffset = state.mode == InputMode::Prompt ? 0 : kTabRows;
        state.browser.setScreen(state.screenW, state.screenH);
    }
    syncMinimapState(state);
    state.stickyRows = static_cast<int>(g_pinnedRows);
    syncDocState(state, tab);
    state.modified = false;       // the quit and "open anyway" warnings are handled by the tabs, not by the input handler

    std::vector<Cursor> cursorsBefore = tab.view.cursors();
    handleInput(data.data(), static_cast<ssize_t>(data.size()), tab.view, state);
    if (state.clipboardDirty) {                                // copy to the system clipboard too
        copyToSystemClipboard(state.clipboard);
        state.clipboardDirty = false;
    }
    g_minimapOn = state.minimapOn;
    handleMinimapEvent(tab, state);
    handleStickyClick(tab, state);
    if (state.wheelRows != 0) {
        long long t = static_cast<long long>(tab.scrollTop) + state.wheelRows;
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
    handleFileRequest(state);
}

// ---- unsaved-changes dialog ----

static size_t indexOfTab(int id) {
    for (size_t i = 0; i < g_tabs.count(); ++i)
        if (g_tabs.at(i).id == id) return i;
    return g_tabs.count();
}

// Starts saving a tab by sending it Ctrl+S. A tab without a file name gets the Save As browser.
static void startSave(size_t index, InputState& st) {
    g_tabs.select(index);
    feedInput("\x13", st);
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

// Carries out one tab command.
static void applyTabAction(const TabAction& a, InputState& st, int W) {
    bool onBar = a.kind == TabAction::BarClick || a.kind == TabAction::BarMiddle || a.kind == TabAction::BarWheel;
    if (onBar && st.mode == InputMode::Prompt) return;    // the file browser has the keyboard

    switch (a.kind) {
        case TabAction::New:
            g_tabs.newTab();
            st.status = "new tab";
            break;
        case TabAction::Close:
            requestClose(g_tabs.currentIndex(), st);
            break;
        case TabAction::Next: g_tabs.next(); break;
        case TabAction::Prev: g_tabs.prev(); break;
        case TabAction::Goto:
            if (static_cast<size_t>(a.a) < g_tabs.count()) g_tabs.select(static_cast<size_t>(a.a));
            else st.status = "no tab " + std::to_string(a.a + 1);
            break;
        case TabAction::GotoLast: g_tabs.selectLast(); break;
        case TabAction::Reopen: {
            std::string msg;
            g_tabs.reopenClosed(msg);
            st.status = msg;
            break;
        }
        case TabAction::Quit:
            requestQuit(st);
            break;
        case TabAction::BarClick: {
            TabHit h = g_tabs.hit(a.a, W);
            if (h.kind == TabHit::Tab) { g_tabs.select(h.index); g_tabs.beginDrag(a.a, W); }   // select it, and a drag may follow
            else if (h.kind == TabHit::Close) requestClose(h.index, st);
            else if (h.kind == TabHit::ArrowLeft) g_tabs.scrollBar(-1);
            else if (h.kind == TabHit::ArrowRight) g_tabs.scrollBar(1);
            else if (h.kind == TabHit::Plus) { g_tabs.newTab(); st.status = "new tab"; }
            break;
        }
        case TabAction::BarMiddle: {
            TabHit h = g_tabs.hit(a.a, W);
            if (h.kind == TabHit::Tab || h.kind == TabHit::Close) requestClose(h.index, st);
            break;
        }
        case TabAction::BarWheel:
            g_tabs.scrollBar(a.a);
            break;
        case TabAction::BarDrag:
            g_tabs.dragTo(a.a, W);
            break;
        case TabAction::BarRelease:
            g_tabs.endDrag();
            break;
        case TabAction::MoveLeft:
        case TabAction::MoveRight: {
            size_t i = g_tabs.currentIndex();
            size_t to = a.kind == TabAction::MoveLeft ? (i > 0 ? i - 1 : 0)
                                                      : std::min(i + 1, g_tabs.count() - 1);
            if (to != i) { g_tabs.move(i, to); st.status = "moved tab"; }
            break;
        }
    }
}

// The find bar stays open when you switch tabs: the tab you left loses its highlights, the new one is searched.
static void onTabSwitched(int leftId, InputState& st) {
    size_t i = indexOfTab(leftId);
    if (i < g_tabs.count()) g_tabs.at(i).view.clearFind();
    refreshFind(g_tabs.current().view, st);
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
    syncDocState(state, g_tabs.current());
    render(g_tabs.current(), state);

    char buf[1024];
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

            if (g_dialog.active()) {                           // the dialog takes all input while it is open
                Dialog::Result r = g_dialog.feed(buf, n);
                if (r != Dialog::Result::None) onDialogResult(r, state);
            } else {
                int W = termCols();
                bool prompt = state.mode == InputMode::Prompt;
                bool tabKeys = state.mode != InputMode::Prompt;      // tab shortcuts also work with the find bar open
                std::vector<InputPiece> pieces = TabInput::split(buf, n, tabKeys, prompt ? 0 : kTabRows, g_tabs.dragging());
                for (const InputPiece& p : pieces) {
                    if (!state.running || g_dialog.active()) break;
                    if (p.isAction) {
                        int beforeId = g_tabs.current().id;
                        applyTabAction(p.action, state, W);
                        if (g_tabs.current().id != beforeId) onTabSwitched(beforeId, state);   // switched, opened or closed a tab
                    } else {
                        feedInput(p.data, state);
                    }
                }
                advanceSaveFlow(state);
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
