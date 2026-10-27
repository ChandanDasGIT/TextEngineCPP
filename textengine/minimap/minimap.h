#pragma once
#include <cstddef>
#include <functional>
#include <vector>
#include "../textbuffer/textbuffer.h"
#include "../multicursor/multicursor.h"
#include "../findreplace/findreplace.h"

// The minimap is a small picture of the whole document: one "pixel" per few characters.
// This class only computes the picture and the scroll maths. It draws nothing, so a terminal
// (braille characters, kitty image) and a QML item can both use it.

struct MinimapConfig {
    size_t width = 20;           // pixel columns
    size_t height = 40;          // most pixel rows the picture may use
    size_t charsPerPixel = 1;    // how many characters of a line one pixel column covers
    size_t tabWidth = 4;         // a tab counts as this many characters
};

struct MinimapCell {
    unsigned char density = 0;   // 0 = empty, 255 = every character under this pixel is not a space
    unsigned char kind = 0;      // syntax token of most characters under this pixel (0 = plain text)
};

// What is on a line. Drawn by the UI on top of the picture (for example as small coloured ticks).
enum MinimapMark : unsigned char {
    MarkCursor = 1,
    MarkSelection = 2,
    MarkFind = 4,
    MarkFindCurrent = 8,
    MarkChangedUnsaved = 16,
    MarkChangedSaved = 32,
    MarkError = 64,
    MarkWarning = 128,
};

class Minimap {
public:
    Minimap() = default;
    explicit Minimap(const MinimapConfig& c);

    void setConfig(const MinimapConfig& c);   // call rebuild() afterwards
    const MinimapConfig& config() const { return m_cfg; }

    // ---- the picture ----
    // Call when the text (or its syntax colours) changed.
    // kindAt(line, byteColumn) returns the syntax token of a character, as a number. It may be empty.
    // It does nothing if the text is the one the picture was last built for, so calling it every redraw is fine.
    // If only the syntax colours changed (a new language, say), call invalidate() first.
    void rebuild(const TextBuffer& b, const std::function<unsigned char(size_t, size_t)>& kindAt = {});
    void invalidate() { m_builtRev = 0; }

    size_t rows() const { return m_rows; }                // pixel rows in the picture (at most config().height)
    size_t width() const { return m_cfg.width; }
    size_t linesPerRow() const { return m_lpr; }          // 1 for short documents, more when it is squeezed to fit
    MinimapCell cell(size_t row, size_t col) const;

    // The picture as 0xFFRRGGBB pixels, row by row (rows() * width() values).
    // colorOf(kind) gives the colour (0xRRGGBB) of a token. Empty space becomes `background`.
    std::vector<unsigned> toArgb(const std::function<unsigned(unsigned char)>& colorOf, unsigned background) const;

    // ---- the visible part of the document (the rectangle drawn on the minimap) ----
    void setViewport(size_t firstVisibleLine, size_t visibleLines);
    size_t viewportTopRow() const;
    size_t viewportRows() const;                          // at least 1 once there is a picture
    bool rowInViewport(size_t row) const;

    // ---- mouse ----
    // Click on a row: the first visible line that puts that row in the middle of the screen.
    size_t scrollTopForRow(size_t row, size_t visibleLines) const;
    // Drag the rectangle: `grabOffsetRows` = (row where the mouse went down) - viewportTopRow(), kept while dragging.
    size_t scrollTopForDrag(size_t row, size_t grabOffsetRows, size_t visibleLines) const;

    // ---- line <-> row ----
    size_t rowForLine(size_t line) const;
    size_t firstLineOfRow(size_t row) const;

    // ---- marks (kept per line). Call rebuild() first, and refresh the marks after the text changes. ----
    void clearMarks(unsigned char bits);                  // remove these marks from every line
    void markLine(size_t line, unsigned char bits);
    void setCursorMarks(const std::vector<Cursor>& cursors);                  // MarkCursor and MarkSelection
    void setFindMarks(const std::vector<Range>& matches, int currentIndex);   // MarkFind and MarkFindCurrent
    // MarkChangedUnsaved and MarkChangedSaved: unsaved(line) / savedChange(line) say which lines were edited.
    void setChangeMarks(const std::function<bool(size_t)>& unsaved, const std::function<bool(size_t)>& savedChange);
    unsigned char marks(size_t row) const;                // marks of all lines in this row, combined

private:
    MinimapConfig m_cfg;
    size_t m_lineCount = 0;
    size_t m_rows = 0;
    size_t m_lpr = 1;
    std::vector<MinimapCell> m_cells;                     // m_rows * width
    std::vector<unsigned char> m_lineMarks;               // one per line
    unsigned long long m_builtRev = 0;                    // revision of the text the picture shows (0 = none)
    size_t m_vFirst = 0;
    size_t m_vVisible = 1;
};
