#include "cpphighlighter.h"
#include <QTextDocument>
#include <QTextBlock>
#include <QFileInfo>

CppHighlighter::CppHighlighter(QObject *parent)
: QSyntaxHighlighter(parent)
{
    buildRules();
}

void CppHighlighter::buildRules()
{
    m_highlightingRules.clear();
    const bool dark = m_isDark;
    HighlightingRule rule;

    // 1. C++ Keywords & Commands
    m_keywordFormat.setForeground(QColor(dark ? "#c678dd" : "#a626a4")); // Purple
    m_keywordFormat.setFontWeight(QFont::Bold);
    const QString keywordPatterns[] = {
        QStringLiteral("\\bchar\\b"), QStringLiteral("\\bclass\\b"), QStringLiteral("\\bconst\\b"),
        QStringLiteral("\\bdouble\\b"), QStringLiteral("\\benum\\b"), QStringLiteral("\\bexplicit\\b"),
        QStringLiteral("\\bfriend\\b"), QStringLiteral("\\binline\\b"), QStringLiteral("\\bint\\b"),
        QStringLiteral("\\blong\\b"), QStringLiteral("\\bnamespace\\b"), QStringLiteral("\\boperator\\b"),
        QStringLiteral("\\bprivate\\b"), QStringLiteral("\\bprotected\\b"), QStringLiteral("\\bpublic\\b"),
        QStringLiteral("\\bshort\\b"), QStringLiteral("\\bsignals\\b"), QStringLiteral("\\bsigned\\b"),
        QStringLiteral("\\bslots\\b"), QStringLiteral("\\bstatic\\b"), QStringLiteral("\\bstruct\\b"),
        QStringLiteral("\\btemplate\\b"), QStringLiteral("\\btypedef\\b"), QStringLiteral("\\btypename\\b"),
        QStringLiteral("\\bunion\\b"), QStringLiteral("\\bunsigned\\b"), QStringLiteral("\\bvirtual\\b"),
        QStringLiteral("\\bvoid\\b"), QStringLiteral("\\bvolatile\\b"), QStringLiteral("\\bbool\\b"),
        QStringLiteral("\\bauto\\b"), QStringLiteral("\\bconstexpr\\b"), QStringLiteral("\\bnullptr\\b"),
        QStringLiteral("\\bif\\b"), QStringLiteral("\\belse\\b"), QStringLiteral("\\bfor\\b"),
        QStringLiteral("\\bwhile\\b"), QStringLiteral("\\bdo\\b"), QStringLiteral("\\breturn\\b"),
        QStringLiteral("\\bswitch\\b"), QStringLiteral("\\bcase\\b"), QStringLiteral("\\bbreak\\b"),
        QStringLiteral("\\bcontinue\\b"), QStringLiteral("\\bdefault\\b"), QStringLiteral("\\bnew\\b"),
        QStringLiteral("\\bdelete\\b"), QStringLiteral("\\btry\\b"), QStringLiteral("\\bcatch\\b"),
        QStringLiteral("\\bthrow\\b"), QStringLiteral("\\busing\\b"), QStringLiteral("\\bco_await\\b"),
        QStringLiteral("\\bco_yield\\b"), QStringLiteral("\\bco_return\\b"), QStringLiteral("\\brequires\\b"),
        QStringLiteral("\\bconcept\\b"), QStringLiteral("\\bimport\\b"), QStringLiteral("\\bmodule\\b"),
                QStringLiteral("\\bfunction\\b"),
        QStringLiteral("\\bstring\\b"), QStringLiteral("\\bforeach\\b"), QStringLiteral("\\bin\\b"),
        QStringLiteral("\\binternal\\b"), QStringLiteral("\\breadonly\\b"), QStringLiteral("\\boverride\\b"),
        QStringLiteral("\\bsealed\\b"), QStringLiteral("\\babstract\\b"), QStringLiteral("\\basync\\b"),
        QStringLiteral("\\bawait\\b"), QStringLiteral("\\bnull\\b"), QStringLiteral("\\btrue\\b"),
        QStringLiteral("\\bfalse\\b"), QStringLiteral("\\bis\\b"), QStringLiteral("\\bas\\b"),
        QStringLiteral("\\bout\\b"), QStringLiteral("\\bref\\b"), QStringLiteral("\\bget\\b"),
        QStringLiteral("\\bset\\b"), QStringLiteral("\\bthis\\b"), QStringLiteral("\\bbase\\b"),
        QStringLiteral("\\binterface\\b"), QStringLiteral("\\bobject\\b"), QStringLiteral("\\bdecimal\\b"),
        QStringLiteral("\\bbyte\\b"), QStringLiteral("\\bfloat\\b"), QStringLiteral("\\bchecked\\b"),
        QStringLiteral("\\block\\b"), QStringLiteral("\\bvalue\\b")
    };
    // One combined regex instead of ~60 separate ones: "\bchar\b" -> "char"
    QStringList keywordWords;
    for (const QString &pattern : keywordPatterns)
        keywordWords << pattern.mid(2, pattern.size() - 4);
    rule.pattern = QRegularExpression(QStringLiteral("\\b(?:") + keywordWords.join(QLatin1Char('|'))
    + QStringLiteral(")\\b"));
    rule.format = m_keywordFormat;
    m_highlightingRules.append(rule);

    // 2. QML Declarations: 'property' and 'var'
    QTextCharFormat qmlDeclFormat;
    qmlDeclFormat.setForeground(QColor(dark ? "#e5c07b" : "#986801")); // Warm Gold
    qmlDeclFormat.setFontWeight(QFont::Bold);
    const QString qmlDeclPatterns[] = {
        QStringLiteral("\\bproperty\\b"),
        QStringLiteral("\\bvar\\b")
    };
    for (const QString &pattern : qmlDeclPatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = qmlDeclFormat;
        m_highlightingRules.append(rule);
    }

    // 3. Preprocessor Directives (#include, #define, #pragma, etc.)
    m_preprocessorFormat.setForeground(QColor(dark ? "#61afef" : "#4078f2")); // Sky Blue
    rule.pattern = QRegularExpression(QStringLiteral("#[a-zA-Z_]+"));
    rule.format = m_preprocessorFormat;
    m_highlightingRules.append(rule);

    // 4. C++ Scope Resolution Operator: word1::word2 (e.g. std::cout, Qt::Key_Up)
    QTextCharFormat scopeFormat;
    scopeFormat.setForeground(QColor(dark ? "#56b6c2" : "#0184bc")); // Cyan
    rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=::)|(?<=::)[A-Za-z_][A-Za-z0-9_]*\\b"));
    rule.format = scopeFormat;
    m_highlightingRules.append(rule);

    // 5. Class / Type Names (anything starting with a capital letter)
    // Same green as strings / include paths, no bold.
    m_classFormat.setForeground(QColor(dark ? "#98c379" : "#3a8a3d"));
    rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b"));
    rule.format = m_classFormat;
    m_highlightingRules.append(rule);

    // 6. Member / Property Access (words around dot: word.word2)
    QTextCharFormat memberFormat;
    memberFormat.setForeground(QColor(dark ? "#e06c75" : "#e45649")); // Soft Coral / Red
    rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\.)|(?<=\\.)[A-Za-z_][A-Za-z0-9_]*\\b"));
    rule.format = memberFormat;
    m_highlightingRules.append(rule);

    // 7. Words followed by a colon (e.g. key:, property:, case label:)
    // Excludes C++ scope operator '::'
    QTextCharFormat colonKeyFormat;
    colonKeyFormat.setForeground(QColor(dark ? "#d19a66" : "#b45f06")); // Warm Sand / Orange
    rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*:(?!:))"));
    rule.format = colonKeyFormat;
    m_highlightingRules.append(rule);

    // 8. Words followed by '=' assignment (e.g. let foo = 10; var x = ...)
    // Excludes '==' and '=>'
    QTextCharFormat assignTargetFormat;
    assignTargetFormat.setForeground(QColor(dark ? "#d19a66" : "#b45f06")); // Warm Sand / Orange
    rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*=(?![=>]))"));
    rule.format = assignTargetFormat;
    m_highlightingRules.append(rule);

    // 9. Functions / Words with brackets or braces: word(...), word{...}, word[...]
    m_functionFormat.setForeground(QColor(dark ? "#61afef" : "#4078f2")); // Blue
    rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*[\\(\\{\\[])"));
    rule.format = m_functionFormat;
    m_highlightingRules.append(rule);

    // 10. Numeric literals (Hex, floats, integers)
    m_numberFormat.setForeground(QColor(dark ? "#98c379" : "#3a8a3d")); // Mint Green
    rule.pattern = QRegularExpression(QStringLiteral("\\b0x[0-9a-fA-F]+\\b|\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?\\b"));
    rule.format = m_numberFormat;
    m_highlightingRules.append(rule);

    // 11. Strings
    m_quotationFormat.setForeground(QColor(dark ? "#98c379" : "#3a8a3d")); // Soft Green
    rule.pattern = QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'"));
    rule.format = m_quotationFormat;
    m_highlightingRules.append(rule);

    // Comment formats. Comments are NOT regex rules: they are found by a small
    // scanner in applyCommentFormats() that runs after all the rules above,
    // so they always override them (and ignore comment tokens inside strings).
    m_singleLineCommentFormat.setForeground(QColor(dark ? "#5c6370" : "#8a8d96")); // Muted Gray
    m_singleLineCommentFormat.setFontItalic(true);
    m_multiLineCommentFormat.setForeground(QColor(dark ? "#5c6370" : "#8a8d96"));
    m_multiLineCommentFormat.setFontItalic(true);

    applyLanguage();
}

void CppHighlighter::setDocument(QQuickTextDocument *quickDocument)
{
    m_matchedBracket1 = -1;
    m_matchedBracket2 = -1;
    if (quickDocument) {
        QSyntaxHighlighter::setDocument(quickDocument->textDocument());
    } else {
        QSyntaxHighlighter::setDocument(nullptr);
    }
}

QString CppHighlighter::filePath() const
{
    return m_filePath;
}

void CppHighlighter::setFilePath(const QString &path)
{
    if (m_filePath == path) return;
    m_filePath = path;
    emit filePathChanged();

    const QString p = path.toLower();
    const QString fileName = QFileInfo(path).fileName().toLower();
    Language lang = Language::Plain;
    if (p.endsWith(QStringLiteral(".lua"))) {
        lang = Language::Lua;
    } else if (p.endsWith(QStringLiteral(".fish"))) {
        lang = Language::Fish;
    } else if (fileName == QLatin1String("cmakelists.txt")) {
        lang = Language::CMake;
    } else {
        static const QStringList cppLike = {
            QStringLiteral(".cpp"), QStringLiteral(".h"),   QStringLiteral(".hpp"),
            QStringLiteral(".cc"),  QStringLiteral(".cxx"), QStringLiteral(".cs"),
            QStringLiteral(".qml"), QStringLiteral(".js"),  QStringLiteral(".mjs")
        };
        for (const QString &ext : cppLike) {
            if (p.endsWith(ext)) { lang = Language::Cpp; break; }
        }
    }

    if (lang != m_language) {
        m_language = lang;
        applyLanguage();
        rehighlight();
    }
}

bool CppHighlighter::isDark() const
{
    return m_isDark;
}

void CppHighlighter::setIsDark(bool dark)
{
    if (m_isDark == dark) return;
    m_isDark = dark;
    buildRules();
    emit isDarkChanged();
    rehighlight();
}

void CppHighlighter::applyLanguage()
{
    switch (m_language) {
        case Language::Lua:
            m_lineCommentToken = QStringLiteral("--");
            m_blockStartToken  = QStringLiteral("--[[");
            m_blockEndToken    = QStringLiteral("]]");
            break;
        case Language::Fish:
            m_lineCommentToken = QStringLiteral("#");
            m_blockStartToken.clear();
            m_blockEndToken.clear();
            break;
        case Language::CMake:
            m_lineCommentToken = QStringLiteral("#");
            m_blockStartToken  = QStringLiteral("#[[");
            m_blockEndToken    = QStringLiteral("]]");
            break;
        case Language::Plain:
            m_lineCommentToken.clear();
            m_blockStartToken.clear();
            m_blockEndToken.clear();
            break;
        case Language::Cpp:
        default:
            m_lineCommentToken = QStringLiteral("//");
            m_blockStartToken  = QStringLiteral("/*");
            m_blockEndToken    = QStringLiteral("*/");
            break;
    }
}

QString CppHighlighter::highlightedWord() const
{
    return m_highlightedWord;
}

void CppHighlighter::setHighlightedWord(const QString &word)
{
    if (m_highlightedWord == word) return;

    const QString oldWord = m_highlightedWord;
    m_highlightedWord = word;
    m_wordRegex = (word.trimmed().isEmpty() || word.length() <= 1)
    ? QRegularExpression()
    : QRegularExpression(QStringLiteral("\\b") + QRegularExpression::escape(word)
    + QStringLiteral("\\b"));
    emit highlightedWordChanged();

    QTextDocument *doc = document();
    if (!doc) return;

    // Only the lines that contain the old or the new word need to be redone
    QVector<QTextBlock> hits;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString t = b.text();
        if ((!oldWord.isEmpty() && t.contains(oldWord)) || (!word.isEmpty() && t.contains(word))) {
            hits.append(b);
            if (hits.size() > 300) break;
        }
    }
    if (hits.size() > 300) {      // a very common word: one full pass is cheaper
        rehighlight();
        return;
    }
    for (const QTextBlock &b : std::as_const(hits))
        rehighlightBlock(b);
}

int CppHighlighter::cursorPosition() const
{
    return m_cursorPosition;
}

void CppHighlighter::setCursorPosition(int pos)
{
    if (m_cursorPosition != pos) {
        m_cursorPosition = pos;
        emit cursorPositionChanged();
        updateBracketMatching();
    }
}

void CppHighlighter::clearBracketMatches()
{
    int oldPos1 = m_matchedBracket1;
    int oldPos2 = m_matchedBracket2;

    m_matchedBracket1 = -1;
    m_matchedBracket2 = -1;

    if (!document()) return;

    if (oldPos1 >= 0) {
        QTextBlock b = document()->findBlock(oldPos1);
        if (b.isValid()) rehighlightBlock(b);
    }
    if (oldPos2 >= 0) {
        QTextBlock b = document()->findBlock(oldPos2);
        if (b.isValid()) rehighlightBlock(b);
    }
}

// mask[i] is true where text[i] is real code (not inside a string or comment).
// Uses the same rules as applyCommentFormats(), and the previous block's state
// to know whether this block starts inside a /* */ comment.
void CppHighlighter::codeMask(const QTextBlock &block, QList<bool> &mask) const
{
    const QString text = block.text();
    const int n = text.length();
    mask.fill(true, n);

    const bool hasBlock = !m_blockStartToken.isEmpty();
    const QTextBlock prev = block.previous();
    bool inBlock = hasBlock && prev.isValid() && prev.userState() == 1;
    const QStringView view(text);
    int i = 0;

    auto kill = [&mask](int from, int to) {
        for (int k = from; k < to; ++k) mask[k] = false;
    };

        while (i < n) {
            if (inBlock) {
                const int end = text.indexOf(m_blockEndToken, i);
                if (end == -1) { kill(i, n); return; }
                const int stop = end + m_blockEndToken.length();
                kill(i, stop);
                i = stop;
                inBlock = false;
                continue;
            }

            const QChar c = text.at(i);
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                int j = i + 1;
                while (j < n && text.at(j) != c) {
                    if (text.at(j) == QLatin1Char('\\')) ++j;
                    ++j;
                }
                const int stop = qMin(j + 1, n);
                kill(i, stop);
                i = stop;
                continue;
            }

            if (hasBlock && view.mid(i, m_blockStartToken.length()) == m_blockStartToken) {
                kill(i, i + m_blockStartToken.length());
                i += m_blockStartToken.length();
                inBlock = true;
                continue;
            }

            if (!m_lineCommentToken.isEmpty()
                && view.mid(i, m_lineCommentToken.length()) == m_lineCommentToken) {
                kill(i, n);
                return;
            }
            ++i;
        }
}

// Document position of the bracket matching the one at (block, col), or -1.
int CppHighlighter::findPartner(QTextBlock block, int col) const
{
    static const QString opens = QStringLiteral("([{");
    static const QString closes = QStringLiteral(")]}");

    const QChar same = block.text().at(col);
    const int oi = opens.indexOf(same);
    const bool forward = oi >= 0;
    const QChar other = forward ? closes.at(oi) : opens.at(closes.indexOf(same));
    const int step = forward ? 1 : -1;

    constexpr int kMaxScan = 200000;   // characters; keeps huge files responsive
    int scanned = 0;
    int depth = 0;
    bool first = true;

    while (block.isValid()) {
        const QString t = block.text();
        QList<bool> mask;
        codeMask(block, mask);

        int i = first ? col : (forward ? 0 : t.length() - 1);
        for (; i >= 0 && i < t.length(); i += step) {
            if (!mask.at(i)) continue;
            const QChar c = t.at(i);
            if (c == same) {
                ++depth;
            } else if (c == other) {
                if (--depth == 0)
                    return block.position() + i;
            }
        }

        scanned += t.length() + 1;
        if (scanned > kMaxScan) return -1;
        first = false;
        block = forward ? block.next() : block.previous();
    }
    return -1;
}

void CppHighlighter::updateBracketMatching()
{
    const int oldPos1 = m_matchedBracket1;
    const int oldPos2 = m_matchedBracket2;
    int new1 = -1;
    int new2 = -1;

    QTextDocument *doc = document();
    if (doc && m_cursorPosition >= 0) {
        // Like Kate: the bracket under the caret first, then the one before it
        const int candidates[2] = { m_cursorPosition, m_cursorPosition - 1 };
        for (int cand : candidates) {
            if (cand < 0 || cand >= doc->characterCount() - 1) continue;
            const QTextBlock b = doc->findBlock(cand);
            if (!b.isValid()) continue;

            const int col = cand - b.position();
            const QString t = b.text();
            if (col < 0 || col >= t.length()) continue;

            const QChar ch = t.at(col);
            if (QStringLiteral("()[]{}").indexOf(ch) == -1) continue;

            QList<bool> mask;
            codeMask(b, mask);
            if (!mask.at(col)) continue;       // bracket is inside a string/comment

            const int partner = findPartner(b, col);
            if (partner != -1) {
                new1 = cand;
                new2 = partner;
                break;
            }
        }
    }

    if (new1 == oldPos1 && new2 == oldPos2)
        return;

    m_matchedBracket1 = new1;
    m_matchedBracket2 = new2;

    if (!doc) return;
    for (int pos : { oldPos1, oldPos2, new1, new2 }) {
        if (pos < 0) continue;
        const QTextBlock b = doc->findBlock(pos);
        if (b.isValid()) rehighlightBlock(b);
    }
}

// Scans the block for comments and paints them over whatever the regex rules
// produced. Skips comment tokens that appear inside string / char literals and
// carries block-comment state across lines via the block state.
void CppHighlighter::applyCommentFormats(const QString &text)
{
    const bool hasBlock = !m_blockStartToken.isEmpty();
    const int n = text.length();
    int i = 0;
    bool inBlock = hasBlock && previousBlockState() == 1;

    setCurrentBlockState(0);

    while (i < n) {
        if (inBlock) {
            const int end = text.indexOf(m_blockEndToken, i);
            if (end == -1) {
                setFormat(i, n - i, m_multiLineCommentFormat);
                setCurrentBlockState(1);
                return;
            }
            const int len = end + m_blockEndToken.length() - i;
            setFormat(i, len, m_multiLineCommentFormat);
            i += len;
            inBlock = false;
            continue;
        }

        const QChar c = text.at(i);

        // Skip string / char literals so "http://x" is not a comment
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            int j = i + 1;
            while (j < n && text.at(j) != c) {
                if (text.at(j) == QLatin1Char('\\')) ++j;
                ++j;
            }
            i = j + 1;
            continue;
        }

        // Block start must be tested before the line token (Lua: "--[[" vs "--")
        if (hasBlock && text.mid(i, m_blockStartToken.length()) == m_blockStartToken) {
            setFormat(i, m_blockStartToken.length(), m_multiLineCommentFormat);
            i += m_blockStartToken.length();
            inBlock = true;
            continue;
        }

        if (text.mid(i, m_lineCommentToken.length()) == m_lineCommentToken) {
            setFormat(i, n - i, m_singleLineCommentFormat);
            return;
        }

        ++i;
    }
}

void CppHighlighter::highlightBlock(const QString &text)
{
    // 1. Normal syntax coloring rules
    if (m_language == Language::Plain) {
        setCurrentBlockState(0);   // no syntax coloring; word and bracket highlight happen below
    } else {
        // 1. Normal syntax coloring rules
        for (const HighlightingRule &rule : std::as_const(m_highlightingRules)) {
            QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
            while (matchIterator.hasNext()) {
                QRegularExpressionMatch match = matchIterator.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }

        // 2. Comments last, so they override every rule above
        applyCommentFormats(text);
    }

    // 3. Word occurrence highlighting (background only, keeps the text color)
    if (!m_wordRegex.pattern().isEmpty() && text.contains(m_highlightedWord)) {
        QRegularExpressionMatchIterator it = m_wordRegex.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            for (int k = match.capturedStart(); k < match.capturedEnd(); ++k) {
                QTextCharFormat f = format(k);
                f.setBackground(QColor(m_isDark ? "#3e4451" : "#d6e4f7"));
                setFormat(k, 1, f);
            }
        }
    }

    // 4. Bracket / Brace pair matching highlight
    int blockStart = currentBlock().position();
    int blockEnd = blockStart + text.length();

    QTextCharFormat bracketFormat;
    bracketFormat.setBackground(QColor(m_isDark ? "#3d4451" : "#d6e4f7"));
    bracketFormat.setForeground(QColor(m_isDark ? "#00e6c3" : "#007a66"));
    bracketFormat.setFontWeight(QFont::Bold);

    if (m_matchedBracket1 >= blockStart && m_matchedBracket1 < blockEnd) {
        setFormat(m_matchedBracket1 - blockStart, 1, bracketFormat);
    }
    if (m_matchedBracket2 >= blockStart && m_matchedBracket2 < blockEnd) {
        setFormat(m_matchedBracket2 - blockStart, 1, bracketFormat);
    }
}
