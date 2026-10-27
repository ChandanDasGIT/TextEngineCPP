#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "../textbuffer/textbuffer.h"

// Tracks which lines were edited, for the line-number gutter and the minimap:
//   Unsaved   = changed since the last save (the front end shows it red)
//   Saved     = changed since the file was opened, and saved since (green)
//   Untouched = same as when the file was opened (gray)
// This class only works out the state. Colours and drawing are up to the front end.
class GutterStyle {
public:
    enum class ChangeMark : unsigned char { Untouched, Unsaved, Saved };

    void reset(const TextBuffer& b);      // call when a file is opened: nothing is changed
    void markSaved(const TextBuffer& b);  // call after saving: red lines become green
    void update(const TextBuffer& b);     // call after the text changed (once per redraw is fine)

    ChangeMark markAt(size_t line) const { return line < m_marks.size() ? m_marks[line] : ChangeMark::Untouched; }
    bool unsavedAt(size_t line) const { return markAt(line) == ChangeMark::Unsaved; }
    bool savedChangeAt(size_t line) const { return markAt(line) == ChangeMark::Saved; }

private:
    std::vector<std::string> m_original{""};   // text when the file was opened
    std::vector<std::string> m_saved{""};      // text at the last save
    std::vector<ChangeMark> m_marks;           // one per current line
    unsigned long long m_rev = 0;              // revision of the text the marks were worked out for (0 = none)
};
