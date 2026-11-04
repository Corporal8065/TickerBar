#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "Json.hpp"
#include "MarketParse.hpp" // upperSymbol
#include "Model.hpp"
namespace tickerbar {
// ---- Hyperliquid: POST https://api.hyperliquid.xyz/info (candleSnapshot) ----
// Drill-in candles ONLY (v0.6.12). The main strip is untouched by this
// header: strip prices stay on Coinbase / CoinGecko exactly as
// MarketParse routes them, because v0.1 read Hyperliquid for strip
// prices and its API geo-blocked US IPs (see MarketParse). The
// candleSnapshot endpoint was re-verified live from this network on
// 2026-11-03: keyless POST, HTTP 200, full OHLCV for HYPE.
//
// Wire shape (one entry per candle, OLDEST first):
//   [{"t":1793653200000,"T":1793656799999,"s":"HYPE","i":"1h",
//     "o":"83.135","c":"84.516","h":"84.672","l":"83.114",
//     "v":"387291.45","n":25951}, ...]
// t/T are epoch milliseconds (numbers); o/c/h/l/v arrive as STRINGS
// (asNumber coerces, like Coinbase). "v" is volume in COIN units —
// the same semantics Model::Candle::volume already carries for
// Coinbase candles, so the chart's relative volume bars need no
// rescaling and must never be read as USD. "n" (trade count) is not
// modelled and is ignored.
//
// Failure shapes, all verified live 2026-11-03:
//  - a coin Hyperliquid does not list: HTTP 500, body literally
//    "null" — parses as JSON null, i.e. not an array: rejected here,
//    and the HTTP status fails the fetch before parsing anyway.
//  - an unknown interval string: HTTP 422 with a plain-text body
//    (not JSON at all): rejected.
//  - an empty result set is a JSON array of length 0: VALID here
//    (an empty vector), and the caller treats empty as "no data"
//    and falls back — an empty chart is never drawn from it.
// Strictness follows the house discipline (parseCoinbaseCandles):
// any row that is short, non-numeric, non-positive in price, or
// labelled for a different coin POISONS the whole set (nullopt) —
// the caller keeps its last good / fallback data instead of
// rendering a guess.

// Interval per detail range: 1h for 24h and 7d (25 / 169 candles
// observed live), 1d for 30d (32 candles over a 31-day window
// observed live — the daily interval exists, so 30d does NOT need
// the CoinGecko closes-only fallback).
inline const char* hyperliquidIntervalForRange(DetailRange range) {
    return range == DetailRange::D30 ? "1d" : "1h";
}
inline int64_t hyperliquidSpanMsForRange(DetailRange range) {
    const int64_t dayMs = 24LL * 3600LL * 1000LL;
    switch (range) {
        case DetailRange::H24: return dayMs;
        case DetailRange::D7:  return 7LL * dayMs;
        case DetailRange::D30: return 30LL * dayMs;
    }
    return dayMs;
}

// The candleSnapshot request body for one symbol/range ending at
// endMs. Built here (not in the JUCE TU) so the exact bytes the app
// sends are unit-testable. The symbol is uppercased; watchlist
// symbols are validated [A-Z0-9] long before they can reach this.
inline std::string hyperliquidCandleRequestJson(const std::string& symbol,
                                                DetailRange range, int64_t endMs) {
    const int64_t startMs = endMs - hyperliquidSpanMsForRange(range);
    std::string out = "{\"type\":\"candleSnapshot\",\"req\":{\"coin\":\"";
    out += upperSymbol(symbol);
    out += "\",\"interval\":\"";
    out += hyperliquidIntervalForRange(range);
    out += "\",\"startTime\":" + std::to_string(startMs);
    out += ",\"endTime\":" + std::to_string(endMs);
    out += "}}";
    return out;
}

// The honest note for a range Hyperliquid actually served: full
// OHLC + volume, never the CoinGecko "closes only" wording.
inline std::string hyperliquidDetailNote(DetailRange range) {
    return std::string("Hyperliquid candles: full OHLC + volume (")
         + hyperliquidIntervalForRange(range) + ").";
}

// Parse a candleSnapshot response into candles, ascending by open
// time. expectedSymbol (any case) is checked against each row's "s"
// when given: a payload labelled for another coin is not this
// symbol's chart. Empty expectedSymbol skips the label check.
inline std::optional<std::vector<Candle>> parseHyperliquidCandles(const std::string& text,
                                                                  const std::string& expectedSymbol = "") {
    auto root = parseJson(text);
    if (!root || !root->isArray()) return std::nullopt; // covers "null", error objects, 422 text
    const std::string want = upperSymbol(expectedSymbol);
    std::vector<Candle> out;
    out.reserve(root->array.size());
    for (auto& row : root->array) {
        if (!row.isObject()) return std::nullopt;
        auto num = [&](const char* key) -> std::optional<double> {
            const Json* f = row.find(key);
            if (!f) return std::nullopt;
            return f->asNumber();
        };
        auto t = num("t");
        auto o = num("o"), c = num("c"), h = num("h"), l = num("l"), v = num("v");
        if (!t || !o || !c || !h || !l || !v) return std::nullopt;
        if (!(*t > 0.0) || !(*o > 0.0) || !(*c > 0.0) || !(*h > 0.0) || !(*l > 0.0) || *v < 0.0)
            return std::nullopt;
        if (!(*h >= *l)) return std::nullopt;
        if (!want.empty()) {
            const Json* s = row.find("s");
            auto sym = s ? s->asString() : std::nullopt;
            if (!sym || upperSymbol(*sym) != want) return std::nullopt;
        }
        Candle cd;
        cd.tOpenMs = (int64_t)(*t);
        cd.open = *o; cd.close = *c; cd.high = *h; cd.low = *l; cd.volume = *v;
        out.push_back(cd);
    }
    std::sort(out.begin(), out.end(),
              [](const Candle& a, const Candle& b) { return a.tOpenMs < b.tOpenMs; });
    return out;
}
} // namespace tickerbar
