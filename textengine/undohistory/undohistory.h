#pragma once
#include <cstddef>
#include <deque>
#include <vector>
#include "../textbuffer/textbuffer.h"
#include "../textstyle/textstyle.h"
#include "../multicursor/multicursor.h"

// One change to the document. Undo and redo do not keep copies of the whole text: they keep only these small
// pieces, so a long editing session on a big file costs very little memory.
struct Change {
    bool styleOnly = false;                    // true: only the formatting of [at, to) changed, the text did not
    Position at;                               // where the change starts
    Position to;                               // styleOnly: where the formatted range ends
    std::string removed;                       // text: what was there before
    std::vector<CharStyle> removedStyles;      //       its formatting (styleOnly: the formatting before)
    std::string inserted;                      //       what took its place
    std::vector<CharStyle> insertedStyles;     //       its formatting (styleOnly: the formatting after)
};

// One undo step: the changes made by one user action (typing in a row counts as one action), in the order they
// were made, and the cursors before and after.
struct UndoStep {
    std::vector<Change> changes;
    std::vector<Cursor> before;
    size_t mainBefore = 0;
    std::vector<Cursor> after;                 // filled in when the step is undone, for redo
    size_t mainAfter = 0;
    size_t bytes = 0;                          // rough memory use, to keep the history from growing without end
};

// Undo and redo stacks of steps.
class UndoHistory {
public:
    explicit UndoHistory(size_t limit = 200, size_t maxBytes = 256u * 1024u * 1024u);

    void begin(std::vector<Cursor> cursors, size_t mainIndex);   // call BEFORE a change: starts a step; empties the redo stack
    void add(Change c);                                          // adds a change to the newest step

    bool canUndo() const;
    bool canRedo() const;
    UndoStep* stepToUndo();     // the newest step, or nullptr. Undo it, then call markUndone().
    void markUndone();          // moves that step to the redo stack
    UndoStep* stepToRedo();
    void markRedone();
    void clear();

private:
    void trim();

    size_t m_limit;
    size_t m_maxBytes;
    size_t m_bytes = 0;
    std::deque<UndoStep> m_undo;
    std::deque<UndoStep> m_redo;
};