#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "textengine/inputevents/inputevents.h"   // adjust to your folder name if it isn't "engine"

// TEST HARNESS ONLY. Turns the bytes a terminal sends into the engine's input types.
// The engine never sees escape codes: this file is the only place that knows them.
//
// Expects the terminal to be set up like test.cpp does:
//   \x1b[?1000h\x1b[?1002h\x1b[?1006h   mouse press / drag / wheel, SGR format
//   \x1b[?2004h                          bracketed paste
//   \x1b[>1u                             kitty keyboard protocol (Ctrl+letter arrives as CSI u)
// Terminals without the kitty protocol also work: Ctrl+letter then arrives as a control byte.

// A mouse event as the terminal reports it. The decoder cannot know the screen layout, so the
// caller turns x / y into an Input::MouseEvent (region, document position, row ...).
struct RawMouse {
    Input::MouseEvent::Kind kind = Input::MouseEvent::Kind::Press;
    Input::Button button = Input::Button::None;
    Input::Mods mods;
    int x = 1;              // screen column, 1-based
    int y = 1;              // screen row, 1-based
    int clicks = 1;         // 1, 2 or 3 on a left Press (same cell, within 400 ms)
    int wheelRows = 0;      // Wheel: negative = up
};

struct TermInput {
    enum class Type { Key, Text, Paste, Mouse };
    Type type = Type::Key;
    Input::KeyEvent key;    // Key
    std::string text;       // Text and Paste (UTF-8)
    RawMouse mouse;         // Mouse
};

class TerminalDecoder {
public:
    // Give it what read() returned. A sequence cut in half is kept until the rest arrives.
    std::vector<TermInput> feed(const char* data, size_t n);

private:
    size_t parseOne(const std::string& b, size_t i, std::vector<TermInput>& out);   // 0 = incomplete
    void handleCsi(const std::string& params, char fin, std::vector<TermInput>& out);
    void handleMouse(const std::string& params, char fin, std::vector<TermInput>& out);

    std::string m_buf;          // bytes not decoded yet
    bool m_inPaste = false;
    std::string m_paste;        // text collected since the paste started

    long long m_lastClickMs = 0;
    int m_lastX = 0, m_lastY = 0;
    int m_lastClicks = 0;
};