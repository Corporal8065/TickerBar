#include "PriceClient.h"
#include <ctime>

namespace {
// Coinbase candle windows are ISO-8601 UTC strings.
juce::String iso8601Utc(juce::int64 ms) {
    const std::time_t t = (std::time_t)(ms / 1000);
    std::tm tmv {};
#if JUCE_WINDOWS
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return juce::String(buf);
}
} // namespace

void PriceClient::setWatchlist(const std::vector<std::string>& wl) {
    juce::ScopedLock sl(lock);
    watch = wl;
}
tickerbar::MarketSnapshot PriceClient::snapshot() const {
    juce::ScopedLock sl(const_cast<juce::CriticalSection&>(lock));
    tickerbar::MarketSnapshot snap;
    for (auto& sym : watch) {
        auto it = bySymbol.find(sym);
        if (it != bySymbol.end()) snap.assets.push_back(it->second);
    }
    return snap;
}
std::vector<tickerbar::Candle> PriceClient::candlesFor(const std::string& symbol) const {
    juce::ScopedLock sl(const_cast<juce::CriticalSection&>(lock));
    auto it = candleCache.find(symbol);
    return it == candleCache.end() ? std::vector<tickerbar::Candle>{} : it->second;
}
std::vector<std::string> PriceClient::tradableSymbols() const {
    juce::ScopedLock sl(const_cast<juce::CriticalSection&>(lock));
    auto out = products;
    // CoinGecko-routed symbols are valid watchlist entries even though
    // they are not served by Coinbase stats.
    if (std::find(out.begin(), out.end(), "HYPE") == out.end() && !out.empty())
        out.push_back("HYPE");
    return out;
}
bool PriceClient::hasData() const {
    juce::ScopedLock sl(const_cast<juce::CriticalSection&>(lock));
    for (auto& kv : bySymbol) if (kv.second.hasData) return true;
    return false;
}
double PriceClient::ageSecFor(const std::string& symbol) const {
    juce::ScopedLock sl(const_cast<juce::CriticalSection&>(lock));
    auto it = symbolOkMs.find(symbol);
    if (it == symbolOkMs.end()) return 1e9;
    return (juce::Time::currentTimeMillis() - it->second) / 1000.0;
}
double PriceClient::lastOkAgeSec() const {
    juce::ScopedLock sl(const_cast<juce::CriticalSection&>(lock));
    if (watch.empty()) return 1e9;
    const juce::int64 now = juce::Time::currentTimeMillis();
    double worst = 0.0;
    for (auto& sym : watch) {
        auto it = symbolOkMs.find(sym);
        if (it == symbolOkMs.end()) return 1e9;
        worst = std::max(worst, (now - it->second) / 1000.0);
    }
    return worst;
}

std::optional<std::string> PriceClient::getText(const juce::String& url, int maxBytes) {
    juce::URL u(url);
    auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(10000)
                    .withExtraHeaders("User-Agent: TickerBar/0.1.1\r\nAccept: application/json");
    std::unique_ptr<juce::InputStream> in(u.createInputStream(opts));
    if (in == nullptr) return std::nullopt;
    auto text = in->readEntireStreamAsString();
    if (text.isEmpty() || text.length() > maxBytes) return std::nullopt;
    return text.toStdString();
}

bool PriceClient::fetchStats(const std::string& symbol, juce::int64 nowMs) {
    auto text = getText("https://api.exchange.coinbase.com/products/" + juce::String(symbol) + "-USD/stats",
                        64 * 1024);
    if (!text) return false;
    auto parsed = tickerbar::parseCoinbaseStats(*text, symbol);
    if (!parsed) return false; // unparseable = failed fetch, keep last good data
    juce::ScopedLock sl(lock);
    bySymbol[symbol] = *parsed;
    symbolOkMs[symbol] = nowMs;
    return true;
}

bool PriceClient::fetchCandles(const std::string& symbol, juce::int64 nowMs) {
    const juce::int64 dayMs = 24LL * 3600LL * 1000LL;
    juce::URL u("https://api.exchange.coinbase.com/products/" + juce::String(symbol) + "-USD/candles");
    u = u.withParameter("granularity", "3600")
          .withParameter("start", iso8601Utc(nowMs - dayMs))
          .withParameter("end", iso8601Utc(nowMs));
    auto text = getText(u.toString(true), 512 * 1024);
    if (!text) return false;
    auto parsed = tickerbar::parseCoinbaseCandles(*text);
    if (!parsed) return false;
    juce::ScopedLock sl(lock);
    candleCache[symbol] = std::move(*parsed);
    candleFetchedMs[symbol] = nowMs;
    return true;
}

bool PriceClient::fetchProducts(juce::int64 nowMs) {
    auto text = getText("https://api.exchange.coinbase.com/products", 4 * 1024 * 1024);
    if (!text) return false;
    auto parsed = tickerbar::parseCoinbaseProducts(*text);
    if (!parsed) return false;
    juce::ScopedLock sl(lock);
    products = std::move(*parsed);
    productsOkMs = nowMs;
    return true;
}

bool PriceClient::fetchGecko(const std::vector<std::string>& syms, juce::int64 nowMs) {
    juce::String ids;
    for (auto& s : syms) {
        auto id = tickerbar::coinGeckoIdForSymbol(s);
        if (!id) return false; // routed here without an id mapping — a bug, not data
        if (ids.isNotEmpty()) ids += ",";
        ids += juce::String(*id);
    }
    juce::URL u("https://api.coingecko.com/api/v3/coins/markets");
    u = u.withParameter("vs_currency", "usd")
          .withParameter("ids", ids)
          .withParameter("sparkline", "true")
          .withParameter("price_change_percentage", "24h");
    auto text = getText(u.toString(true), 1024 * 1024);
    if (!text) return false; // includes HTTP 429: absorbed as staleness
    auto parsed = tickerbar::parseCoinGeckoMarkets(*text);
    if (!parsed) return false;
    juce::ScopedLock sl(lock);
    bool allFound = true;
    for (auto& s : syms) {
        bool found = false;
        for (auto& q : *parsed) {
            if (q.ctx.name == s) {
                bySymbol[s] = q.ctx;
                symbolOkMs[s] = nowMs;
                // The chart series: trailing 24h of the hourly
                // sparkline (the payload's 7d series, last 25 points).
                candleCache[s] = tickerbar::candlesFromHourlySeries(q.sparkline7d, nowMs, 25);
                candleFetchedMs[s] = nowMs;
                found = true;
                break;
            }
        }
        if (!found) allFound = false;
    }
    return allFound;
}

void PriceClient::run() {
    while (!threadShouldExit()) {
        const juce::int64 now = juce::Time::currentTimeMillis();
        std::vector<std::string> wl;
        { juce::ScopedLock sl(lock); wl = watch; }
        std::vector<std::string> cbSyms, cgSyms;
        for (auto& s : wl)
            (tickerbar::sourceForSymbol(s) == tickerbar::PriceSource::CoinGecko ? cgSyms : cbSyms).push_back(s);

        bool dueFailed = false, storedNew = false;

        // Validation universe: fetch until loaded, refresh ~30 min.
        {
            bool needProducts;
            { juce::ScopedLock sl(lock); needProducts = products.empty() || (now - productsOkMs > 30LL * 60LL * 1000LL); }
            if (needProducts) {
                if (fetchProducts(now)) storedNew = true;
                else {
                    juce::ScopedLock sl(lock);
                    if (products.empty()) dueFailed = true; // a refresh miss with a list in hand is harmless
                }
            }
        }
        // Coinbase stats: the fast path, every cycle.
        for (auto& sym : cbSyms) {
            if (threadShouldExit()) break;
            if (fetchStats(sym, now)) {
                storedNew = true;
                juce::int64 lastCandle = 0;
                { juce::ScopedLock sl(lock); auto it = candleFetchedMs.find(sym); if (it != candleFetchedMs.end()) lastCandle = it->second; }
                // Candles ride along, throttled to ~4 min per symbol:
                // the chart is a 24h shape, not a tick feed. A candle
                // failure never fails the cycle — prices are the point.
                if (now - lastCandle > 240000 && fetchCandles(sym, now)) storedNew = true;
            } else {
                dueFailed = true;
            }
        }
        // CoinGecko symbols (HYPE): ~60 s cadence, failures absorbed.
        if (!cgSyms.empty() && now - geckoLastTryMs >= 60000) {
            geckoLastTryMs = now;
            if (fetchGecko(cgSyms, now)) storedNew = true;
            else if (cbSyms.empty()) dueFailed = true; // CG is the only source watched: its failure is the feed's failure
        }

        if (dueFailed) failures.store(failures.load() + 1);
        else failures.store(0);
        if (storedNew && onUpdate) {
            auto cb = onUpdate;
            juce::MessageManager::callAsync([cb]{ cb(); });
        }
        // Sleep: the poll cadence when healthy, the tested backoff when not.
        const int waitMs = dueFailed ? (int)(tickerbar::retryDelaySec(failures.load()) * 1000.0)
                                     : pollSec.load() * 1000;
        wait(waitMs);
    }
}
