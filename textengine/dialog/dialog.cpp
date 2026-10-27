#include "dialog.h"
#include <algorithm>
#include <cctype>

namespace {
const Dialog::Result kResults[3] = {Dialog::Result::DontSave, Dialog::Result::Cancel, Dialog::Result::Save};
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

Dialog::Result Dialog::handleKey(const Input::KeyEvent& k) {
    using Input::Key;
    if (!m_active) return Result::None;
    switch (k.key) {
        case Key::Escape: return choose(m_escape);
        case Key::Enter: return choose(m_selected);
        case Key::Right: move(1); break;
        case Key::Left: move(-1); break;
        case Key::Tab: move(k.mods.shift ? -1 : 1); break;
        case Key::Char:
            if (k.mods.ctrl && !m_custom && k.ch == 's') return choose(2);      // Ctrl+S saves in the standard box
            break;
        default: break;
    }
    return Result::None;
}

Dialog::Result Dialog::handleText(const std::string& text) {
    if (!m_active) return Result::None;
    for (char ch : text) {
        int l = std::tolower(static_cast<unsigned char>(ch));
        if (m_custom) {                                                    // the first letter of a button
            for (int k = 0; k < m_count; ++k) {
                const std::string& label = m_labels[static_cast<size_t>(k)];
                if (!label.empty() && std::tolower(static_cast<unsigned char>(label[0])) == l) return choose(k);
            }
            continue;
        }
        if (l == 'd' || l == 'n') return choose(0);
        if (l == 'c') return choose(1);
        if (l == 's') return choose(2);
    }
    return Result::None;
}

Dialog::Result Dialog::clickButton(int index) {
    if (!m_active || index < 0 || index >= m_count) return Result::None;
    return choose(index);
}
