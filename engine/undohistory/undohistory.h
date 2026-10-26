#pragma once
#include <cstddef>
#include <deque>
#include <vector>
#include "../textbuffer/textbuffer.h"
#include "../textstyle/textstyle.h"
#include "../multicursor/multicursor.h"

// A full copy of the editor state at one moment.
struct Snapshot {
    TextBuffer buffer;
    TextStyle style;
    std::vector<Cursor> cursors;
    size_t mainIndex = 0;
};

// Undo and redo stacks of snapshots.
class UndoHistory {
public:
    explicit UndoHistory(size_t limit = 200);

    void record(Snapshot before);      // call BEFORE a change; also empties the redo stack
    bool canUndo() const;
    bool canRedo() const;
    bool undo(Snapshot& current);      // `current` goes to the redo stack and is replaced by the previous state
    bool redo(Snapshot& current);      // the reverse
    void clear();

private:
    size_t m_limit;
    std::deque<Snapshot> m_undo;
    std::deque<Snapshot> m_redo;
};
