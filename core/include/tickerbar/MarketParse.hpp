#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>
#include "Json.hpp"
#include "Model.hpp"
#include "Stale.hpp"
namespace tickerbar {
// Parsing of the v0.1.1 price sources. Strict on purpose, same
// discipline as v0.1's HlParse: any structural surprise returns
// nullopt and the app keeps showing the last good data in its stale
// state instead of rendering a guess. (Fixture-tested, including
// malformed bodies and a captured 429 body — see tests/.)
//
// Why these sources (v0.1.1 root cause): v0.1 read the Hyperliquid
// public API, which geo-blocks US IP addresses — the bar showed a
// permanent "no connection" on US PCs. Coinbase Exchange and
// CoinGecko are keyless and US-reachable. Prices are SPOT.
//
// Routing: a symbol trades on Coinbase when Coinbase lists SYM-USD
// (validated against the live /products list); HYPE is the pinned
// exception — it is served by CoinGecko even though Coinbase also
// lists it, per the v0.1.1 design. Everything else is rejected by
// watchlist validation before it can reach the poller.
enum class PriceSource { Coinbase, CoinGecko };

inline std::string upperSymbol(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

inline PriceSource sourceForSymbol(const std::string& symbol) {
    return upperSymbol(symbol) == "HYPE" ? PriceSource::CoinGecko
                                          : PriceSource::Coinbase;
}

inline std::optional<std::string> coinGeckoIdForSymbol(const std::string& symbol) {
    if (upperSymbol(symbol) == "HYPE") return std::string("hyperliquid");
    return std::nullopt;
}

// Stale threshold per symbol, following the "3x cadence" rule:
// Coinbase stats poll at ~20 s -> stale at 60 s (kStaleAfterSec);
// CoinGecko polls at ~60 s -> stale at 180 s. Without this, a healthy
// CoinGecko symbol would flicker STALE between its own polls.
inline constexpr double kGeckoStaleAfterSec = 180.0;
inline double staleAfterSecFor(const std::string& symbol) {
    return sourceForSymbol(symbol) == PriceSource::CoinGecko ? kGeckoStaleAfterSec
                                                             : kStaleAfterSec;
}

// One merged view of every symbol the app currently holds data for,
// whatever source served it — the UI never needs to know which.
struct MarketSnapshot {
    std::vector<AssetCtx> assets; // in insertion (fetch) order
    const AssetCtx* find(const std::string& name) const {
        for (auto& a : assets) if (a.name == name) return &a;
        return nullptr;
    }
    bool hasSymbol(const std::string& name) const { return find(name) != nullptr; }
};

// ---- Coinbase Exchange: GET /products/{SYM}-USD/stats ----
// {"open":"80873.62","high":"81715.01","low":"79586.17",
//  "last":"79736.11","volume":"5391.14478694", ...}
// Numerics arrive as strings (asNumber coerces). "volume" is in COIN
// units over 24h; the model stores USD volume, so it is converted
// with the last price. last + open are the minimum honest payload.
inline std::optional<AssetCtx> parseCoinbaseStats(const std::string& text,
                                                  const std::string& symbol) {
    auto root = parseJson(text);
    if (!root || !root->isObject()) return std::nullopt;
    auto num = [&](const char* key) -> std::optional<double> {
        const Json* f = root->find(key);
        if (!f) return std::nullopt;
        return f->asNumber();
    };
    auto last = num("last");
    auto open = num("open");
    if (!last || !open || !(*last > 0.0) || !(*open > 0.0)) return std::nullopt;
    AssetCtx a;
    a.name = upperSymbol(symbol);
    a.price = *last;
    a.open24h = *open;
    if (auto v = num("high")) a.high24h = *v;
    if (auto v = num("low")) a.low24h = *v;
    if (auto v = num("volume")) a.dayVolumeUsd = *v * *last;
    a.hasData = true;
    return a;
}

// ---- Coinbase Exchange: GET /products/{SYM}-USD/candles?granularity=3600 ----
// [[timeSec, low, high, open, close, volumeCoin], ...], NEWEST first.
// Returned ascending by time. A row that is short or non-numeric
// poisons the set — never guess a chart. Zero candles is valid.
inline std::optional<std::vector<Candle>> parseCoinbaseCandles(const std::string& text) {
    auto root = parseJson(text);
    if (!root || !root->isArray()) return std::nullopt;
    std::vector<Candle> out;
    out.reserve(root->array.size());
    for (auto& row : root->array) {
        if (!row.isArray() || row.array.size() < 6) return std::nullopt;
        double f[6];
        for (int i = 0; i < 6; ++i) {
            auto v = row.array[(size_t)i].asNumber();
            if (!v) return std::nullopt;
            f[i] = *v;
        }
        Candle c;
        c.tOpenMs = (int64_t)(f[0] * 1000.0);
        c.low = f[1]; c.high = f[2]; c.open = f[3]; c.close = f[4]; c.volume = f[5];
        out.push_back(c);
    }
    std::reverse(out.begin(), out.end()); // API sends newest first
    return out;
}

// ---- Coinbase Exchange: GET /products ----
// [{"id":"BTC-USD","base_currency":"BTC","quote_currency":"USD",
//   "status":"online", ...}, ...]
// Returns the sorted set of base symbols with a live SYM-USD product —
// the watchlist validation universe. Delisted products and non-USD
// quotes are excluded. A row missing its fields poisons the payload.
inline std::optional<std::vector<std::string>> parseCoinbaseProducts(const std::string& text) {
    auto root = parseJson(text);
    if (!root || !root->isArray()) return std::nullopt;
    std::vector<std::string> out;
    for (auto& row : root->array) {
        if (!row.isObject()) return std::nullopt;
        auto base = row.find("base_currency") ? row.find("base_currency")->asString() : std::nullopt;
        auto quote = row.find("quote_currency") ? row.find("quote_currency")->asString() : std::nullopt;
        auto status = row.find("status") ? row.find("status")->asString() : std::nullopt;
        if (!base || !quote || !status) return std::nullopt;
        if (*quote == "USD" && *status == "online") out.push_back(upperSymbol(*base));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ---- CoinGecko: GET /coins/markets?vs_currency=usd&ids=...&sparkline=true ----
// [{"id":"hyperliquid","symbol":"hype","current_price":78.54,
//   "market_cap":..., "total_volume":..., "high_24h":..., "low_24h":...,
//   "price_change_percentage_24h":-3.39382,
//   "sparkline_in_7d":{"price":[... hourly, 7 days ...]}}, ...]
// open24h is DERIVED from the source's own 24h % (price / (1+pct/100))
// — the same reference the source itself quotes, not an invented one;
// when the % is absent the change stays unknown (open24h = 0) and the
// UI shows a dash. A rate-limit body has no array and is rejected.
struct GeckoQuote {
    AssetCtx ctx;
    std::vector<double> sparkline7d; // hourly closes, oldest first; may be empty
};

inline std::optional<std::vector<GeckoQuote>> parseCoinGeckoMarkets(const std::string& text) {
    auto root = parseJson(text);
    if (!root || !root->isArray()) return std::nullopt;
    std::vector<GeckoQuote> out;
    for (auto& row : root->array) {
        if (!row.isObject()) return std::nullopt;
        auto sym = row.find("symbol") ? row.find("symbol")->asString() : std::nullopt;
        const Json* pxF = row.find("current_price");
        auto px = pxF ? pxF->asNumber() : std::nullopt;
        if (!sym || sym->empty() || !px || !(*px > 0.0)) return std::nullopt;
        GeckoQuote q;
        q.ctx.name = upperSymbol(*sym);
        q.ctx.price = *px;
        auto optNum = [&](const char* key) -> std::optional<double> {
            const Json* f = row.find(key);
            if (!f) return std::nullopt;
            return f->asNumber();
        };
        if (auto v = optNum("price_change_percentage_24h")) {
            const double denom = 1.0 + *v / 100.0;
            if (denom > 0.0) q.ctx.open24h = *px / denom;
        }
        if (auto v = optNum("high_24h")) q.ctx.high24h = *v;
        if (auto v = optNum("low_24h")) q.ctx.low24h = *v;
        if (auto v = optNum("total_volume")) q.ctx.dayVolumeUsd = *v;
        if (auto v = optNum("market_cap")) q.ctx.marketCapUsd = *v;
        if (const Json* sp = row.find("sparkline_in_7d")) {
            if (const Json* prices = sp->find("price")) {
                if (!prices->isArray()) return std::nullopt;
                for (auto& p : prices->array) {
                    auto v = p.asNumber();
                    if (!v) return std::nullopt;
                    q.sparkline7d.push_back(*v);
                }
            }
        }
        q.ctx.hasData = true;
        out.push_back(std::move(q));
    }
    return out;
}

// Trailing `count` points of an hourly series as candles for the
// chart path. open/high/low = the point itself: the series carries
// closes only, and inventing a range would be a fabrication — the
// detail card's 24h high/low come from the source's own fields.
// endMs is aligned DOWN to the hour (the series' last point is the
// current, partial hour).
inline std::vector<Candle> candlesFromHourlySeries(const std::vector<double>& series,
                                                   int64_t endMs, size_t count) {
    std::vector<Candle> out;
    if (series.empty() || count == 0) return out;
    const size_t take = std::min(count, series.size());
    const int64_t hourMs = 3600LL * 1000LL;
    const int64_t lastHour = (endMs / hourMs) * hourMs;
    out.reserve(take);
    for (size_t i = 0; i < take; ++i) {
        const double px = series[series.size() - take + i];
        Candle c;
        c.tOpenMs = lastHour - (int64_t)(take - 1 - i) * hourMs;
        c.open = c.close = c.high = c.low = px;
        c.volume = 0.0;
        out.push_back(c);
    }
    return out;
}
} // namespace tickerbar
