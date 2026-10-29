#pragma once
#include <algorithm>
namespace tickerbar {
// ARRANGE MATH — ported from MeterBar v0.18 (meterbar::arrangeFit), the
// fitting rule behind the "Arrange windows" sweep in
// app/Source/ScreenSpace.cpp (Windows-only: EnumWindows +
// SetWindowPos). The rule itself is a PURE function with no OS calls,
// so the Linux test suite grades it exactly, even though the sweep that
// consumes it can only run on a Windows PC. Same division of labour as
// MeterBar: math here, platform plumbing in the app.

// Integer rectangle in pixels: x/y = top-left corner, w/h = size.
// On Windows these are PHYSICAL pixels (what GetWindowRect /
// GetMonitorInfo report); the tests use plain numbers.
struct IntRect {
    int x = 0, y = 0, w = 0, h = 0;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool operator==(const IntRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const IntRect& o) const { return !(*this == o); }
};

// Fit `window` inside `monitor` BELOW a reserved strip of stripPx pixels
// at the monitor's top edge (the docked bar's height). Rules, in order:
//  1. stripPx is clamped to [0, monitor.h]. A monitor with no free area
//     left gets the window back UNCHANGED — never make things worse.
//  2. A window already fully inside the free area is returned UNCHANGED.
//  3. Otherwise the window keeps as much size as fits and slides the
//     minimum distance to sit fully inside the free area.
inline IntRect arrangeFit(IntRect window, IntRect monitor, int stripPx) {
    if (window.w <= 0 || window.h <= 0 || monitor.w <= 0 || monitor.h <= 0) return window;
    const int strip = std::clamp(stripPx, 0, monitor.h);
    const IntRect freeArea{ monitor.x, monitor.y + strip, monitor.w, monitor.h - strip };
    if (freeArea.w <= 0 || freeArea.h <= 0) return window;
    IntRect out;
    out.w = std::min(window.w, freeArea.w);
    out.h = std::min(window.h, freeArea.h);
    out.x = std::clamp(window.x, freeArea.x, freeArea.right() - out.w);
    out.y = std::clamp(window.y, freeArea.y, freeArea.bottom() - out.h);
    return out;
}

// Bottom-edge variant (TickerBar can dock at the bottom): fit `window`
// inside `monitor` ABOVE a reserved strip of stripPx at the bottom.
inline IntRect arrangeFitAbove(IntRect window, IntRect monitor, int stripPx) {
    if (window.w <= 0 || window.h <= 0 || monitor.w <= 0 || monitor.h <= 0) return window;
    const int strip = std::clamp(stripPx, 0, monitor.h);
    const IntRect freeArea{ monitor.x, monitor.y, monitor.w, monitor.h - strip };
    if (freeArea.w <= 0 || freeArea.h <= 0) return window;
    IntRect out;
    out.w = std::min(window.w, freeArea.w);
    out.h = std::min(window.h, freeArea.h);
    out.x = std::clamp(window.x, freeArea.x, freeArea.right() - out.w);
    out.y = std::clamp(window.y, freeArea.y, freeArea.bottom() - out.h);
    return out;
}
} // namespace tickerbar
