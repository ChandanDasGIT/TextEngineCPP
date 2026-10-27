#pragma once
#include <string>
#include <vector>
#include "../inputevents/inputevents.h"

// A modal box with 2 or 3 buttons.
// This class only keeps the state: which button is selected and what each key does. How the box looks and
// where its buttons are is up to the front end, which reads title(), lines(), buttons() and selectedButton().
// The first letter of every button label can be chosen by typing it: the front end may underline it.
class Dialog {
public:
    // For the standard box the buttons are, in order: DontSave, Cancel, Save.
    // For custom boxes the result is given by position (1st = DontSave, 2nd = Cancel, 3rd = Save): use lastChoice().
    enum class Result { None, DontSave, Cancel, Save };

    // The "unsaved changes" box: Don't save, Cancel, Save (Save is selected).
    void show(const std::string& title, const std::vector<std::string>& lines);

    // Any box with 2 or 3 buttons.
    // defaultButton is selected at first and used by Enter; escapeButton is what Esc chooses.
    void show(const std::string& title, const std::vector<std::string>& lines,
              const std::vector<std::string>& buttons, int defaultButton, int escapeButton);

    bool active() const { return m_active; }
    int lastChoice() const { return m_last; }     // index of the button that closed the box

    // ---- what the front end draws ----
    const std::string& title() const { return m_title; }
    const std::vector<std::string>& lines() const { return m_lines; }
    const std::vector<std::string>& buttons() const { return m_labels; }   // 2 or 3 labels
    int selectedButton() const { return m_selected; }                      // the one Enter would choose

    // ---- input: each call returns the chosen button, or None while the box is still open ----
    Result handleKey(const Input::KeyEvent& key);      // Esc, Enter, Left / Right, Tab, Shift+Tab, Ctrl+S (standard box)
    Result handleText(const std::string& text);        // letters: the first letter of a button (or D / N / C / S)
    Result clickButton(int index);                     // the front end found which button was clicked (0-based)

private:
    Result choose(int button);
    void move(int delta);

    bool m_active = false;
    bool m_custom = false;
    std::string m_title;
    std::vector<std::string> m_lines;
    std::vector<std::string> m_labels;
    int m_count = 3;
    int m_selected = 2;
    int m_escape = 1;
    int m_last = -1;
};
