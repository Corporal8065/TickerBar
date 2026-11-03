#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tickerbar/Model.hpp>
#include <tickerbar/SectionParse.hpp>
#include <tickerbar/Sections.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
// v0.6.11 rotation-section poller: the keyless public sources behind
// the ETFs & Indexes / Top 100 / Bonds / Market Internals sections
// (CNBC batch quotes, FRED yield CSVs, Yahoo daily closes for
// sparklines — endpoints and verification notes in core Sections.hpp).
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
    YieldState yieldFor(const std::string& fredId) const;
    InternalsState internals() const;

    void run() override;
private:
    std::optional<std::string> getText(const std::string& url);
    bool fetchCnbcBatch(const std::vector<std::string>& ids,
                        const std::map<std::string, std::string>& cnbcToDisplay);
    bool fetchFredSeries(const std::string& fredId);
    bool fetchOneSeries(); // one queued Yahoo daily-close fetch

    mutable juce::CriticalSection lock;
    std::map<std::string, tickerbar::SectionQuote> quotes_;     // by display symbol
    std::map<std::string, juce::int64> quoteOkMs_;              // by display symbol
    std::map<std::string, tickerbar::YieldReading> yields_;     // by FRED series id
    std::map<std::string, juce::int64> yieldOkMs_;              // by FRED series id
    std::map<std::string, std::vector<double>> series_;         // by Yahoo symbol
    std::map<std::string, juce::int64> seriesOkMs_;             // by Yahoo symbol
    std::optional<tickerbar::InternalsProxy> internals_;
    juce::int64 internalsOkMs_ = 0;
    // Per-source-family cycle state (attempted / consecutive failures).
    bool mainAttempted_ = false; int mainFailures_ = 0;
    bool topAttempted_ = false;  int topFailures_ = 0;
    bool fredAttempted_ = false; int fredFailures_ = 0;
    std::vector<std::string> seriesQueue_; // Yahoo symbols awaiting a (re)fetch
};
