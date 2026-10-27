#include "syntaxhighlighter.h"
#include <algorithm>
#include <cctype>

namespace {

std::vector<const LanguageDef*>& registry() {
    static std::vector<const LanguageDef*> r;
    return r;
}

std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAlnum(char c) { return isDigit(c) || isAlpha(c); }

bool startsWith(const std::string& s, size_t i, const std::string& p) {
    return !p.empty() && s.compare(i, p.size(), p) == 0;
}

// s[k] is '<'. True if it closes with a matching '>' that is followed by '(' - as in std::min<size_t>(a, b)
bool templateCallAhead(const std::string& s, size_t k) {
    if (k + 1 < s.size() && (s[k + 1] == '<' || s[k + 1] == '=')) return false;   // << and <=
    int depth = 0;
    for (size_t p = k; p < s.size(); ++p) {
        char c = s[p];
        if (c == '<') {
            ++depth;
        } else if (c == '>') {
            if (--depth == 0) {
                size_t q = p + 1;
                while (q < s.size() && (s[q] == ' ' || s[q] == '\t')) ++q;
                return q < s.size() && s[q] == '(';
            }
        } else if (!(isAlnum(c) || c == '_' || c == ':' || c == ',' || c == ' ' || c == '*')) {
            return false;
        }
    }
    return false;
}

size_t utf8Len(unsigned char c) {
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

}

bool registerLanguage(const LanguageDef& def) {
    registry().push_back(&def);
    return true;
}

// ---------------------------------------------------------------- language choice

const LanguageDef* findLanguage(const std::string& fileName) {
    size_t slash = fileName.find_last_of('/');
    std::string base = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    std::string ext = (dot == std::string::npos || dot == 0) ? "" : lowered(base.substr(dot + 1));

    for (const LanguageDef* d : registry())
        if (std::find(d->fileNames.begin(), d->fileNames.end(), base) != d->fileNames.end()) return d;
    if (!ext.empty())
        for (const LanguageDef* d : registry())
            if (std::find(d->extensions.begin(), d->extensions.end(), ext) != d->extensions.end()) return d;
    return nullptr;
}

void SyntaxHighlighter::setLanguageFor(const std::string& fileName) {
    setLanguage(findLanguage(fileName));
}

void SyntaxHighlighter::setLanguage(const LanguageDef* def) {
    m_lang = def;
    m_kw.clear(); m_types.clear(); m_consts.clear(); m_builtins.clear(); m_mods.clear(); m_intro.clear();
    if (def) {
        auto fill = [def](std::unordered_set<std::string>& set, const std::vector<std::string>& words) {
            for (const std::string& w : words) set.insert(def->caseInsensitive ? lowered(w) : w);
        };
        fill(m_kw, def->keywords);
        fill(m_types, def->types);
        fill(m_consts, def->constants);
        fill(m_builtins, def->builtins);
        fill(m_mods, def->modifiers);
        fill(m_intro, def->classIntroducers);
    }
    reset();
}

void SyntaxHighlighter::reset() {
    m_prev.clear();
    m_tokens.clear();
    m_end.clear();
    m_ids.clear();
    m_dirty = true;
}

// QML: finds every name that appears as "id: name" anywhere in the file.
std::unordered_set<std::string> SyntaxHighlighter::collectIds(const TextBuffer& b) const {
    std::unordered_set<std::string> ids;
    const std::string& kw = m_lang->idKeyword;
    for (size_t l = 0; l < b.lineCount(); ++l) {
        const std::string& s = b.line(l);
        size_t pos = 0;
        while ((pos = s.find(kw, pos)) != std::string::npos) {
            size_t e = pos + kw.size();
            bool startOk = pos == 0 || !(isAlnum(s[pos - 1]) || s[pos - 1] == '_' || s[pos - 1] == '.');
            size_t j = e;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
            if (startOk && j < s.size() && s[j] == ':') {
                ++j;
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
                size_t w = j;
                while (w < s.size() && (isAlnum(s[w]) || s[w] == '_')) ++w;
                if (w > j) ids.insert(s.substr(j, w - j));
            }
            pos = e;
        }
    }
    return ids;
}

// ---------------------------------------------------------------- scanning

SyntaxHighlighter::State SyntaxHighlighter::scanLine(const std::string& s, State st, bool firstLine,
                                                     std::vector<Token>& out) const {
    const LanguageDef& L = *m_lang;
    const size_t n = s.size();
    const size_t npos = std::string::npos;
    out.assign(n, Token::None);

    auto mark = [&](size_t from, size_t to, Token t) {
        for (size_t k = from; k < to && k < n; ++k) out[k] = t;
    };
    auto isIdent = [&](char c) {
        return isAlnum(c) || c == '_' || static_cast<unsigned char>(c) >= 0x80 || L.identExtra.find(c) != npos;
    };
    // true if the word at `pos` is the first thing on its line, or comes right after '{' ',' or ';'
    auto startsField = [&](size_t pos) {
        while (pos > 0 && (s[pos - 1] == ' ' || s[pos - 1] == '\t')) --pos;
        return pos == 0 || s[pos - 1] == '{' || s[pos - 1] == ',' || s[pos - 1] == ';';
    };

    // block comment: `from` is where the colouring starts, `p` where the text after the opener begins
    auto scanBlock = [&](size_t from, size_t p) -> size_t {
        while (p < n) {
            if (startsWith(s, p, L.blockClose)) {
                p += L.blockClose.size();
                if (--st.depth <= 0) {
                    st.depth = 0;
                    st.kind = State::Normal;
                    mark(from, p, Token::Comment);
                    return p;
                }
                continue;
            }
            if (L.nestedBlockComments && startsWith(s, p, L.blockOpen)) { ++st.depth; p += L.blockOpen.size(); continue; }
            ++p;
        }
        mark(from, n, Token::Comment);
        return n;
    };

    auto scanString = [&](size_t from, size_t p, char q) -> size_t {
        bool raw = L.rawQuotes.find(q) != npos;
        while (p < n) {
            if (!raw && s[p] == '\\') { p += 2; continue; }
            if (s[p] == q) { mark(from, p + 1, Token::String); return p + 1; }
            ++p;
        }
        mark(from, n, Token::String);
        if (L.multiLineQuotes.find(q) != npos) { st.kind = State::InString; st.quote = q; }
        return n;
    };

    auto scanRaw = [&](size_t from, size_t p, int hashes) -> size_t {
        const size_t h = static_cast<size_t>(hashes);
        while (p < n) {
            if (s[p] == '"') {
                size_t k = 0;
                while (k < h && p + 1 + k < n && s[p + 1 + k] == '#') ++k;
                if (k == h) { mark(from, p + 1 + k, Token::String); return p + 1 + k; }
            }
            ++p;
        }
        mark(from, n, Token::String);
        st.kind = State::InRaw;
        st.hashes = hashes;
        return n;
    };

    // Lua long brackets: [[ ... ]] and [==[ ... ]==].  `from` is where the colouring starts, `p` the start of the text inside.
    auto scanLong = [&](size_t from, size_t p, int level, Token tok) -> size_t {
        std::string close = "]" + std::string(static_cast<size_t>(level), '=') + "]";
        size_t e = s.find(close, p);
        if (e != npos) { mark(from, e + close.size(), tok); return e + close.size(); }
        mark(from, n, tok);
        st.kind = State::InLong;
        st.hashes = level;
        st.comment = (tok == Token::Comment);
        return n;
    };
    // Does a long bracket open at p? Returns its level (the number of '='), or -1. `body` gets the start of the text inside.
    auto longOpenAt = [&](size_t p, size_t& body) -> int {
        if (p >= n || s[p] != '[') return -1;
        size_t q = p + 1;
        int level = 0;
        while (q < n && s[q] == '=') { ++level; ++q; }
        if (q < n && s[q] == '[') { body = q + 1; return level; }
        return -1;
    };

    // ---- a comment or string that started on an earlier line ----
    size_t i = 0;
    if (st.kind == State::InBlock) {
        i = scanBlock(0, 0);
    } else if (st.kind == State::InString) {
        char q = st.quote;
        st.kind = State::Normal;
        i = scanString(0, 0, q);
    } else if (st.kind == State::InRaw) {
        int h = st.hashes;
        st.kind = State::Normal;
        i = scanRaw(0, 0, h);
    } else if (st.kind == State::InLong) {
        int lv = st.hashes;
        Token tk = st.comment ? Token::Comment : Token::String;
        st.kind = State::Normal;
        i = scanLong(0, 0, lv, tk);
    }

    if (firstLine && L.shebang && i == 0 && startsWith(s, 0, "#!")) {
        mark(0, n, Token::Preprocessor);
        return st;
    }

    bool onlySpace = (i == 0);       // nothing but whitespace before this point (for preprocessor lines)
    int ternary = 0;                 // open '?' on this line, so "a ? b : c" does not make b a property
    std::string lastWord;

    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
        bool atStart = onlySpace;
        onlySpace = false;

        // comments
        if (L.longBrackets && startsWith(s, i, L.lineComment)) {      // --[[ ... ]] and --[==[ ... ]==]
            size_t body = 0;
            int lv = longOpenAt(i + L.lineComment.size(), body);
            if (lv >= 0) { i = scanLong(i, body, lv, Token::Comment); continue; }
        }
        if (startsWith(s, i, L.blockOpen)) {
            st.kind = State::InBlock;
            st.depth = 1;
            i = scanBlock(i, i + L.blockOpen.size());
            continue;
        }
        if (startsWith(s, i, L.lineComment)) {
            bool ok = !L.lineCommentAtBoundary || i == 0 || std::string(" \t;&|(").find(s[i - 1]) != npos;
            if (ok) { mark(i, n, Token::Comment); break; }
        }

        // #include, #define ...
        if (L.preprocessor && c == L.preprocessor && atStart) {
            size_t j = i + 1;
            while (j < n && (s[j] == ' ' || s[j] == '\t')) ++j;
            size_t w = j;
            while (w < n && isAlpha(s[w])) ++w;
            if (w > j) {
                std::string word = s.substr(j, w - j);
                mark(i, w, Token::Preprocessor);
                i = w;
                if (word == "include" || word == "import") {
                    size_t k = i;
                    while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
                    size_t e = (k < n && s[k] == '<') ? s.find('>', k) : npos;
                    if (e != npos) { mark(k, e + 1, Token::String); i = e + 1; }
                }
                continue;
            }
        }

        // Rust #[...] and #![...]
        bool matched = false;
        for (const std::string& pre : L.attributePrefixes) {
            if (!startsWith(s, i, pre)) continue;
            size_t p = i + pre.size();
            int d = 1;
            while (p < n && d > 0) { if (s[p] == '[') ++d; else if (s[p] == ']') --d; ++p; }
            mark(i, p, Token::Attribute);
            i = p;
            matched = true;
            break;
        }
        if (matched) continue;

        // Rust raw strings: r"..." r#"..."# br"..."
        if (L.rawStrings && (c == 'r' || (c == 'b' && i + 1 < n && s[i + 1] == 'r')) && (i == 0 || !isIdent(s[i - 1]))) {
            size_t j = i + (c == 'b' ? 2 : 1);
            int h = 0;
            while (j < n && s[j] == '#') { ++h; ++j; }
            if (j < n && s[j] == '"') { i = scanRaw(i, j + 1, h); continue; }
        }

        // Rust: 'x' is a character, 'a is a lifetime
        if (L.lifetimes && c == '\'') {
            size_t a = i + 1;
            size_t endq = npos;
            if (a < n && s[a] == '\\') endq = s.find('\'', a + 2);
            else if (a < n) {
                size_t e = a + utf8Len(static_cast<unsigned char>(s[a]));
                if (e < n && s[e] == '\'') endq = e;
            }
            if (endq != npos) { mark(i, endq + 1, Token::String); i = endq + 1; }
            else {
                size_t e = a;
                while (e < n && isIdent(s[e])) ++e;
                mark(i, e, Token::Attribute);
                i = e;
            }
            continue;
        }

        // Lua long strings: [[ ... ]]
        if (L.longBrackets) {
            size_t body = 0;
            int lv = longOpenAt(i, body);
            if (lv >= 0) { i = scanLong(i, body, lv, Token::String); continue; }
        }

        // strings
        if (L.quotes.find(c) != npos) { i = scanString(i, i + 1, c); continue; }

        // $var ${var} $1 $?
        if (L.variableSigil && c == L.variableSigil) {
            size_t j = i + 1;
            if (j < n && s[j] == '{') {
                size_t e = s.find('}', j);
                size_t stop = e == npos ? n : e + 1;
                mark(i, stop, Token::Variable);
                i = stop;
            } else if (j < n && (isAlpha(s[j]) || s[j] == '_')) {
                while (j < n && (isAlnum(s[j]) || s[j] == '_')) ++j;
                mark(i, j, Token::Variable);
                i = j;
            } else if (j < n && std::string("?#@*!$-0123456789").find(s[j]) != npos) {
                mark(i, j + 1, Token::Variable);
                i = j + 1;
            } else {
                mark(i, j, Token::Variable);      // a lone '$', as in $(...)
                i = j;
            }
            continue;
        }

        // numbers
        if (isDigit(c) && (i == 0 || !isIdent(s[i - 1]))) {
            bool hex = c == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X');
            size_t j = i;
            while (j < n) {
                char d = s[j];
                if (!hex && (d == 'e' || d == 'E') && j + 2 < n && (s[j + 1] == '+' || s[j + 1] == '-') && isDigit(s[j + 2])) { j += 2; continue; }
                if (d == '.') { if (j + 1 < n && isDigit(s[j + 1])) { ++j; continue; } break; }
                if (isAlnum(d) || d == '_') { ++j; continue; }
                if (L.digitSeparator && d == L.digitSeparator && j + 1 < n && isAlnum(s[j + 1])) { ++j; continue; }
                break;
            }
            mark(i, j, Token::Number);
            i = j;
            continue;
        }

        // words
        if (isAlpha(c) || c == '_' || static_cast<unsigned char>(c) >= 0x80 || L.identExtra.find(c) != npos) {
            size_t j = i + 1;
            while (j < n && isIdent(s[j])) ++j;
            std::string word = s.substr(i, j - i);
            std::string key = L.caseInsensitive ? lowered(word) : word;
            size_t k = j;
            while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
            char next = k < n ? s[k] : 0;

            Token t = Token::None;
            if (m_mods.count(key)) t = Token::Modifier;
            else if (m_kw.count(key)) t = Token::Keyword;
            else if (!m_ids.empty() && m_ids.count(word) && !(i > 0 && s[i - 1] == '.')) t = Token::Identifier;
            else if (L.propertyColon && next == ':' && !(k + 1 < n && s[k + 1] == ':') && ternary == 0 && lastWord != "case")
                t = Token::Property;
            else if (L.fieldAccess && i > 0 && s[i - 1] == '.' && !(i > 1 && s[i - 2] == '.'))        // obj.name
                t = next == '(' ? Token::Function : Token::Property;
            else if (L.propertyEquals && next == '=' && !(k + 1 < n && s[k + 1] == '=') && startsField(i))   // { name = value }
                t = Token::Property;
            else if (L.scopeQualifierIsClass && next == ':' && k + 1 < n && s[k + 1] == ':') t = Token::Class;
            else if (!lastWord.empty() && m_intro.count(lastWord)) t = Token::Class;
            else if (m_types.count(key)) t = Token::Type;
            else if (m_consts.count(key)) t = Token::Constant;
            else if (m_builtins.count(key)) t = Token::Function;
            else if (L.macroBang && j < n && s[j] == '!' && !(j + 1 < n && s[j + 1] == '=')) t = Token::Function;
            else if (L.callsAreFunctions && (next == '(' || (L.templateCalls && next == '<' && templateCallAhead(s, k))))
                t = Token::Function;
            else if ((L.capitalizedIsType || L.capitalizedIsClass) && word[0] >= 'A' && word[0] <= 'Z') {
                bool allUpper = word.size() > 1;
                for (char ch : word) if (isAlpha(ch) && !(ch >= 'A' && ch <= 'Z')) allUpper = false;
                t = allUpper ? Token::Constant : (L.capitalizedIsClass ? Token::Class : Token::Type);
            }
            else if (L.plainWordsAreVariables) t = Token::Variable;
            mark(i, j, t);
            lastWord = key;
            i = j;
            continue;
        }

        // everything else (operators, brackets)
        if (c == '?') {
            if (i + 1 < n && s[i + 1] == '?') { i += 2; continue; }      // ??
            if (!(i + 1 < n && s[i + 1] == '.')) ++ternary;              // not ?.
        } else if (c == ':' && ternary > 0) {
            --ternary;
        }
        ++i;
    }
    return st;
}

void SyntaxHighlighter::update(const TextBuffer& b) {
    if (!m_lang) return;
    if (!m_dirty && b.revision() == m_rev) return;          // the same text as last time: the usual case (cursor moves, scrolling)
    size_t n = b.lineCount();
    size_t first = 0;
    if (!m_dirty) {
        size_t lim = std::min(n, m_prev.size());
        while (first < lim && m_prev[first] == b.line(first)) ++first;
        if (first == n && n == m_prev.size()) { m_rev = b.revision(); return; }       // nothing changed
    }
    if (!m_lang->idKeyword.empty()) {                        // QML: a new or removed id recolours the whole file
        auto ids = collectIds(b);
        if (ids != m_ids) { m_ids = std::move(ids); first = 0; }
    }
    m_prev.resize(n);
    m_tokens.resize(n);
    m_end.resize(n);
    State st = first > 0 ? m_end[first - 1] : State();
    for (size_t i = first; i < n; ++i) {
        m_prev[i] = b.line(i);
        st = scanLine(m_prev[i], st, i == 0, m_tokens[i]);
        m_end[i] = st;
    }
    m_dirty = false;
    m_rev = b.revision();
}

Token SyntaxHighlighter::tokenAt(size_t line, size_t col) const {
    if (line >= m_tokens.size() || col >= m_tokens[line].size()) return Token::None;
    return m_tokens[line][col];
}

