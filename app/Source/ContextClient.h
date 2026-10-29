#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tickerbar/ContextParse.hpp>
#include <functional>
#include <optional>
// Market-context poller: Crypto Fear & Greed (Alternative.me) and BTC
// dominance (CoinGecko global). Deliberately DECOUPLED from the price
// poller: its own thread, a slow ~5-minute cadence, and its own failure
// state — a context outage can never delay, block, or crash the price
// path; the context cells just go stale with their last real values,
// under the same honesty rules (core tickerbar::contextStale).
// No keys: both endpoints are free and public.
class ContextClient : public juce::Thread {
public:
    ContextClient() : juce::Thread("ContextClient") {}
    ~ContextClient() override { stopThread(4000); }

    std::function<void()> onUpdate; // message thread, after any fetch stored new data

    void startPolling() { startThread(); }

    struct FngState { std::optional<tickerbar::FearGreed> value; double ageSec = 1e9; };
    struct DomState { std::optional<double> value; double ageSec = 1e9; };
    FngState fng() const;
    DomState dominance() const;

    void run() override;
private:
    std::optional<std::string> getText(const char* url);
    bool fetchFng();
    bool fetchDom();

    juce::CriticalSection lock;
    std::optional<tickerbar::FearGreed> fngValue;
    std::optional<double> domValue;
    juce::int64 fngOkMs = 0, domOkMs = 0;
};
