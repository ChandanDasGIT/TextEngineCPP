#include "dialog.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

const Dialog::Result kResults[3] = {Dialog::Result::DontSave, Dialog::Result::Cancel, Dialog::Result::Save};

size_t cols(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string clip(const std::string& s, size_t maxCols) {
    if (cols(s) <= maxCols) return s;
    std::string out;
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (n + 1 >= maxCols) break;
        out.append(s, i, len);
        i += len;
        ++n;
    }
    return out + "…";
}

std::string pad(const std::string& s, size_t width) {
    std::string t = clip(s, width);
    size_t c = cols(t);
    if (c < width) t.append(width - c, ' ');
    return t;
}

std::string at(int row, int col) { return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H"; }

std::string repeat(const std::string& s, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) out += s;
    return out;
}

}

void Dialog::show(const std::string& title, const std::vector<std::string>& lines,
                  const std::vector<std::string>& buttons, int defaultButton, int escapeButton) {
    m_active = true;
    m_custom = true;
    m_title = title;
    m_lines = lines;
    m_labels = buttons;
    if (m_labels.empty()) m_labels.push_back("OK");
    if (m_labels.size() > 3) m_labels.resize(3);
    m_count = static_cast<int>(m_labels.size());
    m_selected = std::max(0, std::min(defaultButton, m_count - 1));
    m_escape = std::max(0, std::min(escapeButton, m_count - 1));
    m_last = -1;
    m_btnRow = 0;
}

void Dialog::show(const std::string& title, const std::vector<std::string>& lines) {
    show(title, lines, {"Don't save", "Cancel", "Save"}, 2, 1);
    m_custom = false;
}

Dialog::Result Dialog::choose(int button) {
    m_active = false;
    m_last = button;
    return kResults[std::max(0, std::min(button, 2))];
}

void Dialog::move(int delta) { m_selected = (m_selected + delta + m_count) % m_count; }

Dialog::Result Dialog::feed(const char* buf, ssize_t n) {
    for (ssize_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(buf[i]);

        if (c == 27) {
            if (i + 1 >= n) return choose(m_escape);                       // the Esc key
            if (buf[i + 1] == '[') {
                ssize_t j = i + 2;
                while (j < n && !(buf[j] >= 0x40 && buf[j] <= 0x7E)) ++j;
                if (j >= n) return Result::None;
                std::string params(buf + i + 2, buf + j);
                char fin = buf[j];
                i = j;

                if (!params.empty() && params[0] == '<') {                 // mouse press on a button
                    int b = 0, x = 0, y = 0;
                    if (fin == 'M' && std::sscanf(params.c_str() + 1, "%d;%d;%d", &b, &x, &y) == 3 &&
                        (b & ~(4 | 8 | 16)) == 0) {
                        for (int k = 0; k < m_count; ++k)
                            if (y == m_btnRow && x >= m_hitX[k] && x < m_hitX[k] + m_hitW[k]) return choose(k);
                    }
                } else if (fin == 'C') {
                    move(1);
                } else if (fin == 'D' || fin == 'Z') {
                    move(-1);
                } else if (fin == 'u') {                                   // kitty style: ESC [ key ; mod u
                    int key = std::atoi(params.c_str());
                    size_t semi = params.find(';');
                    int mod = semi == std::string::npos ? 1 : std::atoi(params.c_str() + semi + 1);
                    bool shift = mod > 1 && ((mod - 1) & 1) != 0;
                    bool ctrl = mod > 1 && ((mod - 1) & 4) != 0;
                    if (key == 27) return choose(m_escape);
                    if (key == 13) return choose(m_selected);
                    if (key == 9) move(shift ? -1 : 1);
                    if (ctrl && !m_custom && (key == 's' || key == 'S')) return choose(2);
                }
                continue;
            }
            if (buf[i + 1] == 'O') { i += 2; continue; }                   // function keys: ignored
            ++i;                                                           // Alt+key: ignored
            continue;
        }

        if (c == 13 || c == 10) return choose(m_selected);
        if (c == 9) { move(1); continue; }
        if (c == 0x13 && !m_custom) return choose(2);                      // Ctrl+S
        int l = std::tolower(c);
        if (m_custom) {                                                    // the first letter of a button
            for (int k = 0; k < m_count; ++k)
                if (!m_labels[static_cast<size_t>(k)].empty() &&
                    std::tolower(static_cast<unsigned char>(m_labels[static_cast<size_t>(k)][0])) == l)
                    return choose(k);
            continue;
        }
        if (l == 'd' || l == 'n') return choose(0);
        if (l == 'c') return choose(1);
        if (l == 's') return choose(2);
    }
    return Result::None;
}

std::string Dialog::draw(int W, int H) {
    if (!m_active) return "";

    const size_t btnPad = 2, gap = 2;
    size_t btnW[3] = {0, 0, 0}, btnsTotal = 0;
    for (int k = 0; k < m_count; ++k) {
        btnW[k] = cols(m_labels[static_cast<size_t>(k)]) + 2 * btnPad;
        btnsTotal += btnW[k] + (k ? gap : 0);
    }

    size_t content = std::max<size_t>(30, cols(m_title));
    for (const std::string& l : m_lines) content = std::max(content, cols(l));
    size_t cap = W > 10 ? static_cast<size_t>(W) - 10 : 1;
    content = std::max(std::min(content, cap), btnsTotal);

    const size_t inner = content + 4;                                      // 2 columns of padding on each side
    const size_t boxW = inner + 2;
    const size_t boxH = m_lines.size() + 6;
    int x0 = std::max(1, (W - static_cast<int>(boxW)) / 2 + 1);
    int y0 = std::max(1, (H - static_cast<int>(boxH)) / 2 + 1);

    const std::string base = "\x1b[0m\x1b[48;5;236m\x1b[38;5;252m";
    const std::string border = "\x1b[38;5;75m";

    auto text = [&](int r, const std::string& inside) {
        return at(y0 + r, x0) + base + border + "│" + base + inside + border + "│\x1b[0m";
    };
    auto plain = [&](int r, const std::string& t, const char* style) {
        return text(r, std::string(style) + "  " + pad(t, content) + "  " + base);
    };

    std::string s;
    s += at(y0, x0) + base + border + "╭" + repeat("─", inner) + "╮\x1b[0m";
    s += plain(1, m_title, "\x1b[1m");
    s += plain(2, "", "");
    for (size_t k = 0; k < m_lines.size(); ++k) s += plain(3 + static_cast<int>(k), m_lines[k], "");
    s += plain(3 + static_cast<int>(m_lines.size()), "", "");

    // the buttons, centred
    int btnRowIdx = 4 + static_cast<int>(m_lines.size());
    size_t left = (inner - btnsTotal) / 2;
    size_t right = inner - btnsTotal - left;
    std::string row = base + std::string(left, ' ');
    size_t offset = 0;
    for (int k = 0; k < m_count; ++k) {
        row += (k == m_selected) ? "\x1b[48;5;75m\x1b[38;5;16m\x1b[1m" : "\x1b[48;5;240m\x1b[38;5;255m";
        std::string label = m_labels[static_cast<size_t>(k)];
        row += std::string(btnPad, ' ') + "\x1b[4m" + label.substr(0, 1) + "\x1b[24m" + label.substr(1) +
               std::string(btnPad, ' ');
        row += base;
        if (k < m_count - 1) row += std::string(gap, ' ');
        m_hitX[k] = x0 + 1 + static_cast<int>(left + offset);
        m_hitW[k] = static_cast<int>(btnW[k]);
        offset += btnW[k] + gap;
    }
    row += std::string(right, ' ');
    s += text(btnRowIdx, row);
    m_btnRow = y0 + btnRowIdx;

    s += at(y0 + static_cast<int>(boxH) - 1, x0) + base + border + "╰" + repeat("─", inner) + "╯\x1b[0m";
    return s;
}