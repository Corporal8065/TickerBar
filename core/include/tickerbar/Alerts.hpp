#pragma once
#include <cmath>
#include <limits>
namespace tickerbar {
// Price-cross alerts. The tracker is a PURE state machine — same inputs,
// same events, no clocks, no I/O — so the unit suite grades it exactly.
//
// Semantics (pinned by tests):
//  - A level is disabled when it is NaN.
//  - The FIRST valid price only positions the tracker; it never fires.
//    (An alert set below the current price must not "fire" at startup —
//    nothing crossed, the price was simply already there.)
//  - CrossedAbove fires on the tick where price moves from <= level
//    to > level. It then stays quiet until price returns to <= level
//    (re-arm) and crosses again.
//  - CrossedBelow mirrors: fires on >= level to < level.
//  - NaN prices are ignored entirely (no state change).
struct AlertLevels {
    double above = std::numeric_limits<double>::quiet_NaN();
    double below = std::numeric_limits<double>::quiet_NaN();
    bool anySet() const { return !std::isnan(above) || !std::isnan(below); }
};

enum AlertEvent : unsigned {
    AlertNone = 0,
    AlertCrossedAbove = 1u << 0,
    AlertCrossedBelow = 1u << 1,
};

struct AlertTracker {
    bool primed = false;
    bool wasAbove = false; // last price strictly above the 'above' level
    bool wasBelow = false; // last price strictly below the 'below' level

    unsigned step(const AlertLevels& lv, double price) {
        if (std::isnan(price)) return AlertNone;
        unsigned ev = AlertNone;
        if (!primed) {
            primed = true;
            wasAbove = !std::isnan(lv.above) && price > lv.above;
            wasBelow = !std::isnan(lv.below) && price < lv.below;
            return AlertNone;
        }
        if (!std::isnan(lv.above)) {
            const bool nowAbove = price > lv.above;
            if (nowAbove && !wasAbove) ev |= AlertCrossedAbove;
            wasAbove = nowAbove;
        }
        if (!std::isnan(lv.below)) {
            const bool nowBelow = price < lv.below;
            if (nowBelow && !wasBelow) ev |= AlertCrossedBelow;
            wasBelow = nowBelow;
        }
        return ev;
    }
    // When the user edits levels, drop positional memory so the next
    // tick re-primes against the new levels (no stale crossings).
    void reset() { primed = false; wasAbove = wasBelow = false; }
};
} // namespace tickerbar
