#pragma once
namespace tickerbar {
// DATA COLOUR SCHEMES (v0.3.0) — user-selectable colour pairs for the
// DATA itself (gain/loss text, sparkline strokes), modelled on
// MeterBar's colour-map family: each map scheme's pair is two real
// stops of that map's LUT (see meters-app ColorMap.hpp) — the warm
// 0.75 stop for UP, the cool 0.25 stop for DOWN — so a scheme reads
// as the map it is named after. "Family" keeps the theme's own
// good/bad colours (the app passes those in; the core table carries
// the Midnight Kids defaults).
struct Rgb {
    int r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};
struct DataColorPair { Rgb up, down; };

inline constexpr int kDataColorSchemeCount = 5;
inline const char* dataColorSchemeName(int id) {
    switch (id) {
        case 1: return "Plasma";
        case 2: return "Inferno";
        case 3: return "Viridis";
        case 4: return "Magma";
        default: return "Family";
    }
}
inline DataColorPair dataColorPair(int id) {
    switch (id) {
        // Plasma LUT stops: .25 (126,3,168) / .75 (248,149,64)
        case 1: return { {248, 149, 64}, {126, 3, 168} };
        // Inferno LUT stops: .25 (87,16,110) / .75 (249,142,9)
        case 2: return { {249, 142, 9}, {87, 16, 110} };
        // Viridis LUT stops: .25 (59,82,139) / .75 (94,201,98)
        case 3: return { {94, 201, 98}, {59, 82, 139} };
        // Magma LUT stops: .25 (81,18,124) / .75 (252,136,97)
        case 4: return { {252, 136, 97}, {81, 18, 124} };
        // Family: Midnight Kids good/bad (the theme defaults).
        default: return { {47, 214, 138}, {254, 90, 60} };
    }
}
} // namespace tickerbar
