#pragma once
#include <string>
#include <vector>
#include <tickerbar/Arrange.hpp>
// Screen-space management for the docked bar — PORTED from MeterBar
// v0.18 (same service, same semantics), extended with a bottom edge
// because TickerBar docks top OR bottom:
//  1) RESERVE — register the bar's window as a Windows AppBar at its
//     docked edge, so Windows shrinks that monitor's work area and
//     maximised windows stop at the bar instead of sliding under it.
//     The mechanism the taskbar itself uses.
//  2) ARRANGE — a one-shot sweep that slides every other top-level
//     window on the bar's monitor into the free area (fitting rule =
//     tickerbar::arrangeFit / arrangeFitAbove, unit-tested), with
//     Restore putting them back.
// Windows: the real implementation lives in ScreenSpace.cpp.
// Other platforms: every call is a no-op and statusInfo() says so —
// the stub idiom, so the UI never branches on platform and the feature
// reads honestly as unavailable there.
class ScreenSpace {
public:
    ScreenSpace();
    ~ScreenSpace();
    // Register hwnd as an AppBar at `edge` (0 = top, 1 = bottom)
    // occupying the given rect in PHYSICAL pixels (GetWindowRect —
    // AppBar rects are physical; per-monitor DPI makes a logical rect
    // land at the wrong size). Idempotent for an unchanged rect (the UI
    // re-syncs several times a second); a different rect/edge
    // re-registers cleanly. False = refused.
    bool reserve(void* hwnd, int edge, int physX, int physY, int physW, int physH);
    void release(); // ABM_REMOVE; safe to call at any time
    bool isReserved() const;
    struct SavedWindow { void* hwnd; tickerbar::IntRect rect; };
    // Fit every other top-level window on hwnd's monitor into the area
    // beyond a strip of stripPhysPx pixels at the given edge. Skips:
    // the bar itself, the shell, minimised, maximised (the AppBar owns
    // those), cloaked and tool windows. Originals are remembered for
    // restore(). Returns how many windows were actually moved.
    int arrange(void* hwnd, int stripPhysPx, int edge);
    int restore(); // put the remembered windows back; returns the count
    std::string statusInfo() const; // one status line for diagnostics
private:
    struct Impl; Impl* impl_ = nullptr;
};
