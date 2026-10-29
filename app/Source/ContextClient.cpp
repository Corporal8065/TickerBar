#include "ContextClient.h"

ContextClient::FngState ContextClient::fng() const {
    juce::ScopedLock sl(const_cast<juce::CriticalSection&>(lock));
    FngState s; s.value = fngValue;
    if (fngOkMs != 0) s.ageSec = (juce::Time::currentTimeMillis() - fngOkMs) / 1000.0;
    return s;
}
ContextClient::DomState ContextClient::dominance() const {
    juce::ScopedLock sl(const_cast<juce::CriticalSection&>(lock));
    DomState s; s.value = domValue;
    if (domOkMs != 0) s.ageSec = (juce::Time::currentTimeMillis() - domOkMs) / 1000.0;
    return s;
}

std::optional<std::string> ContextClient::getText(const char* url) {
    juce::URL u(url);
    auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(10000);
    std::unique_ptr<juce::InputStream> in(u.createInputStream(opts));
    if (in == nullptr) return std::nullopt;
    auto text = in->readEntireStreamAsString();
    if (text.isEmpty() || text.length() > 1024 * 1024) return std::nullopt;
    return text.toStdString();
}

bool ContextClient::fetchFng() {
    auto text = getText("https://api.alternative.me/fng/?limit=1");
    if (!text) return false;
    auto parsed = tickerbar::parseFearGreed(*text);
    if (!parsed) return false;
    juce::ScopedLock sl(lock);
    fngValue = *parsed;
    fngOkMs = juce::Time::currentTimeMillis();
    return true;
}

bool ContextClient::fetchDom() {
    auto text = getText("https://api.coingecko.com/api/v3/global");
    if (!text) return false;
    auto parsed = tickerbar::parseBtcDominance(*text);
    if (!parsed) return false; // rate-limit bodies land here too: keep last value
    juce::ScopedLock sl(lock);
    domValue = *parsed;
    domOkMs = juce::Time::currentTimeMillis();
    return true;
}

void ContextClient::run() {
    while (!threadShouldExit()) {
        const bool okF = fetchFng();
        const bool okD = fetchDom();
        if ((okF || okD) && onUpdate) {
            auto cb = onUpdate;
            juce::MessageManager::callAsync([cb]{ cb(); });
        }
        // Slow cadence when healthy (the F&G index itself updates about
        // daily; dominance drifts slowly). A failure retries in a minute
        // so a transient rate-limit clears without hammering the source.
        wait(okF && okD ? 300000 : 60000);
    }
}
