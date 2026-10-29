// TickerBar core tests — plain asserts, no framework (MeterBar idiom).
// Fixtures are REAL payloads captured 2026-10-29 from the v0.1.1
// sources (Coinbase Exchange public API + CoinGecko markets), plus
// hand-broken variants and a captured CoinGecko 429 body.
#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <tickerbar/Json.hpp>
#include <tickerbar/Model.hpp>
#include <tickerbar/MarketParse.hpp>
#include <tickerbar/ContextParse.hpp>
#include <tickerbar/Format.hpp>
#include <tickerbar/Alerts.hpp>
#include <tickerbar/Stale.hpp>
#include <tickerbar/Arrange.hpp>
#include <tickerbar/ReserveState.hpp>
#include <tickerbar/Layout.hpp>
#include <tickerbar/State.hpp>
#include <tickerbar/BarLink.hpp>
#include <tickerbar/SemVer.hpp>
#include <tickerbar/ReleaseParse.hpp>
#include <tickerbar/WindowGeom.hpp>
#include <tickerbar/ChartMath.hpp>
#include <tickerbar/DataColors.hpp>
#include <tickerbar/Visualizer.hpp>
#include <tickerbar/Glow.hpp>
#include <tickerbar/Mascot.hpp>
#ifndef FIXTURES_DIR
#define FIXTURES_DIR "tests/fixtures"
#endif
static int failures = 0, checks = 0;
#define CHECK(cond) do{ ++checks; if(!(cond)){ ++failures; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} else { std::printf("ok   %s\n", #cond);} }while(0)
static std::string readFile(const std::string& name) {
    std::ifstream in(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
    std::stringstream ss; ss << in.rdbuf(); return ss.str();
}
static bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

int main() {
    using namespace tickerbar;

    // ---------- Json: basics ----------
    {
        auto v = parseJson(R"({"a": [1, 2.5, "x", true, null], "b": {"c": -3e2}})");
        CHECK(v && v->isObject());
        const Json* a = v->find("a");
        CHECK(a && a->isArray() && a->array.size() == 5);
        CHECK(a->at(0)->asNumber() && near(*a->at(0)->asNumber(), 1.0));
        CHECK(a->at(1)->asNumber() && near(*a->at(1)->asNumber(), 2.5));
        CHECK(a->at(2)->asString() && *a->at(2)->asString() == "x");
        CHECK(a->at(3)->asBool() && *a->at(3)->asBool());
        CHECK(a->at(4)->isNull());
        const Json* c = v->find("b")->find("c");
        CHECK(c && c->asNumber() && near(*c->asNumber(), -300.0));
        CHECK(v->find("zzz") == nullptr);
        // string escapes + \u
        auto s = parseJson(R"("a\nbé")");
        CHECK(s && s->asString() && *s->asString() == std::string("a\nb\xc3\xa9"));
        // numeric-string coercion (Coinbase style)
        auto n = parseJson(R"("79736.11")");
        CHECK(n && n->asNumber() && near(*n->asNumber(), 79736.11));
        auto junk = parseJson(R"("12x")");
        CHECK(junk && !junk->asNumber()); // trailing junk is not a number
    }
    // ---------- Json: malformed input never parses, never crashes ----------
    {
        CHECK(!parseJson(""));
        CHECK(!parseJson("{"));
        CHECK(!parseJson("[1,2"));
        CHECK(!parseJson(R"({"a":})"));
        CHECK(!parseJson("[1,2] trailing"));
        CHECK(!parseJson("nul"));
        CHECK(!parseJson(readFile("garbage.json")));
        CHECK(!parseJson(readFile("cb_stats_truncated.json")));
        // depth bomb: 5000 nested arrays must fail cleanly, not overflow the stack
        std::string deep(5000, '[');
        CHECK(!parseJson(deep));
    }
    // ---------- Coinbase stats: the real fixtures ----------
    {
        auto btc = parseCoinbaseStats(readFile("cb_stats_btc.json"), "BTC");
        CHECK(btc.has_value());
        CHECK(btc && btc->hasData && btc->name == "BTC");
        CHECK(btc && near(btc->price, 79736.11, 1e-6));       // "last" at capture
        CHECK(btc && near(btc->open24h, 80873.62, 1e-6));     // "open" = price 24h ago
        CHECK(btc && near(btc->high24h, 81715.01, 1e-6));
        CHECK(btc && near(btc->low24h, 79586.17, 1e-6));
        // volume arrives in COIN units and is stored as USD (x last)
        CHECK(btc && near(btc->dayVolumeUsd, 5391.14478694 * 79736.11, 1.0));
        CHECK(btc && btc->dayVolumeUsd > 1e8);                // majors trade > $100M/day
        auto pct = btc ? changePct(btc->price, btc->open24h) : std::nullopt;
        CHECK(pct && near(*pct, (79736.11 / 80873.62 - 1.0) * 100.0, 1e-6));
        auto eth = parseCoinbaseStats(readFile("cb_stats_eth.json"), "eth"); // symbol case-insensitive
        CHECK(eth && eth->name == "ETH" && near(eth->price, 2304.94, 1e-6));
    }
    // ---------- Coinbase stats: broken payloads rejected ----------
    {
        CHECK(!parseCoinbaseStats(readFile("cb_stats_wrongtypes.json"), "BTC"));   // object/null fields
        CHECK(!parseCoinbaseStats(readFile("cb_stats_missing_last.json"), "BTC")); // no price at all
        CHECK(!parseCoinbaseStats(readFile("cb_stats_zerolast.json"), "BTC"));     // a 0 price is not data
        CHECK(!parseCoinbaseStats(readFile("cb_stats_truncated.json"), "BTC"));
        CHECK(!parseCoinbaseStats(readFile("garbage.json"), "BTC"));
        CHECK(!parseCoinbaseStats("{}", "BTC"));
        CHECK(!parseCoinbaseStats("[]", "BTC"));
    }
    // ---------- Coinbase candles: newest-first in, ascending out ----------
    {
        auto cs = parseCoinbaseCandles(readFile("cb_candles_btc.json"));
        CHECK(cs.has_value());
        CHECK(cs && cs->size() >= 24);                        // 30 rows in the captured window
        bool sane = true, ascending = true;
        for (size_t i = 0; i < cs->size(); ++i) {
            auto& c = (*cs)[i];
            if (!(c.high >= c.low && c.close >= c.low - 1e-9 && c.close <= c.high + 1e-9)) sane = false;
            if (i && c.tOpenMs <= (*cs)[i - 1].tOpenMs) ascending = false;
        }
        CHECK(sane);
        CHECK(ascending);                                     // the API's newest-first order was reversed
        CHECK(cs && near(cs->back().close, 79775.68, 1e-6));  // newest captured close is last
        auto hi = candlesHigh(*cs), lo = candlesLow(*cs);
        CHECK(hi && lo && *hi >= *lo);
        double mx = 0; for (auto& c : *cs) mx = c.high > mx ? c.high : mx;
        CHECK(hi && near(*hi, mx, 1e-6));
        auto empty = parseCoinbaseCandles(readFile("cb_candles_empty.json"));
        CHECK(empty && empty->empty());                       // zero candles is VALID (nothing to draw)
        CHECK(!candlesHigh(*empty).has_value());
        CHECK(!parseCoinbaseCandles(readFile("cb_candles_brokenrow.json"))); // short/string row -> reject the set
        CHECK(!parseCoinbaseCandles(readFile("garbage.json")));
    }
    // ---------- Coinbase products: the validation universe ----------
    {
        auto syms = parseCoinbaseProducts(readFile("cb_products.json"));
        CHECK(syms.has_value());
        CHECK(syms && syms->size() >= 400);                   // 405 USD-online bases at capture
        auto has = [&](const char* s){ return syms && std::find(syms->begin(), syms->end(), s) != syms->end(); };
        CHECK(has("BTC") && has("ETH") && has("SOL"));
        CHECK(!has("ACX"));                                   // ACX-USD exists but is delisted -> excluded
        CHECK(!has("NOTACOIN"));
        CHECK(syms && std::is_sorted(syms->begin(), syms->end()));
        CHECK(!parseCoinbaseProducts(readFile("cb_products_wrongtypes.json"))); // object, not array
        CHECK(!parseCoinbaseProducts(readFile("garbage.json")));
        CHECK(!parseCoinbaseProducts("{}"));
    }
    // ---------- Routing: who serves which symbol, and their stale clocks ----------
    {
        CHECK(sourceForSymbol("HYPE") == PriceSource::CoinGecko);
        CHECK(sourceForSymbol("hype") == PriceSource::CoinGecko);
        CHECK(sourceForSymbol("BTC") == PriceSource::Coinbase);
        CHECK(sourceForSymbol("ETH") == PriceSource::Coinbase);
        CHECK(coinGeckoIdForSymbol("HYPE") && *coinGeckoIdForSymbol("HYPE") == "hyperliquid");
        CHECK(!coinGeckoIdForSymbol("BTC"));
        CHECK(staleAfterSecFor("BTC") == 60.0);               // 3x the 20 s Coinbase poll
        CHECK(staleAfterSecFor("HYPE") == 180.0);             // 3x the ~60 s CoinGecko poll
        CHECK(feedStatus(true, 120.0, staleAfterSecFor("HYPE")) == FeedStatus::Live);  // mid-cycle HYPE is LIVE
        CHECK(feedStatus(true, 181.0, staleAfterSecFor("HYPE")) == FeedStatus::Stale);
        CHECK(feedStatus(false, 0.0, staleAfterSecFor("HYPE")) == FeedStatus::Down);
    }
    // ---------- CoinGecko markets: the real HYPE fixture ----------
    {
        auto qs = parseCoinGeckoMarkets(readFile("cg_markets_hype.json"));
        CHECK(qs.has_value());
        CHECK(qs && qs->size() == 1);
        const GeckoQuote* q = qs ? &qs->front() : nullptr;
        CHECK(q && q->ctx.hasData && q->ctx.name == "HYPE");  // lowercase "hype" uppercased
        CHECK(q && near(q->ctx.price, 78.54, 1e-6));
        // open24h is derived from the source's own 24h % (-3.39382 at capture)
        CHECK(q && near(q->ctx.open24h, 78.54 / (1.0 + -3.39382 / 100.0), 1e-4));
        CHECK(q && q->ctx.open24h > q->ctx.price);            // a down day: reference sits above price
        auto pct = q ? changePct(q->ctx.price, q->ctx.open24h) : std::nullopt;
        CHECK(pct && near(*pct, -3.39382, 1e-3));             // round-trips to the source's own number
        CHECK(q && near(q->ctx.high24h, 82.35, 1e-6));
        CHECK(q && near(q->ctx.low24h, 78.11, 1e-6));
        CHECK(q && near(q->ctx.dayVolumeUsd, 793995817.0, 1.0));
        CHECK(q && near(q->ctx.marketCapUsd, 18811468566.0, 1000.0));
        CHECK(q && q->sparkline7d.size() == 168);             // 7 days of hourly points
        CHECK(q && near(q->sparkline7d.back(), 78.63113662504989, 1e-9));
        // the chart path: trailing 24h (25 hourly points) as candles
        auto candles = q ? candlesFromHourlySeries(q->sparkline7d, 1793246400000LL, 25) : std::vector<Candle>{};
        CHECK(candles.size() == 25);
        bool asc = true, flat = true;
        for (size_t i = 0; i < candles.size(); ++i) {
            if (i && candles[i].tOpenMs - candles[i - 1].tOpenMs != 3600000LL) asc = false;
            if (!(candles[i].open == candles[i].close && candles[i].high == candles[i].close
                  && candles[i].low == candles[i].close)) flat = false; // closes only — no invented range
        }
        CHECK(asc);
        CHECK(flat);
        CHECK(!candles.empty() && near(candles.back().close, q->sparkline7d.back(), 1e-9));
        CHECK(candlesFromHourlySeries({}, 1793246400000LL, 25).empty());
        CHECK(candlesFromHourlySeries(q->sparkline7d, 1793246400000LL, 100000).size() == 168); // count caps at series
    }
    // ---------- CoinGecko markets: broken payloads rejected ----------
    {
        CHECK(!parseCoinGeckoMarkets(readFile("cg_markets_wrongtypes.json"))); // price as object
        CHECK(!parseCoinGeckoMarkets(readFile("cg_markets_ratelimit.json"))); // captured 429 body: no array
        CHECK(!parseCoinGeckoMarkets(readFile("garbage.json")));
        CHECK(!parseCoinGeckoMarkets("{}"));
        auto empty = parseCoinGeckoMarkets(readFile("cg_markets_empty.json"));
        CHECK(empty && empty->empty());                       // an empty page is valid (nothing quoted)
    }
    // ---------- Model helpers ----------
    {
        CHECK(changePct(110, 100) && near(*changePct(110, 100), 10.0));
        CHECK(changePct(90, 100) && near(*changePct(90, 100), -10.0));
        CHECK(!changePct(100, 0));       // no reference -> no invented number
        CHECK(!changePct(100, -5));
        auto flat = normalizeSeries({ 7.0, 7.0, 7.0 });
        CHECK(flat.size() == 3 && flat[0] == 0.5f && flat[2] == 0.5f);
        auto ramp = normalizeSeries({ 0.0, 5.0, 10.0 });
        CHECK(ramp.size() == 3 && ramp[0] == 0.0f && ramp[1] == 0.5f && ramp[2] == 1.0f);
        CHECK(normalizeSeries({}).empty());
    }
    // ---------- Format: pinned strings ----------
    {
        CHECK(formatPrice(97123.4) == "$97,123.40");
        CHECK(formatPrice(3412.5) == "$3,412.50");
        CHECK(formatPrice(38.423) == "$38.42");
        CHECK(formatPrice(1000000.0) == "$1,000,000.00");
        CHECK(formatPrice(0.123456) == "$0.1235");
        CHECK(formatPrice(0.00123456) == "$0.001235");
        CHECK(decimalsForPrice(2.5) == 2 && decimalsForPrice(0.5) == 4 && decimalsForPrice(0.005) == 6);
        CHECK(formatPct(1.234) == "+1.23%");
        CHECK(formatPct(-0.456) == "-0.46%");
        CHECK(formatPct(0.0) == "+0.00%");
        CHECK(formatVolume(1.23e9) == "$1.23B");
        CHECK(formatVolume(4.567e8) == "$456.70M");
        CHECK(formatVolume(12345.0) == "$12.35K");
        CHECK(formatVolume(999.4) == "$999");
    }
    // ---------- Alerts: crossing state machine ----------
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        AlertLevels lv; lv.above = 100.0; lv.below = 90.0;
        AlertTracker tr;
        CHECK(tr.step(lv, 95.0) == AlertNone);            // first tick only primes
        CHECK(tr.step(lv, 99.0) == AlertNone);
        CHECK(tr.step(lv, 101.0) == AlertCrossedAbove);   // the cross
        CHECK(tr.step(lv, 102.0) == AlertNone);           // stays above: quiet
        CHECK(tr.step(lv, 100.0) == AlertNone);           // back at level: re-arms
        CHECK(tr.step(lv, 100.5) == AlertCrossedAbove);   // crosses again: fires again
        CHECK(tr.step(lv, 95.0) == AlertNone);
        CHECK(tr.step(lv, 89.0) == AlertCrossedBelow);
        CHECK(tr.step(lv, 88.0) == AlertNone);
        CHECK(tr.step(lv, nan) == AlertNone);             // NaN tick ignored
        CHECK(tr.step(lv, 89.5) == AlertNone);            // state survived the NaN (still below)
        // starting already beyond a level must NOT fire (nothing crossed)
        AlertTracker tr2;
        CHECK(tr2.step(lv, 150.0) == AlertNone);
        CHECK(tr2.step(lv, 151.0) == AlertNone);
        // unset levels never fire
        AlertLevels none;
        AlertTracker tr3;
        CHECK(tr3.step(none, 1.0) == AlertNone);
        CHECK(tr3.step(none, 1e9) == AlertNone);
        // reset() re-primes after an edit
        tr.reset();
        CHECK(tr.step(lv, 101.0) == AlertNone);
    }
    // ---------- Stale / backoff ----------
    {
        CHECK(feedStatus(false, 0) == FeedStatus::Down);
        CHECK(feedStatus(false, 9999) == FeedStatus::Down);
        CHECK(feedStatus(true, 0) == FeedStatus::Live);
        CHECK(feedStatus(true, 60) == FeedStatus::Live);
        CHECK(feedStatus(true, 61) == FeedStatus::Stale);
        CHECK(feedStatus(true, 3600) == FeedStatus::Stale);
        CHECK(retryDelaySec(0) == 0.0);
        CHECK(retryDelaySec(1) == 5.0);
        CHECK(retryDelaySec(2) == 10.0);
        CHECK(retryDelaySec(3) == 20.0);
        CHECK(retryDelaySec(4) == 40.0);
        CHECK(retryDelaySec(5) == 60.0);
        CHECK(retryDelaySec(12) == 60.0);
    }
    // ---------- Arrange math (ported rule) ----------
    {
        const IntRect mon{ 0, 0, 1920, 1080 };
        IntRect fits{ 100, 200, 800, 600 };
        CHECK(arrangeFit(fits, mon, 72) == fits);                       // already clear: untouched
        IntRect under{ 100, 10, 800, 600 };
        IntRect moved = arrangeFit(under, mon, 72);
        CHECK(moved.y == 72 && moved.x == 100 && moved.w == 800);      // slid to just under the strip
        IntRect huge{ -50, 0, 2500, 2000 };
        IntRect shrunk = arrangeFit(huge, mon, 72);
        CHECK(shrunk.w == 1920 && shrunk.h == 1008 && shrunk.x == 0 && shrunk.y == 72);
        IntRect deg{ 0, 0, 0, 0 };
        CHECK(arrangeFit(deg, mon, 72) == deg);                        // degenerate: unchanged
        CHECK(arrangeFit(fits, mon, 5000) == fits);                    // strip eats the monitor: unchanged
        // bottom-dock variant
        IntRect low{ 100, 900, 800, 300 };
        IntRect lifted = arrangeFitAbove(low, mon, 72);
        CHECK(lifted.y + lifted.h == 1008 && lifted.x == 100);
        IntRect high{ 100, 100, 800, 300 };
        CHECK(arrangeFitAbove(high, mon, 72) == high);
    }
    // ---------- State defaults (spec pins) ----------
    {
        TickerState st;
        CHECK(st.watchlist.size() == 3);
        CHECK(st.watchlist[0] == "BTC" && st.watchlist[1] == "ETH" && st.watchlist[2] == "HYPE");
        CHECK(st.reserveScreenSpace == false);
        CHECK(st.alertSound == false);
        CHECK(st.pinned == true);
        CHECK(st.pollSec == 20);
        CHECK(st.themeName == "Midnight Kids");
        CHECK(st.backgroundOpacity == 1.0f);
    }

    // ---- market context: Fear & Greed (live fixture) ----
    {
        auto fg = parseFearGreed(readFile("fng_ok.json"));
        CHECK(fg.has_value());
        CHECK(fg && fg->value >= 0 && fg->value <= 100);
        CHECK(fg && !fg->classification.empty());
        CHECK(fg && fg->timestamp > 0);
        CHECK(!parseFearGreed(readFile("fng_empty.json")));
        CHECK(!parseFearGreed(readFile("fng_wrongtypes.json")));
        CHECK(!parseFearGreed(readFile("fng_noclass.json")));
        CHECK(!parseFearGreed(readFile("fng_truncated.json")));
        CHECK(!parseFearGreed(readFile("garbage.json")));
        CHECK(!parseFearGreed("{}"));
        CHECK(!parseFearGreed("{\"data\":[{\"value\":\"140\",\"value_classification\":\"Greed\"}]}")); // out of range
    }
    // ---- market context: BTC dominance (live fixture) ----
    {
        auto dom = parseBtcDominance(readFile("coingecko_global.json"));
        CHECK(dom.has_value());
        CHECK(dom && *dom > 30.0 && *dom < 80.0);
        CHECK(!parseBtcDominance(readFile("coingecko_wrongtypes.json")));
        CHECK(!parseBtcDominance(readFile("coingecko_ratelimit.json")));
        CHECK(!parseBtcDominance(readFile("garbage.json")));
        CHECK(!parseBtcDominance("{}"));
    }
    // ---- context formatting + bands + staleness ----
    {
        CHECK(formatDominance(59.46) == "59.5%");
        CHECK(formatDominance(50.04) == "50.0%");
        CHECK(fngBandIndex(0) == 0 && fngBandIndex(24) == 0);
        CHECK(fngBandIndex(25) == 1 && fngBandIndex(44) == 1);
        CHECK(fngBandIndex(45) == 2 && fngBandIndex(55) == 2);
        CHECK(fngBandIndex(56) == 3 && fngBandIndex(74) == 3);
        CHECK(fngBandIndex(75) == 4 && fngBandIndex(100) == 4);
        CHECK(contextStale(false, 0));
        CHECK(!contextStale(true, 0));
        CHECK(!contextStale(true, 900));
        CHECK(contextStale(true, 901));
    }

    // ---------- v0.2.0: reserve machine (MeterBar port) ----------
    {
        const IntRect mon{ 0, 0, 1920, 1080 };
        ReserveMachine m;
        EdgeGaps flushTop{ 0, 1000, 0, 0 };
        IntRect barTop{ 0, 0, 1920, 72 };
        CHECK(m.update(true, flushTop, barTop, mon, 7) == ReserveAction::Register);
        CHECK(m.reserved() && m.edge() == ReserveEdge::Top);
        CHECK(m.update(true, flushTop, barTop, mon, 7) == ReserveAction::None); // identical repeat: no flap
        EdgeGaps deadZone{ 5, 1000, 0, 0 };
        IntRect barDead{ 0, 5, 1920, 72 };
        CHECK(m.update(true, deadZone, barDead, mon, 7) == ReserveAction::Update); // geometry drift: exactly one Update
        CHECK(m.update(true, deadZone, barDead, mon, 7) == ReserveAction::None);
        EdgeGaps clear{ 40, 1000, 0, 0 };
        CHECK(m.update(true, clear, IntRect{ 0, 40, 1920, 72 }, mon, 7) == ReserveAction::Release);
        CHECK(!m.reserved());
        // dead zone does not dock a non-reserved bar
        CHECK(m.update(true, deadZone, barDead, mon, 7) == ReserveAction::None);
        CHECK(!m.reserved());
        // bottom dock + switch to top in one move
        EdgeGaps flushBottom{ 1000, 0, 0, 0 };
        IntRect barBottom{ 0, 1008, 1920, 72 };
        CHECK(m.update(true, flushBottom, barBottom, mon, 7) == ReserveAction::Register);
        CHECK(m.edge() == ReserveEdge::Bottom);
        CHECK(m.update(true, flushTop, barTop, mon, 7) == ReserveAction::Switch);
        CHECK(m.edge() == ReserveEdge::Top);
        // toggle off always wins, exactly once
        CHECK(m.update(false, flushTop, barTop, mon, 7) == ReserveAction::Release);
        CHECK(m.update(false, flushTop, barTop, mon, 7) == ReserveAction::None);
        // strip rect: full-width strip anchored at the edge, bar thickness
        IntRect strip = ReserveMachine::stripRect(ReserveEdge::Bottom, barBottom, mon);
        CHECK(strip == IntRect({ 0, 1008, 1920, 72 }));
        // tall bar docks left/right only
        ReserveMachine m2;
        EdgeGaps flushLeft{ 0, 0, 0, 900 };
        CHECK(m2.update(true, flushLeft, IntRect{ 0, 0, 72, 1080 }, mon, 7) == ReserveAction::Register);
        CHECK(m2.edge() == ReserveEdge::Left);
    }
    // ---------- v0.2.0: edge-aware arrange ----------
    {
        const IntRect mon{ 0, 0, 1920, 1080 };
        IntRect w{ 10, 100, 800, 600 };
        IntRect fitL = arrangeFit(w, mon, ReserveEdge::Left, 72);
        CHECK(fitL.x == 72 && fitL.w == 800);
        IntRect fitR = arrangeFit(IntRect{ 1800, 100, 800, 600 }, mon, ReserveEdge::Right, 72);
        CHECK(fitR.x + fitR.w == 1920 - 72);
        CHECK(arrangeFreeArea(mon, ReserveEdge::Left, 72) == IntRect({ 72, 0, 1848, 1080 }));
        CHECK(arrangeFreeArea(mon, ReserveEdge::Bottom, 72) == IntRect({ 0, 0, 1920, 1008 }));
        // legacy wrappers still agree with the edge-aware core
        IntRect under{ 100, 10, 800, 600 };
        CHECK(arrangeFit(under, mon, 72) == arrangeFit(under, mon, ReserveEdge::Top, 72));
        CHECK(arrangeFitAbove(under, mon, 72) == arrangeFit(under, mon, ReserveEdge::Bottom, 72));
    }
    // ---------- v0.2.0: cell layout — the % is never truncated ----------
    {
        // measured widths at the render fonts (approx JUCE metrics):
        // symbol "BTC" bold 13.5 ~ 30 px; pct "-12.34%" bold 12.5 ~ 58 px
        for (int contentW = 40; contentW <= 200; contentW += 4) {
            auto l = coinCellTopRow(contentW, 30, 58);
            CHECK(pctFullyVisible(l, 58) || contentW < 58); // % full whenever the row can hold it at all
            CHECK(l.symbolW + l.gap + l.pctW <= contentW);
        }
        auto wide = coinCellTopRow(180, 30, 58);
        CHECK(wide.pctW == 58 && wide.symbolW == 180 - 58 - kTopRowGapPx);
        auto minCell = minCoinCellWidth(30, 58);
        auto atMin = coinCellTopRow(minCell - kCellPadPx, 30, 58);
        CHECK(pctFullyVisible(atMin, 58));
        CHECK(atMin.symbolW >= kMinSymbolPx);
        auto full = fullCoinCellWidth(30, 58);
        auto atFull = coinCellTopRow(full - kCellPadPx, 30, 58);
        CHECK(atFull.symbolW >= 30 && pctFullyVisible(atFull, 58));
    }
    // ---------- v0.2.0: 1h change from hourly candles ----------
    {
        std::vector<Candle> cs;
        const int64_t h = 3600LL * 1000LL;
        for (int i = 0; i < 25; ++i) {
            Candle c; c.tOpenMs = i * h; c.open = c.low = c.high = 100.0 + i; c.close = 100.0 + i; c.volume = 1.0;
            cs.push_back(c);
        }
        auto p1h = changePct1h(cs);
        CHECK(p1h && near(*p1h, (124.0 / 123.0 - 1.0) * 100.0, 1e-6)); // last close vs the candle 1h earlier
        CHECK(!changePct1h({ cs.front() }));                            // one candle: no hour of history
        std::vector<Candle> half{ cs[0], cs[1] };
        half[1].tOpenMs = h / 2;                                        // 30 min spacing: still no full hour back
        CHECK(!changePct1h(half));
        // candle range / volume honesty
        auto r = candleRange(cs);
        CHECK(r && r->hasVolume && near(r->maxVolume, 1.0));
        for (auto& c : cs) c.volume = 0.0;                              // CoinGecko closes-only shape
        auto r2 = candleRange(cs);
        CHECK(r2 && !r2->hasVolume && r2->hi >= r2->lo);
        CHECK(!candleRange({}));
    }
    // ---------- v0.2.0: alert notification text + new state defaults ----------
    {
        AlertLevels lv; lv.above = 100.0; lv.below = 90.0;
        CHECK(alertNotificationText("BTC", AlertCrossedAbove, lv, 101.5).find("BTC crossed above") == 0);
        CHECK(alertNotificationText("BTC", AlertCrossedBelow, lv, 88.0).find("BTC crossed below") == 0);
        CHECK(alertNotificationText("BTC", AlertNone, lv, 95.0).empty());
        TickerState st;
        CHECK(st.alertToasts == true);
        CHECK(st.linkEnabled == false);
    }

// ---- BarLink protocol geometry (v0.2.0, item k) ----
{
    using namespace tickerbar;
    LinkShared sh; const long long now = 100000;
    auto& peer = sh.slots[1]; peer.appId = 1; peer.linked = 1; peer.heartbeatMs = now; peer.rect = { 100, 0, 800, 40 };
    // snap: left edge 6 px from the peer's left edge aligns exactly
    auto s1 = snapToPeers({ 106, 300, 500, 40 }, sh, 0, now);
    CHECK(s1.x == 100);
    // snap: top edge 8 px above the peer's bottom stacks flush
    auto s2 = snapToPeers({ 400, 48, 500, 40 }, sh, 0, now);
    CHECK(s2.y == 40);
    // beyond the threshold: untouched
    auto s3 = snapToPeers({ 130, 300, 500, 40 }, sh, 0, now);
    CHECK(s3.x == 130 && s3.y == 300);
    // unlinked or stale peers never snap
    peer.linked = 0; CHECK(snapToPeers({ 106, 300, 500, 40 }, sh, 0, now).x == 106);
    peer.linked = 1; peer.heartbeatMs = now - kPeerStaleMs - 1;
    CHECK(snapToPeers({ 106, 300, 500, 40 }, sh, 0, now).x == 106);
    // never snap to self
    peer.heartbeatMs = now; peer.appId = 0;
    CHECK(snapToPeers({ 106, 300, 500, 40 }, sh, 0, now).x == 106);
    // follow: pure translation, size preserved
    auto f = followRect({ 50, 500, 300, 40 }, { 0, 0, 800, 40 }, { 25, -10, 800, 40 });
    CHECK(f.x == 75 && f.y == 490 && f.w == 300 && f.h == 40);
    // group dock stacking on all four edges
    IntRect lead{ 0, 0, 800, 40 }, fol{ 200, 300, 600, 36 };
    CHECK(groupDockRect(lead, fol, ReserveEdge::Top).y == 40);
    IntRect leadB{ 0, 1040, 800, 40 };
    CHECK(groupDockRect(leadB, fol, ReserveEdge::Bottom).y == 1040 - 36);
    CHECK(groupDockRect(lead, fol, ReserveEdge::Left).x == 800);
    IntRect leadR{ 1120, 0, 800, 40 };
    CHECK(groupDockRect(leadR, fol, ReserveEdge::Right).x == 1120 - 600);
    // v0.2.1: the cross axis ALIGNS to the leader (field fix — a
    // follower stacked under a sideways-moved peer used to keep its
    // own x and no longer sat under the peer). Vertical stacks align
    // left edges; horizontal stacks align top edges.
    IntRect leadM{ 350, 0, 800, 40 };
    CHECK(groupDockRect(leadM, fol, ReserveEdge::Top).x == 350);
    IntRect leadMB{ 350, 1040, 800, 40 };
    auto gdB = groupDockRect(leadMB, fol, ReserveEdge::Bottom);
    CHECK(gdB.y == 1040 - 36 && gdB.x == 350);
    IntRect leadL{ 0, 120, 40, 800 };
    auto gdL = groupDockRect(leadL, fol, ReserveEdge::Left);
    CHECK(gdL.x == 40 && gdL.y == 120);
    IntRect leadRM{ 1880, 120, 40, 800 };
    auto gdR = groupDockRect(leadRM, fol, ReserveEdge::Right);
    CHECK(gdR.x == 1880 - 600 && gdR.y == 120);
    // size is never touched by stacking
    CHECK(gdB.w == 600 && gdB.h == 36);
    // drag liveness is staleness-based
    LinkSlot d; d.dragSeq = 3; d.dragStampMs = now;
    CHECK(dragLive(d, now)); CHECK(!dragLive(d, now + kLeaderStaleMs + 1));
}
// ---- Background sync decisions (v0.2.1, item k hardening) ----
{
    using namespace tickerbar;
    const long long now = 200000;
    BgSyncState sync;
    // first sight of our own background is a change (nothing published yet)
    CHECK(bgIsLocalChange(sync, 1, 100));
    sync.publishedStyle = 1; sync.publishedPct = 100;
    CHECK(!bgIsLocalChange(sync, 1, 100));
    CHECK(bgIsLocalChange(sync, 1, 55));  // opacity moved
    CHECK(bgIsLocalChange(sync, 2, 100)); // style moved
    // a peer publishes a new background: adopted once
    LinkShared sh;
    auto& peer = sh.slots[0]; peer.appId = 0; peer.linked = 1; peer.heartbeatMs = now;
    peer.bgSeq = 7; peer.bgStyle = 2; peer.bgOpacityPct = 40;
    BgPacket out;
    CHECK(bgAdoption(sh, 1, sync, now, out));
    CHECK(out.style == 2 && out.opacityPct == 40);
    // the adopted values are now OUR published values: no echo publish
    CHECK(!bgIsLocalChange(sync, 2, 40));
    // the same seq is never adopted twice
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    // a genuinely new seq adopts again
    peer.bgSeq = 8; peer.bgStyle = 0; peer.bgOpacityPct = 90;
    CHECK(bgAdoption(sh, 1, sync, now, out));
    CHECK(out.style == 0 && out.opacityPct == 90);
    // unlinked peers, stale peers, seq 0 and self never supply a background
    peer.linked = 0; peer.bgSeq = 9;
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    peer.linked = 1; peer.heartbeatMs = now - kPeerStaleMs - 1;
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    peer.heartbeatMs = now; peer.bgSeq = 0;
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    peer.bgSeq = 11; peer.appId = 1; // the peer slot carries OUR id: self, ignored
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    // two peers changed at once: the LAST in slot order wins, both are marked seen
    LinkShared sh2; BgSyncState sync2;
    auto& p0 = sh2.slots[0]; p0.appId = 0; p0.linked = 1; p0.heartbeatMs = now; p0.bgSeq = 3; p0.bgStyle = 1; p0.bgOpacityPct = 70;
    auto& p2 = sh2.slots[2]; p2.appId = 2; p2.linked = 1; p2.heartbeatMs = now; p2.bgSeq = 4; p2.bgStyle = 2; p2.bgOpacityPct = 30;
    CHECK(bgAdoption(sh2, 1, sync2, now, out));
    CHECK(out.style == 2 && out.opacityPct == 30);
    CHECK(sync2.seenSeq[0] == 3 && sync2.seenSeq[2] == 4);
    CHECK(!bgAdoption(sh2, 1, sync2, now, out));
}


    // ---------- v0.3.0: SemVer ----------
    {
        SemVer v;
        CHECK(parseSemVer("v0.3.0", v) && v.major == 0 && v.minor == 3 && v.patch == 0);
        CHECK(parseSemVer("0.20.1", v) && v.minor == 20 && v.patch == 1);
        CHECK(!parseSemVer("banana", v));
        CHECK(semVerIsNewer("v0.3.0", "0.2.1"));
        CHECK(!semVerIsNewer("v0.2.1", "0.3.0"));
        CHECK(!semVerIsNewer("v0.3.0", "0.3.0"));
    }
    // ---------- v0.3.0: ReleaseParse ----------
    {
        ReleaseFields f;
        const std::string json = R"({"tag_name":"v0.3.0","body":"notes here","assets":[{"name":"TickerBar-Setup.exe","browser_download_url":"https://example.invalid/TickerBar-Setup.exe","size":1234567,"digest":"sha256:abcd"}]})";
        CHECK(parseReleaseJson(json, f));
        CHECK(f.tag == "v0.3.0" && f.hasAsset && f.assetSize == 1234567);
        CHECK(f.assetUrl.find("TickerBar-Setup.exe") != std::string::npos);
        CHECK(f.assetDigest == "sha256:abcd");
        ReleaseFields g;
        CHECK(parseReleaseJson(R"({"tag_name":"v9.9.9","assets":[]})", g));
        CHECK(!g.hasAsset);
    }
    // ---------- v0.3.0: WindowGeom clamp (off-screen restore fix) ----------
    {
        std::vector<IntRect> areas{ IntRect{0, 0, 1920, 1040} };
        IntRect inside{ 100, 100, 900, 96 };
        CHECK(clampRectToWorkAreas(inside, areas) == inside);
        IntRect offRight{ 5000, 100, 900, 96 };
        auto c1 = clampRectToWorkAreas(offRight, areas);
        CHECK(c1.x == 1920 - 900 && c1.y == 100 && c1.w == 900);
        IntRect offTop{ 100, -500, 900, 96 };
        auto c2 = clampRectToWorkAreas(offTop, areas);
        CHECK(c2.y == 0 && c2.x == 100);
        IntRect huge{ -50, -50, 4000, 2000 };
        auto c3 = clampRectToWorkAreas(huge, areas);
        CHECK(c3.x == 0 && c3.y == 0 && c3.w == 4000); // size preserved, pinned to origin
        std::vector<IntRect> two{ IntRect{0, 0, 1920, 1040}, IntRect{1920, 0, 1920, 1040} };
        IntRect onSecond{ 2000, 50, 800, 96 };
        CHECK(clampRectToWorkAreas(onSecond, two) == onSecond);
        CHECK(rectTouchesAnyArea(onSecond, two));
        CHECK(!rectTouchesAnyArea(IntRect{9000, 9000, 100, 100}, two));
        auto c4 = clampRectToWorkAreas(IntRect{9000, 9000, 100, 100}, two); // nearest area wins
        CHECK(c4.x + c4.w <= 3840 && c4.y + c4.h <= 1040);
        CHECK(clampRectToWorkAreas(inside, {}) == inside); // no areas = no opinion
    }
    // ---------- v0.3.0: ChartMath volume MA/SD ----------
    {
        std::vector<double> flat(60, 100.0);
        auto pts = volumeMaSd(flat);
        CHECK(pts.size() == 60);
        CHECK(near(pts[59].ma, 100.0) && near(pts[59].sd, 0.0));
        bool anyHot = false; for (auto& p : pts) anyHot = anyHot || p.hot;
        CHECK(!anyHot); // a flat series has no hot bars, by construction
        std::vector<double> spiky(30, 100.0); spiky.push_back(10000.0);
        auto sp = volumeMaSd(spiky);
        CHECK(sp.back().window == 31);
        CHECK(sp.back().hot);
        std::vector<double> early; early.push_back(5000.0); for (int i = 0; i < 30; ++i) early.push_back(100.0);
        auto ep = volumeMaSd(early);
        CHECK(!ep[0].hot); // window too small for a 3-sigma claim
        CHECK(ep[0].window == 1 && near(ep[0].ma, 5000.0));
    }
    // ---------- v0.3.0: DataColors ----------
    {
        for (int i = 0; i < kDataColorSchemeCount; ++i) {
            auto pr = dataColorPair(i);
            CHECK(pr.up != pr.down);
        }
        CHECK(dataColorPair(1).up == Rgb({248, 149, 64})); // Plasma .75 stop
        CHECK(std::string(dataColorSchemeName(0)) == "Family");
        CHECK(std::string(dataColorSchemeName(4)) == "Magma");
    }
    // ---------- v0.3.0: GlowField ----------
    {
        GlowSettings gs;
        GlowField a, b;
        const float zero[3] = { 0.f, 0.f, 0.f };
        for (int i = 0; i < 30; ++i) { a.update(1.f / 30.f, zero, 0.f); b.update(1.f / 30.f, zero, 0.f); }
        auto base = a.sample(0.5f, 0.5f, gs);
        CHECK(base.r <= 10 && base.g <= 10 && base.b <= 14); // near-black with no energy
        CHECK(base.r == b.sample(0.5f, 0.5f, gs).r);        // deterministic
        const float hot[3] = { 1.f, 0.8f, 0.6f };
        GlowField c;
        c.update(0.05f, hot, 0.1f); // the rise from silence is itself an onset...
        for (int i = 0; i < 60; ++i) c.update(0.05f, hot, 0.1f);
        CHECK(c.pump() < 0.2f);     // ...which decays away at a steady level
        c.update(0.05f, hot, 0.9f);
        CHECK(c.pump() > 0.5f); // a fresh jump slams the pump envelope
        for (int i = 0; i < 60; ++i) c.update(1.f / 30.f, hot, 0.9f);
        CHECK(c.band(0) > 0.9f); // smoothing converges to the target
        auto lit = c.sample(0.22f, 0.58f, gs); // low bloom centre
        CHECK(lit.r > 40 || lit.b > 40);       // the bloom actually lights
        CHECK(GlowField::pinstripe(0.0f) == 0.05f);
        CHECK(GlowField::pinstripe(0.01f) == 0.0f);
        float bands[3], lvl = 0.f;
        ambientBands(3.7, bands, lvl);
        CHECK(bands[0] >= 0.f && bands[0] <= 1.f && lvl >= 0.f && lvl <= 1.f);
    }
    // ---------- v0.3.0: visualizer emitter model ----------
    {
        VisualizerSettings vs; vs.enabled = true;
        VisualizerEngine e;
        const float loud[3] = { 0.9f, 0.2f, 0.2f };
        e.update(0.016f, loud, vs);
        CHECK(e.burstCount() >= 1);
        int heads = 0; for (auto& p : e.particles()) if (p.emitter) ++heads;
        CHECK(heads >= 3);
        // speed of a head eases out over time; strand dots appear
        float speed0 = 0.f;
        for (auto& p : e.particles()) if (p.emitter) { speed0 = std::sqrt(p.vx * p.vx + p.vy * p.vy); break; }
        for (int i = 0; i < 30; ++i) e.update(0.016f, loud, vs);
        int dots = 0; float speed1 = -1.f;
        for (auto& p : e.particles()) {
            if (p.burst && !p.emitter) ++dots;
            if (p.emitter && speed1 < 0.f) speed1 = std::sqrt(p.vx * p.vx + p.vy * p.vy);
        }
        CHECK(dots > 0);                       // heads shed strand dots
        CHECK(speed1 >= 0.f && speed1 < speed0); // ease-out deceleration
        // colour drifts with distance travelled
        VizParticle probe; probe.burst = true; probe.hueT = 0.2f; probe.dist = 0.f; probe.life = 10.f;
        auto cA = e.colorFor(probe, vs);
        probe.dist = 0.5f;
        auto cB = e.colorFor(probe, vs);
        CHECK(cA.r != cB.r || cA.g != cB.g || cA.b != cB.b);
        // cap respected even under sustained triggers
        VisualizerSettings small = vs; small.particleCap = 40;
        VisualizerEngine e2;
        for (int i = 0; i < 120; ++i) e2.update(0.016f, loud, small);
        CHECK((int)e2.particles().size() <= 40);
    }
    // ---------- v0.3.0: BarLink stack planner + audio + republish ----------
    {
        const long long now = 100000;
        LinkShared sh;
        auto& m = sh.slots[0]; m.appId = 0; m.linked = 1; m.heartbeatMs = now; m.rect = IntRect{100, 96, 800, 40};
        auto& t = sh.slots[1]; t.appId = 1; t.linked = 1; t.heartbeatMs = now; t.rect = IntRect{100, 0, 900, 96};
        t.stackEdge = 0; t.stackAnchor = t.rect;
        auto& s2 = sh.slots[2]; s2.appId = 2; s2.linked = 1; s2.heartbeatMs = now; s2.rect = IntRect{50, 300, 700, 108};
        IntRect targets[3];
        CHECK(planStackAll(sh, now, 1, targets) == 3);
        CHECK(targets[1] == t.rect);
        CHECK(targets[0].y == 96 && targets[0].x == 100);        // meter below the ticker, x aligned
        CHECK(targets[2].y == 96 + 40 && targets[2].x == 100);   // session below the meter
        s2.linked = 0;
        CHECK(planStackAll(sh, now, 1, targets) == 2);
        CHECK(targets[2].w == 0);
        t.stackEdge = 1; // bottom edge: stack upward
        s2.linked = 1;
        CHECK(planStackAll(sh, now, 1, targets) == 3);
        CHECK(targets[0].y == 0 - 40);
        float pkt[4] = { -1, -1, -1, -1 };
        CHECK(!audioPacket(m, now, pkt)); // never published
        m.audioSeq = 7; m.audioLow = 0.5f; m.audioMid = 0.25f; m.audioHigh = 0.125f; m.audioLevel = 0.75f;
        CHECK(audioPacket(m, now, pkt));
        CHECK(near(pkt[0], 0.5) && near(pkt[3], 0.75));
        CHECK(!audioPacket(m, now + kPeerStaleMs + 1, pkt)); // stale heartbeat = not live
        BgSyncState bs; bs.publishedStyle = 2; bs.publishedPct = 80;
        CHECK(!bgIsLocalChange(bs, 2, 80));
        bgForceRepublish(bs);
        CHECK(bgIsLocalChange(bs, 2, 80)); // re-stamp forced
    }
    // ---------- v0.3.0: cell width solver + reorder math ----------
    {
        auto w = solveCellWidths(1000, 8, {300, 0, 0}, 100);
        CHECK(w[0] == 300 && w[1] == 342 && w[2] == 342);
        auto w2 = solveCellWidths(500, 10, {0, 0}, 60);
        CHECK(w2[0] + w2[1] == 490);
        auto pr = adjustPairWidths(200, 200, 50, 80);
        CHECK(pr.first == 250 && pr.second == 150);
        auto pr2 = adjustPairWidths(200, 200, 500, 80);
        CHECK(pr2.first == 320 && pr2.second == 80); // min width clamps
        CHECK(pr2.first + pr2.second == 400);        // pair sum preserved
        CHECK(dropSlot({100, 300, 500}, 350) == 2);
        CHECK(dropSlot({100, 300, 500}, 50) == 0);
        CHECK(indexOrderAfterMove(4, 0, 2) == std::vector<int>({1, 2, 0, 3}));
        CHECK(indexOrderAfterMove(4, 3, 0) == std::vector<int>({3, 0, 1, 2}));
    }
    // ---------- v0.3.0: Mascot envelope ----------
    {
        MascotAnim ma;
        CHECK(ma.bobPx(1000) == 0.f && !ma.excited(1000));
        ma.triggerBob(1000);
        CHECK(ma.bobPx(1000) == 0.f);
        CHECK(ma.bobPx(1065) < -4.9f); // half-sine peak mid-hop
        CHECK(ma.bobPx(1200) == 0.f);
        ma.triggerExcited(2000, 2000);
        CHECK(ma.excited(3999) && !ma.excited(4001));
    }
    // ---------- v0.3.0: state defaults ----------
    {
        TickerState d;
        CHECK(!d.clickSounds && d.bgEffect == 0 && d.dataColorScheme == 0);
        CHECK(d.uiVer == 3 && d.cellWidths.empty());
        CHECK((int)BackgroundStyle::Pastel == 3);
        CHECK(d.windowH == 96);
    }
    // ---------- post-v0.3.0: context cells (Vishona's request) ----------
    {
        TickerState d;
        CHECK(d.showFearGreed && d.showBtcDominance); // both shown by default
        CHECK(d.fngCellWidth == 0 && d.domCellWidth == 0); // 0 = default extent
        // defaults are the pre-request fixed chrome sizes
        CHECK(contextCellExtent(0, false) == 112);
        CHECK(contextCellExtent(0, true) == 66);
        // a dragged-in width is honoured in both orientations
        CHECK(contextCellExtent(200, false) == 200);
        CHECK(contextCellExtent(200, true) == 200);
        // clamps: below the drag floor and above the cap
        CHECK(contextCellExtent(10, false) == kContextCellMin);
        CHECK(contextCellExtent(9999, false) == kContextCellMax);
        // negative stored values count as unset
        CHECK(contextCellExtent(-5, false) == 112);
        // the pair-drag math the context dividers use: the pair
        // total is preserved and neither side crosses the floor
        auto cp = adjustPairWidths(112, 112, 40, kContextCellMin);
        CHECK(cp.first == 152 && cp.second == 72);
        auto cp2 = adjustPairWidths(112, 112, -500, kContextCellMin);
        CHECK(cp2.first == kContextCellMin && cp2.second == 224 - kContextCellMin);
    }
    // ---------- v0.3.2: layout fill + trailing divider + reset (Vishona's follow-ups) ----------
    {
        // THE FIELD BUG: every coin explicit (the post-drag steady
        // state) and the context cells hidden — the explicit sum
        // undershoots the strip and the leftover was dead space.
        // Now the last cell absorbs it and the row fills exactly.
        auto f = solveCellWidths(1000, 16, {200, 200, 200}, 90);
        CHECK(f[0] == 200 && f[1] == 200);
        CHECK(f[2] == 568); // 1000 - 2*16 gaps - 400 = 568: the last cell fills
        CHECK(f[0] + f[1] + f[2] + 2 * 16 == 1000);
        // exact fit: nothing to absorb, widths stand
        auto fx = solveCellWidths(632, 16, {200, 200, 200}, 90);
        CHECK(fx[0] == 200 && fx[1] == 200 && fx[2] == 200);
        // overshoot: explicit widths stand (the window floor keeps
        // this out of the app; the solver never shrinks a drag)
        auto fo = solveCellWidths(500, 16, {200, 200, 200}, 90);
        CHECK(fo[0] == 200 && fo[1] == 200 && fo[2] == 200);
        // a single explicit cell fills the whole strip
        auto f1 = solveCellWidths(800, 16, {300}, 90);
        CHECK(f1.size() == 1 && f1[0] == 800);
        // mixed explicit/auto still shares the rest (pre-v0.3.2 rule)
        auto fm = solveCellWidths(1000, 16, {200, 0, 200}, 90);
        CHECK(fm[0] == 200 && fm[1] == 568 && fm[2] == 200);
        // trailing-divider clamp: the window extent follows the
        // drag inside its limits and stops at them
        CHECK(trailingWindowExtent(1000, 50, 600, 3840) == 1050);
        CHECK(trailingWindowExtent(1000, -50, 600, 3840) == 950);
        CHECK(trailingWindowExtent(1000, -9999, 600, 3840) == 600);
        CHECK(trailingWindowExtent(1000, 9999, 600, 3840) == 3840);
        // reset: layout fields back to factory, nothing else moves
        TickerState s;
        s.watchlist = { "HYPE", "SOL", "BTC", "ETH" }; // scrambled + one extra
        s.cellWidths = { { "BTC", 300 }, { "HYPE", 150 } };
        s.fngCellWidth = 200; s.domCellWidth = 180;
        s.showFearGreed = false; s.showBtcDominance = false;
        s.pollSec = 60; s.themeName = "Anime"; s.clickSounds = true;
        AlertLevels lv; lv.above = 100000.0; s.alerts["BTC"] = lv;
        resetLayoutToDefaults(s);
        CHECK(s.cellWidths.empty());
        CHECK(s.fngCellWidth == 0 && s.domCellWidth == 0);
        CHECK(s.showFearGreed && s.showBtcDominance);
        CHECK(s.watchlist == std::vector<std::string>({ "BTC", "ETH", "HYPE", "SOL" })); // trio restored, extra kept
        CHECK(s.pollSec == 60 && s.themeName == "Anime" && s.clickSounds); // untouched settings
        CHECK(s.alerts.count("BTC") == 1 && s.alerts["BTC"].above == 100000.0); // alerts untouched
        // reset on a factory state is a no-op
        TickerState d2;
        resetLayoutToDefaults(d2);
        CHECK(d2.watchlist == TickerState{}.watchlist && d2.cellWidths.empty());
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

