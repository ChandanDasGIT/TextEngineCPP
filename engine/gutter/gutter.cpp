#include "gutter.h"
#include <algorithm>

namespace {
int digitsOf(size_t n) {
    int d = 1;
    while (n >= 10) { n /= 10; ++d; }
    return d;
}
}

// layout: space + number (right-aligned) + space + bar
int Gutter::width(size_t lineCount) {
    return std::max(2, digitsOf(lineCount)) + 3;
}

std::string Gutter::label(size_t line, size_t lineCount) {
    size_t digits = static_cast<size_t>(width(lineCount) - 3);
    std::string num = std::to_string(line + 1);
    std::string s = " ";
    s.append(digits - num.size(), ' ');
    s += num;
    s += " │";
    return s;
}
