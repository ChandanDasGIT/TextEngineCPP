#pragma once
#include <cstddef>
#include <utility>
#include <vector>
#include "../textbuffer/textbuffer.h"

// Sticky scrolling: when a function, class or block has scrolled off the top, its header line stays
// pinned at the top of the screen. This class only decides WHICH lines to pin. It draws nothing,
// so a terminal and a QML item can both use it.
//
// It needs the scopes of the document as a list of regions. The folding code already knows them, and
// language servers can give them later (document symbols).

// One scope: from the line that opens it to the line that closes it.
struct ScopeRegion {
    size_t start = 0;    // first line of the scope
    size_t end = 0;      // last line of the scope (included)
    size_t header = 0;   // the line to show when it is pinned (usually the same as start)

    ScopeRegion() = default;
    ScopeRegion(size_t s, size_t e) : start(s), end(e), header(s) {}
    ScopeRegion(size_t s, size_t e, size_t h) : start(s), end(e), header(h) {}
};

// One pinned line.
struct StickyLine {
    size_t line = 0;         // the document line to draw
    size_t scopeStart = 0;
    size_t scopeEnd = 0;
    size_t depth = 0;        // 0 = outermost scope; it is also the row it is drawn in (row 0 is the top)
    double pushUp = 0.0;     // 0..1: how far it has slid up because its scope is ending (for smooth scrolling)
};

class StickyScroll {
public:
    // Call when the document (and so its regions) changed. Regions that are broken (end before start),
    // that overlap their neighbours badly, or open on the same line as another scope are left out.
    void setRegions(std::vector<ScopeRegion> regions);
    std::vector<ScopeRegion> regions() const;          // the regions that are used, sorted

    // Takes the regions straight from the folding code (any list whose items have .start and .hiddenTo,
    // such as Folding::regions()). A scope that opens with a line holding only "{" shows the line above it.
    template <class FoldRegions>
    void setRegionsFromFolds(const FoldRegions& folds, const TextBuffer& b) {
        std::vector<ScopeRegion> list;
        list.reserve(folds.size());
        for (const auto& f : folds)
            list.push_back(ScopeRegion(f.start, f.hiddenTo, headerLineFor(b, f.start)));
        setRegions(std::move(list));
    }

    void setMaxLines(size_t n) { m_max = n; }          // at most this many pinned lines (default 5)
    size_t maxLines() const { return m_max; }

    // The lines to pin when `firstLine` is the first document line of the screen.
    // `fraction` (0..1) is how far that line has scrolled up in smooth scrolling; the terminal passes 0.
    // The pinned lines cover the first rows of the screen: row i is covered by result[i].
    std::vector<StickyLine> compute(size_t firstLine, double fraction = 0.0) const;

    // The pinned lines hide the first rows of the text. If `line` (for example the cursor line) would be
    // hidden, this gives the first screen line that shows it just below them. Always <= line.
    size_t scrollTopToReveal(size_t line) const;

    // If the scope opens with a line that holds only "{", the line to show is the one above it.
    static size_t headerLineFor(const TextBuffer& b, size_t startLine);

private:
    static constexpr size_t kNone = static_cast<size_t>(-1);
    struct Node {
        ScopeRegion r;
        size_t parent;   // the scope around this one, or kNone
        size_t depth;    // how many scopes are around this one
    };
    size_t innermostAt(size_t line) const;   // innermost scope with start < line <= end, or kNone

    std::vector<Node> m_nodes;               // sorted by start, outer scopes first
    size_t m_max = 5;
};
