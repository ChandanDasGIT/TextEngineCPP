#pragma once
#include <sys/types.h>
#include <string>
#include <vector>

// A modal box with 2 or 3 buttons.
class Dialog {
public:
    // For the standard box the buttons are, in order: DontSave, Cancel, Save.
    // For custom boxes the result is given by position (1st = DontSave, 2nd = Cancel, 3rd = Save): use lastChoice().
    enum class Result { None, DontSave, Cancel, Save };

    // The "unsaved changes" box: Don't save, Cancel, Save (Save is selected).
    void show(const std::string& title, const std::vector<std::string>& lines);

    // Any box with 2 or 3 buttons. Each button can be chosen with the first letter of its label.
    // defaultButton is selected at first and used by Enter; escapeButton is what Esc chooses.
    void show(const std::string& title, const std::vector<std::string>& lines,
              const std::vector<std::string>& buttons, int defaultButton, int escapeButton);

    bool active() const { return m_active; }
    int lastChoice() const { return m_last; }     // index of the button that closed the box

    // Gives raw terminal input to the dialog. Returns the chosen button, or None while it is still open.
    Result feed(const char* buf, ssize_t n);

    // The box, as terminal output. Call it on every redraw while the dialog is active.
    std::string draw(int W, int H);

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
    int m_btnRow = 0;            // screen row of the buttons (1-based), set by draw()
    int m_hitX[3] = {0, 0, 0};   // first column of each button (1-based), set by draw()
    int m_hitW[3] = {0, 0, 0};
};