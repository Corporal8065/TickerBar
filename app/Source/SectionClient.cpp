#include "SectionClient.h"
#include <algorithm>
#include <cstdlib>
#include <set>

namespace {
// Display symbol for a CNBC id in the main batch: the section tables
// carry both forms (indexes are ".SPX" at CNBC, "SPX" on the cell).
std::map<std::string, std::string> mainBatchDisplayMap() {
    std::map<std::string, std::string> m;
    for (const auto& s : tickerbar::etfIndexSymbols()) m[s.cnbc] = s.display;
    for (const auto& s : tickerbar::bondEtfSymbols())  m[s.cnbc] = s.display;
    return m;
}
// CNBC id -> tenor display for the yield batch (US10Y -> "10Y").
std::map<std::string, std::string> yieldBatchDisplayMap() {
    std::map<std::string, std::string> m;
    for (const auto& y : tickerbar::treasuryYieldSeries()) m[y.cnbc] = y.display;
    return m;
}
bool isTop100Display(const std::string& display) {
    static const std::set<std::string> kTop = [] {
        std::set<std::string> s;
        for (const auto& e : tickerbar::top100Stocks()) s.insert(e.symbol);
        return s;
    }();
    return kTop.count(display) != 0;
}
// Every Yahoo symbol any section can ask for, in sweep order. The
// user's custom tickers join the sweep (their sparklines come from
// the same Yahoo closes as the Top 100's).
std::vector<std::string> allYahooSymbols(const std::vector<std::string>& customStocks) {
    std::vector<std::string> out;
    for (int sec = 1; sec < tickerbar::kRotationSectionCount; ++sec) {
        const auto defs = sec == (int)tickerbar::RotationSection::Top100
                              ? tickerbar::stocksSectionCells(customStocks)
                              : tickerbar::sectionCells(sec);
        for (const auto& def : defs)
            if (def.kind == tickerbar::SectionCellKind::Quote && !def.yahoo.empty())
                out.push_back(def.yahoo);
    }
    return out;
}
} // namespace

void SectionClient::setCustomStocks(const std::vector<std::string>& symbols) {
    juce::ScopedLock sl(lock);
    if (symbols == customStocks_) return;
    customStocks_ = symbols;
    // The sweep must pick the new symbols up: drop the queue so the
    // next rebuild includes them (fetched series stay cached).
    seriesQueue_.clear();
    notify(); // wake the poller so the custom batch starts promptly
}

SectionClient::QuoteState SectionClient::quote(const std::string& display) const {
    juce::ScopedLock sl(lock);
    QuoteState s;
    const bool custom = std::find(customStocks_.begin(), customStocks_.end(), display)
                        != customStocks_.end();
    const bool top = isTop100Display(display);
    if (custom)     { s.attempted = customAttempted_; s.failures = customFailures_; }
    else if (top)   { s.attempted = topAttempted_;    s.failures = topFailures_; }
    else            { s.attempted = mainAttempted_;   s.failures = mainFailures_; }
    auto it = quotes_.find(display);
    if (it != quotes_.end()) {
        s.value = it->second;
        auto ok = quoteOkMs_.find(display);
        if (ok != quoteOkMs_.end())
            s.ageSec = (juce::Time::currentTimeMillis() - ok->second) / 1000.0;
        // Attach the sparkline series at read time: fresh enough
        // (< the core max age) or the cell draws its day range
        // instead of a quietly ancient line.
        std::string yahoo;
        if (top || custom) yahoo = tickerbar::yahooSymbolFor(display);
        else {
            for (const auto& def : tickerbar::sectionCells(1))
                if (def.display == display) yahoo = def.yahoo;
            if (yahoo.empty())
                for (const auto& def : tickerbar::sectionCells(3))
                    if (def.display == display) yahoo = def.yahoo;
        }
        if (!yahoo.empty()) {
            auto sit = series_.find(yahoo);
            auto sok = seriesOkMs_.find(yahoo);
            if (sit != series_.end() && sok != seriesOkMs_.end()) {
                const double age = (juce::Time::currentTimeMillis() - sok->second) / 1000.0;
                if (age <= tickerbar::kYahooSeriesMaxAgeSec) s.value->dailyCloses = sit->second;
            }
        }
    }
    return s;
}

SectionClient::YieldState SectionClient::yieldFor(const std::string& display) const {
    juce::ScopedLock sl(lock);
    YieldState s;
    s.attempted = yieldAttempted_;
    s.failures = yieldFailures_;
    auto it = yields_.find(display);
    if (it != yields_.end()) {
        s.value = it->second;
        auto ok = yieldOkMs_.find(display);
        if (ok != yieldOkMs_.end())
            s.ageSec = (juce::Time::currentTimeMillis() - ok->second) / 1000.0;
    }
    return s;
}

SectionClient::InternalsState SectionClient::internals() const {
    juce::ScopedLock sl(lock);
    InternalsState s;
    s.attempted = topAttempted_;
    s.failures = topFailures_;
    s.value = internals_;
    if (internalsOkMs_ != 0)
        s.ageSec = (juce::Time::currentTimeMillis() - internalsOkMs_) / 1000.0;
    return s;
}

// ---- Drill-in candles (v0.6.15) ----------------------------------------

std::vector<tickerbar::Candle> SectionClient::detailCandles(
        const std::string& yahooSymbol, tickerbar::DetailRange range) const {
    juce::ScopedLock sl(lock);
    auto it = detailCache_.find(detailKey(yahooSymbol, range));
    return it == detailCache_.end() ? std::vector<tickerbar::Candle>{} : it->second;
}

void SectionClient::requestDetailCandles(const std::string& yahooSymbol,
                                         tickerbar::DetailRange range) {
    if (yahooSymbol.empty()) return;
    const std::string key = detailKey(yahooSymbol, range);
    const juce::int64 now = juce::Time::currentTimeMillis();
    {
        juce::ScopedLock sl(lock);
        auto att = detailAttemptMs_.find(key);
        if (att != detailAttemptMs_.end() && now - att->second < 240000) return;
        if (pendingDetail_.count(key)) return;
        pendingDetail_.insert(key);
    }
    notify(); // the popup is open NOW; don't make it wait out the 5 s tick
}

std::string SectionClient::detailNote(const std::string& yahooSymbol,
                                      tickerbar::DetailRange range) const {
    juce::ScopedLock sl(lock);
    auto it = detailNotes_.find(detailKey(yahooSymbol, range));
    return it == detailNotes_.end() ? std::string() : it->second;
}

std::string SectionClient::detailSourceName(const std::string& yahooSymbol,
                                            tickerbar::DetailRange range) const {
    juce::ScopedLock sl(lock);
    auto it = detailCache_.find(detailKey(yahooSymbol, range));
    return (it != detailCache_.end() && !it->second.empty()) ? "Yahoo" : "";
}

bool SectionClient::fetchDetailCandles(const std::string& key) {
    const size_t bar = key.rfind('|');
    if (bar == std::string::npos) return false;
    const std::string sym = key.substr(0, bar);
    const auto range = (tickerbar::DetailRange)std::atoi(key.substr(bar + 1).c_str());
    {
        juce::ScopedLock sl(lock);
        detailAttemptMs_[key] = juce::Time::currentTimeMillis();
    }
    auto text = getText(tickerbar::yahooDetailChartUrl(sym, range));
    auto parsed = text ? tickerbar::parseYahooCandles(*text)
                       : std::optional<std::vector<tickerbar::Candle>>{};
    juce::ScopedLock sl(lock);
    if (parsed && !parsed->empty()) {
        detailCache_[key] = std::move(*parsed);
        detailNotes_[key] = tickerbar::yahooDetailNote(range);
        return true;
    }
    // Honest failure: keep any candles already held, and say plainly
    // when there is nothing to draw.
    if (detailCache_[key].empty())
        detailNotes_[key] = "Yahoo could not supply candles for this range just now.";
    return false;
}

// ---- fetching -----------------------------------------------------------

std::optional<std::string> SectionClient::getText(const std::string& url) {
    juce::URL u { juce::String(url) };
    // The endpoints were verified with a browser user agent (and
    // also answer bare ones); send the verified shape.
    auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(10000)
                    .withExtraHeaders("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36");
    std::unique_ptr<juce::InputStream> in(u.createInputStream(opts));
    if (in == nullptr) return std::nullopt;
    auto text = in->readEntireStreamAsString();
    if (text.isEmpty() || text.length() > 4 * 1024 * 1024) return std::nullopt;
    return text.toStdString();
}

bool SectionClient::fetchCnbcBatch(const std::vector<std::string>& ids,
                                   const std::map<std::string, std::string>& cnbcToDisplay) {
    auto text = getText(tickerbar::cnbcBatchUrl(ids));
    if (!text) return false;
    auto parsed = tickerbar::parseCnbcQuotes(*text);
    if (!parsed || parsed->empty()) return false;
    juce::ScopedLock sl(lock);
    const juce::int64 now = juce::Time::currentTimeMillis();
    for (const auto& q : *parsed) {
        auto it = cnbcToDisplay.find(q.symbol);
        if (it == cnbcToDisplay.end()) continue; // not a symbol we asked to display
        quotes_[it->second] = tickerbar::sectionQuoteFrom(q);
        quoteOkMs_[it->second] = now;
    }
    return true;
}

// The v0.6.15 yield route: the five tenors ride ONE CNBC batch on
// the yields' own clock (core kCnbcYieldPollSec). A batch counts as
// landed only when at least one tenor stored — the parser skips the
// source's unknown-symbol rows whole, so a payload of nothing but
// code-1 rows parses empty and fails the cycle honestly.
bool SectionClient::fetchYieldBatch() {
    static const std::map<std::string, std::string> kMap = yieldBatchDisplayMap();
    auto text = getText(tickerbar::cnbcBatchUrl(tickerbar::cnbcYieldBatchIds()));
    if (!text) return false;
    auto parsed = tickerbar::parseCnbcQuotes(*text);
    if (!parsed || parsed->empty()) return false;
    juce::ScopedLock sl(lock);
    const juce::int64 now = juce::Time::currentTimeMillis();
    bool stored = false;
    for (const auto& q : *parsed) {
        auto it = kMap.find(q.symbol);
        if (it == kMap.end()) continue; // not a tenor we display
        yields_[it->second] = tickerbar::yieldReadingFromCnbc(q);
        yieldOkMs_[it->second] = now;
        stored = true;
    }
    return stored;
}

bool SectionClient::fetchOneSeries() {
    std::string sym;
    {
        juce::ScopedLock sl(lock);
        if (seriesQueue_.empty()) {
            // Rebuild the sweep: every symbol whose series is missing
            // or past the refresh age.
            const juce::int64 now = juce::Time::currentTimeMillis();
            for (const auto& y : allYahooSymbols(customStocks_)) {
                auto ok = seriesOkMs_.find(y);
                if (ok == seriesOkMs_.end() ||
                    (now - ok->second) / 1000.0 > tickerbar::kYahooSeriesRefreshSec)
                    seriesQueue_.push_back(y);
            }
            if (seriesQueue_.empty()) return false;
        }
        sym = seriesQueue_.back();
        seriesQueue_.pop_back();
    }
    auto text = getText(tickerbar::yahooChartUrl(sym));
    if (!text) return false;
    auto parsed = tickerbar::parseYahooDailyCloses(*text);
    if (!parsed) return false;
    juce::ScopedLock sl(lock);
    series_[sym] = *parsed;
    seriesOkMs_[sym] = juce::Time::currentTimeMillis();
    return true;
}

void SectionClient::run() {
    const auto mainMap = mainBatchDisplayMap();
    std::map<std::string, std::string> topMap;
    for (const auto& e : tickerbar::top100Stocks()) topMap[e.symbol] = e.symbol;
    juce::int64 mainDue = 0, topDue = 0, yieldDue = 0;
    while (!threadShouldExit()) {
        bool changed = false;
        const juce::int64 now = juce::Time::currentTimeMillis();
        // Drill-in requests first: a popup is waiting on them, and
        // they are one-shot fetches, not part of any poll clock.
        {
            std::vector<std::string> pending;
            {
                juce::ScopedLock sl(lock);
                pending.assign(pendingDetail_.begin(), pendingDetail_.end());
                pendingDetail_.clear();
            }
            for (const auto& key : pending) {
                if (threadShouldExit()) break;
                changed |= fetchDetailCandles(key);
            }
        }
        if (now >= mainDue) {
            const bool ok = fetchCnbcBatch(tickerbar::cnbcMainBatchIds(), mainMap);
            { juce::ScopedLock sl(lock); mainAttempted_ = true; mainFailures_ = ok ? 0 : mainFailures_ + 1; }
            changed |= ok;
            mainDue = juce::Time::currentTimeMillis() +
                      (juce::int64)((ok ? tickerbar::kCnbcMainPollSec : 60.0) * 1000.0);
        }
        if (now >= topDue) {
            const bool okA = fetchCnbcBatch(tickerbar::cnbcTop100BatchIds(0), topMap);
            const bool okB = fetchCnbcBatch(tickerbar::cnbcTop100BatchIds(1), topMap);
            // The user's own tickers ride the Top-100 clock on their
            // own batch, with their own failure state — a bogus
            // custom symbol can never fail the Top 100's cycle (the
            // parser skips its code-1 row whole).
            std::vector<std::string> customIds;
            { juce::ScopedLock sl(lock); customIds = tickerbar::cnbcCustomBatchIds(customStocks_); }
            if (!customIds.empty()) {
                std::map<std::string, std::string> customMap;
                for (const auto& id : customIds) customMap[id] = id;
                const bool okC = fetchCnbcBatch(customIds, customMap);
                { juce::ScopedLock sl(lock); customAttempted_ = true; customFailures_ = okC ? 0 : customFailures_ + 1; }
                changed |= okC;
            }
            const bool ok = okA && okB;
            { juce::ScopedLock sl(lock); topAttempted_ = true; topFailures_ = ok ? 0 : topFailures_ + 1; }
            changed |= okA || okB;
            // The Internals proxies recompute from whatever Top-100
            // quotes are currently stored (both halves, each on its
            // own stamp) whenever a half lands.
            if (okA || okB) {
                std::vector<tickerbar::CnbcQuote> all;
                {
                    juce::ScopedLock sl(lock);
                    for (const auto& e : tickerbar::top100Stocks()) {
                        auto it = quotes_.find(e.symbol);
                        if (it == quotes_.end()) continue;
                        tickerbar::CnbcQuote q;
                        q.symbol = e.symbol; q.name = it->second.name;
                        q.last = it->second.last; q.prevClose = it->second.prevClose;
                        all.push_back(q);
                    }
                }
                if (auto proxy = tickerbar::computeInternalsProxy(all)) {
                    juce::ScopedLock sl(lock);
                    internals_ = *proxy;
                    internalsOkMs_ = juce::Time::currentTimeMillis();
                    changed = true;
                }
            }
            topDue = juce::Time::currentTimeMillis() +
                     (juce::int64)((ok ? tickerbar::kCnbcTop100PollSec : 60.0) * 1000.0);
        }
        if (now >= yieldDue) {
            const bool ok = fetchYieldBatch();
            { juce::ScopedLock sl(lock); yieldAttempted_ = true; yieldFailures_ = ok ? 0 : yieldFailures_ + 1; }
            changed |= ok;
            yieldDue = juce::Time::currentTimeMillis() +
                       (juce::int64)((ok ? tickerbar::kCnbcYieldPollSec : 60.0) * 1000.0);
        }
        // Yahoo sparkline drip: a few per tick, so a cold start fills
        // the visible sections quickly without a 118-request burst.
        for (int i = 0; i < 3; ++i) changed |= fetchOneSeries();
        if (changed && onUpdate) {
            auto cb = onUpdate;
            juce::MessageManager::callAsync([cb]{ cb(); });
        }
        wait(5000);
    }
}
