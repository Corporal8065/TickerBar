#pragma once
#include <cmath>
namespace tickerbar {
// MASCOT ANIMATION ENVELOPE (v0.3.0, item 18) — the timing math
// behind the Anime-theme mascot cell, shared shape with MeterBar's
// (a ~130 ms sine hop on a trigger; an excited window that a
// sustained "hot" condition can hold open). What TRIGGERS it differs
// per bar (MeterBar: beat onsets + loudness; TickerBar: alert crosses
// + price ticks; SessionBar: session state changes) — the triggers
// live in the apps; the envelope lives here so its shape is tested.
struct MascotAnim {
    long long bobStartMs = -1;     // -1 = never bobbed
    long long excitedUntilMs = -1; // excited while now < this
    void triggerBob(long long nowMs) { bobStartMs = nowMs; }
    void triggerExcited(long long nowMs, long long durMs = 2000) {
        if (nowMs + durMs > excitedUntilMs) excitedUntilMs = nowMs + durMs;
    }
    // Hop height in px: a half-sine bump peaking at -5 px, 130 ms.
    float bobPx(long long nowMs) const {
        if (bobStartMs < 0) return 0.f;
        const double t = (double)(nowMs - bobStartMs);
        if (t < 0.0 || t >= 130.0) return 0.f;
        return (float)(-5.0 * std::sin(3.141592653589793 * t / 130.0));
    }
    bool excited(long long nowMs) const { return nowMs < excitedUntilMs; }
};
} // namespace tickerbar
