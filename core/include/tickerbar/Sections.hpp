// TickerBar core: the v0.6.11 rotation sections — which instruments
// the main strip shows when the user switches away from Crypto.
//
// Every source here is KEYLESS and public (contract: data must work
// for public users, no keys, no accounts, no external software):
//   - quotes: CNBC's public restQuote batch endpoint (the same JSON
//     the cnbc.com quote pages poll). Verified live 2026-11-03: one
//     request carries up to 50 symbols, indexes included (.SPX form).
//   - yields: FRED's public fredgraph CSV (no key at this endpoint).
//     Verified live 2026-11-03. Daily observations, roughly one
//     business day of publication lag — cells print the observation
//     date so the lag is never hidden.
//   - sparklines: Yahoo Finance's public v8 chart endpoint (daily
//     closes). Verified live 2026-11-03.
// Stooq was evaluated for this round and REJECTED: its CSV endpoints
// answer plain HTTP clients with an HTML error page / a JavaScript
// proof-of-work wall (verified live 2026-11-03), so the app could
// never read them. CBOE's delayed-quote CSV sits behind a Cloudflare
// redirect wall — also rejected.
//
// Market Internals is computed proxies ONLY (the standing rule): the
// advancer share / median / equal-weight figures are computed from
// the Top-100 quote batch, never an official exchange breadth print,
// and the cells say so.
#pragma once

#include <string>
#include <vector>

#include "Top100.hpp"

namespace tickerbar {

// Section indices persist in settings (TickerState::rotationSection),
// so the order is an ABI: never renumber.
enum class RotationSection : int {
    Crypto      = 0,  // the watchlist, unchanged since v0.1
    EtfsIndexes = 1,
    Top100      = 2,
    Bonds       = 3,
    Internals   = 4,
};
inline constexpr int kRotationSectionCount = 5;

inline const char* rotationSectionName(int idx) {
    switch (idx) {
        case (int)RotationSection::Crypto:      return "Crypto";
        case (int)RotationSection::EtfsIndexes: return "ETFs & Indexes";
        case (int)RotationSection::Top100:      return "Top 100 Stocks";
        case (int)RotationSection::Bonds:       return "Bonds";
        case (int)RotationSection::Internals:   return "Market Internals";
        default:                                return "";
    }
}

// Poll clocks (seconds) and the matching staleness budgets. A section
// reading goes STALE at 3x its poll clock without a successful fetch
// (the family's feedStatus rule) — one section's failure never marks
// another section or the title rotator stale.
inline constexpr double kCnbcMainPollSec    = 45.0;    // ETFs/indexes + bond ETFs, one batch
inline constexpr double kCnbcTop100PollSec  = 90.0;    // two batches of 50
inline constexpr double kFredPollSec        = 1800.0;  // daily data; a slow clock is honest
inline constexpr double kSectionStaleSec    = 3.0 * kCnbcMainPollSec;    // 135
inline constexpr double kTop100StaleSec     = 3.0 * kCnbcTop100PollSec;  // 270
inline constexpr double kFredStaleSec       = 3.0 * kFredPollSec;        // 5400
// Yahoo daily closes are sparkline garnish: refresh a missing/old
// series opportunistically, never block a cell's quote on them.
inline constexpr double kYahooSeriesRefreshSec = 6.0 * 3600.0;
inline constexpr double kYahooSeriesMaxAgeSec  = 24.0 * 3600.0;

struct SectionSymbol {
    const char* display;  // cell title + quote-map key
    const char* cnbc;     // CNBC symbol id
    const char* yahoo;    // Yahoo chart symbol ("" = no sparkline)
    const char* label;    // full name, for tooltips
};

// ETFs & Indexes membership: the four headline indexes + VIX, then
// the liquid index/sector/gold ETFs. CNBC ids for indexes carry the
// leading dot; the display drops it.
inline const std::vector<SectionSymbol>& etfIndexSymbols() {
    static const std::vector<SectionSymbol> k = {
        { "SPX",  ".SPX",  "^GSPC", "S&P 500 index" },
        { "DJI",  ".DJI",  "^DJI",  "Dow Jones Industrial Average" },
        { "IXIC", ".IXIC", "^IXIC", "Nasdaq Composite" },
        { "RUT",  ".RUT",  "^RUT",  "Russell 2000 index" },
        { "VIX",  ".VIX",  "^VIX",  "CBOE Volatility Index" },
        { "SPY",  "SPY",   "SPY",   "SPDR S&P 500 ETF Trust" },
        { "QQQ",  "QQQ",   "QQQ",   "Invesco QQQ Trust (Nasdaq-100)" },
        { "DIA",  "DIA",   "DIA",   "SPDR Dow Jones Industrial Average ETF" },
        { "IWM",  "IWM",   "IWM",   "iShares Russell 2000 ETF" },
        { "XLK",  "XLK",   "XLK",   "Technology Select Sector SPDR" },
        { "XLF",  "XLF",   "XLF",   "Financial Select Sector SPDR" },
        { "GLD",  "GLD",   "GLD",   "SPDR Gold Shares" },
    };
    return k;
}

inline const std::vector<SectionSymbol>& bondEtfSymbols() {
    static const std::vector<SectionSymbol> k = {
        { "TLT", "TLT", "TLT", "iShares 20+ Year Treasury Bond ETF" },
        { "IEF", "IEF", "IEF", "iShares 7-10 Year Treasury Bond ETF" },
        { "SHY", "SHY", "SHY", "iShares 1-3 Year Treasury Bond ETF" },
        { "AGG", "AGG", "AGG", "iShares Core U.S. Aggregate Bond ETF" },
        { "LQD", "LQD", "LQD", "iShares iBoxx Investment Grade Corporate Bond ETF" },
        { "HYG", "HYG", "HYG", "iShares iBoxx High Yield Corporate Bond ETF" },
    };
    return k;
}

struct YieldSeriesDef {
    const char* fredId;   // FRED series id
    const char* display;  // cell title
    const char* label;    // full name, for tooltips
};

// The Treasury curve points the Bonds section shows. Yields are
// percent per annum, FRED "Market Yield on U.S. Treasury Securities"
// (DGS) daily series.
inline const std::vector<YieldSeriesDef>& treasuryYieldSeries() {
    static const std::vector<YieldSeriesDef> k = {
        { "DGS3MO", "3M",  "3-month Treasury yield (FRED DGS3MO)" },
        { "DGS2",   "2Y",  "2-year Treasury yield (FRED DGS2)" },
        { "DGS5",   "5Y",  "5-year Treasury yield (FRED DGS5)" },
        { "DGS10",  "10Y", "10-year Treasury yield (FRED DGS10)" },
        { "DGS30",  "30Y", "30-year Treasury yield (FRED DGS30)" },
    };
    return k;
}

// VIX history for the Internals VIX sparkline rides FRED too
// (CBOE's own CSV is behind a bot wall — see the header note).
inline constexpr const char* kFredVixSeriesId = "VIXCLS";

// One cell of a section, in display order. Strings (not const char*)
// because the Top-100 Yahoo ids are derived ("." -> "-").
enum class SectionCellKind {
    Quote,            // a CNBC quote (price + day change)
    Yield,            // a FRED yield reading
    InternalsAdv,     // computed proxy: advancers share of Top 100
    InternalsMedian,  // computed proxy: median day change, Top 100
    InternalsEqWt,    // computed proxy: equal-weight mean day change
};
struct SectionCellDef {
    SectionCellKind kind = SectionCellKind::Quote;
    std::string display;   // cell title; also the quote-map key
    std::string cnbc;      // Quote kind: CNBC id
    std::string yahoo;     // Quote kind: sparkline symbol ("" = none)
    std::string fredId;    // Yield kind: FRED series id
    std::string label;     // tooltip text
};

inline std::string yahooSymbolFor(const std::string& display) {
    std::string out = display;
    for (auto& c : out) if (c == '.') c = '-';
    return out;
}

inline std::vector<SectionCellDef> sectionCells(int sectionIdx) {
    std::vector<SectionCellDef> out;
    switch (sectionIdx) {
        case (int)RotationSection::EtfsIndexes:
            for (const auto& s : etfIndexSymbols())
                out.push_back({ SectionCellKind::Quote, s.display, s.cnbc, s.yahoo, "", s.label });
            break;
        case (int)RotationSection::Top100:
            for (const auto& e : top100Stocks())
                out.push_back({ SectionCellKind::Quote, e.symbol, e.symbol,
                                yahooSymbolFor(e.symbol), "", e.name });
            break;
        case (int)RotationSection::Bonds:
            for (const auto& y : treasuryYieldSeries())
                out.push_back({ SectionCellKind::Yield, y.display, "", "", y.fredId, y.label });
            for (const auto& s : bondEtfSymbols())
                out.push_back({ SectionCellKind::Quote, s.display, s.cnbc, s.yahoo, "", s.label });
            break;
        case (int)RotationSection::Internals:
            out.push_back({ SectionCellKind::Quote, "VIX", ".VIX", "",
                            "", "CBOE Volatility Index (quote: CNBC)" });
            out.push_back({ SectionCellKind::InternalsAdv, "ADVANCERS", "", "",
                            "", "Share of Top 100 stocks up on the day — computed proxy" });
            out.push_back({ SectionCellKind::InternalsMedian, "MEDIAN", "", "",
                            "", "Median day change across the Top 100 — computed proxy" });
            out.push_back({ SectionCellKind::InternalsEqWt, "EQUAL WT", "", "",
                            "", "Equal-weight mean day change across the Top 100 — computed proxy" });
            break;
        default:
            break;  // Crypto has no section cells (the watchlist rules it)
    }
    return out;
}

// The CNBC symbol ids of one batch request, in request order: the
// "main" batch (ETFs/indexes + bond ETFs — 18 ids) serves three
// sections at once; the Top 100 rides two batches of 50.
inline std::vector<std::string> cnbcMainBatchIds() {
    std::vector<std::string> out;
    for (const auto& s : etfIndexSymbols()) out.emplace_back(s.cnbc);
    for (const auto& s : bondEtfSymbols())  out.emplace_back(s.cnbc);
    return out;
}
inline std::vector<std::string> cnbcTop100BatchIds(int half) {
    std::vector<std::string> out;
    const auto& t = top100Stocks();
    const size_t begin = half == 0 ? 0 : t.size() / 2;
    const size_t end   = half == 0 ? t.size() / 2 : t.size();
    for (size_t i = begin; i < end; ++i) out.emplace_back(t[i].symbol);
    return out;
}

// URL builders (pure, unit-tested; the clients just fetch them).
// CNBC ids contain only [A-Z0-9.], safe unencoded in a query; the
// batch separator "|" must travel as %7C.
inline std::string cnbcBatchUrl(const std::vector<std::string>& cnbcIds) {
    std::string url = "https://quote.cnbc.com/quote-html-webservice/restQuote/symbolType/symbol?symbols=";
    for (size_t i = 0; i < cnbcIds.size(); ++i) {
        if (i) url += "%7C";
        url += cnbcIds[i];
    }
    url += "&requestMethod=itv&noform=1&partnerId=2&fund=1&exthrs=1&output=json";
    return url;
}
// cosd bounds the CSV to a window ending today (FRED serves the full
// history otherwise — DGS10 starts in 1962).
inline std::string fredCsvUrl(const std::string& seriesId, const std::string& cosd) {
    return "https://fred.stlouisfed.org/graph/fredgraph.csv?id=" + seriesId + "&cosd=" + cosd;
}
inline std::string yahooChartUrl(const std::string& yahooSymbol) {
    std::string sym;
    for (char c : yahooSymbol) sym += (c == '^') ? "%5E" : std::string(1, c);
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + sym +
           "?range=1mo&interval=1d";
}

} // namespace tickerbar
