#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "../textbuffer/textbuffer.h"
#include "../folding/folding.h"

// Colours the line-number gutter:
//   number: white on lines that have a cursor, gray otherwise
//   bar:    red = unsaved change, green = changed and saved, gray = untouched
class GutterStyle {
public:
    void reset(const TextBuffer& b);      // call when a file is opened: nothing is changed
    void markSaved(const TextBuffer& b);  // call after saving: red lines become green
    void update(const TextBuffer& b);     // call once per redraw, before draw()

    // The gutter text for one screen row, including colour codes.
    std::string draw(size_t line, bool continuation, bool hasCursor, size_t lineCount, FoldMark fold) const;

    // For the minimap: the same red / green the gutter bar shows.
    bool unsavedAt(size_t line) const { return line < m_marks.size() && m_marks[line] == Mark::Unsaved; }
    bool savedChangeAt(size_t line) const { return line < m_marks.size() && m_marks[line] == Mark::Saved; }
private:
    enum class Mark : unsigned char { Untouched, Unsaved, Saved };

    std::vector<std::string> m_original{""};   // text when the file was opened
    std::vector<std::string> m_saved{""};      // text at the last save
    std::vector<Mark> m_marks;                 // one per current line
};
