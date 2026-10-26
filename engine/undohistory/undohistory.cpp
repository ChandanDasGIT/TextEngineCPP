#include "undohistory.h"
#include <utility>

UndoHistory::UndoHistory(size_t limit) : m_limit(limit) {}

void UndoHistory::record(Snapshot before) {
    m_undo.push_back(std::move(before));
    if (m_undo.size() > m_limit) m_undo.pop_front();
    m_redo.clear();
}

bool UndoHistory::canUndo() const { return !m_undo.empty(); }
bool UndoHistory::canRedo() const { return !m_redo.empty(); }

bool UndoHistory::undo(Snapshot& current) {
    if (m_undo.empty()) return false;
    m_redo.push_back(std::move(current));
    current = std::move(m_undo.back());
    m_undo.pop_back();
    return true;
}

bool UndoHistory::redo(Snapshot& current) {
    if (m_redo.empty()) return false;
    m_undo.push_back(std::move(current));
    current = std::move(m_redo.back());
    m_redo.pop_back();
    return true;
}

void UndoHistory::clear() {
    m_undo.clear();
    m_redo.clear();
}
