#ifndef OPENMW_MWGUI_QUICKBARRULES_H
#define OPENMW_MWGUI_QUICKBARRULES_H
#include <algorithm>
#include <cmath>
namespace MWGui { namespace QuickBarRules {
constexpr int Columns = 10, Rows = 6, Count = Columns * Rows;
inline int unlocked(int level) { return std::clamp(1 + std::max(1, level) / 30, 1, Rows); }
inline bool valid(int slot) { return slot >= 1 && slot <= Count; }
template<class Assigned> int select(int column, int current, int rows, bool cycle, Assigned assigned) {
    if (column < 0 || column >= Columns) return -1;
    rows = std::clamp(rows, 1, Rows); current = std::clamp(current, 0, rows - 1);
    for (int n = cycle ? 1 : 0; n < rows + (cycle ? 1 : 0); ++n) {
        const int slot = ((current + n) % rows) * Columns + column;
        if (assigned(slot)) return slot;
    }
    return -1;
}
struct Gesture {
    enum Result { None, Tap, Hold };
    bool pending = false, consumed = false; float age = 0.f;
    void press() { if (!pending) { pending = true; consumed = false; age = 0.f; } }
    void cancel() { pending = consumed = false; age = 0.f; }
    Result update(float dt, bool down) {
        if (!pending) return None;
        if (!down) { const bool tap = !consumed; cancel(); return tap ? Tap : None; }
        age += std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
        if (!consumed && age >= .55f) { consumed = true; return Hold; }
        return None;
    }
};
}}
#endif
