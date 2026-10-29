#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tickerbar/MarketParse.hpp>
#include <tickerbar/Stale.hpp>
#include <functional>
#include <map>
#include <set>
// Price poller (v0.1.1). Replaces v0.1's Hyperliquid client: that API
// geo-blocks US IP addresses, so the bar showed a permanent "no
// connection" on US PCs. Both sources here are keyless and
// US-reachable, and prices are SPOT:
//  - Coinbase Exchange: /products/{SYM}-USD/stats per watched symbol
//    every poll cycle (~20 s); /products/{SYM}-USD/candles (1h,
//    trailing 24h) throttled to ~4 min per symbol; /products for the
//    watchlist validation universe (refreshed ~30 min).
//  - CoinGecko /coins/markets for CoinGecko-routed symbols (HYPE) on a
//    ~60 s cadence (free-tier limits); a 429 or failure is absorbed as
//    staleness for those symbols only — it never touches Coinbase data.
// Failure honesty (unchanged rules from v0.1): a failed or unparseable
// response changes NOTHING stored; per-symbol success timestamps drive
// the core-tested tickerbar::feedStatus for each cell; the failure
// count drives tickerbar::retryDelaySec backoff. Parsed with the SAME
// core parsers the unit fixtures grade.
class PriceClient : public juce::Thread {
public:
    PriceClient() : juce::Thread("PriceClient") {}
    ~PriceClient() override { stopThread(4000); }

    std::function<void()> onUpdate; // fired on the MESSAGE thread after any cycle that stored new data

    void setWatchlist(const std::vector<std::string>& wl);
    void setPollSec(int sec) { pollSec.store(sec); }
    void startPolling() { startThread(); }

    // Message-thread reads (internally locked):
    tickerbar::MarketSnapshot snapshot() const;
    std::vector<tickerbar::Candle> candlesFor(const std::string& symbol) const;
    // v0.2.0 detail ranges: cached candles for a timescale; an empty
    // vector means "not fetched yet" OR "the feed cannot supply it" —
    // detailNote() distinguishes the two in words.
    std::vector<tickerbar::Candle> candlesFor(const std::string& symbol, tickerbar::DetailRange range) const;
    void requestCandles(const std::string& symbol, tickerbar::DetailRange range); // message thread: queue a fetch, wake the poller
    std::string detailNote(const std::string& symbol, tickerbar::DetailRange range) const;
    // Watchlist validation universe: Coinbase SYM-USD bases + the
    // CoinGecko-routed symbols (HYPE). Empty until /products lands —
    // Settings shows its "coin list hasn't loaded yet" message then.
    std::vector<std::string> tradableSymbols() const;
    bool hasData() const;
    double lastOkAgeSec() const; // worst per-symbol age across the watchlist; huge if any never landed
    double ageSecFor(const std::string& symbol) const; // huge if that symbol never landed
    int consecutiveFailures() const { return failures.load(); }

    void run() override;
private:
    std::optional<std::string> getText(const juce::String& url, int maxBytes);
    bool fetchStats(const std::string& symbol, juce::int64 nowMs);
    bool fetchCandles(const std::string& symbol, juce::int64 nowMs);
    bool fetchDetailCandles(const std::string& symbol, tickerbar::DetailRange range, juce::int64 nowMs);
    bool fetchProducts(juce::int64 nowMs);
    bool fetchGecko(const std::vector<std::string>& syms, juce::int64 nowMs);

    juce::CriticalSection lock;
    std::map<std::string, tickerbar::AssetCtx> bySymbol;
    std::map<std::string, std::vector<tickerbar::Candle>> candleCache;
    std::map<std::string, juce::int64> candleFetchedMs;
    struct RangeKey {
        std::string symbol; tickerbar::DetailRange range;
        bool operator<(const RangeKey& o) const {
            return symbol != o.symbol ? symbol < o.symbol : (int)range < (int)o.range;
        }
    };
    std::map<RangeKey, std::vector<tickerbar::Candle>> detailCache;
    std::map<RangeKey, juce::int64> detailFetchedMs;
    std::map<RangeKey, std::string> detailNotes;
    std::set<RangeKey> pendingDetail;
    std::map<std::string, juce::int64> symbolOkMs;
    std::vector<std::string> products; // sorted Coinbase USD bases
    std::vector<std::string> watch;
    juce::int64 productsOkMs = 0, geckoLastTryMs = 0;
    std::atomic<int> failures { 0 };
    std::atomic<int> pollSec { 20 };
};
