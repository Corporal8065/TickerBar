// TickerBar core: parsers for the v0.6.11 rotation-section sources
// (CNBC batch quotes, FRED CSV, Yahoo daily closes) and the Market
// Internals computed-proxy math.
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

// ---- FRED fredgraph CSV ----------------------------------------------
struct FredPoint {
    std::string date;  // YYYY-MM-DD
    double value = 0.0;
};

inline bool fredDateShape(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

// Header line first ("observation_date,DGS10"), then one row per
// observation. FRED marks missing observations with "." — those rows
// are skipped (the source's own gap convention, e.g. holidays inside
// the window), anything else malformed poisons the whole series.
inline std::optional<std::vector<FredPoint>> parseFredGraphCsv(const std::string& body) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : body) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    size_t li = 0;
    while (li < lines.size() && detail::trimAscii(lines[li]).empty()) ++li;
    if (li >= lines.size()) return std::nullopt;
    // Header: must contain a comma and must NOT itself be a data row.
    const std::string& header = lines[li++];
    const size_t hComma = header.find(',');
    if (hComma == std::string::npos) return std::nullopt;
    if (fredDateShape(detail::trimAscii(header.substr(0, hComma)))) return std::nullopt;
    std::vector<FredPoint> out;
    for (; li < lines.size(); ++li) {
        const std::string line = detail::trimAscii(lines[li]);
        if (line.empty()) continue;
        const size_t comma = line.find(',');
        if (comma == std::string::npos) return std::nullopt;
        const std::string date = detail::trimAscii(line.substr(0, comma));
        const std::string valStr = detail::trimAscii(line.substr(comma + 1));
        if (!fredDateShape(date)) return std::nullopt;
        if (valStr.empty() || valStr == ".") continue;  // FRED's gap marks
        auto v = detail::strictDecimal(valStr);
        if (!v) return std::nullopt;
        out.push_back({ date, *v });
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// The reading a Bonds cell shows: latest observation, the previous
// one (for the bp change), and up to `historyPoints` of history for
// the sparkline.
inline YieldReading yieldReadingFrom(const std::vector<FredPoint>& points,
                                     size_t historyPoints = 90) {
    YieldReading r;
    if (points.empty()) return r;
    const FredPoint& lastP = points.back();
    r.valuePct = lastP.value;
    r.date = lastP.date;
    if (points.size() >= 2) r.prevPct = points[points.size() - 2].value;
    const size_t begin = points.size() > historyPoints ? points.size() - historyPoints : 0;
    for (size_t i = begin; i < points.size(); ++i) r.history.push_back(points[i].value);
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
