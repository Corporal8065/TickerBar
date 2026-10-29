#pragma once
#include <map>
#include <string>
#include <vector>
#include "Alerts.hpp"
namespace tickerbar {
// Every persisted user setting, with v0.1 defaults. The JUCE app maps
// this 1:1 to a JSON settings file (app/Source/SettingsStore.h); the
// core tests pin the defaults.
enum class BackgroundStyle { CleanDark, MidnightKids, Flat };
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
    int windowX = 120, windowY = 24, windowW = 1040, windowH = 72; // wide enough for 3 coins + 2 context cells
    // Data
    int pollSec = 20;               // Coinbase stats poll cadence (spec: 20 s)
};
} // namespace tickerbar
