#include "textstats.h"
#include <cctype>

TextStats computeStats(const std::string& text) {
    TextStats s;
    bool inWord = false;
    bool lineHasText = false;
    bool prevLineBlank = true;

    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        bool continuation = (c & 0xC0) == 0x80;       // inside a multi-byte UTF-8 character
        bool space = std::isspace(c) != 0;

        if (!continuation) {
            ++s.characters;
            if (!space) ++s.charactersNoSpaces;
        }

        if (space) {
            inWord = false;
        } else {
            if (!inWord) { ++s.words; inWord = true; }
            lineHasText = true;
        }

        if (c == '\n') {
            ++s.lines;
            if (lineHasText && prevLineBlank) ++s.paragraphs;
            prevLineBlank = !lineHasText;
            lineHasText = false;
        }
    }
    if (lineHasText && prevLineBlank) ++s.paragraphs;

    s.readingMinutes = (s.words + 199) / 200;
    return s;
}
