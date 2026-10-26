#include "findreplace.h"
#include <algorithm>
#include <cctype>
#include <regex>

namespace {

std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool wordCh(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

std::vector<size_t> lineStartsOf(const TextBuffer& b) {
    std::vector<size_t> v;
    v.reserve(b.lineCount());
    size_t off = 0;
    for (size_t l = 0; l < b.lineCount(); ++l) { v.push_back(off); off += b.lineLength(l) + 1; }
    return v;
}

Position positionAt(const std::vector<size_t>& starts, size_t off) {
    size_t i = static_cast<size_t>(std::upper_bound(starts.begin(), starts.end(), off) - starts.begin());
    i = i == 0 ? 0 : i - 1;
    return Position{i, off - starts[i]};
}

size_t offsetAt(const std::vector<size_t>& starts, Position p) { return starts[p.line] + p.col; }

bool buildRegex(const std::string& q, const FindOptions& o, std::regex& re, std::string* err) {
    try {
        std::string pat = o.wholeWord ? "\\b(?:" + q + ")\\b" : q;
        std::regex::flag_type flags = std::regex_constants::ECMAScript | std::regex_constants::multiline;
        if (!o.matchCase) flags |= std::regex_constants::icase;
        re = std::regex(pat, flags);
        return true;
    } catch (const std::regex_error&) {
        if (err) *err = "invalid regex";
        return false;
    }
}

std::string expandTemplate(const std::string& tpl, const std::smatch& m) {
    std::string out;
    for (size_t i = 0; i < tpl.size(); ++i) {
        char c = tpl[i];
        if ((c == '$' || c == '\\') && i + 1 < tpl.size()) {
            char n = tpl[i + 1];
            if (c == '$' && n == '$') { out += '$'; ++i; continue; }
            if (c == '$' && n == '&') { out += m.str(0); ++i; continue; }
            if (n >= '0' && n <= '9') {
                size_t g = static_cast<size_t>(n - '0');
                if (g < m.size()) out += m.str(g);
                ++i;
                continue;
            }
            if (c == '\\') {
                if (n == 'n') { out += '\n'; ++i; continue; }
                if (n == 't') { out += '\t'; ++i; continue; }
                if (n == '\\') { out += '\\'; ++i; continue; }
            }
        }
        out += c;
    }
    return out;
}

}

// ---------------------------------------------------------------- searching

std::vector<Range> FindReplace::findAll(const TextBuffer& b, const std::string& query, const FindOptions& o,
                                        const Range* scope, std::string* error) {
    std::vector<Range> out;
    if (error) error->clear();
    if (query.empty()) return out;

    std::vector<size_t> starts = lineStartsOf(b);
    size_t base = 0;
    std::string text;
    if (scope) {
        Position a = b.clamp(scope->from), z = b.clamp(scope->to);
        if (z < a) std::swap(a, z);
        base = offsetAt(starts, a);
        text = b.textInRange(a, z);
    } else {
        text = b.text();
    }

    if (o.regex) {
        std::regex re;
        if (!buildRegex(query, o, re, error)) return out;
        try {
            for (std::sregex_iterator it(text.cbegin(), text.cend(), re), end; it != end; ++it) {
                size_t a = base + static_cast<size_t>(it->position(0));
                size_t n = static_cast<size_t>(it->length(0));
                out.push_back({positionAt(starts, a), positionAt(starts, a + n)});
            }
        } catch (const std::regex_error&) {
            out.clear();
            if (error) *error = "regex too complex";
        }
        return out;
    }

    std::string hay = o.matchCase ? text : lowered(text);
    std::string needle = o.matchCase ? query : lowered(query);
    bool checkLeft = o.wholeWord && wordCh(static_cast<unsigned char>(needle.front()));
    bool checkRight = o.wholeWord && wordCh(static_cast<unsigned char>(needle.back()));

    size_t at = 0;
    while ((at = hay.find(needle, at)) != std::string::npos) {
        size_t end = at + needle.size();
        bool ok = true;
        if (checkLeft && at > 0 && wordCh(static_cast<unsigned char>(hay[at - 1]))) ok = false;
        if (ok && checkRight && end < hay.size() && wordCh(static_cast<unsigned char>(hay[end]))) ok = false;
        if (ok) {
            out.push_back({positionAt(starts, base + at), positionAt(starts, base + end)});
            at = end;
        } else {
            ++at;
        }
    }
    return out;
}

std::vector<Range> FindReplace::findAll(const TextBuffer& b, const std::string& query, bool caseSensitive) {
    FindOptions o;
    o.matchCase = caseSensitive;
    return findAll(b, query, o);
}

std::vector<std::string> FindReplace::expand(const TextBuffer& b, const std::string& query, const FindOptions& o,
                                             const std::vector<Range>& matches, const std::string& replacement) {
    std::vector<std::string> out;
    out.reserve(matches.size());
    std::regex re;
    if (!o.regex || !buildRegex(query, o, re, nullptr)) {
        out.assign(matches.size(), replacement);
        return out;
    }

    std::string t = b.text();
    std::vector<size_t> starts = lineStartsOf(b);
    for (const Range& m : matches) {
        size_t off = offsetAt(starts, b.clamp(m.from));
        std::smatch sm;
        std::regex_constants::match_flag_type flags = std::regex_constants::match_continuous;
        if (off > 0) flags |= std::regex_constants::match_prev_avail;
        bool found = false;
        try { found = std::regex_search(t.cbegin() + static_cast<long>(off), t.cend(), sm, re, flags); }
        catch (const std::regex_error&) {}
        out.push_back(found ? expandTemplate(replacement, sm) : replacement);
    }
    return out;
}

// ---------------------------------------------------------------- the match list

void FindReplace::runSearch(const TextBuffer& b) {
    m_error.clear();
    if (m_query.empty()) { m_matches.clear(); return; }
    m_matches = findAll(b, m_query, m_opts, m_hasScope ? &m_scope : nullptr, &m_error);
}

void FindReplace::setQuery(const TextBuffer& b, const std::string& query, const FindOptions& opts, const Range* scope) {
    m_query = query;
    m_opts = opts;
    setScope(scope);
    runSearch(b);
    m_current = -1;
}

void FindReplace::setQuery(const TextBuffer& b, const std::string& query, bool caseSensitive) {
    FindOptions o;
    o.matchCase = caseSensitive;
    setQuery(b, query, o, nullptr);
}

void FindReplace::setScope(const Range* scope) {
    m_hasScope = scope != nullptr;
    if (scope) m_scope = *scope;
}

void FindReplace::refresh(const TextBuffer& b) {
    if (!active()) return;
    runSearch(b);
    if (m_matches.empty()) m_current = -1;
    else m_current = std::min(m_current, static_cast<int>(m_matches.size()) - 1);
}

void FindReplace::clear() {
    m_query.clear();
    m_matches.clear();
    m_error.clear();
    m_hasScope = false;
    m_current = -1;
}

const Range* FindReplace::currentMatch() const {
    return m_current >= 0 ? &m_matches[static_cast<size_t>(m_current)] : nullptr;
}

bool FindReplace::selectFrom(Position p) {
    if (m_matches.empty()) { m_current = -1; return false; }
    for (size_t i = 0; i < m_matches.size(); ++i) {
        if (!(m_matches[i].from < p)) { m_current = static_cast<int>(i); return true; }
    }
    m_current = 0;   // nothing after p: wrap around
    return true;
}

bool FindReplace::selectAfter(Position p) {
    if (m_matches.empty()) { m_current = -1; return false; }
    for (size_t i = 0; i < m_matches.size(); ++i) {
        if (p < m_matches[i].from) { m_current = static_cast<int>(i); return true; }
    }
    m_current = 0;
    return true;
}

bool FindReplace::selectBefore(Position p) {
    if (m_matches.empty()) { m_current = -1; return false; }
    for (size_t i = m_matches.size(); i-- > 0;) {
        if (m_matches[i].from < p) { m_current = static_cast<int>(i); return true; }
    }
    m_current = static_cast<int>(m_matches.size()) - 1;   // nothing before p: wrap around
    return true;
}