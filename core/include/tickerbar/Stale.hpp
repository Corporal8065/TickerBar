#pragma once
#include <algorithm>
namespace tickerbar {
// Feed freshness + retry policy — pure functions over plain numbers so
// the honesty rules are unit-tested, not vibes.
//  Live:  we hold data and it is <= kStaleAfterSec old.
//  Stale: we hold data but it is older than that — the UI greys the
//         cells and keeps the last real prices (labelled), never
//         pretending they are current.
//  Down:  we have NEVER held data (first run offline, API dead) —
//         cells show placeholders and the status dot is red.
enum class FeedStatus { Live, Stale, Down };
inline constexpr double kStaleAfterSec = 60.0;

inline FeedStatus feedStatus(bool hasData, double ageSec) {
    if (!hasData) return FeedStatus::Down;
    return ageSec <= kStaleAfterSec ? FeedStatus::Live : FeedStatus::Stale;
}

// Same rule with a caller-chosen threshold — the "3x cadence" shape:
// a source polled every T seconds goes stale at 3T. The 2-arg form
// above is the price-feed case (60 s on a 20 s poll); the CoinGecko
// path polls at ~60 s, so its symbols go stale at 180 s (see
// staleAfterSecFor in MarketParse.hpp).
inline FeedStatus feedStatus(bool hasData, double ageSec, double staleAfterSec) {
    if (!hasData) return FeedStatus::Down;
    return ageSec <= staleAfterSec ? FeedStatus::Live : FeedStatus::Stale;
}

// Market-context freshness: context polls every ~300 s, so stale means
// roughly three missed cycles (900 s) — the same "3x cadence" rule the
// price feed uses (60 s stale on a 20 s poll). No data yet = stale.
inline constexpr double kContextStaleSec = 900.0;
inline bool contextStale(bool hasData, double ageSec) {
    return !hasData || ageSec > kContextStaleSec;
}

// Exponential backoff between failed poll cycles: 5, 10, 20, 40, then
// capped at 60 s. Success resets the failure count (caller-side).
inline double retryDelaySec(int consecutiveFailures) {
    if (consecutiveFailures <= 0) return 0.0;
    double d = 5.0;
    for (int i = 1; i < consecutiveFailures; ++i) d *= 2.0;
    return std::min(d, 60.0);
}
} // namespace tickerbar
