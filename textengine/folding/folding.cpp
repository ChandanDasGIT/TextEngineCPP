#include "folding.h"
#include <algorithm>
#include <cctype>
#include "../syntaxhighlight/languagedef.h"

namespace {

struct Candidate {
    size_t start;
    size_t end;          // line of the closing bracket (or end of the comment)
    std::string tail;
};

char openerOf(char c) {
    switch (c) {
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default: return 0;
    }
}

bool isOpener(char c) { return c == '(' || c == '[' || c == '{'; }

// the text from the closing bracket to the end of its line, shortened for display
std::string tailFrom(const std::string& s, size_t col, const Folding::Syntax& sx) {
    std::string t = s.substr(col);
    if (!sx.lineComment.empty()) {
        size_t cm = t.find(sx.lineComment);
        if (cm != std::string::npos) t.resize(cm);
    }
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
    const size_t kMax = 24;
    if (t.size() > kMax) {
        size_t cut = kMax;
        while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;   // do not cut a UTF-8 character
        t.resize(cut);
        t += "…";
    }
    return t;
}

// Finds every multi-line bracket pair and block comment, skipping strings and comments.
std::vector<Candidate> scanBrackets(const std::vector<std::string>& lines, const Folding::Syntax& sx) {
    struct Open { char ch; size_t line; };
    std::vector<Open> stack;
    std::vector<Candidate> out;
    bool inBlock = false;
    size_t blockStart = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& s = lines[i];
        size_t c = 0;
        while (c < s.size()) {
            if (inBlock) {
                size_t e = s.find(sx.blockClose, c);
                if (e == std::string::npos) break;                       // the comment goes on in the next line
                if (i > blockStart) out.push_back({blockStart, i, tailFrom(s, e, sx)});
                inBlock = false;
                c = e + sx.blockClose.size();
                continue;
            }
            if (!sx.blockOpen.empty() && s.compare(c, sx.blockOpen.size(), sx.blockOpen) == 0) {   // before the line comment: CMake's "#[[" starts with "#"
                inBlock = true;
                blockStart = i;
                c += sx.blockOpen.size();
                continue;
            }
            if (!sx.lineComment.empty() && s.compare(c, sx.lineComment.size(), sx.lineComment) == 0) break;
            char ch = s[c];
            if (sx.quotes.find(ch) != std::string::npos) {               // skip a string up to its closing quote
                size_t j = c + 1;
                while (j < s.size() && s[j] != ch) j += (s[j] == '\\') ? 2 : 1;
                c = j + 1;
                continue;
            }
            if (isOpener(ch)) {
                stack.push_back({ch, i});
            } else if (char want = openerOf(ch)) {
                size_t k = stack.size();
                while (k > 0 && stack[k - 1].ch != want) --k;            // unmatched openers above are dropped
                if (k > 0) {
                    size_t start = stack[k - 1].line;
                    stack.resize(k - 1);
                    if (start != i) out.push_back({start, i, tailFrom(s, c, sx)});
                }
            }
            ++c;
        }
    }
    return out;
}

// indentation width of a line (tab = 4); blank is true for empty or whitespace-only lines
int indentOf(const std::string& s, bool& blank) {
    int w = 0;
    size_t i = 0;
    for (; i < s.size(); ++i) {
        if (s[i] == ' ') ++w;
        else if (s[i] == '\t') w += 4;
        else if (s[i] != '\r') break;
    }
    blank = i >= s.size();
    return w;
}

std::string stripLeft(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

}

// ---------------------------------------------------------------- settings

Folding::Syntax Folding::cLikeSyntax() {
    Syntax s;
    s.lineComment = "//";
    s.blockOpen = "/*";
    s.blockClose = "*/";
    s.quotes = "\"'`";
    return s;
}

Folding::Syntax Folding::hashSyntax() {
    Syntax s;
    s.lineComment = "#";
    s.quotes = "\"'";
    return s;
}

void Folding::setMode(Mode m) {
    m_mode = m;
    m_dirty = true;
}

void Folding::setSyntax(const Syntax& s) {
    m_syntax = s;
    if (m_syntax.blockOpen.empty() || m_syntax.blockClose.empty()) {
        m_syntax.blockOpen.clear();
        m_syntax.blockClose.clear();
    }
    m_dirty = true;
}

void Folding::setLanguageFor(const std::string& fileName) {
    // A language the highlighter knows describes itself: comments, strings and whether it folds by indentation.
    if (const LanguageDef* def = findLanguage(fileName)) {
        m_mode = def->foldByIndent ? Mode::Indent : Mode::Brackets;
        m_syntax = Syntax();
        m_syntax.lineComment = def->lineComment;
        m_syntax.blockOpen = def->blockOpen;
        m_syntax.blockClose = def->blockClose;
        for (const std::string* q : {&def->quotes, &def->rawQuotes})      // every character that starts a string
            for (char ch : *q)
                if (m_syntax.quotes.find(ch) == std::string::npos) m_syntax.quotes += ch;
        if (m_syntax.blockOpen.empty() || m_syntax.blockClose.empty()) {
            m_syntax.blockOpen.clear();
            m_syntax.blockClose.clear();
        }
        m_dirty = true;
        return;
    }

    // No language known for this file: a short list of plain formats, otherwise C-like brackets.
    size_t slash = fileName.find_last_of('/');
    std::string base = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : base.substr(dot + 1);
    for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    if (ext == "rb" || ext == "toml") {
        m_mode = Mode::Brackets;
        m_syntax = hashSyntax();
    } else if (ext == "py" || ext == "yml" || ext == "yaml" || ext == "txt" || ext == "md") {
        m_mode = Mode::Indent;
        m_syntax = hashSyntax();
    } else {
        m_mode = Mode::Brackets;
        m_syntax = cLikeSyntax();
    }
    m_dirty = true;
}

// ---------------------------------------------------------------- finding regions

void Folding::rebuild(const std::vector<std::string>& lines) {
    size_t n = lines.size();
    std::vector<FoldRegion> regs;

    if (m_mode == Mode::Brackets) {
        std::vector<Candidate> cands = scanBrackets(lines, m_syntax);

        // one region per start line: the one that reaches furthest (on a tie, the inner one)
        std::vector<int> best(n, -1);
        for (size_t k = 0; k < cands.size(); ++k) {
            int& slot = best[cands[k].start];
            if (slot < 0 || cands[k].end > cands[static_cast<size_t>(slot)].end) slot = static_cast<int>(k);
        }
        for (size_t i = 0; i < n; ++i) {
            if (best[i] < 0) continue;
            const Candidate& c = cands[static_cast<size_t>(best[i])];
            FoldRegion r{c.start, c.end - 1, std::string()};   // the closing line (e.g. "}") always stays visible
            if (r.hiddenTo > r.start) regs.push_back(r);
        }
    } else {
        std::vector<int> ind(n);
        std::vector<char> blank(n);
        for (size_t i = 0; i < n; ++i) {
            bool bl = false;
            ind[i] = indentOf(lines[i], bl);
            blank[i] = bl ? 1 : 0;
        }
        std::vector<size_t> nextNB(n + 1, n);                            // next non-blank line at or after i
        for (size_t i = n; i-- > 0;) nextNB[i] = blank[i] ? nextNB[i + 1] : i;

        struct Open { int indent; size_t line; };
        std::vector<Open> st;
        size_t prevNB = 0;
        auto closeBlock = [&](const Open& o) {
            if (prevNB > o.line) regs.push_back({o.line, prevNB, std::string()});
        };
        for (size_t i = 0; i < n; ++i) {
            if (blank[i]) continue;
            while (!st.empty() && ind[i] <= st.back().indent) { closeBlock(st.back()); st.pop_back(); }
            size_t j = nextNB[i + 1];
            if (j < n && ind[j] > ind[i]) st.push_back({ind[i], i});
            prevNB = i;
        }
        while (!st.empty()) { closeBlock(st.back()); st.pop_back(); }
        std::sort(regs.begin(), regs.end(),
                  [](const FoldRegion& a, const FoldRegion& b) { return a.start < b.start; });
    }

    m_regions = std::move(regs);
    m_startIdx.assign(n, -1);
    for (size_t k = 0; k < m_regions.size(); ++k) m_startIdx[m_regions[k].start] = static_cast<int>(k);
}

// ---------------------------------------------------------------- keeping folds while the text changes

void Folding::remapFolds(const std::vector<std::string>& prev, const std::vector<std::string>& cur) {
    if (m_folded.empty()) return;
    size_t pn = prev.size(), cn = cur.size();
    std::set<size_t> next;

    if (pn == cn) {
        // same number of lines: a fold stays where it is if its header is still (nearly) the same line
        size_t diffs = 0, firstDiff = 0;
        for (size_t i = 0; i < pn; ++i)
            if (prev[i] != cur[i]) { if (diffs++ == 0) firstDiff = i; }
        for (size_t s : m_folded)
            if (prev[s] == cur[s] || stripLeft(prev[s]) == stripLeft(cur[s]) || (diffs == 1 && s == firstDiff))
                next.insert(s);
    } else {
        // lines were added or removed: lines before the change stay, lines after it shift
        size_t lim = std::min(pn, cn), pre = 0;
        while (pre < lim && prev[pre] == cur[pre]) ++pre;
        size_t suf = 0;
        while (suf < lim - pre && prev[pn - 1 - suf] == cur[cn - 1 - suf]) ++suf;
        for (size_t s : m_folded) {
            if (s < pre) next.insert(s);
            else if (s >= pn - suf) next.insert(s + cn - pn);
            else if (s == pre) next.insert(s);                           // the line where the edit began
        }
    }
    m_folded = std::move(next);
}

void Folding::update(const TextBuffer& b) {
    std::vector<std::string> cur;
    cur.reserve(b.lineCount());
    for (size_t i = 0; i < b.lineCount(); ++i) cur.push_back(b.line(i));
    if (!m_dirty && cur == m_prev) return;

    remapFolds(m_prev, cur);
    rebuild(cur);
    for (auto it = m_folded.begin(); it != m_folded.end();) {            // a fold needs a region on its line
        if (*it >= cur.size() || m_startIdx[*it] < 0) it = m_folded.erase(it);
        else ++it;
    }
    m_prev = std::move(cur);
    m_dirty = false;
    rebuildHidden();
}

void Folding::reset() {
    m_folded.clear();
    m_prev.clear();
    m_regions.clear();
    m_startIdx.clear();
    m_hidden.clear();
}

void Folding::rebuildHidden() {
    size_t n = m_startIdx.size();
    m_hidden.assign(n, 0);
    for (size_t s : m_folded) {
        const FoldRegion* r = regionAt(s);
        if (!r) continue;
        for (size_t l = s + 1; l <= r->hiddenTo && l < n; ++l) m_hidden[l] = 1;
    }
}

const FoldRegion* Folding::regionAt(size_t line) const {
    if (line >= m_startIdx.size() || m_startIdx[line] < 0) return nullptr;
    return &m_regions[static_cast<size_t>(m_startIdx[line])];
}

// ---------------------------------------------------------------- queries

FoldMark Folding::markAt(size_t line) const {
    if (!regionAt(line)) return FoldMark::None;
    return m_folded.count(line) ? FoldMark::Folded : FoldMark::Expanded;
}

bool Folding::canFold(size_t line) const { return regionAt(line) != nullptr; }
bool Folding::isFolded(size_t line) const { return m_folded.count(line) != 0 && regionAt(line) != nullptr; }
bool Folding::isHidden(size_t line) const { return line < m_hidden.size() && m_hidden[line] != 0; }

std::string Folding::placeholder(size_t line) const {
    const FoldRegion* r = regionAt(line);
    return (r && m_folded.count(line)) ? r->tail : std::string();
}

// ---------------------------------------------------------------- changing folds

bool Folding::fold(size_t line) {
    if (!regionAt(line) || m_folded.count(line)) return false;
    m_folded.insert(line);
    rebuildHidden();
    return true;
}

bool Folding::unfold(size_t line) {
    if (m_folded.erase(line) == 0) return false;
    rebuildHidden();
    return true;
}

bool Folding::foldAt(size_t line) {
    const FoldRegion* best = nullptr;
    for (const FoldRegion& r : m_regions) {
        if (r.start > line) break;
        if (line > r.hiddenTo) continue;
        if (m_folded.count(r.start)) continue;
        best = &r;                                   // regions are sorted by start: the last one is the innermost
    }
    if (!best) return false;
    m_folded.insert(best->start);
    rebuildHidden();
    return true;
}

bool Folding::unfoldAt(size_t line) {
    if (m_folded.erase(line) != 0) { rebuildHidden(); return true; }
    const FoldRegion* best = nullptr;
    for (const FoldRegion& r : m_regions) {
        if (r.start >= line) break;
        if (line <= r.hiddenTo && m_folded.count(r.start)) best = &r;
    }
    if (!best) return false;
    m_folded.erase(best->start);
    rebuildHidden();
    return true;
}

void Folding::foldAll() {
    for (const FoldRegion& r : m_regions) m_folded.insert(r.start);
    rebuildHidden();
}

void Folding::unfoldAll() {
    m_folded.clear();
    rebuildHidden();
}

bool Folding::reveal(size_t line) {
    bool changed = false;
    for (auto it = m_folded.begin(); it != m_folded.end();) {
        const FoldRegion* r = regionAt(*it);
        if (r && *it < line && line <= r->hiddenTo) { it = m_folded.erase(it); changed = true; }
        else ++it;
    }
    if (changed) rebuildHidden();
    return changed;
}

std::vector<size_t> Folding::foldedLines() const {
    return std::vector<size_t>(m_folded.begin(), m_folded.end());
}

void Folding::restoreFolded(const std::vector<size_t>& lines) {
    for (size_t l : lines)
        if (regionAt(l)) m_folded.insert(l);
    rebuildHidden();
}

// ---------------------------------------------------------------- navigation helpers

size_t Folding::nextVisible(size_t line) const {
    size_t n = m_hidden.size();
    size_t l = line + 1;
    while (l < n && m_hidden[l]) ++l;
    return l;
}

size_t Folding::prevVisible(size_t line) const {
    if (line == 0) return 0;
    size_t l = line - 1;
    while (l > 0 && isHidden(l)) --l;
    return l;
}

size_t Folding::headerOf(size_t line) const {
    size_t h = line;
    for (size_t s : m_folded) {
        const FoldRegion* r = regionAt(s);
        if (r && s < line && line <= r->hiddenTo && s < h) h = s;
    }
    return h;
}

size_t Folding::foldEndOf(size_t line) const {
    size_t e = line;
    for (size_t s : m_folded) {
        const FoldRegion* r = regionAt(s);
        if (r && s <= line && line <= r->hiddenTo && r->hiddenTo > e) e = r->hiddenTo;
    }
    return e;
}