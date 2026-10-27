#pragma once
#include <cstddef>
#include <set>
#include <string>
#include <vector>
#include "../textbuffer/textbuffer.h"

// What the gutter draws next to a line.
enum class FoldMark { None, Expanded, Folded };

struct FoldRegion {
    size_t start;        // header line (always stays visible)
    size_t hiddenTo;     // last hidden line: lines start+1 .. hiddenTo disappear when folded
    std::string tail;    // text shown after the header when folded, e.g. "}" or "});"
};

class Folding {
public:
    enum class Mode { Brackets, Indent };

    struct Syntax {
        std::string lineComment;   // e.g. "//"
        std::string blockOpen;     // e.g. "/*"  (empty = no block comments)
        std::string blockClose;    // e.g. "*/"
        std::string quotes;        // characters that start a string, e.g. "\"'"
    };
    static Syntax cLikeSyntax();
    static Syntax hashSyntax();

    void setMode(Mode m);
    void setSyntax(const Syntax& s);
    void setLanguageFor(const std::string& fileName);   // picks mode and syntax from the extension

    // Call after the text changed. Finds the regions again and moves the folds with the lines.
    void update(const TextBuffer& b);
    void reset();                                       // forget everything (a new file was loaded)

    // ---- queries ----
    FoldMark markAt(size_t line) const;
    bool canFold(size_t line) const;                    // a region starts on this line
    bool isFolded(size_t line) const;                   // this line is a folded header
    bool isHidden(size_t line) const;
    std::string placeholder(size_t line) const;         // text after a folded header, or ""
    const std::vector<FoldRegion>& regions() const { return m_regions; }
    size_t foldedCount() const { return m_folded.size(); }

    // ---- changing folds ----
    bool fold(size_t line);                             // header line
    bool unfold(size_t line);
    bool foldAt(size_t line);                           // innermost open region around line
    bool unfoldAt(size_t line);                         // folded header at line, else the fold around it
    void foldAll();
    void unfoldAll();
    bool reveal(size_t line);                           // unfold whatever hides this line

    // ---- saving and restoring fold state ----
    std::vector<size_t> foldedLines() const;
    void restoreFolded(const std::vector<size_t>& lines);

    // ---- navigation helpers ----
    size_t nextVisible(size_t line) const;              // lineCount if there is none
    size_t prevVisible(size_t line) const;
    size_t headerOf(size_t line) const;                 // the folded header hiding line, or line itself
    size_t foldEndOf(size_t line) const;                // last line of the folded block line belongs to

    // Keeps only the screen rows whose line is visible. `Rows` is the vector WordWrap::layout returns.
    template <class Rows>
    Rows visibleRows(const Rows& rows) const {
        Rows out;
        out.reserve(rows.size());
        for (const auto& r : rows)
            if (!isHidden(r.line)) out.push_back(r);
        return out;
    }

private:
    void rebuild(const std::vector<std::string>& lines);
    void remapFolds(const std::vector<std::string>& prev, const std::vector<std::string>& cur);
    void rebuildHidden();
    const FoldRegion* regionAt(size_t line) const;

    Mode m_mode = Mode::Brackets;
    Syntax m_syntax = cLikeSyntax();
    bool m_dirty = true;                    // mode or syntax changed: scan again

    std::vector<std::string> m_prev;        // the text the regions were computed from
    std::vector<FoldRegion> m_regions;      // sorted by start, one per start line
    std::vector<int> m_startIdx;            // per line: index into m_regions, or -1
    std::set<size_t> m_folded;              // header lines that are folded
    std::vector<char> m_hidden;             // per line: 1 = hidden
};