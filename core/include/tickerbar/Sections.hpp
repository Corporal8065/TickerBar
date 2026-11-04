// TickerBar core: the v0.6.11 rotation sections — which instruments
// the main strip shows when the user switches away from Crypto.
//
// Every source here is KEYLESS and public (contract: data must work
// for public users, no keys, no accounts, no external software):
//   - quotes: CNBC's public restQuote batch endpoint (the same JSON
//     the cnbc.com quote pages poll). Verified live 2026-11-03: one
//     request carries up to 50 symbols, indexes included (.SPX form).
//   - yields (v0.6.15 RE-ROUTE): the same CNBC batch — US3M / US2Y /
//     US5Y / US10Y / US30Y, verified live 2026-11-03 (the undotted
//     forms; the dotted .US10Y forms answer code 1 = unknown). These
//     are LIVE Tradeweb yields, polled every minute; the payload
//     carries the quote's own timestamp (last_timedate), never an
//     observation date, and the cell prints that time as-is.
//     The v0.6.11 route — FRED's fredgraph CSV — was REMOVED in
//     v0.6.15: Jackson's field report showed the yield cells stuck
//     at "loading..." on a real Windows machine while the CNBC
//     cells beside them loaded, and from this host the mechanism
//     reproduces at the HTTP layer — DNS/TCP/TLS to
//     fred.stlouisfed.org complete in ~0.4 s and then NO response
//     bytes ever arrive (the homepage stalls identically). A feed
//     whose connections are accepted and never answered is worse
//     than none: each fetch could stall the section poller behind
//     it. CNBC's pipe is the one already proven on user machines.
//   - sparklines: Yahoo Finance's public v8 chart endpoint (daily
//     closes). Verified live 2026-11-03.
//   - drill-in candles (v0.6.15): Yahoo v8 chart OHLCV at intraday
//     intervals (see yahooDetailChartUrl) — the same endpoint
//     family as the sparklines, verified live 2026-11-03 for
//     stocks, ETFs, the index symbols (^GSPC etc.) and ^VIX.
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

#include <algorithm>
#include <string>
#include <vector>

#include "Model.hpp"
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

// v0.6.14: the on-bar mode selector's labels — short forms of the
// names above under the SAME indices (the persisted field's ABI),
// sized for bar height, where "ETFs & Indexes" would eat the strip.
// Full names ride the tooltips and the section menu.
inline const char* rotationSectionShortName(int idx) {
    switch (idx) {
        case (int)RotationSection::Crypto:      return "Crypto";
        case (int)RotationSection::EtfsIndexes: return "Indexes";
        case (int)RotationSection::Top100:      return "Stocks";
        case (int)RotationSection::Bonds:       return "Bonds";
        case (int)RotationSection::Internals:   return "Internals";
        default:                                return "";
    }
}

// Poll clocks (seconds) and the matching staleness budgets. A section
// reading goes STALE at 3x its poll clock without a successful fetch
// (the family's feedStatus rule) — one section's failure never marks
// another section or the title rotator stale.
inline constexpr double kCnbcMainPollSec    = 45.0;    // ETFs/indexes + bond ETFs, one batch
inline constexpr double kCnbcTop100PollSec  = 90.0;    // two batches of 50
inline constexpr double kCnbcYieldPollSec   = 60.0;    // v0.6.15: the five yields, one batch (Jackson's 1-minute pick)
inline constexpr double kSectionStaleSec    = 3.0 * kCnbcMainPollSec;    // 135
inline constexpr double kTop100StaleSec     = 3.0 * kCnbcTop100PollSec;  // 270
inline constexpr double kYieldStaleSec      = 3.0 * kCnbcYieldPollSec;   // 180 — the 3x rule on the REAL (CNBC, 1-minute) cadence
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
    const char* cnbc;     // CNBC restQuote id (verified live 2026-11-03)
    const char* display;  // cell title
    const char* label;    // full name, for tooltips
};

// The Treasury curve points the Bonds section shows: live Tradeweb
// yields in percent per annum, served by the CNBC batch (v0.6.15 —
// see the header note for the FRED removal).
inline const std::vector<YieldSeriesDef>& treasuryYieldSeries() {
    static const std::vector<YieldSeriesDef> k = {
        { "US3M",  "3M",  "3-month U.S. Treasury yield (live, CNBC/Tradeweb)" },
        { "US2Y",  "2Y",  "2-year U.S. Treasury yield (live, CNBC/Tradeweb)" },
        { "US5Y",  "5Y",  "5-year U.S. Treasury yield (live, CNBC/Tradeweb)" },
        { "US10Y", "10Y", "10-year U.S. Treasury yield (live, CNBC/Tradeweb)" },
        { "US30Y", "30Y", "30-year U.S. Treasury yield (live, CNBC/Tradeweb)" },
    };
    return k;
}

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
    std::string cnbc;      // Quote kind: CNBC id; Yield kind: the CNBC yield id (US10Y form)
    std::string yahoo;     // Quote kind: sparkline symbol ("" = none)
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
                out.push_back({ SectionCellKind::Quote, s.display, s.cnbc, s.yahoo, s.label });
            break;
        case (int)RotationSection::Top100:
            for (const auto& e : top100Stocks())
                out.push_back({ SectionCellKind::Quote, e.symbol, e.symbol,
                                yahooSymbolFor(e.symbol), e.name });
            break;
        case (int)RotationSection::Bonds:
            for (const auto& y : treasuryYieldSeries())
                out.push_back({ SectionCellKind::Yield, y.display, y.cnbc, "", y.label });
            for (const auto& s : bondEtfSymbols())
                out.push_back({ SectionCellKind::Quote, s.display, s.cnbc, s.yahoo, s.label });
            break;
        case (int)RotationSection::Internals:
            out.push_back({ SectionCellKind::Quote, "VIX", ".VIX", "",
                            "CBOE Volatility Index (quote: CNBC)" });
            out.push_back({ SectionCellKind::InternalsAdv, "ADVANCERS", "", "",
                            "Share of Top 100 stocks up on the day — computed proxy" });
            out.push_back({ SectionCellKind::InternalsMedian, "MEDIAN", "", "",
                            "Median day change across the Top 100 — computed proxy" });
            out.push_back({ SectionCellKind::InternalsEqWt, "EQUAL WT", "", "",
                            "Equal-weight mean day change across the Top 100 — computed proxy" });
            break;
        default:
            break;  // Crypto has no section cells (the watchlist rules it)
    }
    return out;
}

// ------------------------------------------------------------------
// v0.6.15 additions: drill-in routing, user tickers, and the two
// display rules Jackson handed down with the v0.6.14 screenshot.

// The Yahoo symbol whose v8 chart draws a section cell's drill-in,
// or "" when the cell has no chart: Treasury yields and the
// computed Internals cells never chart (there is no traded
// instrument behind a computed proxy, and inventing one would be a
// fake chart). The Internals VIX cell carries no Yahoo id of its
// own (its sparkline borrows the Indexes row's ^VIX series), but
// the instrument is the same VIX, whose ^VIX chart verified live
// 2026-11-03 — so it drills in too. Index cells already carry
// their Yahoo symbols (^GSPC form) in the table.
inline std::string drillInYahooSymbol(const SectionCellDef& def) {
    if (def.kind != SectionCellKind::Quote) return "";
    if (!def.yahoo.empty()) return def.yahoo;
    if (def.display == "VIX") return "^VIX";
    return "";
}

// The title-bar context rotator (Fear & Greed / BTC dominance) is
// CRYPTO context: it shows only in the Crypto section (with the
// title bar's own room rule applied on top by the app), and hides
// entirely in every other section — no replacement content
// (Jackson, 2026-11-03: it "needs to be swapped off when switching
// off of crypto").
inline bool titleContextWanted(int sectionIdx, bool showFng, bool showDom) {
    return sectionIdx == (int)RotationSection::Crypto && (showFng || showDom);
}

// Section strip movement: every section rides the overflow marquee
// when its cells cannot fit. BONDS is the exception (Jackson,
// 2026-11-03: "add the move as well to bonds"): its natural extent
// (11 cells x 148 + gaps = 1788 px) FITS a wide bar where Indexes
// (1952 px) and Top 100 (far wider) still overflow — so on a wide
// bar Bonds sat fitted and static while the others scrolled. The
// Bonds strip therefore runs the marquee even when it fits, at
// the same speed as every other section (Layout's
// kMarqueeSpeedPxPerSec — one speed for the whole strip family).
inline bool sectionMarqueeForced(int sectionIdx) {
    return sectionIdx == (int)RotationSection::Bonds;
}

// ---- Stocks: the user's own tickers (v0.6.15) ---------------------
// Jackson: "for the stocks a spot to add the tickers." His symbols
// LEAD the Stocks rotation (his names first), the bundled Top 100
// follows; a symbol he names that is also in the Top 100 appears
// once, in his lead position. Quotes ride a third CNBC batch (the
// Top-100 halves are computed from the bundled table, so customs
// cannot join them), capped at one batch of 50.
inline constexpr size_t kMaxCustomStocks = 50;

// One raw Settings entry -> the stored form: trimmed, uppercased,
// only the characters a CNBC/Yahoo symbol can carry (letters,
// digits, the class-share dot). "" when the entry is unusable.
inline std::string normalizeStockTicker(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.';
        if (!ok) return "";
        out += c;
    }
    if (out.empty() || out.size() > 12) return "";
    if (out.front() == '.' || out.back() == '.') return "";
    return out;
}

// Stored-list hygiene, shared by the settings loader and Settings'
// Add: normalize, drop unusable entries, dedupe (first wins), cap.
inline std::vector<std::string> cleanCustomStocks(const std::vector<std::string>& raw) {
    std::vector<std::string> out;
    for (const auto& s : raw) {
        const std::string n = normalizeStockTicker(s);
        if (n.empty()) continue;
        if (std::find(out.begin(), out.end(), n) != out.end()) continue;
        out.push_back(n);
        if (out.size() >= kMaxCustomStocks) break;
    }
    return out;
}

// The Stocks section's cells with the user's tickers leading.
inline std::vector<SectionCellDef> stocksSectionCells(const std::vector<std::string>& customRaw) {
    const auto custom = cleanCustomStocks(customRaw);
    std::vector<SectionCellDef> out;
    for (const auto& sym : custom)
        out.push_back({ SectionCellKind::Quote, sym, sym, yahooSymbolFor(sym),
                        "Your ticker — CNBC public quotes" });
    for (const auto& e : top100Stocks()) {
        if (std::find(custom.begin(), custom.end(), std::string(e.symbol)) != custom.end())
            continue;  // already shown in the lead, in his position
        out.push_back({ SectionCellKind::Quote, e.symbol, e.symbol,
                        yahooSymbolFor(e.symbol), e.name });
    }
    return out;
}

// The custom batch's CNBC ids, in stored order (identity-mapped to
// display symbols by the client).
inline std::vector<std::string> cnbcCustomBatchIds(const std::vector<std::string>& customRaw) {
    return cleanCustomStocks(customRaw);
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
// The yield batch's CNBC ids (US3M form), in curve order — the
// v0.6.15 re-route's whole request, one batch of five.
inline std::vector<std::string> cnbcYieldBatchIds() {
    std::vector<std::string> out;
    for (const auto& y : treasuryYieldSeries()) out.emplace_back(y.cnbc);
    return out;
}

inline std::string yahooChartUrl(const std::string& yahooSymbol) {
    std::string sym;
    for (char c : yahooSymbol) sym += (c == '^') ? "%5E" : std::string(1, c);
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + sym +
           "?range=1mo&interval=1d";
}

// The drill-in chart's URL (v0.6.15): the v8 endpoint at the
// detail ranges' grains. D7 uses range=7d — verified live on
// query1, 2026-11-03 (92 bars of 30m for SPY). Same host and
// symbol escaping as the sparkline URL above; Model.hpp's
// candleRange() windows match what these return.
inline std::string yahooDetailChartUrl(const std::string& yahooSymbol, DetailRange range) {
    std::string sym;
    for (char c : yahooSymbol) sym += (c == '^') ? "%5E" : std::string(1, c);
    const char* grain = range == DetailRange::H24 ? "range=1d&interval=5m"
                      : range == DetailRange::D7  ? "range=7d&interval=30m"
                                                  : "range=1mo&interval=1d";
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + sym + "?" + grain;
}

} // namespace tickerbar
