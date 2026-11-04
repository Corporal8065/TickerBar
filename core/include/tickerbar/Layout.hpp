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
// v0.3.2: when EVERY cell carries an explicit width and their sum
// undershoots the strip, the LAST cell absorbs the surplus — the
// row still fills exactly. Without this, the all-explicit state
// (the steady state after divider drags, which persist both sides
// of a pair) left the remainder unassigned: dead space right of
// the last cell, worst after the context cells were hidden and
// freed their width (Vishona's v0.3.1 field report).
// v0.5.9: the auto share and the all-explicit last-cell fill are
// CAPPED at kSymbolCellMaxW — on a wide bar the uncapped share
// made some boxes extremely wide (Jackson's field report).
// Leftover space past the cap stays unallocated: callers place
// cells from the left, so the strip simply ends early. Explicit
// widths themselves are NOT capped — a dragged/stored width keeps
// its existing behaviour; only what the solver ADDS is capped.
// (Declared here rather than in the SYMBOL CELL WIDTHS section
// below because the solver needs it; the value is unchanged.)
inline constexpr int kSymbolCellMaxW = 600; // == kContextCellMax
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
        if (each > kSymbolCellMaxW) each = kSymbolCellMaxW; // v0.5.9 cap
        int lastAuto = -1;
        for (int i = 0; i < n; ++i) if (explicitW[(size_t)i] <= 0) { out[(size_t)i] = each; lastAuto = i; }
        const int remainder = (avail - fixedSum) - each * autos;
        if (lastAuto >= 0 && remainder > 0) {
            out[(size_t)lastAuto] += remainder;
            if (out[(size_t)lastAuto] > kSymbolCellMaxW) out[(size_t)lastAuto] = kSymbolCellMaxW; // v0.5.9 cap
        }
    } else if (fixedSum < avail) {
        // all explicit, room to spare: the last cell fills it — but
        // only up to the cap (v0.5.9); a last cell already at/above
        // the cap (an explicit width, which stands) gains nothing.
        const int room = kSymbolCellMaxW - out[(size_t)n - 1];
        if (room > 0) out[(size_t)n - 1] += std::min(avail - fixedSum, room);
    }
    return out;
}

// TRAILING DIVIDER (v0.3.2) — the divider right of the last coin
// cell when no context cell follows it. It trades the last cell's
// extent against the WINDOW's extent along the bar axis: dragging
// it resizes the window (clamped to the window's own limits) and
// the solver's fill rule hands the change to the last cell. The
// pure clamp lives here so the tests pin it.
inline int trailingWindowExtent(int current, int delta, int minExtent, int maxExtent) {
    const int v = current + delta;
    return v < minExtent ? minExtent : (v > maxExtent ? maxExtent : v);
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

// ------------------------------------------------------------------
// SYMBOL CELL WIDTHS (v0.5.6) — the band the Settings > Sizes panel
// writes into TickerState::cellWidths, the SAME store the on-bar
// dividers drag. The floor is the fitted solver's own minW for the
// horizontal bar (solveCellWidths is called with 90; the vertical
// solver floors lower, at 56, and clamps up at layout time, so a
// panel-written width is valid in both orientations). The ceiling
// mirrors kContextCellMax below — the family's existing sane cap
// for a dragged cell extent; the coin machinery itself has no max,
// so the panel reuses that one rather than inventing a new number.
inline constexpr int kSymbolCellMinW = 90;
// kSymbolCellMaxW (600, == kContextCellMax) is declared above
// solveCellWidths — the solver's v0.5.9 cap uses the same ceiling.
inline int clampSymbolCellWidth(int w) {
    return w < kSymbolCellMinW ? kSymbolCellMinW : (w > kSymbolCellMaxW ? kSymbolCellMaxW : w);
}

// ------------------------------------------------------------------
// CONTEXT CELLS (Fear & Greed / BTC dominance) — post-v0.3.0,
// Vishona's request. Until now they were fixed chrome: 112 px wide
// (horizontal) / 66 px rows (vertical), always shown. Now each has
// a visibility toggle (TickerState) and a user-dragged explicit
// extent stored per cell (0 = default), adjusted through the same
// CellSplitter + adjustPairWidths machinery as the coin cells.
inline constexpr int kContextCellDefaultW = 112; // the pre-request fixed width
inline constexpr int kContextCellDefaultH = 66;  // the pre-request fixed row height
inline constexpr int kContextCellMin = 64;       // drag floor: title + value stay legible
inline constexpr int kContextCellMax = 600;

// The effective axis extent (px) of a context cell: the stored
// value when one was dragged in (clamped to the sane band), else
// the orientation's historical default.
inline int contextCellExtent(int stored, bool vertical) {
    if (stored <= 0) return vertical ? kContextCellDefaultH : kContextCellDefaultW;
    return stored < kContextCellMin ? kContextCellMin : (stored > kContextCellMax ? kContextCellMax : stored);
}

// ------------------------------------------------------------------
// ROTATION SECTIONS + TITLE-BAR CONTEXT ROTATOR (v0.6.11).
// Natural extent of one section quote/yield cell in the strip.
inline constexpr int kQuoteCellExtentH = 148;  // along a horizontal strip
inline constexpr int kQuoteCellExtentV = 64;   // along a vertical stack

// The title-bar context rotator (Fear & Greed + BTC dominance merged
// into ONE small cell between the left cluster — wordmark, version,
// LIVE status — and the three top-right icons) gets room only when
// what remains of the title bar can hold it WITHOUT crushing either
// cluster: below kTitleContextMinPx it hides entirely (the readings
// are a garnish; the wordmark/status and the icons are furniture).
// When it shows, it is capped at kTitleContextMaxPx so a wide bar
// does not grow it into a second status line. Dwell is 6 s per face —
// the family's rotation pace is a few seconds per reading.
inline constexpr int kTitleContextMinPx = 148;
inline constexpr int kTitleContextMaxPx = 216;
inline constexpr double kTitleContextRotateSec = 6.0;
inline int titleContextExtent(int remainingPx) {
    return remainingPx < kTitleContextMinPx ? 0
                                            : std::min(remainingPx, kTitleContextMaxPx);
}

// The rotator's face + dwell state machine (v0.6.12). v0.6.11 split
// this state three ways — the face lived in the title cell widget,
// the dwell accumulator lived in the owner, and the advance was
// gated on a conjunction of separately-maintained flags (the
// widget's visibility flag re-read every timer tick, two settings
// flags, and the widget's own duplicate inclusion flags) — so any
// single false conjunct froze the face forever with no visible
// error, which is what Jackson field-reported: the cell sat on one
// face and never rotated. The machine below is the single source of
// truth: the app feeds it elapsed time + the two facts that matter
// (are both readings included? did layout place the cell?) and
// renders whatever face it says. Inclusion changes keep the face
// valid (a face that stops being included yields to the other);
// losing either gate zeroes the dwell, so a reappearing cell always
// starts a full dwell — the v0.6.11 intent, now in one place and
// covered by core tests that advance the clock and assert flips.
class TitleRotator {
public:
    // Record which readings are included. If the face on show is no
    // longer included (and the other is), the other takes over at
    // once; with fewer than two readings there is nothing to rotate,
    // so the dwell restarts.
    void setIncluded(bool fngIn, bool domIn) {
        if (showingFng_ && !fngIn && domIn) showingFng_ = false;
        else if (!showingFng_ && !domIn && fngIn) showingFng_ = true;
        if (!fngIn || !domIn) dwell_ = 0.0;
    }
    // Advance the dwell by dtSec of real elapsed time. Flips the
    // face and returns true when a full dwell elapses with both
    // readings included and the cell placed on screen; otherwise
    // the dwell holds at zero (hidden or single-reading cells never
    // accumulate time toward a flip).
    bool advance(double dtSec, bool bothIncluded, bool placed) {
        if (!bothIncluded || !placed) { dwell_ = 0.0; return false; }
        dwell_ += dtSec;
        if (dwell_ >= kTitleContextRotateSec) {
            dwell_ = 0.0;
            showingFng_ = !showingFng_;
            return true;
        }
        return false;
    }
    bool showingFng() const { return showingFng_; }
    double dwell() const { return dwell_; }
private:
    bool showingFng_ = true; // Fear & Greed is the opening face
    double dwell_ = 0.0;
};

// ------------------------------------------------------------------
// OVERFLOW MARQUEE (v0.4.0, Jackson's ask): when the strip's natural
// content is wider than the bar itself, the strip scrolls left in a
// seamless loop — news-ticker style — so every cell gets its turn on
// screen instead of being squeezed or clipped away. Pure 1-D math on
// the bar axis (width when horizontal, height when vertical); the
// app feeds natural extents and a free-running pixel offset.
inline constexpr int kMarqueeLoopGapPx = 56; // blank run between the last cell and the first cell's re-entry

// Total natural extent of the strip: extents + the gaps between them.
inline int stripNaturalExtent(const std::vector<int>& extents, int gap) {
    int total = 0;
    for (size_t i = 0; i < extents.size(); ++i) total += extents[i] + (i ? gap : 0);
    return total;
}

// The marquee runs only when the natural content cannot fit.
inline bool marqueeNeeded(const std::vector<int>& extents, int gap, int avail) {
    return !extents.empty() && stripNaturalExtent(extents, gap) > avail;
}

// One full loop of the scroll: the content plus the loop gap.
inline int marqueeCycle(const std::vector<int>& extents, int gap) {
    return stripNaturalExtent(extents, gap) + kMarqueeLoopGapPx;
}

// Strip-relative position of the item whose un-scrolled base offset
// is `base`, at scroll `offset` in a loop of `cycle` px. The result
// may be negative (the item is sliding out) or beyond the visible
// extent (waiting to re-enter) — the caller clips. Over one full
// cycle every item crosses the visible window exactly once.
inline int marqueePosition(int base, int extent, int offset, int cycle) {
    if (cycle <= 0) return base;
    int rel = (base - offset) % cycle;
    if (rel < 0) rel += cycle;
    if (rel > cycle - extent) rel -= cycle; // half-out on the near side reads as a negative position
    return rel;
}

// MARQUEE DIVIDERS (v0.5.4) — where the divider between a strip item
// and its successor sits while the marquee scrolls: centred in the
// gap right after the item, tracking the item's CURRENT scrolled
// position (marqueePosition's output, wrap and all), so the divider
// a user grabs is always the one painted between the two cells it
// resizes. With the shipped numbers (gap 16, divider extent 10)
// this is itemRel + itemExtent + 3 — the fitted layout's own
// placement (cell edge + 3 px of air, then the 10 px grip).
inline int marqueeSplitterRel(int itemRel, int itemExtent, int gap, int splitterExtent) {
    return itemRel + itemExtent + (gap - splitterExtent) / 2;
}

// ------------------------------------------------------------------
// MODE SELECTOR (v0.6.14) — Jackson asked for the rotation-section
// switcher ON the bar, at the left end of the main strip (since
// v0.6.11 it lived only in Settings and the burger Menu, and he
// never found it there). The selector is chrome, not content: it
// claims the strip's left end BEFORE the cells/marquee layout runs,
// and its form follows the room rules below so it can never crush
// the strip it sits in.
//   Expanded  — all five segments in a row, the active one marked.
//   Collapsed — one button showing the current mode; a click opens
//               the section menu (full names, the active one ticked).
//   Hidden    — no room even for the button; Settings and the Menu
//               still switch (the same persisted field throughout).
// Horizontal, the selector eats strip width, so the forms are gated
// on the REMAINDER the strip keeps after it. Vertical, the selector
// is its own full-width row under the toolbar cluster, so the gate
// is that row's own width. The app measures the short labels at its
// paint font and passes the widths in (the titleContextExtent
// pattern); the arithmetic is pinned here.
enum class ModeSelectorForm { Hidden, Collapsed, Expanded };
inline constexpr int kModeSelectorSegPadPx = 7;     // horizontal padding inside one segment (and the collapsed button)
inline constexpr int kModeSelectorChevronPx = 12;   // collapsed form: room for the dropdown triangle
inline constexpr int kModeSelectorKeepPx = 300;     // strip remainder the EXPANDED form must leave behind
inline constexpr int kModeSelectorKeepMinPx = 150;  // ...and the COLLAPSED form (about one minimum cell + dividers)

inline int modeSelectorNaturalWidth(const std::vector<int>& labelWidths) {
    int w = 0;
    for (int lw : labelWidths) w += std::max(0, lw) + 2 * kModeSelectorSegPadPx;
    return w;
}
inline int modeSelectorCollapsedWidth(int currentLabelW) {
    return std::max(0, currentLabelW) + 2 * kModeSelectorSegPadPx + kModeSelectorChevronPx;
}
inline ModeSelectorForm modeSelectorFormH(int stripW, int naturalW, int collapsedW) {
    if (naturalW > 0 && stripW - naturalW >= kModeSelectorKeepPx) return ModeSelectorForm::Expanded;
    if (collapsedW > 0 && stripW - collapsedW >= kModeSelectorKeepMinPx) return ModeSelectorForm::Collapsed;
    return ModeSelectorForm::Hidden;
}
inline ModeSelectorForm modeSelectorFormV(int rowW, int naturalW, int collapsedW) {
    if (naturalW > 0 && naturalW <= rowW) return ModeSelectorForm::Expanded;
    if (collapsedW > 0 && collapsedW <= rowW) return ModeSelectorForm::Collapsed;
    return ModeSelectorForm::Hidden;
}
inline int modeSelectorExtent(ModeSelectorForm f, int naturalW, int collapsedW) {
    return f == ModeSelectorForm::Expanded ? naturalW
         : f == ModeSelectorForm::Collapsed ? collapsedW : 0;
}
// The expanded form's segments divide the granted extent equally.
// Segment i spans [modeSegmentX0, modeSegmentX1); the hit test maps
// an x back to its segment (-1 outside).
inline int modeSegmentX0(int i, int extent, int count) { return count > 0 ? i * extent / count : 0; }
inline int modeSegmentX1(int i, int extent, int count) { return count > 0 ? (i + 1) * extent / count : 0; }
inline int modeSegmentAt(int x, int extent, int count) {
    if (count <= 0 || extent <= 0 || x < 0 || x >= extent) return -1;
    const int i = x * count / extent;
    return i < count ? i : count - 1;
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
