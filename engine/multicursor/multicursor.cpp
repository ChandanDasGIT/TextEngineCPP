#include "multicursor.h"
#include <algorithm>

void MultiCursor::normalize(std::vector<Cursor>& cursors, size_t& main) {
    if (cursors.empty()) return;

    struct Item {
        Cursor c;
        bool isMain;
    };
    std::vector<Item> items;
    for (size_t i = 0; i < cursors.size(); ++i) items.push_back({cursors[i], i == main});

    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.c.start() != b.c.start()) return a.c.start() < b.c.start();
        return a.c.end() < b.c.end();
    });

    std::vector<Item> out;
    for (const Item& it : items) {
        bool touches = !out.empty() && !(out.back().c.end() < it.c.start());
        bool overlaps = touches && (it.c.start() < out.back().c.end() ||     // real overlap, or a caret that touches
                                    !out.back().c.hasSelection() || !it.c.hasSelection());
        if (overlaps) {                                                      // -> merge
            Item& last = out.back();
            Position s = last.c.start();                              // sorted, so last starts first
            Position e = last.c.end() < it.c.end() ? it.c.end() : last.c.end();
            Cursor merged = it.isMain ? it.c : last.c;
            if (merged.pos < merged.anchor) { merged.pos = s; merged.anchor = e; }
            else { merged.anchor = s; merged.pos = e; }
            last.c = merged;
            last.isMain = last.isMain || it.isMain;
        } else {
            out.push_back(it);
        }
    }

    cursors.clear();
    main = 0;
    for (size_t k = 0; k < out.size(); ++k) {
        cursors.push_back(out[k].c);
        if (out[k].isMain) main = k;
    }
}
