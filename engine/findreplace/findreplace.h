#pragma once
#include <string>
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

// Finds a search text in a TextBuffer and keeps the list of matches. It only searches;
// changing the text is done by TextView. Matches may span lines.
// Regex uses std::regex (ECMAScript, '^' and '$' match at every line). It works on bytes, so
// non-ASCII text is not handled specially. Everything regex-related is in findreplace.cpp.
class FindReplace {
public:
    void setQuery(const TextBuffer& b, const std::string& query, const FindOptions& opts, const Range* scope = nullptr);
    void setQuery(const TextBuffer& b, const std::string& query, bool caseSensitive);
    void refresh(const TextBuffer& b);        // text changed: search again with the same query
    void setScope(const Range* scope);        // nullptr = whole text. Call refresh() afterwards.
    void clear();

    bool active() const { return !m_query.empty(); }
    const std::string& query() const { return m_query; }
    const FindOptions& options() const { return m_opts; }
    bool caseSensitive() const { return m_opts.matchCase; }
    const std::string& error() const { return m_error; }   // e.g. "invalid regex", or empty
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

    // The text that replaces each match. Plain mode: the replacement as it is.
    // Regex mode: $1..$9, $&, \1..\9, \n, \t, \\ and $$ are expanded.
    static std::vector<std::string> expand(const TextBuffer& b, const std::string& query, const FindOptions& opts,
                                           const std::vector<Range>& matches, const std::string& replacement);

private:
    void runSearch(const TextBuffer& b);

    std::string m_query;
    FindOptions m_opts;
    bool m_hasScope = false;
    Range m_scope;
    std::string m_error;
    std::vector<Range> m_matches;
    int m_current = -1;
};