#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tickerbar/Model.hpp>
#include <tickerbar/SectionParse.hpp>
#include <tickerbar/Sections.hpp>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
// v0.6.11 rotation-section poller: the keyless public sources behind
// the ETFs & Indexes / Top 100 / Bonds / Market Internals sections
// (CNBC batch quotes, Yahoo daily closes for sparklines — endpoints
// and verification notes in core Sections.hpp).
// v0.6.15: the Treasury yields moved onto the same CNBC batch (the
// FRED route never landed for real users — see Sections.hpp), on
// their own 1-minute clock; the Stocks section gained the user's
// own tickers on a third CNBC batch; and the client now also serves
// the section drill-in's OHLCV candles from Yahoo's v8 chart
// endpoint, on PriceClient's lazy per-range pattern.
// Deliberately DECOUPLED from both the price poller and the context
// poller, on ContextClient's pattern: its own thread, its own clocks
// and its own per-source failure state, so a section outage can never
// delay, block or grey out the crypto strip or the title rotator —
// the affected cells just go stale with their last real values, and
// a source that has never landed reads "unavailable", never a guess.
class SectionClient : public juce::Thread {
public:
    SectionClient() : juce::Thread("SectionClient") {}
    ~SectionClient() override { stopThread(4000); }

    std::function<void()> onUpdate; // message thread, after any fetch stored new data

    void startPolling() { startThread(); }

    // The Stocks section's user tickers (already cleaned by core's
    // cleanCustomStocks at the settings layer). The client keeps
    // its own copy: the custom batch, the per-symbol read state and
    // the sparkline sweep all follow it.
    void setCustomStocks(const std::vector<std::string>& symbols);

    // attempted: the owning source has completed at least one fetch
    // cycle (success or failure). failures: consecutive failed cycles
    // of the owning source since its last success.
    struct QuoteState {
        std::optional<tickerbar::SectionQuote> value; double ageSec = 1e9;
        bool attempted = false; int failures = 0;
    };
    struct YieldState {
        std::optional<tickerbar::YieldReading> value; double ageSec = 1e9;
        bool attempted = false; int failures = 0;
    };
    struct InternalsState {
        std::optional<tickerbar::InternalsProxy> value; double ageSec = 1e9;
        bool attempted = false; int failures = 0;
    };
    QuoteState quote(const std::string& display) const;
    YieldState yieldFor(const std::string& display) const;
    InternalsState internals() const;

    // ---- Drill-in candles (v0.6.15), on PriceClient's surface ----
    // Keyed by the YAHOO symbol (the drill-in's feed symbol). A
    // request is lazy and throttled per symbol+range (240 s): the
    // drill-in popup asks on open and on range switches, the poll
    // thread fetches ahead of the sparkline drip, and a failed fetch
    // keeps the honest note below instead of inventing candles.
    std::vector<tickerbar::Candle> detailCandles(const std::string& yahooSymbol,
                                                 tickerbar::DetailRange range) const;
    void requestDetailCandles(const std::string& yahooSymbol, tickerbar::DetailRange range);
    std::string detailNote(const std::string& yahooSymbol, tickerbar::DetailRange range) const;
    std::string detailSourceName(const std::string& yahooSymbol, tickerbar::DetailRange range) const;

    void run() override;
private:
    std::optional<std::string> getText(const std::string& url);
    bool fetchCnbcBatch(const std::vector<std::string>& ids,
                        const std::map<std::string, std::string>& cnbcToDisplay);
    bool fetchYieldBatch();
    bool fetchDetailCandles(const std::string& key);
    bool fetchOneSeries(); // one queued Yahoo daily-close fetch

    static std::string detailKey(const std::string& yahooSymbol, tickerbar::DetailRange range) {
        return yahooSymbol + "|" + std::to_string((int)range);
    }

    mutable juce::CriticalSection lock;
    std::map<std::string, tickerbar::SectionQuote> quotes_;     // by display symbol
    std::map<std::string, juce::int64> quoteOkMs_;              // by display symbol
    std::map<std::string, tickerbar::YieldReading> yields_;     // by tenor display ("10Y")
    std::map<std::string, juce::int64> yieldOkMs_;              // by tenor display
    std::map<std::string, std::vector<double>> series_;         // by Yahoo symbol
    std::map<std::string, juce::int64> seriesOkMs_;             // by Yahoo symbol
    std::optional<tickerbar::InternalsProxy> internals_;
    juce::int64 internalsOkMs_ = 0;
    // Per-source-family cycle state (attempted / consecutive failures).
    bool mainAttempted_ = false;   int mainFailures_ = 0;
    bool topAttempted_ = false;    int topFailures_ = 0;
    bool customAttempted_ = false; int customFailures_ = 0;
    bool yieldAttempted_ = false;  int yieldFailures_ = 0;
    std::vector<std::string> customStocks_;   // cleaned user tickers (Stocks section)
    std::vector<std::string> seriesQueue_;    // Yahoo symbols awaiting a (re)fetch
    // Drill-in candle state, keyed by detailKey().
    std::map<std::string, std::vector<tickerbar::Candle>> detailCache_;
    std::map<std::string, juce::int64> detailAttemptMs_;
    std::map<std::string, std::string> detailNotes_;
    std::set<std::string> pendingDetail_;
};
