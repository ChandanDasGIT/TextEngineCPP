#pragma once
#include <cstddef>
#include <string>

struct TextStats {
    size_t characters = 0;          // characters, newlines included (UTF-8 aware)
    size_t charactersNoSpaces = 0;  // without spaces, tabs and newlines
    size_t words = 0;               // groups of non-space characters
    size_t lines = 1;               // number of lines (an empty text has 1)
    size_t paragraphs = 0;          // blocks of text separated by blank lines
    size_t readingMinutes = 0;      // rounded up, at 200 words per minute
};

TextStats computeStats(const std::string& text);
