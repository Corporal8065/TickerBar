#pragma once
#include <string>
#include <vector>
#include <tickerbar/Arrange.hpp>
// Screen-space management for the docked bar — PORTED from MeterBar
// v0.18, now v0.2.0: edge-aware on all four edges and driven ONLY on
// ReserveMachine transitions (MeterBar v0.18.1/v0.19.1 semantics):
//  1) RESERVE — register the bar's window as a Windows AppBar at its
//     docked edge, so Windows shrinks that monitor's work area and
//     maximised windows stop at the bar instead of sliding under it.
//     The mechanism the taskbar itself uses.
//  2) ARRANGE — a one-shot sweep that slides every other top-level
//     window on the bar's monitor into the free area (fitting rule =
//     tickerbar::arrangeFit, unit-tested), with Restore putting them
//     back.
// Windows: the real implementation lives in ScreenSpace.cpp.
// Other platforms: every call is a no-op and statusInfo() says so —
// the stub idiom, so the UI never branches on platform and the feature
// reads honestly as unavailable there.
class ScreenSpace {
public:
    ScreenSpace();
    ~ScreenSpace();
    // Register hwnd as an AppBar at `edge` occupying the given rect in
    // PHYSICAL pixels (the ReserveMachine's requested strip rect).
    // Idempotent for an unchanged REQUESTED rect: the comparison is
    // against what we asked for, never against the rect Windows
    // granted after ABM_QUERYPOS (the system may trim the grant —
    // comparing against it could never come back equal and was half
    // of the v0.18 flicker engine). A different rect/edge re-registers
    // cleanly. False = refused.
    bool reserve(void* hwnd, tickerbar::ReserveEdge edge, int physX, int physY, int physW, int physH);
    void release(); // ABM_REMOVE; safe to call at any time
    bool isReserved() const;
    struct SavedWindow { void* hwnd; tickerbar::IntRect rect; };
    // Fit every other top-level window on hwnd's monitor into the area
    // beyond a strip of stripPhysPx pixels at the given edge. Skips:
    // the bar itself, the shell, minimised, maximised (the AppBar owns
    // those), cloaked and tool windows. Originals are remembered for
    // restore(). Returns how many windows were actually moved.
    int arrange(void* hwnd, int stripPhysPx, tickerbar::ReserveEdge edge);
    int restore(); // put the remembered windows back; returns the count
    std::string statusInfo() const; // one status line for diagnostics
private:
    struct Impl; Impl* impl_ = nullptr;
};
