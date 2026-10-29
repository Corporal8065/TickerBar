#pragma once
#include <algorithm>
#include <utility>
#include <vector>
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

// ------------------------------------------------------------------
// CELL WIDTH SOLVER + REORDER MATH (v0.3.0, items 9/10). The bar's
// cells share the strip: cells with a user-dragged explicit width
// keep it (clamped to minW), the rest split what remains equally
// (the last auto cell absorbs rounding so the row fills exactly).
inline std::vector<int> solveCellWidths(int total, int gap, const std::vector<int>& explicitW, int minW) {
    const int n = (int)explicitW.size();
    std::vector<int> out((size_t)n, 0);
    if (n == 0) return out;
    int avail = total - gap * (n - 1);
    if (avail < n) avail = n;
    int fixedSum = 0, autos = 0;
    for (int i = 0; i < n; ++i) {
        if (explicitW[(size_t)i] > 0) { out[(size_t)i] = explicitW[(size_t)i] < minW ? minW : explicitW[(size_t)i]; fixedSum += out[(size_t)i]; }
        else ++autos;
    }
    if (autos > 0) {
        int rest = avail - fixedSum;
        int each = rest / autos;
        if (each < minW) each = minW;
        int lastAuto = -1;
        for (int i = 0; i < n; ++i) if (explicitW[(size_t)i] <= 0) { out[(size_t)i] = each; lastAuto = i; }
        const int remainder = (avail - fixedSum) - each * autos;
        if (lastAuto >= 0 && remainder > 0) out[(size_t)lastAuto] += remainder;
    }
    return out;
}

// Dragging the divider between two adjacent cells: the pair's total
// is preserved; neither side goes below minW (or 1 in degenerate sums).
inline std::pair<int, int> adjustPairWidths(int w1, int w2, int delta, int minW) {
    const int sum = w1 + w2;
    int lo = minW, hi = sum - minW;
    if (hi < lo) { lo = sum / 2; hi = sum - lo; if (lo < 1) lo = 1; if (hi < lo) hi = lo; }
    int n1 = w1 + delta;
    if (n1 < lo) n1 = lo;
    if (n1 > hi) n1 = hi;
    return { n1, sum - n1 };
}

// Where a dragged cell would land: the count of sibling centres the
// pointer has passed = the insertion slot (0..n) in the sibling list
// WITHOUT the dragged cell.
inline int dropSlot(const std::vector<int>& centres, int pointer) {
    int slot = 0;
    for (int c : centres) if (pointer > c) ++slot;
    return slot;
}

// The index sequence after moving the item at `from` to position
// `to` (both in the ORIGINAL indexing; `to` is the slot in the list
// with the item removed, matching dropSlot).
inline std::vector<int> indexOrderAfterMove(int n, int from, int to) {
    std::vector<int> idx;
    for (int i = 0; i < n; ++i) if (i != from) idx.push_back(i);
    if (to < 0) to = 0;
    if (to > (int)idx.size()) to = (int)idx.size();
    idx.insert(idx.begin() + to, from);
    return idx;
}
} // namespace tickerbar
