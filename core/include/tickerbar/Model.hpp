#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace tickerbar {
// Market data model — plain values the UI renders. Prices are SPOT
// prices in USD: Coinbase Exchange spot for Coinbase-listed coins,
// CoinGecko spot for the rest (HYPE). See MarketParse. The app labels
// them as spot in its About/tooltip text, never as perp marks.
struct AssetCtx {
    std::string name;
    double price = 0.0;        // current spot price, USD
    double open24h = 0.0;      // price 24h ago — the 24h-change reference (0 = not provided)
    double high24h = 0.0;      // 24h high (0 = not provided)
    double low24h = 0.0;       // 24h low  (0 = not provided)
    double dayVolumeUsd = 0.0; // 24h traded volume, USD
    double marketCapUsd = 0.0; // USD; only CoinGecko rows carry it (0 = not provided)
    bool hasData = false;      // false until a real payload filled this in
};

// Market context: the Alternative.me Fear & Greed index (0..100).
struct FearGreed {
    int value = 0;
    std::string classification; // "Extreme Fear" .. "Extreme Greed"
    std::int64_t timestamp = 0; // source epoch seconds, if given
};

struct Candle {
    int64_t tOpenMs = 0;
    double open = 0, close = 0, high = 0, low = 0, volume = 0; // volume in coin units
};

// 24h change in percent. nullopt when the reference is unusable — the
// UI shows a dash rather than a made-up number.
inline std::optional<double> changePct(double price, double refPx) {
    if (!(refPx > 0.0) || !(price >= 0.0)) return std::nullopt;
    return (price / refPx - 1.0) * 100.0;
}

inline std::optional<double> candlesHigh(const std::vector<Candle>& cs) {
    if (cs.empty()) return std::nullopt;
    double h = cs.front().high;
    for (auto& c : cs) h = c.high > h ? c.high : h;
    return h;
}
inline std::optional<double> candlesLow(const std::vector<Candle>& cs) {
    if (cs.empty()) return std::nullopt;
    double l = cs.front().low;
    for (auto& c : cs) l = c.low < l ? c.low : l;
    return l;
}

// Map a series to [0,1] for sparkline drawing (0 = series low,
// 1 = series high). A flat series maps to 0.5 everywhere (a level line
// is the honest picture of no movement); empty stays empty.
inline std::vector<float> normalizeSeries(const std::vector<double>& xs) {
    std::vector<float> out;
    if (xs.empty()) return out;
    double mn = xs.front(), mx = xs.front();
    for (double x : xs) { mn = x < mn ? x : mn; mx = x > mx ? x : mx; }
    out.reserve(xs.size());
    if (!(mx > mn)) { out.assign(xs.size(), 0.5f); return out; }
    for (double x : xs) out.push_back((float)((x - mn) / (mx - mn)));
    return out;
}
} // namespace tickerbar
