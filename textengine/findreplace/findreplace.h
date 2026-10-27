#pragma once
#include <string>
#include <utility>
#include <vector>
#include "../textbuffer/textbuffer.h"

// A stretch of text from `from` up to (not including) `to`.
struct Range {
    Position from;
    Position to;
};

struct FindOptions {
    bool matchCase = false;
    bool wholeWord = false;
    bool regex = false;
    bool inSelection = false;   // used by TextView, which turns it into a scope
};

// ---- a regular expression engine the front end can plug in ----
// The engine has a simple built-in regex search (std::regex). A front end that has a better one (Qt's
// QRegularExpression, PCRE2 ...) can hand it over with FindReplace::setRegexMatcher(). Nothing else changes.

// One match. groups[0] is the whole match, groups[1] is "(...)" number 1, and so on.
// Positions are BYTE offsets into the UTF-8 text that was searched: [begin, end). A group that did not
// take part in the match is {npos, npos} (std::string::npos).
struct RegexMatch {
    std::vector<std::pair<size_t, size_t>> groups;
};

class RegexMatcher {
public:
    virtual ~RegexMatcher() = default;

    // Finds the matches of `pattern` in `text` (UTF-8): in order, not overlapping, and not more than `limit`.
    //  - '^' and '$' must match at every line, '.' must not match a line break.
    //  - wholeWord: no letter, digit or '_' directly before or after a match.
    //  - Return false and set `error` if nothing can be searched: "invalid regex" for a bad pattern,
    //    "regex too complex" if it needed too much work (give up instead of freezing the program).
    //  - A front end that works in UTF-16 (QString) converts its offsets back to UTF-8 bytes.
    virtual bool findAll(const std::string& text, const std::string& pattern, bool caseSensitive, bool wholeWord,
                         size_t limit, std::vector<RegexMatch>& out, std::string& error) = 0;
};

// Finds a search text in a TextBuffer and keeps the list of matches. It only searches;
// changing the text is done by TextView. Matches may span lines.
// Plain search is done here, with simple Unicode case folding for Latin, Greek and Cyrillic letters.
// Regex search uses the matcher given to setRegexMatcher(), or the built-in std::regex one
// (ECMAScript syntax; no lookbehind, '\w' and '\b' know ASCII only). A search stops at 100000 matches.
class FindReplace {
public:
    // Chooses the regex engine for all searches (nullptr = the built-in one). Call it once at start-up.
    // The matcher must stay alive while it is set; the engine does not delete it.
    static void setRegexMatcher(RegexMatcher* matcher);

    void setQuery(const TextBuffer& b, const std::string& query, const FindOptions& opts, const Range* scope = nullptr);
    void setQuery(const TextBuffer& b, const std::string& query, bool caseSensitive);
    void refresh(const TextBuffer& b);        // text changed: search again with the same query
    void setScope(const Range* scope);        // nullptr = whole text. Call refresh() afterwards.
    void clear();

    bool active() const { return !m_query.empty(); }
    const std::string& query() const { return m_query; }
    const FindOptions& options() const { return m_opts; }
    bool caseSensitive() const { return m_opts.matchCase; }
    const std::string& error() const { return m_error; }   // "invalid regex", "regex too complex", "too many matches", or empty
    bool hasScope() const { return m_hasScope; }
    const Range& scope() const { return m_scope; }
    const std::vector<Range>& matches() const { return m_matches; }
    int current() const { return m_current; }               // index into matches(), or -1
    const Range* currentMatch() const;

    bool selectFrom(Position p);     // current = first match starting at or after p (wraps around)
    bool selectAfter(Position p);    // current = first match starting after p (wraps around)
    bool selectBefore(Position p);   // current = last match starting before p (wraps around)

    static std::vector<Range> findAll(const TextBuffer& b, const std::string& query, const FindOptions& opts,
                                      const Range* scope = nullptr, std::string* error = nullptr);
    static std::vector<Range> findAll(const TextBuffer& b, const std::string& query, bool caseSensitive);

    // The text that replaces match number `index` (of matches()). Plain mode: the replacement as it is.
    // Regex mode: $1..$9, $&, \1..\9, \n, \t, \\ and $$ are expanded from what that match captured.
    std::string replacementFor(size_t index, const std::string& replacement) const;
    std::vector<std::string> replacementsForAll(const std::string& replacement) const;   // one per match

private:
    void runSearch(const TextBuffer& b);

    std::string m_query;
    FindOptions m_opts;
    bool m_hasScope = false;
    Range m_scope;
    std::string m_error;
    std::vector<Range> m_matches;
    std::vector<std::vector<std::string>> m_captures;   // regex mode: per match, the text of group 0, 1, 2 ...
    int m_current = -1;
};