#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include "../textbuffer/textbuffer.h"
#include "languagedef.h"

// ---- one include per language. To add a language, add its header here. ----
#include "cppsyntax.h"
#include "jssyntax.h"
#include "rustsyntax.h"
#include "qmlsyntax.h"
#include "fishsyntax.h"
#include "shsyntax.h"
#include "cmakesyntax.h"
#include "luasyntax.h"

class SyntaxHighlighter {
public:
    void setLanguageFor(const std::string& fileName);   // picks the language by file name or extension
    void setLanguage(const LanguageDef* def);           // nullptr = plain text
    const LanguageDef* language() const { return m_lang; }

    // Call before drawing. Scans again from the first changed line.
    void update(const TextBuffer& b);
    void reset();

    Token tokenAt(size_t line, size_t col) const;

private:
    struct State {
        enum Kind : unsigned char { Normal, InBlock, InString, InRaw, InLong };
        Kind kind = Normal;
        char quote = 0;     // InString: the quote that opened it
        int hashes = 0;     // InRaw: number of '#'.  InLong: number of '=' in the brackets
        int depth = 0;      // InBlock: nesting depth
        bool comment = false;   // InLong: true for a comment, false for a string
    };
    State scanLine(const std::string& s, State st, bool firstLine, std::vector<Token>& out) const;

    std::unordered_set<std::string> collectIds(const TextBuffer& b) const;
    const LanguageDef* m_lang = nullptr;
    std::unordered_set<std::string> m_kw, m_types, m_consts, m_builtins, m_mods, m_intro;
    std::unordered_set<std::string> m_ids;         // QML: every name used in "id: name"
    bool m_dirty = true;
    unsigned long long m_rev = 0;                  // revision of the text that was scanned (0 = none)
    std::vector<std::string> m_prev;               // the text that was scanned
    std::vector<std::vector<Token>> m_tokens;      // one token per byte, per line
    std::vector<State> m_end;                      // scanner state at the end of each line
};
