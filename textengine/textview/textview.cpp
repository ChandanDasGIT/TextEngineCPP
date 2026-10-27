#include "textview.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <utility>

namespace {

std::string withoutCR(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}

// ---- helpers for word movement and indentation ----

enum class CharClass { Space, Word, Other };

CharClass classOf(unsigned char ch) {
    if (ch == ' ' || ch == '\t') return CharClass::Space;
    if (std::isalnum(ch) || ch == '_' || ch >= 0x80) return CharClass::Word;
    return CharClass::Other;
}

// number of leading spaces/tabs of a line
size_t indentOf(const std::string& line) {
    size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return n;
}

Position wordLeftOf(const TextBuffer& b, Position p) {
    if (p.col == 0) {
        if (p.line > 0) { p.line--; p.col = b.line(p.line).size(); }
        return p;
    }
    const std::string& line = b.line(p.line);
    size_t c = p.col;
    while (c > 0 && classOf(static_cast<unsigned char>(line[c - 1])) == CharClass::Space) --c;
    if (c > 0) {
        CharClass k = classOf(static_cast<unsigned char>(line[c - 1]));
        while (c > 0 && classOf(static_cast<unsigned char>(line[c - 1])) == k) --c;
    }
    return Position{p.line, c};
}

Position wordRightOf(const TextBuffer& b, Position p) {
    const std::string& line = b.line(p.line);
    size_t len = line.size();
    if (p.col >= len) {
        if (p.line + 1 < b.lineCount()) return Position{p.line + 1, 0};
        return p;
    }
    size_t c = p.col;
    while (c < len && classOf(static_cast<unsigned char>(line[c])) == CharClass::Space) ++c;
    if (c < len) {
        CharClass k = classOf(static_cast<unsigned char>(line[c]));
        while (c < len && classOf(static_cast<unsigned char>(line[c])) == k) ++c;
    }
    return Position{p.line, c};
}

// ---- helpers for brackets and quotes ----

char closerOf(char ch) {
    switch (ch) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        default: return 0;
    }
}

bool isQuote(char ch) { return ch == '"' || ch == '\'' || ch == '`'; }

bool isPair(char open, char close) {
    return (closerOf(open) != 0 && closerOf(open) == close) || (isQuote(open) && open == close);
}

bool isWordChar(char ch) {
    unsigned char u = static_cast<unsigned char>(ch);
    return std::isalnum(u) || u == '_' || u >= 0x80;
}

// ---- helpers for numbered and lettered lists ----

enum class ListKind { Number, Lower, Upper };

struct ListMarker {
    size_t indentLen = 0;                 // spaces/tabs before the marker
    ListKind kind = ListKind::Number;
    unsigned long long value = 1;         // the number, or the letter position (a = 1, z = 26, aa = 27)
    char delim = '.';                     // '.' or ')'
    std::string gap;                      // spaces after the delimiter
    size_t end = 0;                       // column where the text after the marker starts
};

std::string lettersOf(unsigned long long v, bool upper) {
    std::string s;
    while (v > 0) {
        --v;
        s.insert(s.begin(), static_cast<char>((upper ? 'A' : 'a') + static_cast<int>(v % 26)));
        v /= 26;
    }
    return s;
}

std::string formatMarker(ListKind kind, unsigned long long value, char delim, const std::string& gap) {
    std::string t = kind == ListKind::Number ? std::to_string(value) : lettersOf(value, kind == ListKind::Upper);
    return t + std::string(1, delim) + gap;
}

ListKind childKindOf(ListKind k) { return k == ListKind::Number ? ListKind::Lower : ListKind::Number; }

// Recognises "  12. text", "3) text", "b. text", "AA) text". Needs at least one space after the delimiter.
bool listMarkerOf(const std::string& line, ListMarker& m) {
    size_t i = indentOf(line);
    size_t d = i;
    ListKind kind = ListKind::Number;
    unsigned long long value = 0;

    if (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
        while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d]))) ++d;
        if (d - i > 9) return false;
        value = std::stoull(line.substr(i, d - i));
    } else if (i < line.size() && std::isalpha(static_cast<unsigned char>(line[i]))) {
        bool upper = std::isupper(static_cast<unsigned char>(line[i])) != 0;
        while (d < line.size() && std::isalpha(static_cast<unsigned char>(line[d])) &&
               (std::isupper(static_cast<unsigned char>(line[d])) != 0) == upper) ++d;
        if (d - i > 3) return false;                           // longer words are text, not markers
        kind = upper ? ListKind::Upper : ListKind::Lower;
        for (size_t k = i; k < d; ++k)
            value = value * 26 + static_cast<unsigned long long>(std::tolower(static_cast<unsigned char>(line[k])) - 'a' + 1);
    } else {
        return false;
    }

    if (d >= line.size() || (line[d] != '.' && line[d] != ')')) return false;
    size_t g = d + 1;
    while (g < line.size() && line[g] == ' ') ++g;
    if (g == d + 1) return false;

    m.indentLen = i;
    m.kind = kind;
    m.value = value;
    m.delim = line[d];
    m.gap = line.substr(d + 1, g - d - 1);
    m.end = g;
    return true;
}

// The marker an item gets when it is moved to an indent of newIndent. Looks up through the list:
// a sibling at that indent is continued; when going deeper, the item above it starts a new sub-list.
bool markerFor(const TextBuffer& b, size_t line, size_t newIndent, bool deeper,
               ListKind& kind, unsigned long long& value, char& delim, std::string& gap) {
    for (size_t l = line; l-- > 0;) {
        ListMarker m;
        if (!listMarkerOf(b.line(l), m)) return false;
        if (m.indentLen == newIndent) {
            kind = m.kind; value = m.value + 1; delim = m.delim; gap = m.gap;
            return true;
        }
        if (m.indentLen < newIndent) {
            if (!deeper) return false;
            kind = childKindOf(m.kind); value = 1; delim = m.delim; gap = m.gap;
            return true;
        }
    }
    return false;
}

// The whole text of an empty list item after it moved one level deeper or back out.
// found is false when no marker could be worked out (the old marker is kept).
std::string reindentedItem(const TextBuffer& b, size_t line, const ListMarker& cur, bool deeper,
                           const IndentStyle& indent, bool& found) {
    const std::string& text = b.line(line);
    size_t newIndent;
    if (deeper) {
        newIndent = cur.indentLen + indent.unit().size();
    } else {                                                    // take away one tab, or up to `width` spaces
        size_t cut = cur.indentLen > 0 && text[cur.indentLen - 1] == '\t' ? 1 : std::min(indent.width, cur.indentLen);
        newIndent = cur.indentLen - cut;
    }
    std::string lead = deeper ? text.substr(0, cur.indentLen) + indent.unit() : text.substr(0, newIndent);

    ListKind kind = ListKind::Number;
    unsigned long long value = 1;
    char delim = '.';
    std::string gap;
    found = markerFor(b, line, newIndent, deeper, kind, value, delim, gap);
    if (found) return lead + formatMarker(kind, value, delim, gap);
    return lead + text.substr(cur.indentLen);
}

// auto-closing only happens in front of nothing, a space or closing punctuation
bool closesBefore(const std::string& line, size_t col) {
    if (col >= line.size()) return true;
    const std::string allowed = " \t;:.,=)]}>";
    return allowed.find(line[col]) != std::string::npos;
}

// Finds the bracket that pairs with the one at p. Gives up after scanning a lot of text.
bool findPartner(const TextBuffer& b, Position p, Position& out) {
    char ch = b.line(p.line)[p.col];
    char want = 0;
    bool forward = true;
    switch (ch) {
        case '(': want = ')'; break;
        case '[': want = ']'; break;
        case '{': want = '}'; break;
        case ')': want = '('; forward = false; break;
        case ']': want = '['; forward = false; break;
        case '}': want = '{'; forward = false; break;
        default: return false;
    }

    int depth = 0;
    size_t budget = 50000;
    size_t ln = p.line;
    if (forward) {
        size_t col = p.col + 1;
        while (true) {
            const std::string& s = b.line(ln);
            for (; col < s.size(); ++col) {
                if (budget-- == 0) return false;
                if (s[col] == ch) ++depth;
                else if (s[col] == want) {
                    if (depth == 0) { out = Position{ln, col}; return true; }
                    --depth;
                }
            }
            if (ln + 1 >= b.lineCount()) return false;
            ++ln;
            col = 0;
        }
    } else {
        size_t col = p.col;
        while (true) {
            const std::string& s = b.line(ln);
            while (col > 0) {
                --col;
                if (budget-- == 0) return false;
                if (s[col] == ch) ++depth;
                else if (s[col] == want) {
                    if (depth == 0) { out = Position{ln, col}; return true; }
                    --depth;
                }
            }
            if (ln == 0) return false;
            --ln;
            col = b.line(ln).size();
        }
    }
}

// ---- helpers for the line operations ----

struct LineBlock {
    size_t a;   // first line
    size_t b;   // last line
};

// The lines touched by the cursors, with overlapping or neighbouring blocks merged.
// A folded block counts as one piece, including its hidden lines.
std::vector<LineBlock> lineBlocksOf(const std::vector<Cursor>& cursors, const Folding& folding) {
    std::vector<LineBlock> merged;
    for (const Cursor& c : cursors) {
        size_t a = c.start().line;
        size_t b = c.end().line;
        if (c.hasSelection() && c.end().col == 0 && b > a) --b;   // selection ends at the start of a line
        a = folding.headerOf(a);
        b = folding.foldEndOf(b);
        if (!merged.empty() && a <= merged.back().b + 1) merged.back().b = std::max(merged.back().b, b);
        else merged.push_back({a, b});
    }
    return merged;
}

// Number of lines in blocks that lie completely above line x.
size_t linesAbove(const std::vector<LineBlock>& blocks, size_t x) {
    size_t n = 0;
    for (const LineBlock& bl : blocks)
        if (bl.b < x) n += bl.b - bl.a + 1;
    return n;
}

const LineBlock* blockAt(const std::vector<LineBlock>& blocks, size_t x) {
    for (const LineBlock& bl : blocks)
        if (x >= bl.a && x <= bl.b) return &bl;
    return nullptr;
}

}

TextView::TextView() : m_cursors(1) {m_folding.update(m_buffer);}

Cursor& TextView::mainCursor() { return m_cursors[m_main]; }
const Cursor& TextView::mainCursor() const { return m_cursors[m_main]; }

// ---------------------------------------------------------------- queries

bool TextView::hasSelection() const { return mainCursor().hasSelection(); }

bool TextView::anySelection() const {
    for (const Cursor& c : m_cursors)
        if (c.hasSelection()) return true;
    return false;
}

Position TextView::selectionStart() const { return mainCursor().start(); }
Position TextView::selectionEnd() const { return mainCursor().end(); }

std::string TextView::selectedText() const {
    return m_buffer.textInRange(selectionStart(), selectionEnd());
}

Position TextView::cursor() const { return mainCursor().pos; }
std::string TextView::text() const { return m_buffer.text(); }
const TextBuffer& TextView::buffer() const { return m_buffer; }

size_t TextView::cursorCount() const { return m_cursors.size(); }
const std::vector<Cursor>& TextView::cursors() const { return m_cursors; }
size_t TextView::mainCursorIndex() const { return m_main; }

CharStyle TextView::styleAt(size_t line, size_t col) const { return m_style.at(line, col); }
CharStyle TextView::typingStyle() const { return m_typingStyle; }

TextStats TextView::stats() const { return computeStats(m_buffer.text()); }
TextStats TextView::selectionStats() const { return computeStats(joinedSelection()); }

std::string TextView::joinedSelection() const {
    std::string out;
    bool first = true;
    for (const Cursor& c : m_cursors) {
        if (!c.hasSelection()) continue;
        if (!first) out += '\n';
        out += m_buffer.textInRange(c.start(), c.end());
        first = false;
    }
    return out;
}

// ---------------------------------------------------------------- cursor movement

void TextView::syncTypingStyle() {
    Position p = mainCursor().pos;
    if (p.col > 0) m_typingStyle = m_style.at(p.line, p.col - 1);
}

void TextView::finishMove() {
    MultiCursor::normalize(m_cursors, m_main);
    revealCursors();
    syncTypingStyle();
    m_lastKind = EditKind::None;   // moving ends the current typing group for undo
}

size_t TextView::goalVisual(const Cursor& c) const {
    if (c.vertValid && c.pos == c.vertPos) return c.vertGoal;           // still where the last Up / Down left it
    return Utf8::columnOf(m_buffer.line(c.pos.line), c.pos.col, m_indent.width);
}

void TextView::moveAll(bool extend, bool keepGoal, const std::function<Position(const Cursor&)>& target) {
    for (Cursor& c : m_cursors) {
        size_t goal = keepGoal ? goalVisual(c) : 0;
        Position p = m_buffer.clamp(target(c));
        c.pos = p;
        if (!extend) c.anchor = p;
        if (!keepGoal) {
            c.goalCol = p.col;
        } else {
            c.vertGoal = goal;
            c.vertPos = p;
            c.vertValid = true;
        }
    }
    finishMove();
}

void TextView::moveCursor(Position p, bool extend) {
    p = m_buffer.clamp(p);
    if (!extend) {                       // a click: back to a single cursor
        Cursor c;
        c.pos = c.anchor = p;
        c.goalCol = p.col;
        m_cursors.assign(1, c);
        m_main = 0;
    } else {                             // a drag: stretches the main cursor
        Cursor& c = mainCursor();
        c.pos = p;
        c.goalCol = p.col;
    }
    finishMove();
}

void TextView::moveLeft(bool extend) {
    m_suppressFoldSnap = false;
    if (extend)
        for (const Cursor& c : m_cursors)
            if (selectionEndsAtFoldEnd(c)) { m_suppressFoldSnap = true; break; }

    moveAll(extend, false, [this, extend](const Cursor& c) {
        if (!extend && c.hasSelection()) return c.start();
        if (extend && selectionEndsAtFoldEnd(c)) {            // shrink back to the folded header
            size_t h = m_folding.headerOf(c.pos.line);
            return Position{h, m_buffer.lineLength(h)};
        }
        Position p = c.pos;
        if (p.col > 0) p.col = Utf8::prevBoundary(m_buffer.line(p.line), p.col);
        else if (p.line > 0) { p.line = m_folding.prevVisible(p.line); p.col = m_buffer.lineLength(p.line); }
        return p;
    });
    m_suppressFoldSnap = false;
}

void TextView::moveRight(bool extend) {
    moveAll(extend, false, [this, extend](const Cursor& c) {
        if (!extend && c.hasSelection()) return c.end();
        Position p = c.pos;
        if (p.col < m_buffer.lineLength(p.line)) p.col = Utf8::nextBoundary(m_buffer.line(p.line), p.col);
        else {
            size_t nv = m_folding.nextVisible(p.line);
            if (nv < m_buffer.lineCount()) { p.line = nv; p.col = 0; }
        }
        return p;
    });
}

void TextView::moveUp(bool extend) {
    m_suppressFoldSnap = false;
    if (extend)
        for (const Cursor& c : m_cursors)
            if (selectionEndsAtFoldEnd(c)) { m_suppressFoldSnap = true; break; }

    moveAll(extend, true, [this](const Cursor& c) {
        Position p = c.pos;
        if (p.line == 0) p.col = 0;
        else { p.line = m_folding.prevVisible(p.line); p.col = Utf8::byteAtColumn(m_buffer.line(p.line), goalVisual(c), m_indent.width); }
        return p;
    });
    m_suppressFoldSnap = false;
}

void TextView::moveDown(bool extend) {
    moveAll(extend, true, [this](const Cursor& c) {
        Position p = c.pos;
        size_t nv = m_folding.nextVisible(p.line);
        if (nv >= m_buffer.lineCount()) p.col = m_buffer.lineLength(p.line);
        else { p.line = nv; p.col = Utf8::byteAtColumn(m_buffer.line(p.line), goalVisual(c), m_indent.width); }
        return p;
    });
}

void TextView::moveHome(bool extend) {
    moveAll(extend, false, [this](const Cursor& c) {
        const std::string& line = m_buffer.line(c.pos.line);
        size_t indent = indentOf(line);
        if (indent >= line.size()) indent = 0;                    // blank line: just column 0
        return Position{c.pos.line, c.pos.col == indent ? size_t(0) : indent};
    });
}

void TextView::moveWordLeft(bool extend) {
    moveAll(extend, false, [this](const Cursor& c) {
        Position p = wordLeftOf(m_buffer, c.pos);
        if (m_folding.isHidden(p.line)) {
            p.line = m_folding.headerOf(p.line);
            p.col = m_buffer.lineLength(p.line);
        }
        return p;
    });
}

void TextView::moveWordRight(bool extend) {
    moveAll(extend, false, [this](const Cursor& c) {
        Position p = wordRightOf(m_buffer, c.pos);
        if (m_folding.isHidden(p.line)) {
            size_t nv = m_folding.nextVisible(c.pos.line);
            p = nv < m_buffer.lineCount() ? Position{nv, 0} : c.pos;
        }
        return p;
    });
}

void TextView::moveEnd(bool extend) {
    moveAll(extend, false, [this](const Cursor& c) {
        return Position{c.pos.line, m_buffer.lineLength(c.pos.line)};
    });
}

void TextView::select(Position from, Position to) {
    Cursor c;
    c.anchor = m_buffer.clamp(from);
    c.pos = m_buffer.clamp(to);
    c.goalCol = c.pos.col;
    m_cursors.assign(1, c);
    m_main = 0;
    finishMove();
}

void TextView::selectAll() { select({0, 0}, m_buffer.endPosition()); }

// ---------------------------------------------------------------- multiple cursors

void TextView::addCursor(Position p) {
    p = m_buffer.clamp(p);
    Cursor c;
    c.pos = c.anchor = p;
    c.goalCol = p.col;
    m_cursors.push_back(c);
    m_main = m_cursors.size() - 1;
    finishMove();
}

void TextView::addCursorVertical(int dir) {
    size_t oldCount = m_cursors.size();
    size_t newMain = m_main;
    for (size_t i = 0; i < oldCount; ++i) {
        Cursor from = m_cursors[i];
        size_t line;
        if (dir < 0) {
            if (from.pos.line == 0) continue;
            line = m_folding.prevVisible(from.pos.line);
        } else {
            line = m_folding.nextVisible(from.pos.line);
            if (line >= m_buffer.lineCount()) continue;
        }
        size_t goal = goalVisual(from);
        Position p = m_buffer.clamp(Position{line, Utf8::byteAtColumn(m_buffer.line(line), goal, m_indent.width)});
        Cursor c;
        c.pos = c.anchor = p;
        c.goalCol = from.goalCol;
        c.vertGoal = goal;
        c.vertPos = p;
        c.vertValid = true;
        m_cursors.push_back(c);
        if (i == m_main) newMain = m_cursors.size() - 1;
    }
    m_main = newMain;
    finishMove();
}

void TextView::addCursorAbove() { addCursorVertical(-1); }
void TextView::addCursorBelow() { addCursorVertical(1); }

bool TextView::addNextOccurrence() {
    Cursor& mc = mainCursor();

    if (!mc.hasSelection()) {            // first press: select the word under the cursor
        const std::string& line = m_buffer.line(mc.pos.line);
        auto isWord = [](unsigned char ch) { return std::isalnum(ch) || ch == '_' || ch >= 0x80; };
        size_t a = mc.pos.col, b = mc.pos.col;
        while (a > 0 && isWord(static_cast<unsigned char>(line[a - 1]))) --a;
        while (b < line.size() && isWord(static_cast<unsigned char>(line[b]))) ++b;
        if (a == b) return false;
        mc.anchor = {mc.pos.line, a};
        mc.pos = {mc.pos.line, b};
        mc.goalCol = b;
        finishMove();
        return true;
    }

    Position s = mc.start(), e = mc.end();
    //if (s.line != e.line) return false;  // only single-line selections are searched for

    std::string needle = m_buffer.textInRange(s, e);
    std::vector<Range> all = FindReplace::findAll(m_buffer, needle, true);

    auto alreadySelected = [this](const Range& r) {
        for (const Cursor& c : m_cursors)
            if (c.start() == r.from && c.end() == r.to) return true;
        return false;
    };

    // matches after the main selection first, then the ones before it (wrap around)
    const Range* pick = nullptr;
    for (const Range& r : all)
        if (!(r.from < e) && !alreadySelected(r)) { pick = &r; break; }
    if (!pick)
        for (const Range& r : all)
            if (!alreadySelected(r)) { pick = &r; break; }
    if (!pick) return false;

    Cursor c;
    c.anchor = pick->from;
    c.pos = pick->to;
    c.goalCol = pick->to.col;
    m_cursors.push_back(c);
    m_main = m_cursors.size() - 1;
    finishMove();
    return true;
}

void TextView::clearExtraCursors() {
    Cursor c = mainCursor();
    m_cursors.assign(1, c);
    m_main = 0;
    finishMove();
}

// ---------------------------------------------------------------- undo / redo

namespace {

// the position after `text` when it starts at `at`
Position endAfter(Position at, const std::string& text) {
    size_t lines = 0, lastBreak = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n') { ++lines; lastBreak = i; }
    if (lines == 0) return {at.line, at.col + text.size()};
    return {at.line + lines, text.size() - lastBreak - 1};
}

}

// Puts one change in (forward) or takes it out again (backward).
void TextView::applyChange(const Change& c, bool forward) {
    if (c.styleOnly) {
        const std::vector<CharStyle>& styles = forward ? c.insertedStyles : c.removedStyles;
        m_style.erase(c.at, c.to);
        m_style.insertStyled(c.at, m_buffer.textInRange(c.at, c.to), styles);
        return;
    }
    const std::string& gone = forward ? c.removed : c.inserted;
    const std::string& come = forward ? c.inserted : c.removed;
    const std::vector<CharStyle>& comeStyles = forward ? c.insertedStyles : c.removedStyles;
    if (!gone.empty()) {
        Position end = endAfter(c.at, gone);
        m_buffer.erase(c.at, end);
        m_style.erase(c.at, end);
    }
    if (!come.empty()) {
        m_buffer.insert(c.at, come);
        m_style.insertStyled(c.at, come, comeStyles);
    }
}

void TextView::afterUndoRedo() {
    ++m_styleStamp;
    if (m_cursors.empty()) m_cursors.assign(1, Cursor{});
    if (m_main >= m_cursors.size()) m_main = 0;
    for (Cursor& c : m_cursors) { c.pos = m_buffer.clamp(c.pos); c.anchor = m_buffer.clamp(c.anchor); }
    m_folding.update(m_buffer);
    revealCursors();
    if (m_find.active()) m_find.refresh(m_buffer);
    syncTypingStyle();
    m_lastKind = EditKind::None;
}

// Starts an undo step before a change. Typing (and backspacing) in a row is one undo step.
void TextView::pushUndo(EditKind kind) {
    bool joinsPrevious = kind != EditKind::Other && kind == m_lastKind;
    if (!joinsPrevious) m_history.begin(m_cursors, m_main);
    m_lastKind = kind;
}

bool TextView::undo() {
    UndoStep* st = m_history.stepToUndo();
    if (!st) return false;
    st->after = m_cursors;
    st->mainAfter = m_main;
    for (size_t i = st->changes.size(); i-- > 0;) applyChange(st->changes[i], false);
    m_cursors = st->before;
    m_main = st->mainBefore;
    m_history.markUndone();
    afterUndoRedo();
    return true;
}

bool TextView::redo() {
    UndoStep* st = m_history.stepToRedo();
    if (!st) return false;
    st->before = m_cursors;                    // undoing this redo puts the cursors back as they are now
    st->mainBefore = m_main;
    for (const Change& c : st->changes) applyChange(c, true);
    m_cursors = st->after;
    m_main = st->mainAfter;
    m_history.markRedone();
    afterUndoRedo();
    return true;
}

// Formatting over the selection of every cursor: one undo step that remembers the formatting before and after.
void TextView::changeStyles(const std::function<void(Position, Position)>& op) {
    pushUndo(EditKind::Other);
    ++m_styleStamp;
    for (const Cursor& c : m_cursors) {
        if (!c.hasSelection()) continue;
        Change ch;
        ch.styleOnly = true;
        ch.at = c.start();
        ch.to = c.end();
        ch.removedStyles = m_style.slice(ch.at, ch.to);
        op(ch.at, ch.to);
        ch.insertedStyles = m_style.slice(ch.at, ch.to);
        m_history.add(std::move(ch));
    }
}

bool TextView::canUndo() const { return m_history.canUndo(); }
bool TextView::canRedo() const { return m_history.canRedo(); }

// ---------------------------------------------------------------- the one place the text changes

std::vector<size_t> TextView::applyEdits(const std::vector<EditRange>& edits, EditKind kind) {
    pushUndo(kind);

    // positions in the text as it is now
    std::vector<std::pair<Position, Position>> where;
    where.reserve(edits.size());
    for (const EditRange& e : edits)
        where.push_back({m_buffer.positionOf(e.from), m_buffer.positionOf(e.to)});

    // last edit first, so the positions of the earlier edits stay valid
    for (size_t i = edits.size(); i-- > 0;) {
        const EditRange& e = edits[i];
        Change ch;                                            // what undo needs to know about this edit
        ch.at = where[i].first;
        if (where[i].first != where[i].second) {
            ch.removed = m_buffer.textInRange(where[i].first, where[i].second);
            ch.removedStyles = m_style.slice(where[i].first, where[i].second);
            m_buffer.erase(where[i].first, where[i].second);
            m_style.erase(where[i].first, where[i].second);
        }
        if (!e.text.empty()) {
            Position end = m_buffer.insert(where[i].first, e.text);
            if (e.styled) m_style.insertStyled(where[i].first, e.text, e.styles);
            else m_style.insert(where[i].first, e.text, m_typingStyle);
            ch.inserted = m_buffer.textInRange(where[i].first, end);
            ch.insertedStyles = m_style.slice(where[i].first, end);
        }
        if (!ch.removed.empty() || !ch.inserted.empty()) m_history.add(std::move(ch));
    }

    m_folding.update(m_buffer);

    // where each replacement ends in the new text
    std::vector<size_t> ends;
    long long shift = 0;
    for (const EditRange& e : edits) {
        ends.push_back(static_cast<size_t>(static_cast<long long>(e.from) + shift) + e.text.size());
        shift += static_cast<long long>(e.text.size()) - static_cast<long long>(e.to - e.from);
    }

    if (m_find.active()) m_find.refresh(m_buffer);
    return ends;
}

void TextView::setCaretsAt(const std::vector<size_t>& offsets) {
    if (offsets.empty()) return;
    std::vector<Cursor> cs;
    for (size_t o : offsets) {
        Position p = m_buffer.positionOf(o);
        Cursor c;
        c.pos = c.anchor = p;
        c.goalCol = p.col;
        cs.push_back(c);
    }
    m_cursors = std::move(cs);
    if (m_main >= m_cursors.size()) m_main = m_cursors.size() - 1;
    MultiCursor::normalize(m_cursors, m_main);
    syncTypingStyle();
    revealCursors();
}

void TextView::setCursors(std::vector<Cursor> cs) {
    if (cs.empty()) return;
    for (Cursor& c : cs) {
        c.pos = m_buffer.clamp(c.pos);
        c.anchor = m_buffer.clamp(c.anchor);
        c.goalCol = c.pos.col;
    }
    m_cursors = std::move(cs);
    if (m_main >= m_cursors.size()) m_main = m_cursors.size() - 1;
    MultiCursor::normalize(m_cursors, m_main);
    syncTypingStyle();
    revealCursors();
}

// ---------------------------------------------------------------- editing

void TextView::insertAtCursors(const std::vector<std::string>& texts, EditKind kind) {
    std::vector<EditRange> edits;
    for (size_t i = 0; i < m_cursors.size(); ++i) {
        const Cursor& c = m_cursors[i];
        edits.push_back({m_buffer.offsetOf(c.start()), m_buffer.offsetOf(c.end()), texts[i], {}, false});
    }
    setCaretsAt(applyEdits(edits, kind));
}

void TextView::insertText(const std::string& s) {
    std::string clean = withoutCR(s);
    if (clean.empty()) return;
    if (m_autoClose && clean.size() == 1 && !anySelection() && insertSmartChar(clean[0])) return;
    EditKind kind = clean.find('\n') == std::string::npos ? EditKind::Typing : EditKind::Other;
    insertAtCursors(std::vector<std::string>(m_cursors.size(), clean), kind);
}

// Auto-close and step-over for one typed character. Returns false if nothing special applies.
bool TextView::insertSmartChar(char ch) {
    bool opener = closerOf(ch) != 0;
    bool closer = ch == ')' || ch == ']' || ch == '}';
    if (!opener && !closer && !isQuote(ch)) return false;

    enum class Act { Plain, Pair, Skip };
    std::vector<Act> acts;
    bool anySpecial = false, allSkip = true;
    for (const Cursor& c : m_cursors) {
        const std::string& line = m_buffer.line(c.pos.line);
        size_t col = c.pos.col;
        char prev = col > 0 ? line[col - 1] : 0;
        char next = col < line.size() ? line[col] : 0;

        Act a = Act::Plain;
        if ((closer || isQuote(ch)) && next == ch) a = Act::Skip;                    // step over
        else if (opener && closesBefore(line, col)) a = Act::Pair;                   // ( [ {
        else if (isQuote(ch) && !isWordChar(prev) && prev != ch && prev != '\\' &&
                 closesBefore(line, col)) a = Act::Pair;                             // quotes
        acts.push_back(a);
        if (a != Act::Plain) anySpecial = true;
        if (a != Act::Skip) allSkip = false;
    }
    if (!anySpecial) return false;
    if (allSkip) { moveRight(false); return true; }

    char close = opener ? closerOf(ch) : ch;
    std::vector<EditRange> edits;
    for (size_t i = 0; i < m_cursors.size(); ++i) {
        size_t at = m_buffer.offsetOf(m_cursors[i].pos);
        std::string t;
        if (acts[i] == Act::Plain) t = std::string(1, ch);
        else if (acts[i] == Act::Pair) { t = std::string(1, ch); t += close; }
        edits.push_back({at, at, t, {}, false});
    }
    std::vector<size_t> ends = applyEdits(edits, EditKind::Typing);
    for (size_t i = 0; i < ends.size(); ++i) {
        if (acts[i] == Act::Pair) --ends[i];          // between the two characters
        else if (acts[i] == Act::Skip) ++ends[i];     // after the character that was already there
    }
    setCaretsAt(ends);
    return true;
}

void TextView::enter() {
    std::vector<EditRange> edits;
    std::vector<size_t> back(m_cursors.size(), 0);   // how far the cursor sits before the end of the inserted text
    size_t limit = 0;                                // end of the previous edit (edits must not overlap)
    for (size_t i = 0; i < m_cursors.size(); ++i) {
        const Cursor& c = m_cursors[i];
        Position s = c.start();
        const std::string& line = m_buffer.line(s.line);
        size_t n = std::min(indentOf(line), s.col);              // only the indentation left of the cursor
        std::string indent = line.substr(0, n);
        std::string text = "\n" + indent;
        size_t from = m_buffer.offsetOf(c.start());
        size_t to = m_buffer.offsetOf(c.end());

        bool between = m_autoClose && !c.hasSelection() && s.col > 0 && s.col < line.size() &&
                       closerOf(line[s.col - 1]) == line[s.col];
        ListMarker lm;
        if (between) {                                           // "{|}" : open a block
            text = "\n" + indent + m_indent.unit() + "\n" + indent;
            back[i] = 1 + indent.size();
        } else if (!c.hasSelection() && listMarkerOf(line, lm) && s.col >= lm.end) {
            std::string lead = line.substr(0, lm.indentLen);
            if (line.size() == lm.end && s.col == lm.end) {      // empty item
                size_t lineStart = m_buffer.offsetOf({s.line, 0});
                if (lineStart >= limit) {
                    from = lineStart;
                    bool found = false;
                    std::string up;
                    if (lm.indentLen > 0) up = reindentedItem(m_buffer, s.line, lm, false, m_indent, found);
                    text = found ? up : lead;                    // nested: one level out. Top level: the list ends
                }
            } else {                                             // next item
                text = "\n" + lead + formatMarker(lm.kind, lm.value + 1, lm.delim, lm.gap);
            }
        }
        limit = to;
        edits.push_back({from, to, text, {}, false});
    }
    std::vector<size_t> ends = applyEdits(edits, EditKind::Other);
    for (size_t k = 0; k < ends.size(); ++k) ends[k] -= back[k];
    setCaretsAt(ends);
}

void TextView::backspace() {
    std::vector<EditRange> edits;
    bool any = false;
    size_t limit = 0;                                // end of the previous edit (edits must not overlap)
    for (const Cursor& c : m_cursors) {
        size_t a = m_buffer.offsetOf(c.start());
        size_t b = m_buffer.offsetOf(c.end());
        if (a == b && a > 0) {                       // no selection: delete the character before
            const std::string& line = m_buffer.line(c.pos.line);
            if (c.pos.col > 0) a -= c.pos.col - Utf8::prevBoundary(line, c.pos.col);   // a whole character
            else --a;                                                                  // the line break
            bool between = m_autoClose && c.pos.col > 0 && c.pos.col < line.size() &&
                           isPair(line[c.pos.col - 1], line[c.pos.col]);
            if (between) ++b;                        // "(|)": delete both brackets
        }
        if (a < limit) a = limit;
        if (b < a) b = a;
        if (a != b) any = true;
        limit = b;
        edits.push_back({a, b, "", {}, false});
    }
    if (!any) return;
    setCaretsAt(applyEdits(edits, EditKind::Backspace));
}

void TextView::del() {
    std::vector<EditRange> edits;
    bool any = false;
    size_t total = m_buffer.length();
    size_t limit = 0;                                // end of the previous edit (edits must not overlap)
    for (const Cursor& c : m_cursors) {
        size_t a = m_buffer.offsetOf(c.start());
        size_t b = m_buffer.offsetOf(c.end());
        if (a == b && b < total) {                   // no selection: delete the character after
            const std::string& line = m_buffer.line(c.pos.line);
            if (c.pos.col < line.size()) b += Utf8::nextBoundary(line, c.pos.col) - c.pos.col;   // a whole character
            else ++b;                                                                          // the line break
        }
        if (a < limit) a = limit;
        if (b < a) b = a;
        if (a != b) any = true;
        limit = b;
        edits.push_back({a, b, "", {}, false});
    }
    if (!any) return;
    setCaretsAt(applyEdits(edits, EditKind::Backspace));
}

// ---------------------------------------------------------------- clipboard

std::string TextView::copy() const { return joinedSelection(); }

std::string TextView::cut() {
    if (!anySelection()) return "";
    std::string s = joinedSelection();
    std::vector<EditRange> edits;
    for (const Cursor& c : m_cursors)
        edits.push_back({m_buffer.offsetOf(c.start()), m_buffer.offsetOf(c.end()), "", {}, false});
    setCaretsAt(applyEdits(edits, EditKind::Other));
    return s;
}

void TextView::paste(const std::string& s) {
    std::string clean = withoutCR(s);
    if (clean.empty()) return;
    std::vector<std::string> texts(m_cursors.size(), clean);
    if (m_cursors.size() > 1) {                      // one line per cursor, if the counts match
        std::vector<std::string> parts(1);
        for (char ch : clean) {
            if (ch == '\n') parts.emplace_back();
            else parts.back() += ch;
        }
        if (parts.size() == m_cursors.size()) texts = parts;
    }
    insertAtCursors(texts, EditKind::Other);
}

// ---------------------------------------------------------------- find and replace

void TextView::selectMatch(const Range& r) { select(r.from, r.to); }

void TextView::findOpen() {
    m_hasCandidate = mainCursor().hasSelection();
    if (m_hasCandidate) m_candidate = Range{mainCursor().start(), mainCursor().end()};
    m_find.setScope(nullptr);
}

void TextView::findSetQuery(const std::string& query, const FindOptions& opts) {
    Range scope;
    const Range* sc = nullptr;
    if (opts.inSelection) {                                   // the scope is fixed the first time it is needed
        if (m_find.hasScope()) { scope = m_find.scope(); sc = &scope; }
        else if (m_hasCandidate) { scope = m_candidate; sc = &scope; }
    }
    m_find.setQuery(m_buffer, query, opts, sc);
    if (m_find.selectFrom(mainCursor().start())) selectMatch(*m_find.currentMatch());
}

void TextView::findSetQuery(const std::string& query, bool caseSensitive) {
    FindOptions o;
    o.matchCase = caseSensitive;
    findSetQuery(query, o);
}

bool TextView::findNext() {
    if (!m_find.active() || m_find.matches().empty()) return false;
    Position s = mainCursor().start(), e = mainCursor().end();
    bool onMatch = false;
    for (const Range& r : m_find.matches())
        if (r.from == s && r.to == e) { onMatch = true; break; }
    bool ok = onMatch ? m_find.selectAfter(s) : m_find.selectFrom(e);
    if (!ok) return false;
    selectMatch(*m_find.currentMatch());
    return true;
}

bool TextView::findPrev() {
    if (!m_find.active() || !m_find.selectBefore(mainCursor().start())) return false;
    selectMatch(*m_find.currentMatch());
    return true;
}

bool TextView::replaceCurrent(const std::string& replacement) {
    if (!m_find.active()) return false;

    Position s = mainCursor().start(), e = mainCursor().end();
    const std::vector<Range>& ms = m_find.matches();
    size_t idx = ms.size();
    for (size_t i = 0; i < ms.size(); ++i)
        if (ms[i].from == s && ms[i].to == e) { idx = i; break; }
    if (idx == ms.size()) { findNext(); return false; }      // first press only moves to the next match

    std::vector<std::string> texts = {m_find.replacementFor(idx, withoutCR(replacement))};
    bool scoped = m_find.hasScope();
    Range scope = m_find.scope();
    size_t scopeTo = scoped ? m_buffer.offsetOf(scope.to) : 0;
    long long delta = static_cast<long long>(texts[0].size()) -
                      static_cast<long long>(m_buffer.offsetOf(e) - m_buffer.offsetOf(s));

    select(s, e);                                            // replace just this match
    insertAtCursors({texts[0]}, EditKind::Other);
    if (scoped) {                                            // the scope grows or shrinks with the edit
        scope.to = m_buffer.positionOf(static_cast<size_t>(static_cast<long long>(scopeTo) + delta));
        m_find.setScope(&scope);
        m_find.refresh(m_buffer);
    }
    findNext();
    return true;
}

size_t TextView::replaceAll(const std::string& replacement) {
    if (!m_find.active() || m_find.matches().empty()) return 0;
    if (!m_find.error().empty()) return 0;   // e.g. "too many matches": the list is not complete

    const std::vector<Range> matches = m_find.matches();     // a copy: the list changes while editing
    std::vector<std::string> texts = m_find.replacementsForAll(withoutCR(replacement));
    bool scoped = m_find.hasScope();
    Range scope = m_find.scope();
    size_t scopeTo = scoped ? m_buffer.offsetOf(scope.to) : 0;

    std::vector<size_t> starts;                              // line start offsets, so offsets are cheap
    size_t off = 0;
    for (size_t l = 0; l < m_buffer.lineCount(); ++l) { starts.push_back(off); off += m_buffer.lineLength(l) + 1; }

    long long delta = 0;
    std::vector<EditRange> edits;
    edits.reserve(matches.size());
    for (size_t i = 0; i < matches.size(); ++i) {
        size_t a = starts[matches[i].from.line] + matches[i].from.col;
        size_t b = starts[matches[i].to.line] + matches[i].to.col;
        edits.push_back({a, b, texts[i], {}, false});
        delta += static_cast<long long>(texts[i].size()) - static_cast<long long>(b - a);
    }
    size_t count = edits.size();

    std::vector<size_t> ends = applyEdits(edits, EditKind::Other);   // one undo step
    Position p = m_buffer.positionOf(ends.back());                    // one cursor after the last replacement
    Cursor c;
    c.pos = c.anchor = p;
    c.goalCol = p.col;
    m_cursors.assign(1, c);
    m_main = 0;
    syncTypingStyle();
    revealCursors();

    if (scoped) {
        scope.to = m_buffer.positionOf(static_cast<size_t>(static_cast<long long>(scopeTo) + delta));
        m_find.setScope(&scope);
    }
    m_find.refresh(m_buffer);
    return count;
}

bool TextView::selectAllMatches() {
    if (!m_find.active() || m_find.matches().empty()) return false;
    std::vector<Cursor> cs;
    for (const Range& r : m_find.matches()) {
        Cursor c;
        c.anchor = r.from;
        c.pos = r.to;
        c.goalCol = r.to.col;
        cs.push_back(c);
    }
    size_t main = m_find.current() >= 0 ? static_cast<size_t>(m_find.current()) : 0;
    m_cursors = std::move(cs);
    m_main = main;
    finishMove();
    return true;
}

bool TextView::selectAllOccurrences() {
    Position mp = mainCursor().pos;
    std::string needle;
    bool word = false;
    if (mainCursor().hasSelection()) {
        needle = m_buffer.textInRange(mainCursor().start(), mainCursor().end());
    } else {                                                 // no selection: the word under the cursor
        const std::string& line = m_buffer.line(mp.line);
        size_t a = mp.col, b = mp.col;
        while (a > 0 && isWordChar(line[a - 1])) --a;
        while (b < line.size() && isWordChar(line[b])) ++b;
        if (a == b) return false;
        needle = line.substr(a, b - a);
        word = true;
    }
    FindOptions o;
    o.matchCase = true;
    o.wholeWord = word;
    std::vector<Range> all = FindReplace::findAll(m_buffer, needle, o);
    if (all.empty()) return false;

    std::vector<Cursor> cs;
    size_t main = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        Cursor c;
        c.anchor = all[i].from;
        c.pos = all[i].to;
        c.goalCol = c.pos.col;
        if (!(mp < all[i].from) && !(all[i].to < mp)) main = i;
        cs.push_back(c);
    }
    m_cursors = std::move(cs);
    m_main = main;
    finishMove();
    return true;
}

void TextView::clearFind() { m_find.clear(); }
bool TextView::findActive() const { return m_find.active(); }
const std::vector<Range>& TextView::findMatches() const { return m_find.matches(); }
size_t TextView::findMatchCount() const { return m_find.matches().size(); }
int TextView::findCurrent() const { return m_find.current(); }
const std::string& TextView::findError() const { return m_find.error(); }
bool TextView::findScoped() const { return m_find.hasScope(); }

// ---------------------------------------------------------------- line operations

void TextView::duplicateLines(bool down) {
    std::vector<LineBlock> blocks = lineBlocksOf(m_cursors, m_folding);

    std::vector<EditRange> edits;
    for (const LineBlock& bl : blocks) {
        Position s{bl.a, 0};
        Position e{bl.b, m_buffer.lineLength(bl.b)};
        std::string t = m_buffer.textInRange(s, e);
        size_t at = down ? m_buffer.offsetOf(e) : m_buffer.offsetOf(s);
        edits.push_back({at, at, down ? "\n" + t : t + "\n", m_style.slice(s, e), true});
    }

    // the cursors follow their lines (downwards onto the copy when duplicating down)
    auto mapLine = [&](size_t x) {
        size_t n = x + linesAbove(blocks, x);
        const LineBlock* bl = blockAt(blocks, x);
        if (down && bl) n += bl->b - bl->a + 1;
        return n;
    };
    std::vector<Cursor> moved = m_cursors;
    for (Cursor& c : moved) {
        c.pos.line = mapLine(c.pos.line);
        c.anchor.line = mapLine(c.anchor.line);
    }

    applyEdits(edits, EditKind::Other);
    setCursors(moved);
}

void TextView::deleteLines() {
    std::vector<LineBlock> blocks = lineBlocksOf(m_cursors, m_folding);

    std::vector<EditRange> edits;
    for (const LineBlock& bl : blocks) {
        size_t from, to;
        if (bl.b + 1 < m_buffer.lineCount()) {       // take the newline after the block with it
            from = m_buffer.offsetOf({bl.a, 0});
            to = m_buffer.offsetOf({bl.b + 1, 0});
        } else if (bl.a > 0) {                       // last lines: take the newline before the block
            from = m_buffer.offsetOf({bl.a - 1, m_buffer.lineLength(bl.a - 1)});
            to = m_buffer.offsetOf({bl.b, m_buffer.lineLength(bl.b)});
        } else {                                     // everything
            from = 0;
            to = m_buffer.length();
        }
        edits.push_back({from, to, "", {}, false});
    }

    std::vector<Cursor> moved = m_cursors;
    for (Cursor& c : moved) {
        size_t x = c.pos.line;
        const LineBlock* bl = blockAt(blocks, x);
        size_t above = linesAbove(blocks, x);
        c.pos.line = bl ? bl->a - above : x - above;
        c.anchor = c.pos;
    }

    applyEdits(edits, EditKind::Other);
    setCursors(moved);
}

void TextView::moveLines(bool up) {
    std::vector<LineBlock> blocks = lineBlocksOf(m_cursors, m_folding);

    struct Swap { size_t a, b, oa, ob; };    // a..b = the block, oa..ob = the lines it swaps places with
    std::vector<EditRange> edits;
    std::vector<Swap> swaps;
    for (const LineBlock& bl : blocks) {
        size_t oa, ob;
        if (up) {
            if (bl.a == 0) continue;
            ob = bl.a - 1;
            oa = m_folding.headerOf(ob);     // a folded block above jumps as one piece
        } else {
            if (bl.b + 1 >= m_buffer.lineCount()) continue;
            oa = bl.b + 1;
            ob = m_folding.foldEndOf(oa);
        }
        Position bs{bl.a, 0}, be{bl.b, m_buffer.lineLength(bl.b)};
        Position os{oa, 0}, oe{ob, m_buffer.lineLength(ob)};
        Position from = up ? os : bs;
        Position to = up ? be : oe;

        std::string blockText = m_buffer.textInRange(bs, be);
        std::string otherText = m_buffer.textInRange(os, oe);
        std::vector<CharStyle> first = m_style.slice(up ? bs : os, up ? be : oe);
        std::vector<CharStyle> second = m_style.slice(up ? os : bs, up ? oe : be);
        first.insert(first.end(), second.begin(), second.end());

        std::string text = up ? blockText + "\n" + otherText : otherText + "\n" + blockText;
        edits.push_back({m_buffer.offsetOf(from), m_buffer.offsetOf(to), text, first, true});
        swaps.push_back({bl.a, bl.b, oa, ob});
    }
    if (edits.empty()) return;

    auto mapLine = [&](size_t x) {
        for (const Swap& w : swaps) {
            size_t h = w.b - w.a + 1, oh = w.ob - w.oa + 1;
            if (x >= w.a && x <= w.b) return up ? x - oh : x + oh;    // a line of the block
            if (x >= w.oa && x <= w.ob) return up ? x + h : x - h;    // a line of the neighbour
        }
        return x;
    };
    std::vector<Cursor> moved = m_cursors;
    for (Cursor& c : moved) {
        c.pos.line = mapLine(c.pos.line);
        c.anchor.line = mapLine(c.anchor.line);
    }

    std::vector<size_t> foldedBefore = m_folding.foldedLines();
    m_folding.unfoldAll();                   // line numbers change; the folds are put back at their new lines

    applyEdits(edits, EditKind::Other);
    setCursors(moved);
    for (size_t x : foldedBefore) m_folding.fold(mapLine(x));
}

// ---------------------------------------------------------------- formatting

void TextView::toggleBold() {
    if (anySelection()) {
        changeStyles([&](Position a, Position b) { m_style.toggleBold(a, b); });
    } else {
        m_typingStyle.bold = !m_typingStyle.bold;
    }
}

void TextView::toggleItalic() {
    if (anySelection()) {
        changeStyles([&](Position a, Position b) { m_style.toggleItalic(a, b); });
    } else {
        m_typingStyle.italic = !m_typingStyle.italic;
    }
}

void TextView::toggleUnderline() {
    if (anySelection()) {
        changeStyles([&](Position a, Position b) { m_style.toggleUnderline(a, b); });
    } else {
        m_typingStyle.underline = !m_typingStyle.underline;
    }
}

void TextView::changeFontSize(int delta) {
    if (anySelection()) {
        changeStyles([&](Position a, Position b) { m_style.changeFontSize(a, b, delta); });
    } else {
        m_typingStyle.fontSize = std::clamp(m_typingStyle.fontSize + delta, kMinFontSize, kMaxFontSize);
    }
}

// ---------------------------------------------------------------- change detection

unsigned long long TextView::stateRevision() const {
    unsigned long long h = 1469598103934665603ULL;            // FNV-1a over the numbers that make up the visible state
    auto mix = [&h](unsigned long long v) {
        for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 1099511628211ULL; }
    };
    mix(m_buffer.revision());
    mix(m_styleStamp);
    mix(m_main);
    mix(m_cursors.size());
    for (const Cursor& c : m_cursors) { mix(c.pos.line); mix(c.pos.col); mix(c.anchor.line); mix(c.anchor.col); }
    mix(static_cast<unsigned>(m_typingStyle.bold) | static_cast<unsigned>(m_typingStyle.italic) << 1 |
        static_cast<unsigned>(m_typingStyle.underline) << 2);
    mix(static_cast<unsigned long long>(m_typingStyle.fontSize));
    mix(m_indent.useTabs);
    mix(m_indent.width);
    for (const FoldRegion& r : m_folding.regions()) { mix(r.start); mix(r.hiddenTo); }
    mix(m_folding.foldedCount());
    for (size_t line : m_folding.foldedLines()) mix(line);
    mix(m_find.active());
    mix(std::hash<std::string>()(m_find.query()));
    mix(static_cast<unsigned>(m_find.options().matchCase) | static_cast<unsigned>(m_find.options().wholeWord) << 1 |
        static_cast<unsigned>(m_find.options().regex) << 2);
    mix(m_find.matches().size());
    mix(static_cast<unsigned long long>(static_cast<long long>(m_find.current())));
    mix(std::hash<std::string>()(m_find.error()));
    mix(m_find.hasScope());
    if (m_find.hasScope()) { mix(m_find.scope().from.line); mix(m_find.scope().from.col); mix(m_find.scope().to.line); mix(m_find.scope().to.col); }
    return h;
}

// ---------------------------------------------------------------- loading a file

void TextView::loadText(const std::string& s) {
    m_buffer = TextBuffer();
    m_folding.reset();
    m_style = TextStyle();
    m_typingStyle = CharStyle();
    m_cursors.assign(1, Cursor{});
    m_main = 0;
    m_find.clear();
    m_hasCandidate = false;
    m_history = UndoHistory();
    m_lastKind = EditKind::None;

    std::string clean = withoutCR(s);
    if (!clean.empty()) {
        std::vector<EditRange> edits;
        edits.push_back({0, 0, clean, {}, false});
        applyEdits(edits, EditKind::Other);
    }

    m_history = UndoHistory();           // loading a file is not an undo step
    m_lastKind = EditKind::None;
    m_cursors.assign(1, Cursor{});       // cursor at the top
    m_main = 0;
    syncTypingStyle();
    m_folding.update(m_buffer);
}

// ---------------------------------------------------------------- word delete and indentation

void TextView::deleteWord(bool forward) {
    std::vector<EditRange> edits;
    bool any = false;
    size_t limit = 0;                                    // end of the previous edit (edits must not overlap)
    for (const Cursor& c : m_cursors) {
        size_t a = m_buffer.offsetOf(c.start());
        size_t b = m_buffer.offsetOf(c.end());
        if (!c.hasSelection()) {
            if (forward) b = m_buffer.offsetOf(wordRightOf(m_buffer, c.pos));
            else a = m_buffer.offsetOf(wordLeftOf(m_buffer, c.pos));
        }
        if (a < limit) a = limit;
        if (b < a) b = a;
        if (a != b) any = true;
        limit = b;
        edits.push_back({a, b, "", {}, false});
    }
    if (!any) return;
    setCaretsAt(applyEdits(edits, EditKind::Other));
}

void TextView::tab() {
    if (reindentListItems(true)) return;
    for (const Cursor& c : m_cursors)
        if (c.hasSelection() && c.start().line != c.end().line) { changeIndent(true); return; }
    insertText(m_indent.unit());
}

void TextView::setIndent(const IndentStyle& s) {
    m_indent = s;
    if (m_indent.width < 1) m_indent.width = 1;
    if (m_indent.width > 16) m_indent.width = 16;
}

void TextView::shiftTab() {
    if (reindentListItems(false)) return;
    changeIndent(false);
}

// Tab / Shift+Tab with every cursor at the end of an empty list item ("2. "): the item moves one level
// deeper or back out, and gets the marker that fits that level. Returns false if that doesn't apply.
bool TextView::reindentListItems(bool deeper) {
    std::vector<EditRange> edits;
    for (const Cursor& c : m_cursors) {
        if (c.hasSelection()) return false;
        const std::string& line = m_buffer.line(c.pos.line);
        ListMarker lm;
        if (!listMarkerOf(line, lm) || line.size() != lm.end || c.pos.col != lm.end) return false;
        if (!deeper && lm.indentLen == 0) return false;
        bool found = false;
        std::string text = reindentedItem(m_buffer, c.pos.line, lm, deeper, m_indent, found);
        edits.push_back({m_buffer.offsetOf({c.pos.line, 0}), m_buffer.offsetOf({c.pos.line, line.size()}), text, {}, false});
    }
    if (edits.empty()) return false;
    setCaretsAt(applyEdits(edits, EditKind::Other));
    return true;
}

void TextView::changeIndent(bool indent) {
    std::vector<LineBlock> blocks = lineBlocksOf(m_cursors, m_folding);
    size_t count = m_buffer.lineCount();
    std::vector<size_t> amount(count, 0);                // columns added (indent) or removed (outdent) per line
    const std::string unit = m_indent.unit();
    std::vector<EditRange> edits;
    for (const LineBlock& bl : blocks) {
        for (size_t ln = bl.a; ln <= bl.b && ln < count; ++ln) {
            const std::string& line = m_buffer.line(ln);
            size_t at = m_buffer.offsetOf({ln, 0});
            if (indent) {
                if (line.empty()) continue;              // empty lines stay empty
                amount[ln] = unit.size();
                edits.push_back({at, at, unit, {}, false});
            } else {
                size_t r = 0;
                if (!line.empty() && line[0] == '\t') r = 1;
                else while (r < m_indent.width && r < line.size() && line[r] == ' ') ++r;
                if (r == 0) continue;
                amount[ln] = r;
                edits.push_back({at, at + r, "", {}, false});
            }
        }
    }
    if (edits.empty()) return;

    auto shifted = [&](Position p) {
        if (p.line < count && amount[p.line] != 0) {
            if (indent) { if (p.col > 0) p.col += amount[p.line]; }
            else p.col = p.col >= amount[p.line] ? p.col - amount[p.line] : 0;
        }
        return p;
    };
    std::vector<Cursor> moved = m_cursors;
    for (Cursor& c : moved) {
        c.pos = shifted(c.pos);
        c.anchor = shifted(c.anchor);
    }

    applyEdits(edits, EditKind::Other);
    setCursors(moved);
}

// -------------------- bracket matching

std::vector<BracketPair> TextView::bracketMatches() const {
    std::vector<BracketPair> result;
    for (const Cursor& c : m_cursors) {
        if (c.hasSelection()) continue;
        const std::string& line = m_buffer.line(c.pos.line);
        for (int side = 0; side < 2; ++side) {                    // the character before the cursor, then the one after
            if (side == 0 && c.pos.col == 0) continue;
            Position q{c.pos.line, side == 0 ? c.pos.col - 1 : c.pos.col};
            if (q.col >= line.size()) continue;
            Position partner{};
            if (findPartner(m_buffer, q, partner)) { result.push_back({q, partner}); break; }
        }
    }
    return result;
}

// ---------------------------------------------------------------- folding

namespace {
bool posBefore(const Position& a, const Position& b) {
    return a.line < b.line || (a.line == b.line && a.col < b.col);
}
}

// True when the selection's end is the end of the last hidden line of a folded block.
bool TextView::selectionEndsAtFoldEnd(const Cursor& c) const {
    if (!c.hasSelection() || !posBefore(c.anchor, c.pos)) return false;   // pos must be the end
    size_t l = c.pos.line;
    return m_folding.isHidden(l) && l == m_folding.foldEndOf(l) && c.pos.col == m_buffer.lineLength(l);
}

// A selection that reaches the end of a folded header line takes the hidden lines with it.
void TextView::snapSelectionsToFolds() {
    if (m_suppressFoldSnap) return;
    for (Cursor& c : m_cursors) {
        if (!c.hasSelection()) continue;
        bool posIsEnd = posBefore(c.anchor, c.pos);
        Position e = posIsEnd ? c.pos : c.anchor;
        if (e.line >= m_buffer.lineCount()) continue;
        if (!m_folding.isFolded(e.line) || e.col < m_buffer.lineLength(e.line)) continue;
        size_t last = m_folding.foldEndOf(e.line);
        if (last == e.line) continue;
        Position ne{last, m_buffer.lineLength(last)};
        if (posIsEnd) { c.pos = ne; c.goalCol = ne.col; }
        else c.anchor = ne;
    }
}

bool TextView::isFoldSelected(size_t line) const {
    if (!m_folding.isFolded(line)) return false;
    size_t last = m_folding.foldEndOf(line);
    size_t len = m_buffer.lineLength(last);
    for (const Cursor& c : m_cursors) {
        if (!c.hasSelection()) continue;
        Position s = c.start(), e = c.end();
        if (s.line <= line && (e.line > last || (e.line == last && e.col >= len))) return true;
    }
    return false;
}

void TextView::revealCursors() {
    snapSelectionsToFolds();
    for (const Cursor& c : m_cursors) {
        // a selection that covers a whole fold may end inside it without opening it
        if (c.hasSelection() && m_folding.isHidden(c.pos.line)) {
            Position e = c.end();
            size_t h = m_folding.headerOf(e.line);
            size_t last = m_folding.foldEndOf(e.line);
            if (posBefore(c.pos, c.anchor) == false &&      // pos is the end of the selection
                c.start().line <= h && e.line == last && e.col >= m_buffer.lineLength(last))
                continue;
        }
        m_folding.reveal(c.pos.line);
    }
}

// After a fold: cursors that ended up inside it move to the end of the header line.
void TextView::moveCursorsOutOfFolds() {
    for (Cursor& c : m_cursors) {
        bool noSelection = !c.hasSelection();
        if (m_folding.isHidden(c.pos.line)) {
            size_t h = m_folding.headerOf(c.pos.line);
            c.pos = Position{h, m_buffer.lineLength(h)};
        }
        if (noSelection) {
            c.anchor = c.pos;
        } else if (m_folding.isHidden(c.anchor.line)) {
            size_t h = m_folding.headerOf(c.anchor.line);
            c.anchor = Position{h, m_buffer.lineLength(h)};
        }
        c.goalCol = c.pos.col;
    }
}

bool TextView::toggleFoldAt(size_t line) {
    if (!m_folding.canFold(line)) return false;
    if (m_folding.isFolded(line)) {
        m_folding.unfold(line);
    } else {
        m_folding.fold(line);
        moveCursorsOutOfFolds();
        finishMove();
    }
    return true;
}

bool TextView::foldAtCursor() {
    bool any = false;
    for (const Cursor& c : m_cursors)
        if (!m_folding.isHidden(c.pos.line) && m_folding.foldAt(c.pos.line)) any = true;
    if (any) {
        moveCursorsOutOfFolds();
        finishMove();
    }
    return any;
}

bool TextView::unfoldAtCursor() {
    bool any = false;
    for (const Cursor& c : m_cursors)
        if (m_folding.unfoldAt(c.pos.line)) any = true;
    return any;
}

void TextView::foldAll() {
    m_folding.foldAll();
    moveCursorsOutOfFolds();
    finishMove();
}

void TextView::unfoldAll() { m_folding.unfoldAll(); }

void TextView::setFoldLanguage(const std::string& fileName) {
    m_folding.setLanguageFor(fileName);
    m_folding.update(m_buffer);
    revealCursors();
}

void TextView::setFoldMode(Folding::Mode m) {
    m_folding.setMode(m);
    m_folding.update(m_buffer);
}

void TextView::setFoldSyntax(const Folding::Syntax& s) {
    m_folding.setSyntax(s);
    m_folding.update(m_buffer);
}