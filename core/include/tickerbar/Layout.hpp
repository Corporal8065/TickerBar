#pragma once
#include <algorithm>
namespace tickerbar {
// COIN-CELL TOP-ROW LAYOUT — v0.2.0 fix for Jackson's field report:
// at smaller bar widths the 24h % text truncated ("-1....") because
// the symbol took a fixed 64 px and the % got whatever was left.
// The rule now: THE % IS NEVER TRUNCATED. The percent text is measured
// first and reserved in full; the symbol takes what remains (and is
// the only thing that may clip). Pure arithmetic on measured widths —
// the app passes juce::Font::getStringWidthFloat() values in, the
// tests pin the arithmetic here.
//
// The supported-width guarantee: the bar enforces a minimum window
// width (app: MainComponent::minimumBarWidth) built from
// minCoinCellWidth() per watched cell plus the fixed chrome, so a
// supported layout always has room for symbol + gap + full %.

struct TopRowLayout {
    int symbolW = 0; // px available to the symbol text (may clip the symbol)
    int pctW = 0;    // px reserved for the % text — always its full measured width
    int gap = 0;     // px between symbol and %
};

// Minimum symbol room worth keeping before the layout is considered
// degenerate; below this the % still wins (it shrinks last — never).
inline constexpr int kMinSymbolPx = 24;
inline constexpr int kTopRowGapPx = 6;
// Horizontal padding inside a coin cell (9 px each side in the paint).
inline constexpr int kCellPadPx = 18;

// Lay out the top row of a cell whose inner content width is
// contentW px. pctTextW / symbolTextW are the measured full widths of
// the two strings at their render fonts.
inline TopRowLayout coinCellTopRow(int contentW, int /*symbolTextW*/, int pctTextW) {
    TopRowLayout out;
    if (contentW <= 0) return out;
    // The % takes its full measured width first, up to the whole row.
    out.pctW = std::min(contentW, std::max(0, pctTextW));
    const int rest = contentW - out.pctW;
    // The symbol takes what remains; the gap is kept only when there
    // is room for it AND at least a sliver of symbol — the gap gives
    // before the % ever does.
    if (rest > kTopRowGapPx) { out.gap = kTopRowGapPx; out.symbolW = rest - kTopRowGapPx; }
    else { out.gap = 0; out.symbolW = rest; }
    return out;
}

// True when the layout gives the % its full measured width — the
// invariant the field-test fix exists to guarantee.
inline bool pctFullyVisible(const TopRowLayout& l, int pctTextW) {
    return l.pctW >= pctTextW;
}

// The narrowest coin cell (outer width, px) in which the symbol keeps
// at least kMinSymbolPx and the % is fully visible.
inline int minCoinCellWidth(int /*symbolTextW*/, int pctTextW) {
    return kCellPadPx + kMinSymbolPx + kTopRowGapPx + std::max(0, pctTextW);
}

// The narrowest cell in which BOTH texts show in full.
inline int fullCoinCellWidth(int symbolTextW, int pctTextW) {
    return kCellPadPx + std::max(0, symbolTextW) + kTopRowGapPx + std::max(0, pctTextW);
}
} // namespace tickerbar
