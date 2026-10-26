#pragma once
#include <cstddef>
#include <string>

namespace Gutter {

// Number of screen columns the gutter takes. Grows as the line count gets more digits.
int width(size_t lineCount);

// Gutter text for one row, e.g. " 12 │". It is exactly width(lineCount) columns wide.
std::string label(size_t line, size_t lineCount);

}
