// TickerBar core: parsers for the rotation-section sources (CNBC
// batch quotes — sections, bond ETFs and, since v0.6.15, the
// Treasury yields too; Yahoo daily closes and OHLCV drill-in
// candles) and the Market Internals computed-proxy math. The
// FRED CSV parser that served yields in v0.6.11 was removed with
// its route in v0.6.15 (see Sections.hpp for the stall evidence).
//
// STRICTNESS (the family rule, same as MarketParse/ContextParse): a
// payload that does not parse cleanly is a FAILED fetch — nullopt,
// never a partial or guessed reading. The one deliberate exception is
// CNBC's own per-symbol signal: a row whose "code" is present and
// non-zero is the SOURCE saying "unknown symbol" (its response for a
// bogus id is exactly {"symbol": ..., "code": 1} and nothing else),
// so that row is skipped, not fatal. A code-0 row whose numbers do
// not parse IS fatal: the source claimed a real quote and lied about
// its shape.
//
// CNBC numbers arrive as strings with thousands separators and
// percent signs ("7,568.65", "+0.36%") and occasionally as plain JSON
// numbers; cnbcNumber() below is the only sanctioned way to read one.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Json.hpp"
#include "MarketParse.hpp"  // changePct
#include "Model.hpp"        // SectionQuote, YieldReading

namespace tickerbar {

struct CnbcQuote {
    std::string symbol;     // the id as requested (".SPX", "BRK.B")
    std::string name;
    double last = 0.0;
    double prevClose = 0.0; // 0 = source did not say
    double open = 0.0, high = 0.0, low = 0.0;
    std::string lastTimedate; // the quote's own timestamp ("8:53 PM EST"); "" when not carried
};

namespace detail {
inline std::string trimAscii(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
// Strict whole-string double (CNBC/FRED shapes): optional sign,
// digits, one dot. No exponent, no hex, no trailing junk — surprises
// must fail, not coerce.
inline std::optional<double> strictDecimal(const std::string& raw) {
    const std::string s = trimAscii(raw);
    if (s.empty()) return std::nullopt;
    size_t i = 0;
    if (s[i] == '+' || s[i] == '-') ++i;
    bool digits = false, dot = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') digits = true;
        else if (c == '.' && !dot) dot = true;
        else return std::nullopt;
    }
    if (!digits) return std::nullopt;
    return std::strtod(s.c_str(), nullptr);
}
} // namespace detail

// One CNBC numeric field: a JSON number, or a string carrying
// thousands separators and (for change_pct) a trailing %.
inline std::optional<double> cnbcNumber(const Json& v) {
    if (auto d = v.asNumber()) return d;
    auto s = v.asString();
    if (!s) return std::nullopt;
    std::string t;
    for (char c : *s) if (c != ',') t += c;
    t = detail::trimAscii(t);
    if (!t.empty() && t.back() == '%') t.pop_back();
    return detail::strictDecimal(t);
}

inline std::optional<std::vector<CnbcQuote>> parseCnbcQuotes(const std::string& body) {
    auto root = parseJson(body);
    if (!root || !root->isObject()) return std::nullopt;
    const Json* result = root->find("FormattedQuoteResult");
    if (result == nullptr || !result->isObject()) return std::nullopt;
    const Json* rows = result->find("FormattedQuote");
    if (rows == nullptr || !rows->isArray()) return std::nullopt;
    std::vector<CnbcQuote> out;
    for (size_t i = 0; i < rows->array.size(); ++i) {
        const Json* row = rows->at((int)i);
        if (row == nullptr || !row->isObject()) return std::nullopt;
        // The source's own unknown-symbol signal: skip the row whole,
        // before any other field is read (the row carries nothing else).
        if (const Json* code = row->find("code")) {
            auto c = code->asNumber();
            if (!c) return std::nullopt;
            if (*c != 0.0) continue;
        }
        auto sym = row->find("symbol") ? row->find("symbol")->asString() : std::nullopt;
        if (!sym || sym->empty()) return std::nullopt;
        const Json* lastJ = row->find("last");
        if (lastJ == nullptr) return std::nullopt;
        auto last = cnbcNumber(*lastJ);
        if (!last || *last <= 0.0) return std::nullopt;  // claimed real, shape is wrong
        CnbcQuote q;
        q.symbol = *sym;
        q.last = *last;
        if (auto nm = row->find("name") ? row->find("name")->asString() : std::nullopt) q.name = *nm;
        // Lenient like `name`: a display string, not a number — a
        // missing/odd timestamp degrades the stamp, never the quote.
        if (auto td = row->find("last_timedate") ? row->find("last_timedate")->asString() : std::nullopt)
            q.lastTimedate = *td;
        // Optional fields: absent = 0 (unknown); present-but-broken = poison.
        struct OptField { const char* key; double* dst; };
        const OptField optFields[] = {
            { "previous_day_closing", &q.prevClose },
            { "open", &q.open }, { "high", &q.high }, { "low", &q.low },
        };
        for (const auto& f : optFields) {
            const Json* j = row->find(f.key);
            if (j == nullptr) continue;
            auto v = cnbcNumber(*j);
            if (!v) return std::nullopt;
            *f.dst = *v;
        }
        out.push_back(std::move(q));
    }
    return out;
}

inline SectionQuote sectionQuoteFrom(const CnbcQuote& q) {
    SectionQuote s;
    s.name = q.name;
    s.last = q.last;
    s.prevClose = q.prevClose;
    s.open = q.open; s.high = q.high; s.low = q.low;
    return s;
}

// ---- Treasury yields from the CNBC batch (v0.6.15) --------------------
// A yield row is an ordinary CnbcQuote whose `last`/`prevClose` are
// yields in percent ("4.904%" / "4.95%" — cnbcNumber strips the %).
// The reading the Bonds cell shows: the live yield, the previous
// close (for the bp change), and the quote's own timestamp as the
// stamp. CNBC carries NO observation date for these — none is
// invented (see Model.hpp's YieldReading note).
inline YieldReading yieldReadingFromCnbc(const CnbcQuote& q) {
    YieldReading r;
    r.valuePct = q.last;
    r.prevPct = q.prevClose;
    r.stamp = q.lastTimedate;
    return r;
}

// ---- Yahoo v8 chart daily closes --------------------------------------
// Null entries in the close array are Yahoo's own halt/no-trade mark
// and are skipped; entries that do not read as a positive number
// poison the series. An explicit chart.error is a failed fetch.
inline std::optional<std::vector<double>> parseYahooDailyCloses(const std::string& body) {
    auto root = parseJson(body);
    if (!root || !root->isObject()) return std::nullopt;
    const Json* chart = root->find("chart");
    if (chart == nullptr || !chart->isObject()) return std::nullopt;
    if (const Json* err = chart->find("error"); err != nullptr && !err->isNull())
        return std::nullopt;
    const Json* result = chart->find("result");
    if (result == nullptr || !result->isArray() || result->array.size() == 0) return std::nullopt;
    const Json* first = result->at(0);
    if (first == nullptr || !first->isObject()) return std::nullopt;
    const Json* indicators = first->find("indicators");
    if (indicators == nullptr || !indicators->isObject()) return std::nullopt;
    const Json* quoteArr = indicators->find("quote");
    if (quoteArr == nullptr || !quoteArr->isArray() || quoteArr->array.size() == 0) return std::nullopt;
    const Json* quote0 = quoteArr->at(0);
    if (quote0 == nullptr || !quote0->isObject()) return std::nullopt;
    const Json* closes = quote0->find("close");
    if (closes == nullptr || !closes->isArray()) return std::nullopt;
    std::vector<double> out;
    for (size_t i = 0; i < closes->array.size(); ++i) {
        const Json* c = closes->at((int)i);
        if (c == nullptr) return std::nullopt;
        if (c->isNull()) continue;
        auto v = c->asNumber();
        if (!v || *v <= 0.0) return std::nullopt;
        out.push_back(*v);
    }
    if (out.size() < 2) return std::nullopt;
    return out;
}

// ---- Yahoo v8 chart OHLCV candles (v0.6.15 drill-in) -------------------
// The section drill-in's parser: timestamp[] plus
// indicators.quote[0]'s open/high/low/close/volume arrays, zipped.
// STRICTNESS: an explicit chart.error, a missing/short array, a
// non-number where a number sits, a non-positive OHLC value, or
// out-of-order timestamps all poison the fetch (nullopt) — same
// family rule as the closes parser. The one skip: a bar with ANY
// null OHLC leg is Yahoo's halt / no-trade / still-forming mark —
// a partial bar is not a candle, so it is dropped, not drawn
// (verified live 2026-11-03: TLT's 1mo payload carries a null-close
// bar mid-series). Volume is softer: a null volume reads as 0 (the
// index feeds report all-zero volume legitimately — ^VIX verified
// live — and Model's candleRange hasVolume flag is what tells the
// chart to say "no volume from this feed").
inline std::optional<std::vector<Candle>> parseYahooCandles(const std::string& body) {
    auto root = parseJson(body);
    if (!root || !root->isObject()) return std::nullopt;
    const Json* chart = root->find("chart");
    if (chart == nullptr || !chart->isObject()) return std::nullopt;
    if (const Json* err = chart->find("error"); err != nullptr && !err->isNull())
        return std::nullopt;
    const Json* result = chart->find("result");
    if (result == nullptr || !result->isArray() || result->array.size() == 0) return std::nullopt;
    const Json* first = result->at(0);
    if (first == nullptr || !first->isObject()) return std::nullopt;
    const Json* stamps = first->find("timestamp");
    if (stamps == nullptr || !stamps->isArray()) return std::nullopt;
    const Json* indicators = first->find("indicators");
    if (indicators == nullptr || !indicators->isObject()) return std::nullopt;
    const Json* quoteArr = indicators->find("quote");
    if (quoteArr == nullptr || !quoteArr->isArray() || quoteArr->array.size() == 0) return std::nullopt;
    const Json* quote0 = quoteArr->at(0);
    if (quote0 == nullptr || !quote0->isObject()) return std::nullopt;
    const Json* opens = quote0->find("open");
    const Json* highs = quote0->find("high");
    const Json* lows = quote0->find("low");
    const Json* closes = quote0->find("close");
    const Json* vols = quote0->find("volume");
    if (opens == nullptr || !opens->isArray() || highs == nullptr || !highs->isArray() ||
        lows == nullptr || !lows->isArray() || closes == nullptr || !closes->isArray())
        return std::nullopt;
    const size_t n = stamps->array.size();
    if (opens->array.size() != n || highs->array.size() != n ||
        lows->array.size() != n || closes->array.size() != n)
        return std::nullopt;
    if (vols != nullptr && (!vols->isArray() || vols->array.size() != n)) return std::nullopt;
    std::vector<Candle> out;
    int64_t lastTs = 0;
    for (size_t i = 0; i < n; ++i) {
        const Json* tsJ = stamps->at((int)i);
        if (tsJ == nullptr) return std::nullopt;
        auto ts = tsJ->asNumber();
        if (!ts || *ts <= 0.0) return std::nullopt;
        const Json* legs[4] = { opens->at((int)i), highs->at((int)i), lows->at((int)i), closes->at((int)i) };
        bool nullLeg = false;
        for (const Json* leg : legs) {
            if (leg == nullptr) return std::nullopt;
            if (leg->isNull()) nullLeg = true;
        }
        if (nullLeg) continue;  // halt / no-trade / forming bar — not a candle
        double v[4];
        for (int k = 0; k < 4; ++k) {
            auto num = legs[k]->asNumber();
            if (!num || *num <= 0.0) return std::nullopt;
            v[k] = *num;
        }
        double vol = 0.0;
        if (vols != nullptr) {
            const Json* volJ = vols->at((int)i);
            if (volJ == nullptr) return std::nullopt;
            if (!volJ->isNull()) {
                auto vn = volJ->asNumber();
                if (!vn || *vn < 0.0) return std::nullopt;
                vol = *vn;
            }
        }
        const int64_t tMs = (int64_t)(*ts) * 1000;
        if (!out.empty() && tMs <= lastTs) return std::nullopt;  // source order is part of the shape
        lastTs = tMs;
        Candle c;
        c.tOpenMs = tMs;
        c.open = v[0]; c.high = v[1]; c.low = v[2]; c.close = v[3];
        c.volume = vol;
        out.push_back(c);
    }
    if (out.size() < 2) return std::nullopt;
    return out;
}

// What the section drill-in says about its feed — named on the
// chart like the crypto detail's notes, per range.
inline std::string yahooDetailNote(DetailRange range) {
    switch (range) {
        case DetailRange::H24: return "Yahoo candles: full OHLC + volume (5m).";
        case DetailRange::D7:  return "Yahoo candles: full OHLC + volume (30m).";
        case DetailRange::D30: return "Yahoo candles: full OHLC + volume (daily).";
    }
    return "Yahoo candles.";
}

// ---- Market Internals: computed proxies --------------------------------
// Computed from the Top-100 quote batch ONLY — this is a proxy for
// market breadth, not an exchange print, and the cells label it so.
struct InternalsProxy {
    int valid = 0;        // quotes with a usable day change
    int advancers = 0;
    int decliners = 0;
    int unchanged = 0;
    double advancersPct = 0.0;       // advancers / valid, %
    double medianChangePct = 0.0;    // median day change, %
    double equalWeightChangePct = 0.0; // mean day change, %
};

inline std::optional<InternalsProxy> computeInternalsProxy(const std::vector<CnbcQuote>& quotes) {
    std::vector<double> pcts;
    InternalsProxy r;
    for (const auto& q : quotes) {
        auto pct = changePct(q.last, q.prevClose);
        if (!pct) continue;
        pcts.push_back(*pct);
        if (*pct > 0.0) ++r.advancers;
        else if (*pct < 0.0) ++r.decliners;
        else ++r.unchanged;
    }
    if (pcts.empty()) return std::nullopt;
    r.valid = (int)pcts.size();
    r.advancersPct = 100.0 * (double)r.advancers / (double)r.valid;
    double sum = 0.0;
    for (double p : pcts) sum += p;
    r.equalWeightChangePct = sum / (double)pcts.size();
    std::vector<double> sorted = pcts;
    std::sort(sorted.begin(), sorted.end());
    const size_t n = sorted.size();
    r.medianChangePct = (n % 2 == 1) ? sorted[n / 2]
                                     : 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
    return r;
}

} // namespace tickerbar
