#pragma once
// Strict parsers for the two market-context sources. Same philosophy as
// MarketParse: anything unexpected -> nullopt, never partial data. Context
// data is decorative next to prices, but it follows the same honesty
// rules — a bad payload is dropped whole and the cell goes stale.
#include <optional>
#include <string>
#include "Json.hpp"
#include "Model.hpp"
namespace tickerbar {

// Alternative.me Fear & Greed: GET https://api.alternative.me/fng/?limit=1
//   {"data":[{"value":"51","value_classification":"Neutral",
//              "timestamp":"1793232000", ...}], "metadata":{"error":null}}
// value is a STRING in this API (asNumber coerces); valid range 0..100.
inline std::optional<FearGreed> parseFearGreed(const std::string& text) {
    auto root = parseJson(text);
    if (!root || !root->isObject()) return std::nullopt;
    const Json* data = root->find("data");
    if (!data || !data->isArray() || data->array.empty()) return std::nullopt;
    const Json& row = data->array.front();
    if (!row.isObject()) return std::nullopt;
    const Json* v = row.find("value");
    const Json* c = row.find("value_classification");
    auto value = v ? v->asNumber() : std::nullopt;
    auto cls = c ? c->asString() : std::nullopt;
    if (!value || !cls || cls->empty()) return std::nullopt;
    if (*value < 0.0 || *value > 100.0) return std::nullopt;
    FearGreed out;
    out.value = (int)(*value + 0.5);
    out.classification = *cls;
    if (const Json* ts = row.find("timestamp"))
        if (auto t = ts->asNumber()) out.timestamp = (std::int64_t)*t;
    return out;
}

// CoinGecko global: GET https://api.coingecko.com/api/v3/global
//   {"data":{"market_cap_percentage":{"btc":59.46, "eth":..., ...}, ...}}
// Returns BTC dominance in percent (0..100). A rate-limit body has no
// "data" object at all and is rejected like any other surprise.
inline std::optional<double> parseBtcDominance(const std::string& text) {
    auto root = parseJson(text);
    if (!root || !root->isObject()) return std::nullopt;
    const Json* data = root->find("data");
    if (!data || !data->isObject()) return std::nullopt;
    const Json* mcp = data->find("market_cap_percentage");
    if (!mcp || !mcp->isObject()) return std::nullopt;
    const Json* b = mcp->find("btc");
    auto btc = b ? b->asNumber() : std::nullopt;
    if (!btc || *btc <= 0.0 || *btc >= 100.0) return std::nullopt;
    return btc;
}
} // namespace tickerbar
