#include "tabinput.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

TabAction act(TabAction::Kind k, int a = 0) {
    TabAction t;
    t.kind = k;
    t.a = a;
    return t;
}

bool keyAction(int key, bool shift, bool alt, bool ctrl, bool tabKeys, TabAction& out) {
    if (key >= 'A' && key <= 'Z') { key += 32; shift = true; }

    if (ctrl && !alt) {
        if (key == 'q') { out = act(TabAction::Quit); return true; }
        if (!tabKeys) return false;
        if (key == 't' && !shift) { out = act(TabAction::New); return true; }
        if (key == 'w' && !shift) { out = act(TabAction::Close); return true; }
        if (key == 9) { out = act(shift ? TabAction::Prev : TabAction::Next); return true; }
        return false;
    }
    if (tabKeys && alt && !ctrl && !shift) {
        if (key >= '1' && key <= '8') { out = act(TabAction::Goto, key - '1'); return true; }
        if (key == '9') { out = act(TabAction::GotoLast); return true; }
        if (key == 't') { out = act(TabAction::Reopen); return true; }
    }
    return false;
}

bool decodeMods(int mod, bool& shift, bool& alt, bool& ctrl) {
    int bits = mod > 1 ? mod - 1 : 0;
    if (bits & ~(1 | 2 | 4 | 64 | 128)) return false;     // super / hyper / meta: not ours
    shift = (bits & 1) != 0;
    alt = (bits & 2) != 0;
    ctrl = (bits & 4) != 0;
    return true;
}

// ESC [ key ; mod u   |   ESC [ 27 ; mod ; key ~   |   ESC [ 5/6 ; mod ~
bool parseCsiKey(const std::string& params, char fin, bool tabKeys, TabAction& out) {
    std::vector<int> nums;
    size_t k = 0;
    while (true) {
        size_t e = params.find(';', k);
        if (e == std::string::npos) e = params.size();
        nums.push_back(std::atoi(params.substr(k, e - k).c_str()));
        if (e >= params.size()) break;
        k = e + 1;
    }

    int key = -1, mod = 1;
    if (fin == 'u') {
        key = nums[0];
        if (nums.size() > 1) mod = nums[1];
    } else if (fin == '~' && nums[0] == 27 && nums.size() >= 3) {
        mod = nums[1];
        key = nums[2];
    } else if (fin == '~' && (nums[0] == 5 || nums[0] == 6) && nums.size() > 1) {
        bool shift, alt, ctrl;
        if (!tabKeys || !decodeMods(nums[1], shift, alt, ctrl)) return false;
        bool down = nums[0] == 6;
        if (ctrl && !alt && !shift) {                                                 // Ctrl+PageUp / PageDown: switch tab
            out = act(down ? TabAction::Next : TabAction::Prev);
            return true;
        }
        if (alt && shift && !ctrl) {                                                  // Alt+Shift+PageUp / PageDown: move tab
            out = act(down ? TabAction::MoveRight : TabAction::MoveLeft);
            return true;
        }
        return false;
    } else {
        return false;
    }

    bool shift, alt, ctrl;
    if (!decodeMods(mod, shift, alt, ctrl)) return false;
    return keyAction(key, shift, alt, ctrl, tabKeys, out);
}

}

namespace TabInput {

std::vector<InputPiece> split(const char* buf, ssize_t n, bool tabKeys, int barRows, bool dragging) {
    std::vector<InputPiece> out;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return;
        InputPiece p;
        p.data = cur;
        out.push_back(p);
        cur.clear();
    };
    auto emit = [&](const TabAction& a) {
        flush();
        InputPiece p;
        p.isAction = true;
        p.action = a;
        out.push_back(p);
    };

    ssize_t i = 0;
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(buf[i]);

        if (c == 27) {
            if (i + 1 >= n) { cur += buf[i]; ++i; continue; }                  // the Esc key
            char nx = buf[i + 1];

            if (nx == '[') {
                ssize_t j = i + 2;
                while (j < n && !(buf[j] >= 0x40 && buf[j] <= 0x7E)) ++j;
                if (j >= n) { cur.append(buf + i, static_cast<size_t>(n - i)); break; }   // incomplete: pass it on
                std::string params(buf + i + 2, buf + j);
                char fin = buf[j];

                if (!params.empty() && params[0] == '<') {                      // mouse: ESC [ < b ; x ; y M/m
                    int b, x, y;
                    if (sscanf(params.c_str() + 1, "%d;%d;%d", &b, &x, &y) == 3) {
                        int button = b & ~(4 | 8 | 16);
                        if (dragging) {                                         // a tab is being dragged: the bar owns the mouse
                            if (fin == 'm') emit(act(TabAction::BarRelease));
                            else if (button == 32) emit(act(TabAction::BarDrag, x - 1));
                            i = j + 1;
                            continue;
                        }
                        bool onBar = y <= barRows;
                        if (onBar && fin == 'M' && (button == 0 || button == 1 || button == 64 || button == 65)) {
                            if (button == 0) emit(act(TabAction::BarClick, x - 1));
                            else if (button == 1) emit(act(TabAction::BarMiddle, x - 1));
                            else emit(act(TabAction::BarWheel, button == 64 ? -1 : 1));
                            i = j + 1;
                            continue;
                        }
                        if (onBar && fin == 'M' && button != 32) { i = j + 1; continue; }   // other presses on the bar: ignored
                        int ny = std::max(y - barRows, 1);                      // motion / release over the bar stick to row 1
                        char head[64];
                        snprintf(head, sizeof head, "\x1b[<%d;%d;%d%c", b, x, ny, fin);
                        cur += head;
                        i = j + 1;
                        continue;
                    }
                } else {
                    TabAction a;
                    if (parseCsiKey(params, fin, tabKeys, a)) { emit(a); i = j + 1; continue; }
                }
                cur.append(buf + i, static_cast<size_t>(j + 1 - i));
                i = j + 1;
                continue;
            }

            if (nx == 'O') {                                                    // ESC O x: function keys
                ssize_t len = std::min<ssize_t>(3, n - i);
                cur.append(buf + i, static_cast<size_t>(len));
                i += len;
                continue;
            }

            TabAction a;                                                        // ESC + letter: Alt+letter
            if (keyAction(static_cast<unsigned char>(nx), false, true, false, tabKeys, a)) {
                emit(a);
                i += 2;
                continue;
            }
            cur += buf[i];
            ++i;
            continue;
        }

        if (c == 0x11) emit(act(TabAction::Quit));                              // Ctrl+Q
        else if (tabKeys && c == 0x14) emit(act(TabAction::New));               // Ctrl+T
        else if (tabKeys && c == 0x17) emit(act(TabAction::Close));             // Ctrl+W
        else cur += buf[i];
        ++i;
    }
    flush();
    return out;
}

}