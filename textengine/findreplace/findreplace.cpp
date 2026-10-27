#include "findreplace.h"
#include <algorithm>
#include <cctype>
#include <regex>

namespace {

const size_t kMaxMatches = 100000;           // a search stops here, so a one-letter search in a huge file cannot freeze the UI
RegexMatcher* g_matcher = nullptr;           // the plugged-in regex engine, or nullptr for the built-in one

// ---- plain search: case folding ----
// Lower-case form of a letter, for the alphabets whose upper and lower case differ by a simple rule.
// The result always takes the same number of UTF-8 bytes, so positions in the folded text are positions in the original.
char32_t foldCp(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;                       // À-Þ
    if (c >= 0x100 && c <= 0x137) return (c % 2 == 0) ? c + 1 : c;                  // Latin Extended-A
    if (c >= 0x139 && c <= 0x148) return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177) return (c % 2 == 0) ? c + 1 : c;
    if (c == 0x178) return 0xFF;                                                    // Ÿ
    if (c >= 0x179 && c <= 0x17E) return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 0x20;                    // Greek
    if (c >= 0x410 && c <= 0x42F) return c + 0x20;                                  // Cyrillic
    if (c >= 0x400 && c <= 0x40F) return c + 0x50;
    return c;
}

std::string lowered(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { out += static_cast<char>(std::tolower(c)); ++i; continue; }
        if (c >= 0xC2 && c <= 0xDF && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {   // 2-byte character
            char32_t cp = (static_cast<char32_t>(c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            char32_t f = foldCp(cp);
            if (f >= 0x80 && f < 0x800) {
                out += static_cast<char>(0xC0 | (f >> 6));
                out += static_cast<char>(0x80 | (f & 0x3F));
                i += 2;
                continue;
            }
        }
        out += s[i++];                                    // anything else is kept as it is
    }
    return out;
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

// ---- the built-in regex engine ----

bool unboundedBrace(const std::string& p, size_t i) {      // p[i] == '{': is it "{n,}" ?
    size_t j = i + 1;
    while (j < p.size() && std::isdigit(static_cast<unsigned char>(p[j]))) ++j;
    return j > i + 1 && j + 1 < p.size() && p[j] == ',' && p[j + 1] == '}';
}

// A group that repeats something that repeats, like "(a+)+" or "(\w+\s*)*", can take exponential time.
// std::regex cannot be told to give up, so such patterns are refused. This is a rough check: it does not
// see every bad pattern, and it never refuses a pattern with a bounded repeat.
bool nestedRepeat(const std::string& p) {
    std::vector<bool> repeats(1, false);                   // per open group: does it contain a repeat?
    bool inClass = false;
    for (size_t i = 0; i < p.size(); ++i) {
        char c = p[i];
        if (c == '\\') { ++i; continue; }
        if (inClass) { if (c == ']') inClass = false; continue; }
        if (c == '[') { inClass = true; continue; }
        if (c == '(') { repeats.push_back(false); continue; }
        if (c == ')') {
            bool inner = repeats.size() > 1 && repeats.back();
            if (repeats.size() > 1) repeats.pop_back();
            char n = i + 1 < p.size() ? p[i + 1] : '\0';
            bool again = n == '+' || n == '*' || (n == '{' && unboundedBrace(p, i + 1));
            if (inner && again) return true;
            if (inner || again) repeats.back() = true;
            continue;
        }
        if (c == '+' || c == '*' || (c == '{' && unboundedBrace(p, i))) repeats.back() = true;
    }
    return false;
}

class StdRegexMatcher : public RegexMatcher {
public:
    bool findAll(const std::string& text, const std::string& pattern, bool caseSensitive, bool wholeWord,
                 size_t limit, std::vector<RegexMatch>& out, std::string& error) override {
        if (nestedRepeat(pattern)) { error = "regex too complex"; return false; }
        std::regex::flag_type flags = std::regex_constants::ECMAScript | std::regex_constants::multiline;
        if (!caseSensitive) flags |= std::regex_constants::icase;
        std::regex re;
        try {
            if (wholeWord) std::regex probe(pattern, flags);       // check the pattern alone: wrapping it could hide an unbalanced ")"
            re = std::regex(wholeWord ? "\\b(?:" + pattern + ")\\b" : pattern, flags);
        } catch (const std::regex_error&) {
            error = "invalid regex";
            return false;
        }
        try {
            for (std::sregex_iterator it(text.cbegin(), text.cend(), re), end; it != end; ++it) {
                RegexMatch m;
                for (size_t g = 0; g < it->size(); ++g) {
                    if ((*it)[g].matched) {
                        size_t a = static_cast<size_t>(it->position(g));
                        m.groups.push_back({a, a + static_cast<size_t>(it->length(g))});
                    } else {
                        m.groups.push_back({std::string::npos, std::string::npos});
                    }
                }
                out.push_back(std::move(m));
                if (out.size() >= limit) break;
            }
        } catch (const std::regex_error&) {
            out.clear();
            error = "regex too complex";
            return false;
        }
        return true;
    }
};

RegexMatcher& activeMatcher() {
    static StdRegexMatcher builtin;
    return g_matcher ? *g_matcher : builtin;
}

// The result of one search: where the matches are and, for a regex, what each group captured.
struct Found {
    std::vector<Range> ranges;
    std::vector<std::vector<std::string>> captures;     // only filled for a regex search
};

Found searchAll(const TextBuffer& b, const std::string& query, const FindOptions& o, const Range* scope, std::string* error) {
    Found found;
    if (error) error->clear();
    if (query.empty()) return found;

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
        std::vector<RegexMatch> ms;
        std::string err;
        if (!activeMatcher().findAll(text, query, o.matchCase, o.wholeWord, kMaxMatches + 1, ms, err)) {
            if (error) *error = err.empty() ? "invalid regex" : err;
            return found;                                 // no matches rather than half of them
        }
        if (ms.size() > kMaxMatches) {                    // keep what fits, but say so: Replace All refuses to run on a partial list
            ms.resize(kMaxMatches);
            if (error) *error = "too many matches";
        }
        for (const RegexMatch& m : ms) {
            if (m.groups.empty() || m.groups[0].first == std::string::npos || m.groups[0].second < m.groups[0].first ||
                m.groups[0].second > text.size())
                continue;                                 // a matcher that reports nonsense is ignored for this match
            size_t a = m.groups[0].first, z = m.groups[0].second;
            found.ranges.push_back({positionAt(starts, base + a), positionAt(starts, base + z)});
            std::vector<std::string> caps;
            for (const auto& g : m.groups)
                caps.push_back(g.first == std::string::npos || g.second < g.first || g.second > text.size()
                                   ? std::string() : text.substr(g.first, g.second - g.first));
            found.captures.push_back(std::move(caps));
        }
        return found;
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
            if (found.ranges.size() >= kMaxMatches) {
                if (error) *error = "too many matches";
                break;
            }
            found.ranges.push_back({positionAt(starts, base + at), positionAt(starts, base + end)});
            at = end;
        } else {
            ++at;
        }
    }
    return found;
}

// The replacement text for one match. `caps` holds what group 0, 1, 2 ... captured.
std::string expandTemplate(const std::string& tpl, const std::vector<std::string>& caps) {
    auto group = [&](size_t g) { return g < caps.size() ? caps[g] : std::string(); };
    std::string out;
    for (size_t i = 0; i < tpl.size(); ++i) {
        char c = tpl[i];
        if ((c == '$' || c == '\\') && i + 1 < tpl.size()) {
            char n = tpl[i + 1];
            if (c == '$' && n == '$') { out += '$'; ++i; continue; }
            if (c == '$' && n == '&') { out += group(0); ++i; continue; }
            if (n >= '0' && n <= '9') { out += group(static_cast<size_t>(n - '0')); ++i; continue; }
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

void FindReplace::setRegexMatcher(RegexMatcher* matcher) { g_matcher = matcher; }

std::vector<Range> FindReplace::findAll(const TextBuffer& b, const std::string& query, const FindOptions& o,
                                        const Range* scope, std::string* error) {
    return searchAll(b, query, o, scope, error).ranges;
}

std::vector<Range> FindReplace::findAll(const TextBuffer& b, const std::string& query, bool caseSensitive) {
    FindOptions o;
    o.matchCase = caseSensitive;
    return findAll(b, query, o);
}

std::string FindReplace::replacementFor(size_t index, const std::string& replacement) const {
    if (!m_opts.regex || index >= m_captures.size()) return replacement;
    return expandTemplate(replacement, m_captures[index]);
}

std::vector<std::string> FindReplace::replacementsForAll(const std::string& replacement) const {
    std::vector<std::string> out;
    out.reserve(m_matches.size());
    for (size_t i = 0; i < m_matches.size(); ++i) out.push_back(replacementFor(i, replacement));
    return out;
}

// ---------------------------------------------------------------- the match list

void FindReplace::runSearch(const TextBuffer& b) {
    m_error.clear();
    if (m_query.empty()) { m_matches.clear(); m_captures.clear(); return; }
    Found found = searchAll(b, m_query, m_opts, m_hasScope ? &m_scope : nullptr, &m_error);
    m_matches = std::move(found.ranges);
    m_captures = std::move(found.captures);
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
    m_captures.clear();
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