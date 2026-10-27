#include "undohistory.h"
#include <utility>

UndoHistory::UndoHistory(size_t limit, size_t maxBytes) : m_limit(limit), m_maxBytes(maxBytes) {}

void UndoHistory::begin(std::vector<Cursor> cursors, size_t mainIndex) {
    UndoStep s;
    s.before = std::move(cursors);
    s.mainBefore = mainIndex;
    m_undo.push_back(std::move(s));
    m_redo.clear();
    trim();
}

void UndoHistory::add(Change c) {
    if (m_undo.empty()) m_undo.emplace_back();               // should not happen: begin() comes first
    size_t n = c.removed.size() + c.inserted.size() + (c.removedStyles.size() + c.insertedStyles.size()) * sizeof(CharStyle) + sizeof(Change);
    m_undo.back().bytes += n;
    m_bytes += n;
    m_undo.back().changes.push_back(std::move(c));
    trim();
}

void UndoHistory::trim() {
    while (m_undo.size() > 1 && (m_undo.size() > m_limit || m_bytes > m_maxBytes)) {   // the newest step always stays
        m_bytes -= m_undo.front().bytes;
        m_undo.pop_front();
    }
}

bool UndoHistory::canUndo() const { return !m_undo.empty(); }
bool UndoHistory::canRedo() const { return !m_redo.empty(); }

UndoStep* UndoHistory::stepToUndo() { return m_undo.empty() ? nullptr : &m_undo.back(); }
UndoStep* UndoHistory::stepToRedo() { return m_redo.empty() ? nullptr : &m_redo.back(); }

void UndoHistory::markUndone() {
    if (m_undo.empty()) return;
    m_bytes -= m_undo.back().bytes;
    m_redo.push_back(std::move(m_undo.back()));
    m_undo.pop_back();
}

void UndoHistory::markRedone() {
    if (m_redo.empty()) return;
    m_bytes += m_redo.back().bytes;
    m_undo.push_back(std::move(m_redo.back()));
    m_redo.pop_back();
    trim();
}

void UndoHistory::clear() {
    m_undo.clear();
    m_redo.clear();
    m_bytes = 0;
}