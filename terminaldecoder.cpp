#include "terminaldecoder.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace {

using Input::Key;
using Input::Mods;

const char* const kPasteEnd = "\x1b[201~";

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// "1;5" -> {1, 5}. A ':' sub-parameter (kitty) is cut off. A missing value is 0.
std::vector<int> splitParams(const std::string& s) {
    std::vector<int> v(1, 0);
    bool skip = false;
    for (char c : s) {
        if (c == ';') { v.push_back(0); skip = false; }
        else if (c == ':') skip = true;
        else if (c >= '0' && c <= '9' && !skip) v.back() = v.back() * 10 + (c - '0');
    }
    return v;
}

// Terminal modifier parameter: 1 + (shift 1, alt 2, ctrl 4)
Mods modsFromParam(int v) {
    Mods m;
    if (v > 1) { v -= 1; m.shift = v & 1; m.alt = v & 2; m.ctrl = v & 4; }
    return m;
}

std::string utf8Encode(char32_t c) {
    std::string s;
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) { s += static_cast<char>(0xC0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
    return s;
}

TermInput keyInput(Key k, Mods m = {}, char32_t ch = 0) {
    TermInput t;
    t.type = TermInput::Type::Key;
    t.key.key = k;
    t.key.ch = ch;
    t.key.mods = m;
    return t;
}

TermInput textInput(const std::string& s) {
    TermInput t;
    t.type = TermInput::Type::Text;
    t.text = s;
    return t;
}

std::string normalizeNewlines(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out += '\n';
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else out += s[i];
    }
    return out;
}

// Expected length of a UTF-8 sequence from its first byte (1 for anything that is not a lead byte).
size_t utf8Len(unsigned char c) {
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}

}  // namespace

std::vector<TermInput> TerminalDecoder::feed(const char* data, size_t n) {
    std::vector<TermInput> out;
    m_buf.append(data, n);

    size_t i = 0;
    while (i < m_buf.size()) {
        if (m_inPaste) {
            size_t end = m_buf.find(kPasteEnd, i);
            if (end == std::string::npos) {
                // keep the last few bytes: they may be the start of the end marker
                size_t keep = std::min<size_t>(5, m_buf.size() - i);
                m_paste.append(m_buf, i, m_buf.size() - i - keep);
                i = m_buf.size() - keep;
                break;
            }
            m_paste.append(m_buf, i, end - i);
            TermInput t;
            t.type = TermInput::Type::Paste;
            t.text = normalizeNewlines(m_paste);
            out.push_back(t);
            m_paste.clear();
            m_inPaste = false;
            i = end + 6;
            continue;
        }
        size_t used = parseOne(m_buf, i, out);
        if (used == 0) break;           // incomplete: wait for more bytes
        i += used;
    }
    m_buf.erase(0, i);
    return out;
}

size_t TerminalDecoder::parseOne(const std::string& b, size_t i, std::vector<TermInput>& out) {
    const unsigned char c = static_cast<unsigned char>(b[i]);

    if (c == 0x1b) {
        if (i + 1 >= b.size()) {                    // a lone Esc at the end of what we got
            out.push_back(keyInput(Key::Escape));
            return 1;
        }
        const char nx = b[i + 1];

        if (nx == '[') {                            // CSI  ESC [ params final
            size_t j = i + 2;
            while (j < b.size() && static_cast<unsigned char>(b[j]) >= 0x20 && static_cast<unsigned char>(b[j]) <= 0x3F) ++j;
            if (j >= b.size()) return 0;            // no final byte yet
            const char fin = b[j];
            std::string params = b.substr(i + 2, j - (i + 2));
            if (!params.empty() && params[0] == '<' && (fin == 'M' || fin == 'm')) handleMouse(params, fin, out);
            else handleCsi(params, fin, out);
            return j - i + 1;
        }

        if (nx == 'O') {                            // SS3  ESC O final (F1-F4, arrows in application mode)
            if (i + 2 >= b.size()) return 0;
            const char f = b[i + 2];
            switch (f) {
                case 'P': out.push_back(keyInput(Key::Function, {}, 1)); break;
                case 'Q': out.push_back(keyInput(Key::Function, {}, 2)); break;
                case 'R': out.push_back(keyInput(Key::Function, {}, 3)); break;
                case 'S': out.push_back(keyInput(Key::Function, {}, 4)); break;
                case 'A': out.push_back(keyInput(Key::Up)); break;
                case 'B': out.push_back(keyInput(Key::Down)); break;
                case 'C': out.push_back(keyInput(Key::Right)); break;
                case 'D': out.push_back(keyInput(Key::Left)); break;
                case 'H': out.push_back(keyInput(Key::Home)); break;
                case 'F': out.push_back(keyInput(Key::End)); break;
                default: break;
            }
            return 3;
        }

        if (nx == ']') {                            // OSC reply: skip to BEL or ESC \ .
            for (size_t j = i + 2; j < b.size(); ++j) {
                if (b[j] == '\a') return j - i + 1;
                if (b[j] == 0x1b) {
                    if (j + 1 >= b.size()) return 0;
                    if (b[j + 1] == '\\') return j - i + 2;
                }
            }
            return 0;
        }

        // ESC followed by a plain key = Alt + that key
        const unsigned char k = static_cast<unsigned char>(nx);
        Mods alt; alt.alt = true;
        if (k == 0x7f) { out.push_back(keyInput(Key::Backspace, alt)); return 2; }
        if (k == '\r' || k == '\n') { out.push_back(keyInput(Key::Enter, alt)); return 2; }
        if (k >= 0x20 && k < 0x7f) {
            char32_t ch = k;
            if (ch >= 'A' && ch <= 'Z') { alt.shift = true; ch = ch - 'A' + 'a'; }
            out.push_back(keyInput(Key::Char, alt, ch));
            return 2;
        }
        out.push_back(keyInput(Key::Escape));       // Esc alone, the next byte is parsed on its own
        return 1;
    }

    if (c == '\r' || c == '\n') { out.push_back(keyInput(Key::Enter)); return 1; }
    if (c == '\t') { out.push_back(keyInput(Key::Tab)); return 1; }
    if (c == 0x7f || c == 0x08) { out.push_back(keyInput(Key::Backspace)); return 1; }

    if (c < 0x20) {                                 // Ctrl+A .. Ctrl+Z (when the kitty protocol is not active)
        if (c >= 1 && c <= 26) {
            Mods ctrl; ctrl.ctrl = true;
            out.push_back(keyInput(Key::Char, ctrl, U'a' + (c - 1)));
        }
        return 1;
    }

    // A run of typed text
    size_t j = i;
    while (j < b.size()) {
        unsigned char x = static_cast<unsigned char>(b[j]);
        if (x < 0x20 || x == 0x7f || x == 0x1b) break;
        ++j;
    }
    if (j == b.size()) {                            // do not cut a character in half
        size_t lead = j;
        int back = 0;
        while (lead > i && (static_cast<unsigned char>(b[lead - 1]) & 0xC0) == 0x80 && back < 3) { --lead; ++back; }
        if (lead > i) {
            --lead;
            if (j - lead < utf8Len(static_cast<unsigned char>(b[lead]))) j = lead;
        }
        if (j == i) return 0;
    }
    out.push_back(textInput(b.substr(i, j - i)));
    return j - i;
}

void TerminalDecoder::handleCsi(const std::string& params, char fin, std::vector<TermInput>& out) {
    std::vector<int> p = splitParams(params);
    const int p0 = p[0];
    const Mods mods = modsFromParam(p.size() > 1 ? p[1] : 0);

    switch (fin) {
        case 'A': out.push_back(keyInput(Key::Up, mods)); return;
        case 'B': out.push_back(keyInput(Key::Down, mods)); return;
        case 'C': out.push_back(keyInput(Key::Right, mods)); return;
        case 'D': out.push_back(keyInput(Key::Left, mods)); return;
        case 'H': out.push_back(keyInput(Key::Home, mods)); return;
        case 'F': out.push_back(keyInput(Key::End, mods)); return;
        case 'P': out.push_back(keyInput(Key::Function, mods, 1)); return;
        case 'Q': out.push_back(keyInput(Key::Function, mods, 2)); return;
        case 'S': out.push_back(keyInput(Key::Function, mods, 4)); return;
        case 'Z': { Mods m = mods; m.shift = true; out.push_back(keyInput(Key::Tab, m)); return; }
        case '~': {
            switch (p0) {
                case 1: case 7: out.push_back(keyInput(Key::Home, mods)); return;
                case 2: out.push_back(keyInput(Key::Insert, mods)); return;
                case 3: out.push_back(keyInput(Key::Delete, mods)); return;
                case 4: case 8: out.push_back(keyInput(Key::End, mods)); return;
                case 5: out.push_back(keyInput(Key::PageUp, mods)); return;
                case 6: out.push_back(keyInput(Key::PageDown, mods)); return;
                case 11: case 12: case 13: case 14: case 15:
                    out.push_back(keyInput(Key::Function, mods, p0 - 10)); return;
                case 17: case 18: case 19: case 20: case 21:
                    out.push_back(keyInput(Key::Function, mods, p0 - 11)); return;
                case 23: case 24:
                    out.push_back(keyInput(Key::Function, mods, p0 - 12)); return;
                case 200: m_inPaste = true; m_paste.clear(); return;
                default: return;
            }
        }
        case 'u': {                                 // kitty: CSI code ; mods u
            Mods m = mods;
            switch (p0) {
                case 13: out.push_back(keyInput(Key::Enter, m)); return;
                case 9: out.push_back(keyInput(Key::Tab, m)); return;
                case 27: out.push_back(keyInput(Key::Escape, m)); return;
                case 127: out.push_back(keyInput(Key::Backspace, m)); return;
                default: break;
            }
            if (p0 < 32) return;
            char32_t ch = static_cast<char32_t>(p0);
            if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
            if (!m.ctrl && !m.alt) {                // plain typing
                char32_t typed = ch;
                if (m.shift && typed >= 'a' && typed <= 'z') typed = typed - 'a' + 'A';
                out.push_back(textInput(utf8Encode(typed)));
            } else {
                out.push_back(keyInput(Key::Char, m, ch));
            }
            return;
        }
        default: return;                            // focus events, cursor reports ... : ignored
    }
}

void TerminalDecoder::handleMouse(const std::string& params, char fin, std::vector<TermInput>& out) {
    std::vector<int> p = splitParams(params.substr(1));     // drop the '<'
    if (p.size() < 3) return;
    const int b = p[0];

    TermInput t;
    t.type = TermInput::Type::Mouse;
    RawMouse& m = t.mouse;
    m.x = p[1];
    m.y = p[2];
    m.mods.shift = b & 4;
    m.mods.alt = b & 8;
    m.mods.ctrl = b & 16;

    const int low = b & 3;
    if (b & 64) {                                   // wheel: 64 up, 65 down
        if (fin != 'M') return;
        m.kind = Input::MouseEvent::Kind::Wheel;
        m.wheelRows = (low == 1) ? 3 : -3;
        out.push_back(t);
        return;
    }
    if (low == 3) return;                           // motion with no button: not used
    m.button = low == 0 ? Input::Button::Left : low == 1 ? Input::Button::Middle : Input::Button::Right;

    if (fin == 'm') {
        m.kind = Input::MouseEvent::Kind::Release;
    } else if (b & 32) {
        m.kind = Input::MouseEvent::Kind::Drag;
    } else {
        m.kind = Input::MouseEvent::Kind::Press;
        if (m.button == Input::Button::Left) {
            long long now = nowMs();
            if (m.x == m_lastX && m.y == m_lastY && now - m_lastClickMs < 400) m_lastClicks = m_lastClicks % 3 + 1;
            else m_lastClicks = 1;
            m_lastClickMs = now;
            m_lastX = m.x;
            m_lastY = m.y;
            m.clicks = m_lastClicks;
        }
    }
    out.push_back(t);
}