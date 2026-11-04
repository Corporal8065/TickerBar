// TickerBar core tests — plain asserts, no framework (MeterBar idiom).
// Fixtures are REAL payloads captured 2026-10-29 from the v0.1.1
// sources (Coinbase Exchange public API + CoinGecko markets), plus
// hand-broken variants and a captured CoinGecko 429 body.
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <tickerbar/Json.hpp>
#include <tickerbar/Model.hpp>
#include <tickerbar/MarketParse.hpp>
#include <tickerbar/HyperliquidParse.hpp> // v0.6.12: drill-in candles for CoinGecko-routed symbols
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
#include <tickerbar/Glyphs.hpp>
#include <tickerbar/Sections.hpp>
#include <tickerbar/SectionParse.hpp>
#include "TickerBarVersion.h" // v0.5.3: generated from the root project() VERSION
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
    // ---------- Arrange: maximized windows (v0.5.7) ----------
    {
        const IntRect zoomedRect{ 0, 0, 1920, 1080 };   // a maximized window's live rect: the whole monitor
        const IntRect normalRect{ 100, 10, 800, 600 };  // its rcNormalPosition — overlaps the top strip
        // zoomed: participates, restore-first, fit input is the NORMAL rect (never the zoomed one)
        auto zp = planArrangeWindow(true, false, true, zoomedRect, normalRect);
        CHECK(zp.participate && zp.restoreFirst && zp.fitInput == normalRect);
        // ...and the fit of that normal rect slides it clear of the strip, like any window
        CHECK(arrangeFit(zp.fitInput, IntRect{ 0, 0, 1920, 1080 }, 72).y == 72);
        // normal window: participates on its live rect, no restore step
        auto np = planArrangeWindow(true, false, false, normalRect, normalRect);
        CHECK(np.participate && !np.restoreFirst && np.fitInput == normalRect);
        // the historical skips stay skips: invisible, iconic — zoomed or not
        CHECK(!planArrangeWindow(false, false, true, zoomedRect, normalRect).participate);
        CHECK(!planArrangeWindow(true, true, true, zoomedRect, normalRect).participate);
        // a zoomed window with a degenerate normal rect is left alone
        CHECK(!planArrangeWindow(true, false, true, zoomedRect, IntRect{ 0, 0, 0, 0 }).participate);
        // restore plan: a was-maximized snapshot re-maximizes after its rect is applied; others don't
        auto rp = planRestoreWindow(true);
        CHECK(rp.applyRect && rp.remaximize);
        auto rp2 = planRestoreWindow(false);
        CHECK(rp2.applyRect && !rp2.remaximize);
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
    CHECK(bgIsLocalChange(sync, 1, 100, 0));
    sync.publishedStyle = 1; sync.publishedPct = 100; sync.publishedEffect = 0;
    CHECK(!bgIsLocalChange(sync, 1, 100, 0));
    CHECK(bgIsLocalChange(sync, 1, 55, 0));  // opacity moved
    CHECK(bgIsLocalChange(sync, 2, 100, 0)); // style moved
    CHECK(bgIsLocalChange(sync, 1, 100, 1)); // effect moved (BLK3)
    // a peer publishes a new background: adopted once
    LinkShared sh;
    auto& peer = sh.slots[0]; peer.appId = 0; peer.linked = 1; peer.heartbeatMs = now;
    peer.bgSeq = 7; peer.bgStyle = 2; peer.bgOpacityPct = 40; peer.bgEffect = 2;
    BgPacket out;
    CHECK(bgAdoption(sh, 1, sync, now, out));
    CHECK(out.style == 2 && out.opacityPct == 40 && out.effect == 2);
    // the adopted values are now OUR published values: no echo publish
    CHECK(!bgIsLocalChange(sync, 2, 40, 2));
    // the same seq is never adopted twice
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    // a genuinely new seq adopts again
    peer.bgSeq = 8; peer.bgStyle = 0; peer.bgOpacityPct = 90; peer.bgEffect = 1;
    CHECK(bgAdoption(sh, 1, sync, now, out));
    CHECK(out.style == 0 && out.opacityPct == 90 && out.effect == 1);
    // BLK4: effect 3 (visualizer Lines) rides the same packet,
    // adopted verbatim like every other ordinal
    peer.bgSeq = 12; peer.bgEffect = 3;
    CHECK(bgAdoption(sh, 1, sync, now, out));
    CHECK(out.effect == 3);
    CHECK(!bgIsLocalChange(sync, 0, 90, 3)); // adopted = published: no echo
    // unlinked peers, stale peers, seq 0 and self never supply a background
    peer.linked = 0; peer.bgSeq = 9;
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    peer.linked = 1; peer.heartbeatMs = now - kPeerStaleMs - 1;
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    peer.heartbeatMs = now; peer.bgSeq = 0;
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    peer.bgSeq = 11; peer.appId = 1; // the peer slot carries OUR id: self, ignored
    CHECK(!bgAdoption(sh, 1, sync, now, out));
    // v0.6.6 parent ownership: two peers changed at once — the
    // PARENT (lowest appId) supplies the background; the follower
    // peer's packet is not consumed at all.
    LinkShared sh2; BgSyncState sync2;
    auto& p0 = sh2.slots[0]; p0.appId = 0; p0.linked = 1; p0.heartbeatMs = now; p0.bgSeq = 3; p0.bgStyle = 1; p0.bgOpacityPct = 70;
    auto& p2 = sh2.slots[2]; p2.appId = 2; p2.linked = 1; p2.heartbeatMs = now; p2.bgSeq = 4; p2.bgStyle = 2; p2.bgOpacityPct = 30;
    CHECK(bgAdoption(sh2, 1, sync2, now, out));
    CHECK(out.style == 1 && out.opacityPct == 70);
    CHECK(sync2.seenSeq[0] == 3 && sync2.seenSeq[2] == 0);
    CHECK(sync2.adoptedFrom == 0);
    CHECK(!bgAdoption(sh2, 1, sync2, now, out));
    // the parent itself adopts from NOBODY, however fresh the
    // followers' packets are (the pre-v0.6.6 swap defect: at link
    // join each bar adopted the other's stale background)
    BgSyncState syncP;
    CHECK(!bgAdoption(sh2, 0, syncP, now, out));
    CHECK(syncP.adoptedFrom == -1);
    // with the parent gone, THIS bar (appId 1) becomes the parent:
    // it adopts from nobody either, and its adoptedFrom resets so a
    // later parent is never mistaken for the source of its values
    p0.linked = 0;
    CHECK(!bgAdoption(sh2, 1, sync2, now, out));
    CHECK(sync2.adoptedFrom == -1);
    // parent turnover, seen from the follower's side: with appId 0
    // live, appId 2 adopts ITS packet; with 0 gone, appId 1 is the
    // parent and appId 2 adopts 1's standing packet — even though
    // that seq was never consumed before
    LinkShared sh3; BgSyncState sync3;
    auto& q0 = sh3.slots[0]; q0.appId = 0; q0.linked = 1; q0.heartbeatMs = now; q0.bgSeq = 5; q0.bgStyle = 3; q0.bgOpacityPct = 80;
    auto& q1 = sh3.slots[1]; q1.appId = 1; q1.linked = 1; q1.heartbeatMs = now; q1.bgSeq = 6; q1.bgStyle = 2; q1.bgOpacityPct = 30;
    CHECK(bgAdoption(sh3, 2, sync3, now, out));
    CHECK(out.style == 3 && out.opacityPct == 80);
    CHECK(sync3.adoptedFrom == 0);
    q0.linked = 0;
    CHECK(bgAdoption(sh3, 2, sync3, now, out));
    CHECK(out.style == 2 && out.opacityPct == 30);
    CHECK(sync3.adoptedFrom == 1);
    // and when the original parent returns with the SAME seq it had
    // before, its packet is authoritative again — adoptedFrom, not
    // the seen marker, decides (the values changed hands meanwhile)
    q0.linked = 1;
    CHECK(bgAdoption(sh3, 2, sync3, now, out));
    CHECK(out.style == 3 && out.opacityPct == 80);
    CHECK(sync3.adoptedFrom == 0);
    CHECK(!bgAdoption(sh3, 2, sync3, now, out));
}
// ---- BLK6: first-clicked-wins parentage + member-drag leadership ----
{
    using namespace tickerbar;
    const long long now = 300000;
    BgPacket out;
    // FIRST-CLICKED WINS: appId 2 claimed linkSeq 1 (clicked first),
    // appId 0 claimed 3 — the parent is 2, NOT the lowest appId (the
    // v0.25.1 fixed-slot rule Jackson's 2026-11-01 field test rejected).
    LinkShared sh;
    auto& s0 = sh.slots[0]; s0.appId = 0; s0.linked = 1; s0.heartbeatMs = now; s0.linkSeq = 3; s0.bgSeq = 4; s0.bgStyle = 1; s0.bgOpacityPct = 70;
    auto& s1 = sh.slots[1]; s1.appId = 1; s1.linked = 1; s1.heartbeatMs = now; s1.linkSeq = 2; s1.bgSeq = 5; s1.bgStyle = 0; s1.bgOpacityPct = 90;
    auto& s2 = sh.slots[2]; s2.appId = 2; s2.linked = 1; s2.heartbeatMs = now; s2.linkSeq = 1; s2.bgSeq = 6; s2.bgStyle = 2; s2.bgOpacityPct = 30;
    CHECK(bgParentApp(sh, 1, 2, now) == 2);
    BgSyncState sy1;
    CHECK(bgAdoption(sh, 1, sy1, now, out));
    CHECK(out.style == 2 && out.opacityPct == 30 && sy1.adoptedFrom == 2);
    CHECK(sy1.seenSeq[0] == 0); // the lowest-appId non-parent packet is never consumed
    // the first-clicked bar itself adopts from nobody
    BgSyncState sy2;
    CHECK(!bgAdoption(sh, 2, sy2, now, out));
    // the parent's SLOT is resolved by appId after selection: with
    // self's own slot sitting between the parent's and a follower's,
    // adoption must still read the PARENT's packet (a first draft of
    // bgParentApp returned self's slot index here and adopted self).
    {
        LinkShared shS; BgSyncState syS;
        auto& q0 = shS.slots[0]; q0.appId = 0; q0.linked = 1; q0.heartbeatMs = now; q0.linkSeq = 1; q0.bgSeq = 21; q0.bgStyle = 3; q0.bgOpacityPct = 25;
        auto& q1 = shS.slots[1]; q1.appId = 1; q1.linked = 1; q1.heartbeatMs = now; q1.linkSeq = 0; q1.bgSeq = 22; // self unclaimed: the geometry that exposed the slot bug q1.bgStyle = 0; q1.bgOpacityPct = 99;
        auto& q2 = shS.slots[2]; q2.appId = 2; q2.linked = 1; q2.heartbeatMs = now; q2.linkSeq = 3; q2.bgSeq = 23; q2.bgStyle = 1; q2.bgOpacityPct = 55;
        CHECK(bgAdoption(shS, 1, syS, now, out));
        CHECK(out.style == 3 && out.opacityPct == 25 && syS.adoptedFrom == 0);
        CHECK(syS.seenSeq[0] == 21 && syS.seenSeq[1] == 0 && syS.seenSeq[2] == 0);
    }
    // LATE JOINER NEVER PARENT: appId 0 re-claims with the largest
    // linkSeq and a fresh packet — the group still follows appId 2,
    // and the joiner adopts the parent's packet from its own seat.
    LinkShared shJ = sh; shJ.slots[0].linkSeq = 9; shJ.slots[0].bgSeq = 40;
    BgSyncState syJ;
    CHECK(bgAdoption(shJ, 0, syJ, now, out));
    CHECK(syJ.adoptedFrom == 2 && out.opacityPct == 30);
    // PARENT PACKET ARRIVES AFTER THE FOLLOWER'S FIRST SCAN: the
    // follower scanned while the first-clicked bar's slot was not yet
    // in the block (its app started later); when the parent's packet
    // appears, adoption fires on that tick.
    LinkShared shL; BgSyncState syL;
    auto& l1 = shL.slots[1]; l1.appId = 1; l1.linked = 1; l1.heartbeatMs = now; l1.linkSeq = 2; l1.bgSeq = 3; l1.bgStyle = 0; l1.bgOpacityPct = 90;
    CHECK(!bgAdoption(shL, 1, syL, now, out)); // alone in the block: self is parent
    auto& l0 = shL.slots[0]; l0.appId = 0; l0.linked = 1; l0.heartbeatMs = now; l0.linkSeq = 1; l0.bgSeq = 2; l0.bgStyle = 3; l0.bgOpacityPct = 15;
    CHECK(bgAdoption(shL, 1, syL, now, out));
    CHECK(out.style == 3 && out.opacityPct == 15 && syL.adoptedFrom == 0);
    // HANDOFF: the parent unlinks — the earliest-linked REMAINING
    // member becomes parent, and its standing packet is adopted even
    // though that bgSeq was already seen (identity change re-fires).
    LinkShared shH; BgSyncState syH;
    auto& h0 = shH.slots[0]; h0.appId = 0; h0.linked = 1; h0.heartbeatMs = now; h0.linkSeq = 1; h0.bgSeq = 7; h0.bgStyle = 1; h0.bgOpacityPct = 60;
    auto& h1 = shH.slots[1]; h1.appId = 1; h1.linked = 1; h1.heartbeatMs = now; h1.linkSeq = 2; h1.bgSeq = 8; h1.bgStyle = 2; h1.bgOpacityPct = 45;
    auto& h2 = shH.slots[2]; h2.appId = 2; h2.linked = 1; h2.heartbeatMs = now; h2.linkSeq = 3; h2.bgSeq = 9; h2.bgStyle = 0; h2.bgOpacityPct = 95;
    CHECK(bgAdoption(shH, 2, syH, now, out)); CHECK(syH.adoptedFrom == 0);
    syH.seenSeq[1] = 8; // h1's packet was consumed under the old regime
    h0.linked = 0;
    CHECK(bgParentApp(shH, 2, 3, now) == 1);
    CHECK(bgAdoption(shH, 2, syH, now, out));
    CHECK(out.style == 2 && out.opacityPct == 45 && syH.adoptedFrom == 1);
    // A RELINKED PARENT RETURNS AS A LATE JOINER: it claims a fresh
    // (largest) linkSeq, so it now ADOPTS from the standing parent.
    h0.linked = 1; h0.linkSeq = 10;
    BgSyncState syH0;
    CHECK(bgAdoption(shH, 0, syH0, now, out));
    CHECK(syH0.adoptedFrom == 1 && out.opacityPct == 45);
    // MEMBER-DRAG LEADERSHIP: whichever member leads a drag session,
    // every other member's follow rect translates by the leader's
    // delta with size preserved — the pure half of Jackson's rule
    // "dragging ANY linked bar moves the whole stack" (the client
    // half — every bar's geometry feed stamps a session — is pinned
    // by the TickerBar v0.6.7 wiring fix; it was the missing half).
    for (int leader = 0; leader < 3; ++leader) {
        LinkShared shD;
        for (int a = 0; a < 3; ++a) {
            auto& sl = shD.slots[a]; sl.appId = a; sl.linked = 1; sl.heartbeatMs = now;
            sl.rect = IntRect{ 100 + a * 700, 200, 600, 40 };
        }
        auto& ld = shD.slots[leader];
        ld.dragSeq = 5; ld.dragStart = ld.rect; ld.dragStampMs = now;
        ld.rect = IntRect{ ld.rect.x + 30, ld.rect.y - 12, ld.rect.w, ld.rect.h };
        CHECK(dragLive(ld, now));
        for (int a = 0; a < 3; ++a) {
            if (a == leader) continue;
            auto fr = followRect(shD.slots[a].rect, ld.dragStart, ld.rect);
            CHECK(fr.x == shD.slots[a].rect.x + 30 && fr.y == shD.slots[a].rect.y - 12);
            CHECK(fr.w == 600 && fr.h == 40);
        }
    }
}


// ---- v0.6.8: CONTINUOUS adoption (defect 6) + full settings reset ----
{
    using namespace tickerbar;
    const long long now = 400000;
    BgPacket out;
    // Parent = appId 0 (linkSeq 1, clicked first); self = appId 1.
    LinkShared sh;
    auto& p0 = sh.slots[0]; p0.appId = 0; p0.linked = 1; p0.heartbeatMs = now; p0.linkSeq = 1; p0.bgSeq = 5; p0.bgStyle = 1; p0.bgOpacityPct = 70; p0.bgEffect = 2;
    auto& p1 = sh.slots[1]; p1.appId = 1; p1.linked = 1; p1.heartbeatMs = now; p1.linkSeq = 2; p1.bgSeq = 3; p1.bgStyle = 0; p1.bgOpacityPct = 100; p1.bgEffect = 0;
    BgSyncState sy;
    // adoption at join (style + opacity + effect all ride the packet)
    CHECK(bgAdoption(sh, 1, sy, now, out));
    CHECK(out.style == 1 && out.opacityPct == 70 && out.effect == 2);
    CHECK(!bgAdoption(sh, 1, sy, now, out)); // settled: nothing to do
    // the parent's LATER change while linked adopts on its new seq
    p0.bgSeq = 6; p0.bgStyle = 3; p0.bgOpacityPct = 25; p0.bgEffect = 4;
    CHECK(bgAdoption(sh, 1, sy, now, out));
    CHECK(out.style == 3 && out.opacityPct == 25 && out.effect == 4);
    CHECK(!bgAdoption(sh, 1, sy, now, out));
    // a change made FROM THE FOLLOWER while linked: the follower's
    // publish path records its own divergent values as published —
    // adoption must re-fire on the SAME parent seq and restore the
    // parent's values (pre-v0.6.8 the gate stayed shut forever).
    sy.publishedStyle = 2; sy.publishedPct = 90; sy.publishedEffect = 1; // the follower's local change, as publishBg records it
    CHECK(bgAdoption(sh, 1, sy, now, out));
    CHECK(out.style == 3 && out.opacityPct == 25 && out.effect == 4);
    CHECK(sy.publishedStyle == 3 && sy.publishedPct == 25 && sy.publishedEffect == 4); // parent's values are the published values again: no echo, no loop
    CHECK(!bgAdoption(sh, 1, sy, now, out));
    // a forced republish (published markers reset) also re-adopts
    // the standing parent packet rather than re-stamping over it
    bgForceRepublish(sy);
    CHECK(bgAdoption(sh, 1, sy, now, out));
    CHECK(out.style == 3 && out.opacityPct == 25);
    // parent turnover re-adopts: the parent unlinks, self becomes
    // the parent (adopts from nobody, adoptedFrom resets)...
    p0.linked = 0;
    CHECK(!bgAdoption(sh, 1, sy, now, out));
    CHECK(sy.adoptedFrom == -1);
    // ...and from a third bar's seat, the takeover parent's standing
    // packet is adopted even mid-divergence
    LinkShared shT = sh;
    auto& t2 = shT.slots[2]; t2.appId = 2; t2.linked = 1; t2.heartbeatMs = now; t2.linkSeq = 3; t2.bgSeq = 9; t2.bgStyle = 0; t2.bgOpacityPct = 55; t2.bgEffect = 0;
    BgSyncState syT; syT.publishedStyle = 0; syT.publishedPct = 55; syT.publishedEffect = 0; syT.adoptedFrom = 2;
    CHECK(bgAdoption(shT, 2, syT, now, out));
    CHECK(syT.adoptedFrom == 1 && out.style == 0 && out.opacityPct == 100 && out.effect == 0);
    // the magic / packet layout did NOT move in v0.6.8 (BLK6 family
    // compatibility with already-shipped builds)
    CHECK(kLinkMagic == 0x424C4B36u);
}
{
    using namespace tickerbar;
    // FULL factory reset (v0.6.8): every field returns to default...
    TickerState s;
    s.watchlist = { "SOL", "DOGE" };
    s.alertSound = true; s.alertToasts = false;
    s.linkEnabled = true; s.linkBackground = false;
    s.themeName = "Anime"; s.backgroundStyle = BackgroundStyle::Pastel; s.backgroundOpacity = 0.3f;
    s.pinned = false; s.reserveScreenSpace = true; s.dockEdge = DockEdge::Right;
    s.pollSec = 60; s.clickSounds = true; s.dataColorScheme = 3;
    s.bgEffect = 2; s.vizStyle = 2; s.vizHue = 0.1f;
    s.cellWidths["SOL"] = 222; s.showFearGreed = false; s.fngCellWidth = 180;
    AlertLevels lv; lv.above = 1.0; s.alerts["SOL"] = lv;
    resetAllSettingsToDefaults(s);
    TickerState d;
    CHECK(s.watchlist == d.watchlist);
    CHECK(s.alerts.empty());
    CHECK(s.alertSound == d.alertSound && s.alertToasts == d.alertToasts);
    CHECK(s.linkEnabled == d.linkEnabled && s.linkBackground == d.linkBackground);
    CHECK(s.themeName == d.themeName && s.backgroundStyle == d.backgroundStyle && s.backgroundOpacity == d.backgroundOpacity);
    CHECK(s.pinned == d.pinned && s.reserveScreenSpace == d.reserveScreenSpace && s.dockEdge == d.dockEdge);
    CHECK(s.pollSec == d.pollSec && s.clickSounds == d.clickSounds && s.dataColorScheme == d.dataColorScheme);
    CHECK(s.bgEffect == d.bgEffect && s.vizStyle == d.vizStyle && s.vizHue == d.vizHue);
    CHECK(s.cellWidths.empty() && s.showFearGreed == d.showFearGreed && s.fngCellWidth == d.fngCellWidth);
    // ...while the LAYOUT reset still touches layout only (the
    // distinction the Settings sheet's two buttons rely on)
    TickerState l; l.pollSec = 60; l.themeName = "Anime"; l.bgEffect = 2; l.cellWidths["BTC"] = 200;
    resetLayoutToDefaults(l);
    CHECK(l.pollSec == 60 && l.themeName == "Anime" && l.bgEffect == 2);
    CHECK(l.cellWidths.empty());
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
    // ---------- v0.5.2: WindowGeom visibility floor (link applies) ----------
    {
        std::vector<IntRect> areas{ IntRect{0, 0, 1920, 1040} };
        std::vector<IntRect> two{ IntRect{0, 0, 1920, 1040}, IntRect{1920, 0, 1920, 1040} };
        // fully off-screen: the floor fires — size kept, primary area,
        // centred horizontally, near the top
        IntRect lost{ 5000, 3000, 900, 96 };
        CHECK(!rectMeaningfullyVisible(lost, areas));
        auto f = ensureRectVisible(lost, areas);
        CHECK(f.x == (1920 - 900) / 2 && f.y == 24 && f.w == 900 && f.h == 96);
        CHECK(rectMeaningfullyVisible(f, areas));
        // genuinely on the second display: untouched
        IntRect onSecond{ 2000, 50, 900, 96 };
        CHECK(rectMeaningfullyVisible(onSecond, two));
        CHECK(ensureRectVisible(onSecond, two) == onSecond);
        // partially visible ABOVE the floor (120x96 showing): untouched
        IntRect partial{ 1800, 100, 900, 96 };
        CHECK(rectMeaningfullyVisible(partial, areas));
        CHECK(ensureRectVisible(partial, areas) == partial);
        // a 20 px sliver is below the floor: rescued to the primary
        IntRect sliver{ 1900, 100, 900, 96 };
        CHECK(!rectMeaningfullyVisible(sliver, areas));
        CHECK(ensureRectVisible(sliver, areas).x == (1920 - 900) / 2);
        // sunk so only 10 px of height shows: below the floor too
        CHECK(!rectMeaningfullyVisible(IntRect{ 100, 1030, 900, 96 }, areas));
        // straddling two monitors with a real chunk on each: untouched
        IntRect straddle{ 1700, 100, 900, 96 };
        CHECK(ensureRectVisible(straddle, two) == straddle);
        // no areas = no opinion
        CHECK(ensureRectVisible(lost, {}) == lost);
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
    // ---------- v0.6.5: distributed emitters + Field style + BLK5 ----------
    {
        VisualizerSettings vs;
        // burst birthplaces follow the band's field position: low
        // left, mid centre, high right (the centre is a coordinate
        // reference, not a spawn point — Jackson, 2026-11-01)
        auto burstMeanX = [&](int band, float balance, int frames) {
            VisualizerEngine e2;
            float hot[3] = { 0, 0, 0 }; hot[(size_t)band] = 0.95f;
            for (int i = 0; i < frames; ++i) e2.update(0.016f, hot, vs, balance);
            double sx = 0; int n = 0;
            for (auto& p : e2.particles()) if (p.burst) { sx += p.x; ++n; }
            CHECK(n > 0);
            return n ? sx / n : 0.5;
        };
        const double lowX = burstMeanX(0, 0.f, 1), midX = burstMeanX(1, 0.f, 1), highX = burstMeanX(2, 0.f, 1);
        CHECK(lowX < 0.40);
        CHECK(midX > 0.38 && midX < 0.62);
        CHECK(highX > 0.60);
        CHECK(lowX < midX && midX < highX);
        // stereo balance shifts the same band's births sideways
        CHECK(burstMeanX(1, 0.9f, 20) > burstMeanX(1, -0.9f, 20) + 0.05);
        // the mapping itself: ordered bands, balance clamped in-field
        CHECK(vizBandFieldX(0, 0.f) < vizBandFieldX(1, 0.f) && vizBandFieldX(1, 0.f) < vizBandFieldX(2, 0.f));
        CHECK(vizBandFieldX(2, 1.f) <= 0.94f && vizBandFieldX(0, -1.f) >= 0.06f);
        // Field style: population fills to its target and never
        // exceeds it; the gate opens with music; vortices wander
        VisualizerField fld;
        const float loud[3] = { 0.8f, 0.6f, 0.4f };
        for (int i = 0; i < 120; ++i) { fld.update(1.f / 30.f, loud, vs); CHECK((int)fld.particles().size() <= VisualizerField::kFieldTarget); }
        CHECK((int)fld.particles().size() == VisualizerField::kFieldTarget);
        CHECK(fld.visibility() == 1.f);
        float wx0, wy0, wx1, wy1;
        fld.vortexCentre(0, wx0, wy0);
        for (int i = 0; i < 600; ++i) fld.update(1.f / 30.f, loud, vs); // +20 s
        fld.vortexCentre(0, wx1, wy1);
        CHECK(std::hypot(wx1 - wx0, wy1 - wy0) > 0.05);
        // silence: the gate closes completely, like the other styles
        VisualizerField quiet;
        const float sil[3] = { 0, 0, 0 };
        for (int i = 0; i < 600; ++i) quiet.update(1.f / 30.f, sil, vs);
        CHECK(quiet.visibility() == 0.f);
        // BLK5: the magic moved with the widened domain, and the
        // stereo balance rides the audio packet at [4]
        CHECK(kLinkMagic == 0x424C4B36u); // BLK6
        LinkSlot slot; slot.appId = 1; slot.heartbeatMs = 1000; slot.audioSeq = 3;
        slot.audioLow = 0.1f; slot.audioMid = 0.2f; slot.audioHigh = 0.3f; slot.audioLevel = 0.4f; slot.audioBalance = 0.7f;
        float pkt[5] = { 0, 0, 0, 0, 0 };
        CHECK(audioPacket(slot, 1000, pkt));
        CHECK(pkt[0] == 0.1f && pkt[3] == 0.4f && pkt[4] == 0.7f);
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
    // ---------- v0.4.0: silence gate — no white particles in silence ----------
    {
        VisualizerSettings vs; vs.enabled = true;
        VisualizerEngine e;
        const float zero[3] = { 0.f, 0.f, 0.f };
        for (int i = 0; i < 240; ++i) e.update(0.016f, zero, vs); // ~4 s of silence
        CHECK(e.activity() < 0.02f);
        CHECK(e.ambientVisibility() < 0.05f);
        int brightest = 0;
        for (auto& p : e.particles()) if (!p.burst) {
            auto c = e.colorFor(p, vs);
            if ((int)c.r > brightest) brightest = c.r;
        }
        CHECK(brightest < 12); // ambient whites are (near-)invisible in silence
        const float mid[3] = { 0.5f, 0.5f, 0.5f }; // real energy, below the burst trigger
        for (int i = 0; i < 90; ++i) e.update(0.016f, mid, vs);
        CHECK(e.activity() > 0.3f);
        CHECK(e.ambientVisibility() > 0.9f);
        brightest = 0;
        for (auto& p : e.particles()) if (!p.burst) {
            auto c = e.colorFor(p, vs);
            if ((int)c.r > brightest) brightest = c.r;
        }
        // v0.5.8 retune: the intent is unchanged (whites clearly
        // back once the bands carry energy) but the scale is the
        // calmed peak now — kAmbientPeak x 255 = 140, not the old
        // full-brightness 255 this check was written against.
        CHECK(brightest > 100);
        CHECK(brightest <= (int)std::lround(255.f * VisualizerEngine::kAmbientPeak) + 1);
    }
    // ---------- v0.5.8: calmed whites + Lines style ----------
    {
        VisualizerSettings vs; vs.enabled = true;
        VisualizerEngine e;
        const float loudMid[3] = { 0, 0.9f, 0 };
        for (int i = 0; i < 90; ++i) e.update(1.f / 30.f, loudMid, vs); // 3 s of music: gate fully open
        CHECK(e.ambientVisibility() == 1.f);
        int peak = 0; bool sawAmbient = false;
        for (auto& p : e.particles()) {
            if (p.burst) continue;
            sawAmbient = true;
            auto c = e.colorFor(p, vs);
            if ((int)c.r > peak) peak = (int)c.r;
        }
        CHECK(sawAmbient);
        CHECK(peak <= (int)std::lround(255.f * VisualizerEngine::kAmbientPeak) + 1);
        CHECK(peak > 60); // still present, not deleted
        // whites fade OUT at end of life, like the burst dots:
        VizParticle mid_; mid_.burst = false; mid_.age = 2.f; mid_.life = 4.f;
        VizParticle old; old.burst = false; old.age = 3.8f; old.life = 4.f;
        CHECK(e.colorFor(old, vs).r < e.colorFor(mid_, vs).r);

        // Lines: silence = still, thin, invisible
        VisualizerLines lines;
        const float silence[3] = { 0, 0, 0 };
        for (int i = 0; i < 60; ++i) lines.update(1.f / 30.f, silence);
        CHECK(lines.visibility() == 0.f);
        CHECK(lines.level(1) == 0.f);
        CHECK(lines.halfThick(1, 24) <= 0.0045f); // hairline at rest (0.004 x breathe <= 1)
        bool inRange = true;
        for (int b = 0; b < 3; ++b)
            for (int i = 0; i < VisualizerLines::kPoints; ++i) {
                float y = lines.centreY(b, i);
                if (!(y > 0.f && y < 1.f)) inRange = false;
            }
        CHECK(inRange);
        // a loud band swells its ribbon
        const float loudLow[3] = { 0.95f, 0, 0 };
        const float thickRest = lines.halfThick(0, 24);
        for (int i = 0; i < 60; ++i) lines.update(1.f / 30.f, loudLow);
        CHECK(lines.level(0) > 0.5f);
        CHECK(lines.level(1) == 0.f);
        CHECK(lines.halfThick(0, 24) > thickRest * 2.f);
        CHECK(lines.visibility() == 1.f);
        // spring, not a copy: one frame must NOT slam to target
        VisualizerLines one;
        one.update(1.f / 30.f, loudLow);
        CHECK(one.level(0) < 0.2f);
        for (int i = 0; i < 30; ++i) lines.update(1.f / 30.f, loudLow);
        inRange = true;
        for (int b = 0; b < 3; ++b)
            for (int i = 0; i < VisualizerLines::kPoints; ++i) {
                float y = lines.centreY(b, i);
                if (!(y > 0.f && y < 1.f)) inRange = false;
            }
        CHECK(inRange);
    }
    // ---------- v0.4.0: overflow marquee math ----------
    {
        const std::vector<int> three = { 160, 160, 160 };
        CHECK(stripNaturalExtent(three, 16) == 512);
        CHECK(marqueeNeeded(three, 16, 400));
        CHECK(!marqueeNeeded(three, 16, 512));
        CHECK(!marqueeNeeded({}, 16, 10));
        const int cyc = marqueeCycle(three, 16);
        CHECK(cyc == 512 + kMarqueeLoopGapPx);
        // un-scrolled bases
        CHECK(marqueePosition(0, 160, 0, cyc) == 0);
        CHECK(marqueePosition(176, 160, 0, cyc) == 176);
        CHECK(marqueePosition(352, 160, 0, cyc) == 352);
        // scrolled 100 px: everything moved left; item 0 is half-out (negative)
        CHECK(marqueePosition(0, 160, 100, cyc) == -100);
        CHECK(marqueePosition(176, 160, 100, cyc) == 76);
        CHECK(marqueePosition(352, 160, 100, cyc) == 252);
        // a full cycle later the picture repeats exactly (seamless loop)
        CHECK(marqueePosition(0, 160, cyc, cyc) == 0);
        CHECK(marqueePosition(352, 160, cyc + 100, cyc) == 252);
        // every item crosses a 400 px window over one cycle (nothing is lost)
        for (int base : { 0, 176, 352 }) {
            bool seen = false;
            for (int off = 0; off < cyc; off += 4) {
                const int p = marqueePosition(base, 160, off, cyc);
                if (p > -160 && p < 400) { seen = true; break; }
            }
            CHECK(seen);
        }
    }
    // ---------- v0.5.4: marquee dividers ride the scroll ----------
    {
        // centred in the 16 px gap after the item: item edge + 3 px
        // of air, then the 10 px grip — the fitted layout's own
        // placement, now applied to the scrolled position.
        CHECK(marqueeSplitterRel(0, 160, 16, 10) == 163);
        CHECK(marqueeSplitterRel(176, 160, 16, 10) == 339);
        // the divider tracks its item's WRAPPED position: item 0
        // half-out at offset 100 sits at -100, its divider at 63 —
        // still the boundary between it and item 1 (at 76).
        CHECK(marqueeSplitterRel(marqueePosition(0, 160, 100, 568), 160, 16, 10) == 63);
        // a narrower item keeps the same 3 px air rule
        CHECK(marqueeSplitterRel(200, 90, 16, 10) == 293);
    }
    // v0.5.6: the Sizes panel's write band for cellWidths — the
    // solver's own floor and the family's dragged-extent ceiling.
    {
        CHECK(clampSymbolCellWidth(10) == kSymbolCellMinW);
        CHECK(clampSymbolCellWidth(90) == 90);
        CHECK(clampSymbolCellWidth(160) == 160);
        CHECK(clampSymbolCellWidth(600) == 600);
        CHECK(clampSymbolCellWidth(5000) == kSymbolCellMaxW);
        CHECK(kSymbolCellMinW == 90 && kSymbolCellMaxW == kContextCellMax);
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
        BgSyncState bs; bs.publishedStyle = 2; bs.publishedPct = 80; bs.publishedEffect = 1;
        CHECK(!bgIsLocalChange(bs, 2, 80, 1));
        bgForceRepublish(bs);
        CHECK(bgIsLocalChange(bs, 2, 80, 1)); // re-stamp forced
    }
    // ---------- group field map: one visualizer field across the linked bars ----------
    {
        // Unlinked / alone: group == own -> the historical bar-local map.
        IntRect own{ 100, 200, 800, 40 };
        auto m0 = fieldMap(own, own, 400, 20);
        CHECK(!m0.grouped);
        float lx = -1, ly = -1;
        fieldToLocal(m0, 0.5f, 0.5f, lx, ly);
        CHECK(lx == 200.f && ly == 10.f);
        fieldToLocal(m0, 0.f, 0.f, lx, ly);
        CHECK(lx == 0.f && ly == 0.f);
        // Degenerate rects fall back to the local map, never a wild scale.
        auto mD = fieldMap(own, IntRect{}, 400, 20);
        CHECK(!mD.grouped && mD.kx == 400.f && mD.ky == 20.f);
        // Two bars side by side: the field centre is the seam — it maps
        // to the left bar's right cache edge and the right bar's left
        // cache edge (the same screen point from both sides).
        IntRect grp{ 0, 0, 1600, 40 };
        IntRect left{ 0, 0, 800, 40 }, right{ 800, 0, 800, 40 };
        auto mL = fieldMap(left, grp, 400, 20);
        auto mR = fieldMap(right, grp, 400, 20);
        CHECK(mL.grouped && mR.grouped);
        fieldToLocal(mL, 0.5f, 0.5f, lx, ly); CHECK(lx == 400.f);
        fieldToLocal(mR, 0.5f, 0.5f, lx, ly); CHECK(lx == 0.f);
        fieldToLocal(mL, 0.25f, 0.5f, lx, ly); CHECK(lx == 200.f); // quarter point: centre of the left bar
        fieldToLocal(mR, 0.75f, 0.5f, lx, ly); CHECK(lx == 200.f); // three-quarter: centre of the right bar
        // Three bars: the middle bar's cache spans the middle third.
        IntRect grp3{ 0, 0, 2400, 40 };
        IntRect mid3{ 800, 0, 800, 40 };
        auto mM = fieldMap(mid3, grp3, 400, 20);
        fieldToLocal(mM, 0.5f, 0.5f, lx, ly); CHECK(lx == 200.f);
        fieldToLocal(mM, 1.f / 3.f, 0.5f, lx, ly); CHECK(lx > -0.01f && lx < 0.01f);
        // Vertical stack: the same on the y axis.
        IntRect vgrp{ 0, 0, 800, 80 };
        IntRect top{ 0, 0, 800, 40 }, bot{ 0, 40, 800, 40 };
        auto mT = fieldMap(top, vgrp, 400, 20);
        auto mB = fieldMap(bot, vgrp, 400, 20);
        fieldToLocal(mT, 0.5f, 0.5f, lx, ly); CHECK(ly == 20.f);
        fieldToLocal(mB, 0.5f, 0.5f, lx, ly); CHECK(ly == 0.f);
        // Visibility culling: a dot deep in the left slice is invisible
        // to the right bar; a dot straddling an edge still draws.
        CHECK(fieldDotVisible(200.f, 10.f, 3.f, 400, 20));
        CHECK(!fieldDotVisible(-60.f, 10.f, 3.f, 400, 20));
        CHECK(!fieldDotVisible(460.f, 10.f, 3.f, 400, 20));
        CHECK(fieldDotVisible(-2.f, 10.f, 3.f, 400, 20));
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
        // a single explicit cell fills the strip — v0.5.9: only up
        // to the kSymbolCellMaxW cap (600); the remaining 200 px of
        // this 800 px strip stay unallocated (see the cap block)
        auto f1 = solveCellWidths(800, 16, {300}, 90);
        CHECK(f1.size() == 1 && f1[0] == 600);
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
    // ---------- v0.5.9: solver cap — no solver-grown cell past 600 ----------
    {
        // auto share capped: two auto cells on a very wide strip
        // would split 3984 evenly (1992 each) uncapped
        auto cap = solveCellWidths(4000, 16, {0, 0}, 90);
        CHECK(cap[0] == kSymbolCellMaxW && cap[1] == kSymbolCellMaxW);
        CHECK(cap[0] + cap[1] + 16 < 4000); // leftover stays unallocated
        // auto share + rounding remainder capped: 1201 over two
        // autos is 600 each + a 1 px remainder — the remainder may
        // NOT push the last auto past the cap
        auto capr = solveCellWidths(1217, 16, {0, 0}, 90);
        CHECK(capr[0] == kSymbolCellMaxW && capr[1] == kSymbolCellMaxW);
        // mixed: explicit stands, the auto share of the rest is capped
        auto capm = solveCellWidths(4000, 16, {200, 0, 0}, 90);
        CHECK(capm[0] == 200 && capm[1] == kSymbolCellMaxW && capm[2] == kSymbolCellMaxW);
        // all-explicit last-cell fill capped: the fill would take
        // the last cell to 1568 uncapped
        auto capf = solveCellWidths(2000, 16, {200, 200, 200}, 90);
        CHECK(capf[0] == 200 && capf[1] == 200 && capf[2] == kSymbolCellMaxW);
        // an explicit width ABOVE the cap keeps its existing
        // behaviour: it stands, and the fill adds nothing to it
        auto capx = solveCellWidths(1000, 16, {800}, 90);
        CHECK(capx.size() == 1 && capx[0] == 800);
        // an under-cap fill still fills exactly (v0.3.2 behaviour)
        auto capu = solveCellWidths(1000, 16, {200, 200, 200}, 90);
        CHECK(capu[2] == 568 && capu[0] + capu[1] + capu[2] + 2 * 16 == 1000);
        // min behaviour unchanged: a too-narrow strip still floors
        // every auto cell at minW (the solver may overflow, as before)
        auto capn = solveCellWidths(200, 16, {0, 0, 0}, 90);
        CHECK(capn[0] == 90 && capn[1] == 90 && capn[2] == 90);
        // pair-drag + trailing-divider math untouched by the cap
        auto capp = adjustPairWidths(200, 200, 50, 80);
        CHECK(capp.first == 250 && capp.second == 150);
        CHECK(trailingWindowExtent(1000, 9999, 600, 7680) == 7680);
    }

    // ---------- v0.5.3: single-source version (generated header) ----------
    {
        // tickerbar::kVersionString is the exact string the updater
        // compares against and getApplicationVersion() reports; it is
        // generated from the root project() VERSION. If it ever
        // disagrees with the project version, the build is lying
        // about its own version (the stuck-on-0.4.0 bug: hard-coded
        // literals in Main.cpp / MainComponent.cpp, never bumped).
        CHECK(std::string(tickerbar::kVersionString) == EXPECTED_PROJECT_VERSION);
        CHECK(std::string(TICKERBAR_VERSION_STRING) == EXPECTED_PROJECT_VERSION);
        const std::string composed = std::to_string(TICKERBAR_VERSION_MAJOR) + "."
                                   + std::to_string(TICKERBAR_VERSION_MINOR) + "."
                                   + std::to_string(TICKERBAR_VERSION_PATCH);
        CHECK(composed == std::string(tickerbar::kVersionString));
    }

    // ---- Glyphs (v0.6.2): the toolbar icon glyphs must be
    // EXACTLY the UTF-8 encoding of their codepoints. The field
    // failure was mojibake — MSVC decoded the raw literals as
    // Windows-1252, so each sequence's first byte surfaced as a
    // Latin-1 char (0xF0, 0xE2). Pin every glyph two
    // ways: against an independent codepoint->UTF-8 encoder
    // written here, and against the byte sequences themselves.
    {
        auto encodeUtf8 = [](std::uint32_t cp) {
            std::string out;
            if (cp < 0x80) {
                out += (char)cp;
            } else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            return out;
        };
        CHECK(std::string(tickerbar::glyphLinkUtf8()) == encodeUtf8(0x1F517));
        CHECK(std::string(tickerbar::glyphLinkUtf8()) == std::string("\xF0\x9F\x94\x97", 4));
        CHECK(std::string(tickerbar::glyphSettingsUtf8()) == encodeUtf8(0x2699));
        CHECK(std::string(tickerbar::glyphSettingsUtf8()) == std::string("\xE2\x9A\x99", 3));
        CHECK(std::string(tickerbar::glyphMenuUtf8()) == encodeUtf8(0x2630));
        CHECK(std::string(tickerbar::glyphMenuUtf8()) == std::string("\xE2\x98\xB0", 3));
    }

    // ---------- v0.6.11: rotation sections ----------
    // Fixtures cnbc_quotes_batch.json / cnbc_quotes_unknown.json /
    // fred_dgs10.csv / yahoo_chart_spy.json are REAL payloads captured
    // live 2026-11-03 from the keyless sources (CNBC restQuote batch,
    // FRED fredgraph, Yahoo v8 chart); broken variants are inline.
    {
        // CNBC number shapes: comma-grouped strings, signed percents,
        // plain JSON numbers; garbage fails.
        auto jNum = [](const char* s) { auto v = parseJson(std::string("{\"x\":") + s + "}"); return cnbcNumber(*v->find("x")); };
        CHECK(jNum("\"7,568.65\"") && near(*jNum("\"7,568.65\""), 7568.65));
        CHECK(jNum("\"+0.36%\"") && near(*jNum("\"+0.36%\""), 0.36));
        CHECK(jNum("\"-1,234.5\"") && near(*jNum("\"-1,234.5\""), -1234.5));
        CHECK(jNum("42.5") && near(*jNum("42.5"), 42.5));
        CHECK(!jNum("\"N/A\""));
        CHECK(!jNum("\"\""));
        CHECK(!jNum("null"));
        CHECK(!jNum("\"12.3.4\""));

        // The real batch: 4 rows, indexes and BRK.B included.
        auto batch = parseCnbcQuotes(readFile("cnbc_quotes_batch.json"));
        CHECK(batch.has_value());
        CHECK(batch && batch->size() == 4);
        CHECK(batch && (*batch)[0].symbol == "SPY" && near((*batch)[0].last, 754.19) && near((*batch)[0].prevClose, 752.10));
        CHECK(batch && (*batch)[1].symbol == ".SPX" && near((*batch)[1].last, 7565.73));  // commas stripped
        CHECK(batch && (*batch)[2].symbol == "BRK.B" && near((*batch)[2].last, 557.18));
        CHECK(batch && (*batch)[3].symbol == "NVDA" && (*batch)[3].name == "NVIDIA Corporation");

        // The source's unknown-symbol answer: the row is SKIPPED (its
        // own signal), the parse still succeeds with zero quotes.
        auto unk = parseCnbcQuotes(readFile("cnbc_quotes_unknown.json"));
        CHECK(unk.has_value());
        CHECK(unk && unk->empty());

        // Broken shapes poison the batch — never a partial reading.
        CHECK(!parseCnbcQuotes("").has_value());
        CHECK(!parseCnbcQuotes("[]").has_value());
        CHECK(!parseCnbcQuotes("{\"FormattedQuoteResult\":{}}").has_value());
        CHECK(!parseCnbcQuotes(R"({"FormattedQuoteResult":{"FormattedQuote":[{"symbol":"SPY","code":0,"last":"N/A"}]}})").has_value());
        CHECK(!parseCnbcQuotes(R"({"FormattedQuoteResult":{"FormattedQuote":[{"symbol":"SPY","code":0,"last":"-5"}]}})").has_value());
        // "code" read through the house numeric view (Json::asNumber
        // blesses numeric strings): "0" accepts, "1" skips.
        auto codeStr = parseCnbcQuotes(R"({"FormattedQuoteResult":{"FormattedQuote":[{"symbol":"SPY","code":"0","last":"10"}]}})");
        CHECK(codeStr && codeStr->size() == 1);
        auto codeStrSkip = parseCnbcQuotes(R"({"FormattedQuoteResult":{"FormattedQuote":[{"symbol":"SPY","code":"1","last":"10"}]}})");
        CHECK(codeStrSkip && codeStrSkip->empty());
        CHECK(!parseCnbcQuotes(R"({"FormattedQuoteResult":{"FormattedQuote":["SPY"]}})").has_value());
        // A code-nonzero row among good ones: only that row is skipped.
        auto mixed = parseCnbcQuotes(R"({"FormattedQuoteResult":{"FormattedQuote":[{"symbol":"ZZZ","code":1},{"symbol":"SPY","code":0,"last":"10.5","previous_day_closing":"10"}]}})");
        CHECK(mixed && mixed->size() == 1 && (*mixed)[0].symbol == "SPY");

        // FRED CSV: the real DGS10 window (header + 45 observations).
        auto fred = parseFredGraphCsv(readFile("fred_dgs10.csv"));
        CHECK(fred.has_value());
                CHECK(fred && fred->size() == 43);  // 45 rows minus 2 holiday gaps (empty value)
        CHECK(fred && fred->back().date == "2026-10-30" && near(fred->back().value, 4.94));
        auto yr = yieldReadingFrom(*fred);
        CHECK(near(yr.valuePct, 4.94) && near(yr.prevPct, 4.98) && yr.date == "2026-10-30");
        CHECK(yr.history.size() == 43);
        // "." gap rows are the source's own mark: skipped, not fatal.
        auto gaps = parseFredGraphCsv("observation_date,DGS10\n2026-10-28,4.90\n2026-10-29,.\n2026-10-30,4.94\n");
        CHECK(gaps && gaps->size() == 2);
        CHECK(!parseFredGraphCsv("").has_value());
        CHECK(!parseFredGraphCsv("2026-10-30,4.94\n").has_value());          // no header
        CHECK(!parseFredGraphCsv("observation_date,DGS10\n2026-10-30,abc\n").has_value());
        CHECK(!parseFredGraphCsv("observation_date,DGS10\n30-10-2026,4.94\n").has_value());
        CHECK(fredDateShape("2026-10-30"));
        CHECK(!fredDateShape("2026-1-3"));
        CHECK(!fredDateShape("abcd-ef-gh"));

        // Yahoo daily closes: the real SPY month (22 sessions).
        auto closes = parseYahooDailyCloses(readFile("yahoo_chart_spy.json"));
        CHECK(closes.has_value());
        CHECK(closes && closes->size() == 22);
        CHECK(closes && near(closes->front(), 774.8300170898438, 1e-6));
        CHECK(closes && near(closes->back(), 754.1699829101562, 1e-6));
        CHECK(!parseYahooDailyCloses(R"({"chart":{"result":null,"error":{"code":"Not Found"}}})").has_value());
        // A close that reads as a number through the house numeric
        // view is accepted; a non-numeric string poisons the series.
        auto strClose = parseYahooDailyCloses(R"({"chart":{"result":[{"indicators":{"quote":[{"close":["774.83",775.0]}]}}],"error":null}})");
        CHECK(strClose && strClose->size() == 2);
        CHECK(!parseYahooDailyCloses(R"({"chart":{"result":[{"indicators":{"quote":[{"close":["soon",775.0]}]}}],"error":null}})").has_value());
        auto withNull = parseYahooDailyCloses(R"({"chart":{"result":[{"indicators":{"quote":[{"close":[100.0,null,101.5]}]}}],"error":null}})");
        CHECK(withNull && withNull->size() == 2);  // null = halt mark, skipped
        CHECK(!parseYahooDailyCloses(R"({"chart":{"result":[{"indicators":{"quote":[{"close":[100.0]}]}}],"error":null}})").has_value());

        // Internals computed proxy: +10 / -10 / flat / unusable-ref.
        std::vector<CnbcQuote> qs = {
            { "A", "", 110.0, 100.0, 0, 0, 0 },
            { "B", "", 90.0, 100.0, 0, 0, 0 },
            { "C", "", 100.0, 100.0, 0, 0, 0 },
            { "D", "", 50.0, 0.0, 0, 0, 0 },   // no reference: excluded
        };
        auto in = computeInternalsProxy(qs);
        CHECK(in.has_value());
        CHECK(in && in->valid == 3 && in->advancers == 1 && in->decliners == 1 && in->unchanged == 1);
        CHECK(in && near(in->advancersPct, 100.0 / 3.0));
        CHECK(in && near(in->medianChangePct, 0.0) && near(in->equalWeightChangePct, 0.0));
        std::vector<CnbcQuote> qs2 = { { "A", "", 96.0, 100.0, 0, 0, 0 }, { "B", "", 102.0, 100.0, 0, 0, 0 } };
        auto in2 = computeInternalsProxy(qs2);
        CHECK(in2 && near(in2->medianChangePct, -1.0));  // even count: mean of the middle two
        CHECK(!computeInternalsProxy({}).has_value());
        CHECK(!computeInternalsProxy({ { "D", "", 50.0, 0.0, 0, 0, 0 } }).has_value());

        // Section tables + URL builders.
        CHECK(std::string(rotationSectionName(0)) == "Crypto");
        CHECK(std::string(rotationSectionName(4)) == "Market Internals");
        CHECK(std::string(rotationSectionName(5)).empty());
        CHECK(sectionCells(0).empty());
        CHECK(sectionCells(1).size() == 12);
        CHECK(sectionCells(2).size() == 100);
        CHECK(sectionCells(3).size() == 11);   // 5 yields + 6 bond ETFs
        CHECK(sectionCells(4).size() == 4);    // VIX + 3 computed proxies
        CHECK(top100Stocks().size() == 100);
        CHECK(top100Stocks().front().symbol == std::string("NVDA"));
        {
            bool unique = true;
            for (size_t i = 0; i < top100Stocks().size() && unique; ++i)
                for (size_t j = i + 1; j < top100Stocks().size(); ++j)
                    if (top100Stocks()[i].symbol == top100Stocks()[j].symbol) unique = false;
            CHECK(unique);
        }
        CHECK(yahooSymbolFor("BRK.B") == "BRK-B");
        CHECK(yahooSymbolFor("SPY") == "SPY");
        CHECK(cnbcMainBatchIds().size() == 18);
        CHECK(cnbcTop100BatchIds(0).size() == 50 && cnbcTop100BatchIds(1).size() == 50);
        CHECK(cnbcBatchUrl({ "SPY", ".SPX" }).find("symbols=SPY%7C.SPX&") != std::string::npos);
        CHECK(fredCsvUrl("DGS10", "2025-11-03") == "https://fred.stlouisfed.org/graph/fredgraph.csv?id=DGS10&cosd=2025-11-03");
        CHECK(yahooChartUrl("^VIX").find("%5EVIX") != std::string::npos);
        // Staleness budgets stay 3x the poll clocks.
        CHECK(near(kSectionStaleSec, 3.0 * kCnbcMainPollSec));
        CHECK(near(kTop100StaleSec, 3.0 * kCnbcTop100PollSec));
        CHECK(near(kFredStaleSec, 3.0 * kFredPollSec));

        // Title-bar rotator room rule: hides rather than crushing.
        CHECK(titleContextExtent(0) == 0);
        CHECK(titleContextExtent(kTitleContextMinPx - 1) == 0);
        CHECK(titleContextExtent(kTitleContextMinPx) == kTitleContextMinPx);
        CHECK(titleContextExtent(200) == 200);
        CHECK(titleContextExtent(1000) == kTitleContextMaxPx);

        // v0.6.12: the rotator state machine. v0.6.11 had NO test of
        // the flip itself (only the room rule above) and the face
        // froze in the field — these advance the dwell clock in
        // timer-sized steps and assert the visible face changes.
        {
            TitleRotator rot;
            rot.setIncluded(true, true);
            CHECK(rot.showingFng()); // Fear & Greed is the opening face
            int flips = 0;
            for (int i = 0; i < 23; ++i) flips += rot.advance(0.25, true, true) ? 1 : 0;
            CHECK(flips == 0 && rot.showingFng()); // 5.75 s: still dwelling
            CHECK(rot.advance(0.25, true, true));  // 6.0 s: the flip fires
            CHECK(!rot.showingFng());              // ...onto BTC dominance
            for (int i = 0; i < 23; ++i) rot.advance(0.25, true, true);
            CHECK(rot.advance(0.25, true, true));  // the next dwell flips back
            CHECK(rot.showingFng());
        }
        // A hidden (unplaced) cell never accumulates dwell, and a
        // reappearing cell starts a FULL dwell — the v0.6.11 intent,
        // now pinned: partial dwell banked before a hide is gone.
        {
            TitleRotator rot;
            rot.setIncluded(true, true);
            for (int i = 0; i < 20; ++i) rot.advance(0.25, true, true); // 5 s banked
            CHECK(!rot.advance(0.25, true, false));                     // hidden: no flip...
            CHECK(near(rot.dwell(), 0.0));                              // ...and the bank is zeroed
            for (int i = 0; i < 23; ++i) CHECK(!rot.advance(0.25, true, true));
            CHECK(rot.advance(0.25, true, true)); // only the full new dwell flips
        }
        // One reading excluded: the face is static, and a shown face
        // that becomes excluded yields to the other at once — the
        // exact stranded-on-dominance shape Jackson field-reported
        // (cell sitting on BTC DOMINANCE) cannot persist once both
        // readings are included again: rotation simply resumes.
        {
            TitleRotator rot;
            rot.setIncluded(true, true);
            for (int i = 0; i < 24; ++i) rot.advance(0.25, true, true);
            CHECK(!rot.showingFng());      // sitting on dominance
            rot.setIncluded(true, false);  // dominance excluded...
            CHECK(rot.showingFng());       // ...F&G takes over at once
            for (int i = 0; i < 100; ++i) CHECK(!rot.advance(0.25, false, true));
            CHECK(rot.showingFng());       // static while only one reading is in
            rot.setIncluded(false, true);  // now F&G excluded instead
            CHECK(!rot.showingFng());      // dominance takes over at once
            rot.setIncluded(true, true);   // both back: rotation resumes
            for (int i = 0; i < 23; ++i) rot.advance(0.25, true, true);
            CHECK(rot.advance(0.25, true, true));
            CHECK(rot.showingFng());
        }

        // Index levels print grouped, 2dp, with no currency sign.
        CHECK(formatIndexValue(7565.73) == "7,565.73");
        CHECK(formatIndexValue(20.8) == "20.80");
        CHECK(formatIndexValue(-1.5) == "-1.50");
        CHECK(formatIndexValue(0.0) == "0.00");

        // The persisted section choice defaults to Crypto.
        CHECK(TickerState{}.rotationSection == 0);
        {
            TickerState s; s.rotationSection = 3;
            resetAllSettingsToDefaults(s);
            CHECK(s.rotationSection == 0);
        }
    }

    // ================= v0.6.12: Hyperliquid drill-in candles =================
    // Fixtures: hl_candles_hype_1h.json is a REAL candleSnapshot
    // response captured 2026-11-03 via POST
    // https://api.hyperliquid.xyz/info (HYPE, 1h, ~25h window, 26
    // candles); hl_candles_unlisted.json is the response BODY
    // actually observed for an unlisted coin (HTTP 500, body
    // literally "null"). The rest are hand-broken variants.
    {
        auto hl = parseHyperliquidCandles(readFile("hl_candles_hype_1h.json"), "HYPE");
        CHECK(hl.has_value());
        CHECK(hl && hl->size() == 26);                        // 25h window, both ends inclusive
        CHECK(hl && hl->front().tOpenMs == 1793653200000LL);  // first candle at capture
        CHECK(hl && near(hl->front().open, 83.135, 1e-6));
        CHECK(hl && near(hl->front().close, 84.516, 1e-6));
        CHECK(hl && near(hl->front().high, 84.672, 1e-6));
        CHECK(hl && near(hl->front().low, 83.114, 1e-6));
        CHECK(hl && near(hl->front().volume, 387291.45, 1e-6)); // "v": COIN units, as Model::Candle::volume
        CHECK(hl && near(hl->back().close, 85.981, 1e-6));      // newest captured close is last
        bool hlAscending = true, hlVol = true, hlSane = true;
        if (hl) for (size_t i = 0; i < hl->size(); ++i) {
            if (i && (*hl)[i].tOpenMs <= (*hl)[i - 1].tOpenMs) hlAscending = false;
            if (!((*hl)[i].volume > 0.0)) hlVol = false;         // real volume, unlike CoinGecko closes
            auto& c = (*hl)[i];
            if (!(c.high >= c.low && c.high >= c.open && c.high >= c.close &&
                  c.low <= c.open && c.low <= c.close)) hlSane = false;
        }
        CHECK(hlAscending);                                   // API sends oldest first; parser keeps it so
        CHECK(hlVol);
        CHECK(hlSane);
        auto hlRange = hl ? candleRange(*hl) : std::nullopt;
        CHECK(hlRange && hlRange->hasVolume);                 // chart MUST draw volume bars for this feed
        CHECK(hlRange && hlRange->maxVolume >= 387291.45);
        CHECK(hl && changePct1h(*hl).has_value());
        // The coin label is checked: the same payload is not BTC's chart.
        CHECK(!parseHyperliquidCandles(readFile("hl_candles_hype_1h.json"), "BTC").has_value());
        CHECK(parseHyperliquidCandles(readFile("hl_candles_hype_1h.json"), "hype").has_value()); // case-insensitive

        // Failure shapes yield NO candles, never garbage.
        auto hlEmpty = parseHyperliquidCandles(readFile("hl_candles_empty.json"), "HYPE");
        CHECK(hlEmpty.has_value() && hlEmpty->empty());       // empty array is VALID (caller falls back)
        CHECK(!parseHyperliquidCandles(readFile("hl_candles_unlisted.json"), "NONSENSEXYZ").has_value()); // body "null" (HTTP 500 live)
        CHECK(!parseHyperliquidCandles(readFile("hl_candles_error.json"), "HYPE").has_value());            // error object, not an array
        CHECK(!parseHyperliquidCandles(readFile("hl_candles_missing_fields.json"), "HYPE").has_value());   // candle without "v" poisons the set
        CHECK(!parseHyperliquidCandles(readFile("garbage.json"), "HYPE").has_value());
        CHECK(!parseHyperliquidCandles("Failed to deserialize the JSON body into the target type", "HYPE").has_value()); // live 422 body for a bad interval
        CHECK(!parseHyperliquidCandles(R"([{"t":1793653200000,"s":"HYPE","o":"83.1","c":"84.5","h":"84.6","l":"83.1","v":"soon"}])", "HYPE").has_value()); // non-numeric volume
        CHECK(!parseHyperliquidCandles(R"([{"t":1793653200000,"s":"HYPE","o":"0","c":"84.5","h":"84.6","l":"83.1","v":"10"}])", "HYPE").has_value());      // a 0 price is not data
        CHECK(!parseHyperliquidCandles(R"([{"t":1793653200000,"s":"HYPE","o":"83.1","c":"84.5","h":"82.0","l":"83.1","v":"10"}])", "HYPE").has_value());  // high < low is not a candle

        // Range -> interval/span mapping (1d verified live for 30d).
        CHECK(std::string(hyperliquidIntervalForRange(DetailRange::H24)) == "1h");
        CHECK(std::string(hyperliquidIntervalForRange(DetailRange::D7)) == "1h");
        CHECK(std::string(hyperliquidIntervalForRange(DetailRange::D30)) == "1d");
        CHECK(hyperliquidSpanMsForRange(DetailRange::H24) == 24LL * 3600LL * 1000LL);
        CHECK(hyperliquidSpanMsForRange(DetailRange::D7) == 7LL * 24LL * 3600LL * 1000LL);
        CHECK(hyperliquidSpanMsForRange(DetailRange::D30) == 30LL * 24LL * 3600LL * 1000LL);

        // The exact request body the app POSTs, re-parsed.
        auto reqJson = parseJson(hyperliquidCandleRequestJson("hype", DetailRange::D7, 1793743200000LL));
        CHECK(reqJson && reqJson->find("type") && reqJson->find("type")->asString() &&
              *reqJson->find("type")->asString() == "candleSnapshot");
        const Json* req = reqJson ? reqJson->find("req") : nullptr;
        CHECK(req && req->find("coin") && req->find("coin")->asString() && *req->find("coin")->asString() == "HYPE"); // uppercased
        CHECK(req && req->find("interval") && req->find("interval")->asString() && *req->find("interval")->asString() == "1h");
        CHECK(req && req->find("endTime") && req->find("endTime")->asNumber() &&
              near(*req->find("endTime")->asNumber(), 1793743200000.0, 1.0));
        CHECK(req && req->find("startTime") && req->find("startTime")->asNumber() &&
              near(*req->find("endTime")->asNumber() - *req->find("startTime")->asNumber(),
                   (double)hyperliquidSpanMsForRange(DetailRange::D7), 1.0));
        auto req30 = parseJson(hyperliquidCandleRequestJson("HYPE", DetailRange::D30, 1793743200000LL));
        const Json* req30r = req30 ? req30->find("req") : nullptr;
        CHECK(req30r && req30r->find("interval") && req30r->find("interval")->asString() &&
              *req30r->find("interval")->asString() == "1d");

        // Honest wording: Hyperliquid-served ranges never claim closes-only.
        CHECK(hyperliquidDetailNote(DetailRange::H24).find("Hyperliquid") != std::string::npos);
        CHECK(hyperliquidDetailNote(DetailRange::H24).find("closes only") == std::string::npos);
        CHECK(hyperliquidDetailNote(DetailRange::D30).find("1d") != std::string::npos);
    }
    // ================= end v0.6.12 Hyperliquid section =================

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

