#include "SectionClient.h"
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
bool isTop100Display(const std::string& display) {
    static const std::set<std::string> kTop = [] {
        std::set<std::string> s;
        for (const auto& e : tickerbar::top100Stocks()) s.insert(e.symbol);
        return s;
    }();
    return kTop.count(display) != 0;
}
// Every Yahoo symbol any section can ask for, in sweep order.
std::vector<std::string> allYahooSymbols() {
    std::vector<std::string> out;
    for (int sec = 1; sec < tickerbar::kRotationSectionCount; ++sec)
        for (const auto& def : tickerbar::sectionCells(sec))
            if (def.kind == tickerbar::SectionCellKind::Quote && !def.yahoo.empty())
                out.push_back(def.yahoo);
    return out;
}
} // namespace

SectionClient::QuoteState SectionClient::quote(const std::string& display) const {
    juce::ScopedLock sl(lock);
    QuoteState s;
    const bool top = isTop100Display(display);
    s.attempted = top ? topAttempted_ : mainAttempted_;
    s.failures  = top ? topFailures_  : mainFailures_;
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
        if (top) yahoo = tickerbar::yahooSymbolFor(display);
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

SectionClient::YieldState SectionClient::yieldFor(const std::string& fredId) const {
    juce::ScopedLock sl(lock);
    YieldState s;
    s.attempted = fredAttempted_;
    s.failures = fredFailures_;
    auto it = yields_.find(fredId);
    if (it != yields_.end()) {
        s.value = it->second;
        auto ok = yieldOkMs_.find(fredId);
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

bool SectionClient::fetchFredSeries(const std::string& fredId) {
    // A ~400-day window: enough history for the sparkline and the
    // previous-observation change, without FRED's full history.
    auto iso = (juce::Time::getCurrentTime() - juce::RelativeTime::days(400))
                    .formatted("%Y-%m-%d");
    auto text = getText(tickerbar::fredCsvUrl(fredId, iso.toStdString()));
    if (!text) return false;
    auto parsed = tickerbar::parseFredGraphCsv(*text);
    if (!parsed) return false;
    juce::ScopedLock sl(lock);
    yields_[fredId] = tickerbar::yieldReadingFrom(*parsed);
    yieldOkMs_[fredId] = juce::Time::currentTimeMillis();
    return true;
}

bool SectionClient::fetchOneSeries() {
    std::string sym;
    {
        juce::ScopedLock sl(lock);
        if (seriesQueue_.empty()) {
            // Rebuild the sweep: every symbol whose series is missing
            // or past the refresh age.
            const juce::int64 now = juce::Time::currentTimeMillis();
            for (const auto& y : allYahooSymbols()) {
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
    juce::int64 mainDue = 0, topDue = 0, fredDue = 0;
    while (!threadShouldExit()) {
        bool changed = false;
        const juce::int64 now = juce::Time::currentTimeMillis();
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
        if (now >= fredDue) {
            bool anyOk = false, allOk = true;
            std::vector<std::string> ids;
            for (const auto& y : tickerbar::treasuryYieldSeries()) ids.emplace_back(y.fredId);
            ids.emplace_back(tickerbar::kFredVixSeriesId); // VIX sparkline history
            for (const auto& id : ids) {
                if (threadShouldExit()) break;
                const bool ok = fetchFredSeries(id);
                anyOk |= ok; allOk &= ok;
                if (ok) changed = true;
            }
            { juce::ScopedLock sl(lock); fredAttempted_ = true; fredFailures_ = allOk ? 0 : fredFailures_ + 1; }
            fredDue = juce::Time::currentTimeMillis() +
                      (juce::int64)(((anyOk) ? tickerbar::kFredPollSec : 600.0) * 1000.0);
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
