#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include "../textbuffer/textbuffer.h"
#include "../textstyle/textstyle.h"
#include "../multicursor/multicursor.h"
#include "../undohistory/undohistory.h"
#include "../findreplace/findreplace.h"
#include "../textstats/textstats.h"
#include "../folding/folding.h"

// A bracket next to a cursor and the bracket it pairs with.
struct BracketPair {
    Position at;        // the bracket next to the cursor
    Position partner;   // its matching bracket
};

// How one level of indentation is written: a tab character, or `width` spaces.
// Used by Tab, Shift+Tab, indenting selected lines and moving list items deeper.
struct IndentStyle {
    bool useTabs = false;
    size_t width = 4;                      // spaces per level (also what a tab character counts as when outdenting)
    std::string unit() const { return useTabs ? std::string("\t") : std::string(width, ' '); }
};

// Owns a TextBuffer plus the cursors, selections, formatting, undo history and find state.
// This is the class the UI talks to.
class TextView {
public:
    TextView();

    // ---- editing (applied at every cursor) ----
    void insertText(const std::string& s);
    void backspace();
    void del();                     // Delete key
    void enter();
        void tab();                     // Tab: indent selected lines, or insert 4 spaces
    void shiftTab();                // Shift+Tab: outdent
    void deleteWord(bool forward);  // Ctrl+Delete (true) / Ctrl+Backspace (false)

    // ---- cursor / selection ----
    void moveCursor(Position p, bool extend = false);  // click = false (single cursor), drag = true
    void moveLeft(bool extend = false);
    void moveRight(bool extend = false);
    void moveUp(bool extend = false);
    void moveDown(bool extend = false);
    void moveHome(bool extend = false);
    void moveEnd(bool extend = false);
    void moveWordLeft(bool extend = false);
    void moveWordRight(bool extend = false);
    void select(Position from, Position to);
    void selectAll();

    // ---- multiple cursors ----
    void addCursor(Position p);          // Alt+click
    void addCursorAbove();
    void addCursorBelow();
    bool addNextOccurrence();            // select the word, then add a cursor at its next occurrence
    void clearExtraCursors();            // keep only the main cursor
    size_t cursorCount() const;
    const std::vector<Cursor>& cursors() const;
    size_t mainCursorIndex() const;

    // ---- clipboard (the caller stores the string) ----
    std::string copy() const;            // selections of all cursors, joined with '\n'
    std::string cut();
    void paste(const std::string& s);    // one line per cursor if the line count matches

    // ---- undo / redo ----
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    // ---- find and replace ----
    void findOpen();                                      // call when the find panel opens: remembers the selection for "in selection"
    void findSetQuery(const std::string& query, const FindOptions& opts);   // search and select the first match
    void findSetQuery(const std::string& query, bool caseSensitive);
    bool findNext();
    bool findPrev();
    bool replaceCurrent(const std::string& replacement);  // true if something was replaced
    size_t replaceAll(const std::string& replacement);    // returns how many were replaced
    bool selectAllMatches();                              // a cursor with a selection on every match
    bool selectAllOccurrences();                          // a cursor on every occurrence of the selection / the word at the cursor
    void clearFind();
    bool findActive() const;
    const std::vector<Range>& findMatches() const;
    size_t findMatchCount() const;
    int findCurrent() const;                              // index of the current match, or -1
    const std::string& findError() const;                 // "invalid regex" etc., or empty
    bool findScoped() const;                              // searching only inside a selection

    // ---- line operations (on the lines touched by each cursor) ----
    void duplicateLines(bool down);      // copy below (down) or above
    void deleteLines();
    void moveLines(bool up);

    // ---- formatting: applies to the selections, or to what you type next if nothing is selected ----
    void toggleBold();
    void toggleItalic();
    void toggleUnderline();
    void changeFontSize(int delta);
    CharStyle styleAt(size_t line, size_t col) const;
    CharStyle typingStyle() const;

    // ---- statistics ----
    TextStats stats() const;             // whole text
    TextStats selectionStats() const;    // all selected text

    // ---- queries (about the main cursor unless stated) ----
    bool hasSelection() const;
    bool anySelection() const;           // true if any cursor has a selection
    Position selectionStart() const;
    Position selectionEnd() const;
    std::string selectedText() const;
    Position cursor() const;
    std::string text() const;
    const TextBuffer& buffer() const;
    void loadText(const std::string& s);   // replaces everything (used when a file is opened)

    // ---- change detection ----
    // A number that differs whenever anything a screen draws has changed: the text, cursors and selections, formatting,
    // folds, find matches, indentation. Remember the last value you drew and redraw (or emit "changed") only when it is
    // different. Cheap enough to call on every frame. It is only for comparing: do not rely on its size or order.
    unsigned long long stateRevision() const;

    // ---- indentation ----
    void setIndent(const IndentStyle& s);              // width is kept between 1 and 16
    const IndentStyle& indent() const { return m_indent; }

    // ---- brackets ----
    std::vector<BracketPair> bracketMatches() const;   // for every cursor that sits next to ( ) [ ] { }
    void setAutoClose(bool on) { m_autoClose = on; }   // auto-close brackets and quotes (default on)
    bool autoClose() const { return m_autoClose; }

    // ---- folding ----
    const Folding& folding() const { return m_folding; }
    bool toggleFoldAt(size_t line);                 // click on the marker of a header line
    bool foldAtCursor();                            // fold the innermost open region around each cursor
    bool unfoldAtCursor();
    void foldAll();
    void unfoldAll();
    void setFoldLanguage(const std::string& fileName);   // by extension: ".py" folds by indentation, others by brackets
    void setFoldMode(Folding::Mode m);
    void setFoldSyntax(const Folding::Syntax& s);
    bool isFoldSelected(size_t line) const;         // a folded header whose whole block is selected

private:
    enum class EditKind { None, Typing, Backspace, Other };

    // Replace the text between two character offsets (offsets are in the text as it is before the edit).
    struct EditRange {
        size_t from;
        size_t to;
        std::string text;
        std::vector<CharStyle> styles;   // used when `styled` is true
        bool styled;
    };

    Cursor& mainCursor();
    const Cursor& mainCursor() const;

    void moveAll(bool extend, bool keepGoal, const std::function<Position(const Cursor&)>& target);
    size_t goalVisual(const Cursor& c) const;   // the display column Up / Down try to keep
    void finishMove();
    void syncTypingStyle();
    void selectMatch(const Range& r);
    std::string joinedSelection() const;

    void pushUndo(EditKind kind);
    void applyChange(const Change& c, bool forward);          // forward = redo direction, false = undo direction
    void afterUndoRedo();                                      // updates folds, find and the typing style after a jump in history
    void changeStyles(const std::function<void(Position, Position)>& op);   // formatting over the selections, as one undo step

    // Applies edits (ascending, not overlapping). Returns the offset where each replacement ends.
    std::vector<size_t> applyEdits(const std::vector<EditRange>& edits, EditKind kind);
    void insertAtCursors(const std::vector<std::string>& texts, EditKind kind);
    void setCaretsAt(const std::vector<size_t>& offsets);
    void setCursors(std::vector<Cursor> cs);

    void addCursorVertical(int dir);
    void changeIndent(bool indent);
    bool reindentListItems(bool deeper);   // Tab / Shift+Tab on empty list items: true if it handled them    
    bool insertSmartChar(char ch);
    void revealCursors();              // unfold anything that hides a cursor
    void snapSelectionsToFolds();      // a selection ending on a folded header grows to the end of the fold
    bool selectionEndsAtFoldEnd(const Cursor& c) const;   // selection end sits at the end of a folded block
    bool m_suppressFoldSnap = false;                      // set during a shrinking move so the snap doesn't undo it
    void moveCursorsOutOfFolds();      // after folding: cursors inside the fold go to its header
    IndentStyle m_indent;
    unsigned long long m_styleStamp = 0;                  // goes up when formatting changes or undo/redo puts other formatting back
    bool m_autoClose = true;

    Folding m_folding;
    TextBuffer m_buffer;
    TextStyle m_style;
    CharStyle m_typingStyle;
    std::vector<Cursor> m_cursors;   // sorted, never empty, never overlapping
    size_t m_main = 0;               // index of the main cursor
    UndoHistory m_history;
    EditKind m_lastKind = EditKind::None;
    FindReplace m_find;
    bool m_hasCandidate = false;     // a selection existed when the find panel opened
    Range m_candidate;               // ... and this is it (the scope for "find in selection")
};
