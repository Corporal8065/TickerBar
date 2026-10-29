#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "Alerts.hpp"
namespace tickerbar {
// Every persisted user setting, with v0.1 defaults. The JUCE app maps
// this 1:1 to a JSON settings file (app/Source/SettingsStore.h); the
// core tests pin the defaults.
enum class BackgroundStyle { CleanDark, MidnightKids, Flat, Pastel }; // Pastel (3) added v0.3.0 — the family ordinal MeterBar already published
enum class DockEdge { Top, Bottom, Left, Right };

struct TickerState {
    // Watchlist — Jackson/Vishona's default trio.
    std::vector<std::string> watchlist = { "BTC", "ETH", "HYPE" };
    // Price alerts per symbol (levels only; NaN = unset).
    std::map<std::string, AlertLevels> alerts;
    bool alertSound = false;       // soft click on a cross — default OFF (spec)
    bool alertToasts = true;       // v0.2.0: Windows toast on a cross — ON (Jackson's request); toggle in Settings
    bool linkEnabled = false;      // v0.2.0: family linking (snap/drag-follow/shared background) — opt-in, default OFF
    // Appearance — MeterBar family: Midnight Kids theme, styled
    // background, opacity slider (bar defaults fully solid: it is a
    // read-at-a-glance instrument, not a meter overlay).
    std::string themeName = "Midnight Kids";
    BackgroundStyle backgroundStyle = BackgroundStyle::MidnightKids;
    float backgroundOpacity = 1.0f; // 0.15..1
    // Window
    bool pinned = true;             // always-on-top: a ticker bar lives on screen
    bool reserveScreenSpace = false; // v0.1 port of MeterBar v0.18 AppBar — strictly opt-in, default OFF
    DockEdge dockEdge = DockEdge::Top;
    int windowX = 120, windowY = 24, windowW = 1040, windowH = 96; // v0.3.0: +30 for the top toolbar strip (pre-0.3.0 files migrate once, see uiVer)
    // Data
    int pollSec = 20;               // Coinbase stats poll cadence (spec: 20 s)
    // v0.3.0 parity-round additions
    int uiVer = 3;                  // settings schema marker; files from older builds read 0 and migrate once
    bool clickSounds = false;       // UI click sounds on bar buttons (MeterBar parity) — default OFF
    int dataColorScheme = 0;        // DataColors scheme ordinal (0 = Family)
    int bgEffect = 0;               // background effect: 0 styled, 1 visualizer particles, 2 reactive glow
    float vizHue = 0.83f, vizSat = 0.85f, vizSpread = 0.75f, vizCycle = 24.f; // visualizer/glow palette
    std::map<std::string, int> cellWidths; // per-symbol explicit cell width px (absent = auto-share)
    // Context cells (Fear & Greed / BTC dominance) — post-v0.3.0,
    // Vishona's request: each cell can be hidden, and each carries
    // its own explicit axis extent px (dragged via the divider
    // beside the cell, persisted like the coin widths; 0 = the
    // default extent from Layout.hpp). One value serves both
    // orientations, exactly like cellWidths (width when horizontal,
    // height when vertical).
    bool showFearGreed = true;
    bool showBtcDominance = true;
    int fngCellWidth = 0;
    int domCellWidth = 0;
};

// RESET LAYOUT TO DEFAULTS (v0.3.2, Vishona's request) — restores
// ONLY the layout/presentation fields: every cell visible again
// (the context toggles), every dragged cell extent forgotten
// (coin widths + context widths back to auto/default), and the
// coin order back to the factory BTC / ETH / HYPE sequence.
// Watchlist MEMBERSHIP is not layout: extra symbols are kept, in
// their current relative order, after the factory trio; alerts,
// poll interval, theme and every other setting are untouched.
inline void resetLayoutToDefaults(TickerState& s) {
    s.cellWidths.clear();
    s.fngCellWidth = 0;
    s.domCellWidth = 0;
    s.showFearGreed = true;
    s.showBtcDominance = true;
    const std::vector<std::string> factory = TickerState{}.watchlist;
    std::vector<std::string> ordered;
    for (auto& sym : factory)
        if (std::find(s.watchlist.begin(), s.watchlist.end(), sym) != s.watchlist.end())
            ordered.push_back(sym);
    for (auto& sym : s.watchlist)
        if (std::find(factory.begin(), factory.end(), sym) == factory.end())
            ordered.push_back(sym);
    s.watchlist = ordered;
}
} // namespace tickerbar
