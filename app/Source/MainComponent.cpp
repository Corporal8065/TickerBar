#include "MainComponent.h"
#include <tickerbar/Format.hpp>
#include <tickerbar/Stale.hpp>
#include <tickerbar/Layout.hpp>
#include "SettingsStore.h"
#if JUCE_WINDOWS
 #include <windows.h> // native rects for the AppBar sync (physical pixels)
#endif
static const char* kVersion = "v0.2.0";
static juce::String dockEdgeName(tickerbar::DockEdge e) {
    switch (e) {
        case tickerbar::DockEdge::Top: return "Top";
        case tickerbar::DockEdge::Bottom: return "Bottom";
        case tickerbar::DockEdge::Left: return "Left";
        case tickerbar::DockEdge::Right: return "Right";
    }
    return "Top";
}

// =====================================================================
// Status dot — the feed honesty light: green live, amber stale, red down
// =====================================================================
class StatusDot : public juce::Component {
public:
    tickerbar::FeedStatus status = tickerbar::FeedStatus::Down;
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat().reduced(1.5f);
        juce::Colour c = status == tickerbar::FeedStatus::Live ? juce::Colour(0xff2fd68a)
                       : status == tickerbar::FeedStatus::Stale ? juce::Colour(0xfffad927)
                       : juce::Colour(0xfffe5a3c);
        g.setColour(c.withAlpha(0.25f)); g.fillEllipse(b.expanded(1.5f));
        g.setColour(c); g.fillEllipse(b);
    }
};

// =====================================================================
// CoinCell — one watched coin: symbol, price, 24h % chip, sparkline.
// A click opens the detail view; a drag moves the whole bar.
// =====================================================================
class CoinCell : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    MainComponent* owner = nullptr;
    std::string symbol;
    tickerbar::AssetCtx ctx;
    std::vector<tickerbar::Candle> candles;
    bool has = false, stale = false;

    explicit CoinCell(MainComponent* o) : owner(o) { setTooltip(""); }
    void setData(const tickerbar::AssetCtx& c, std::vector<tickerbar::Candle> cs, bool isStale) {
        ctx = c; candles = std::move(cs); has = c.hasData; stale = isStale; repaint();
    }
    void flash() { flashUntilMs = juce::Time::currentTimeMillis() + 12000; startTimerHz(6); repaint(); }
    bool flashing() const { return juce::Time::currentTimeMillis() < flashUntilMs; }
    void timerCallback() override { if (!flashing()) { stopTimer(); repaint(); } else repaint(); }

    void mouseDown(const juce::MouseEvent& e) override { owner->windowDragStarted(e); }
    void mouseDrag(const juce::MouseEvent& e) override { owner->windowDragged(e); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.getDistanceFromDragStart() < 5 && e.mouseWasClicked()) owner->openDetail(juce::String(symbol));
    }

    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        auto b = getLocalBounds().toFloat().reduced(1.f);
        const float alpha = stale ? 0.55f : 1.0f;
        g.setColour(th.panel.withAlpha(0.92f * alpha));
        g.fillRoundedRectangle(b, 8.f);
        if (flashing()) {
            const bool phase = (juce::Time::currentTimeMillis() / 400) % 2 == 0;
            g.setColour((phase ? th.warn : th.accent).withAlpha(0.95f));
            g.drawRoundedRectangle(b, 8.f, 2.f);
        } else {
            g.setColour(th.border.withAlpha(alpha));
            g.drawRoundedRectangle(b, 8.f, 1.f);
        }
        auto r = b.reduced(9.f, 5.f);
        // top row: symbol (left) + 24h % (right). v0.2.0 layout rule
        // (core Layout.hpp, unit-tested): the % is measured and
        // reserved IN FULL first; the symbol takes what remains and
        // is the only text that may clip. Jackson's field screenshot
        // showed "-1...." under the old fixed-64px symbol split.
        auto top = r.removeFromTop(18.f);
        auto pct = has ? tickerbar::changePct(ctx.price, ctx.open24h) : std::nullopt;
        const juce::Font symFont(13.5f, juce::Font::bold), pctFont(12.5f, juce::Font::bold);
        const juce::String pctText = pct ? juce::String(tickerbar::formatPct(*pct)) : juce::String();
        auto lay = tickerbar::coinCellTopRow((int)top.getWidth(),
                                             (int)std::ceil(symFont.getStringWidthFloat(juce::String(symbol))),
                                             pct ? (int)std::ceil(pctFont.getStringWidthFloat(pctText)) : 0);
        g.setColour(th.text.withAlpha(alpha)); g.setFont(symFont);
        g.drawText(juce::String(symbol), top.removeFromLeft((float)lay.symbolW), juce::Justification::centredLeft);
        top.removeFromLeft((float)lay.gap);
        if (pct) {
            g.setColour((*pct >= 0 ? th.good : th.bad).withAlpha(alpha));
            g.setFont(pctFont);
            g.drawText(pctText, top.removeFromLeft((float)lay.pctW), juce::Justification::centredRight);
        }
        // price row: price (left) + 1h % (right, v0.2.0 item e — from
        // the hourly candles already fetched for the sparkline; no
        // extra API calls). Same never-truncate rule for the 1h text.
        auto mid = r.removeFromTop(24.f);
        auto pct1h = has ? tickerbar::changePct1h(candles) : std::nullopt;
        const juce::String priceText = has ? juce::String(tickerbar::formatPrice(ctx.price)) : juce::String("--");
        const juce::String h1Text = pct1h ? ("1h " + juce::String(tickerbar::formatPct(*pct1h))) : juce::String();
        const juce::Font priceFont(16.5f, juce::Font::bold), h1Font(11.5f, juce::Font::bold);
        auto lay2 = tickerbar::coinCellTopRow((int)mid.getWidth(),
                                              (int)std::ceil(priceFont.getStringWidthFloat(priceText)),
                                              pct1h ? (int)std::ceil(h1Font.getStringWidthFloat(h1Text)) : 0);
        g.setColour(th.text.withAlpha(alpha)); g.setFont(priceFont);
        g.drawText(priceText, mid.removeFromLeft((float)lay2.symbolW), juce::Justification::centredLeft);
        mid.removeFromLeft((float)lay2.gap);
        if (pct1h) {
            g.setColour((*pct1h >= 0 ? th.good : th.bad).withAlpha(alpha));
            g.setFont(h1Font);
            g.drawText(h1Text, mid.removeFromLeft((float)lay2.pctW), juce::Justification::centredRight);
        }
        // sparkline (24h of hourly closes) or state line
        if (!candles.empty()) {
            std::vector<double> closes;
            for (auto& c : candles) closes.push_back(c.close);
            auto norm = tickerbar::normalizeSeries(closes);
            auto area = r.reduced(0.f, 2.f);
            juce::Path p;
            for (size_t i = 0; i < norm.size(); ++i) {
                float x = area.getX() + area.getWidth() * (float)i / (float)(norm.size() - 1);
                float y = area.getBottom() - norm[i] * area.getHeight();
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            const bool up = pct && *pct >= 0;
            auto lineCol = (up ? th.good : th.bad).withAlpha(0.9f * alpha);
            juce::Path fill = p;
            fill.lineTo(area.getRight(), area.getBottom()); fill.lineTo(area.getX(), area.getBottom()); fill.closeSubPath();
            g.setColour(lineCol.withAlpha(0.16f)); g.fillPath(fill);
            g.setColour(lineCol); g.strokePath(p, juce::PathStrokeType(1.4f));
        } else {
            g.setColour(th.dim.withAlpha(alpha)); g.setFont(10.5f);
            g.drawText(has ? juce::String("chart loading...") : juce::String("waiting for data..."),
                       r, juce::Justification::centredLeft);
        }
        if (stale && has) {
            g.setColour(th.warn); g.setFont(juce::Font(9.f, juce::Font::bold));
            g.drawText("STALE", b.reduced(7.f, 4.f), juce::Justification::bottomRight);
        }
    }
private:
    juce::int64 flashUntilMs = 0;
};

// =====================================================================
// ContextCell — one market-context reading (Fear & Greed / BTC
// dominance). Same visual language and stale honesty as a CoinCell:
// greyed at 0.55 alpha with a STALE tag when the value is older than
// the core context threshold; "--" until the first real value lands.
// =====================================================================
class ContextCell : public juce::Component, public juce::SettableTooltipClient {
public:
    MainComponent* owner = nullptr;
    juce::String title, valueText, subText;
    juce::Colour valueColour;
    bool has = false, stale = false;
    bool fngLink = false; // Fear & Greed cell only: a click opens the index page (v0.2.0 item d)
    explicit ContextCell(MainComponent* o) : owner(o) {}
    void setData(const juce::String& v, const juce::String& sub, juce::Colour col, bool isStale) {
        if (v == valueText && sub == subText && col == valueColour && isStale == stale && has) { repaint(); return; }
        valueText = v; subText = sub; valueColour = col; stale = isStale; has = true; repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override { owner->windowDragStarted(e); }
    void mouseDrag(const juce::MouseEvent& e) override { owner->windowDragged(e); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (fngLink && e.getDistanceFromDragStart() < 5 && e.mouseWasClicked())
            juce::URL("https://alternative.me/crypto/fear-and-greed-index/").launchInDefaultBrowser();
    }
    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        auto b = getLocalBounds().toFloat().reduced(1.f);
        const float alpha = stale ? 0.55f : 1.0f;
        g.setColour(th.panel.withAlpha(0.92f * alpha));
        g.fillRoundedRectangle(b, 8.f);
        g.setColour(th.border.withAlpha(alpha));
        g.drawRoundedRectangle(b, 8.f, 1.f);
        auto r = b.reduced(9.f, 5.f);
        g.setColour(th.dim.withAlpha(alpha)); g.setFont(juce::Font(9.f, juce::Font::bold));
        g.drawText(title, r.removeFromTop(12.f), juce::Justification::centredLeft);
        g.setColour((has ? valueColour : th.dim).withAlpha(alpha)); g.setFont(juce::Font(16.5f, juce::Font::bold));
        g.drawText(has ? valueText : juce::String("--"), r.removeFromTop(24.f), juce::Justification::centredLeft);
        g.setColour(th.dim.withAlpha(alpha)); g.setFont(10.5f);
        g.drawText(has ? subText : juce::String("loading..."), r, juce::Justification::centredLeft);
        if (stale && has) {
            g.setColour(th.warn); g.setFont(juce::Font(9.f, juce::Font::bold));
            g.drawText("STALE", b.reduced(7.f, 4.f), juce::Justification::bottomRight);
        }
    }
};

// =====================================================================
// Detail view (CallOutBox content): candlestick chart + volume bars
// with 24h / 7d / 30d timescale switching (v0.2.0 item c), full stats
// and the alert editors for this symbol. The component polls the
// owner's PriceClient once a second for the selected range and asks
// for a fetch when the cache is empty (the client throttles).
// Feed honesty: Coinbase ranges are real OHLCV (24h/7d hourly, 30d
// daily — Coinbase caps one candles request at 300). CoinGecko
// (HYPE) supplies hourly closes only: candles are flat ticks, there
// are no volume bars, and 30d does not exist — the note line under
// the range buttons says so in words, from PriceClient::detailNote.
// =====================================================================
class DetailComponent : public juce::Component, private juce::Timer {
public:
    DetailComponent(MainComponent* o, const juce::String& sym, const tickerbar::AssetCtx& c,
                    std::vector<tickerbar::Candle> cs, bool isStale, const juce::String& asOf)
        : owner(o), symbol(sym), ctx(c), candles(std::move(cs)), stale(isStale) {
        auto& th = owner->theme();
        const bool gecko = tickerbar::sourceForSymbol(sym.toStdString()) == tickerbar::PriceSource::CoinGecko;
        title.setText(sym + (gecko ? "  -  spot price (CoinGecko)" : "  -  spot price (Coinbase)"),
                      juce::dontSendNotification);
        title.setFont(juce::Font(15.f, juce::Font::bold)); title.setColour(juce::Label::textColourId, th.text);
        addAndMakeVisible(title);
        asOfLabel.setText(isStale ? ("STALE - last data " + asOf) : ("as of " + asOf), juce::dontSendNotification);
        asOfLabel.setFont(10.5f); asOfLabel.setColour(juce::Label::textColourId, isStale ? th.warn : th.dim);
        addAndMakeVisible(asOfLabel);
        auto pct = tickerbar::changePct(ctx.price, ctx.open24h);
        auto hi = ctx.high24h > 0 ? std::optional<double>(ctx.high24h) : tickerbar::candlesHigh(candles);
        auto lo = ctx.low24h > 0 ? std::optional<double>(ctx.low24h) : tickerbar::candlesLow(candles);
        auto row = [&](const juce::String& k, const juce::String& v, juce::Colour vc = juce::Colour()) {
            juce::String s2 = k + "  " + v;
            auto* l = new juce::Label({}, s2);
            l->setFont(juce::Font(13.f)); l->setColour(juce::Label::textColourId, vc.getARGB() ? vc : th.text);
            rows.add(l); addAndMakeVisible(l);
        };
        row("Price (spot)", juce::String(tickerbar::formatPrice(ctx.price)));
        if (pct) row("24h change", juce::String(tickerbar::formatPct(*pct)), *pct >= 0 ? th.good : th.bad);
        if (auto p1h = tickerbar::changePct1h(candles))
            row("1h change", juce::String(tickerbar::formatPct(*p1h)), *p1h >= 0 ? th.good : th.bad);
        if (hi) row("24h high", juce::String(tickerbar::formatPrice(*hi)));
        if (lo) row("24h low", juce::String(tickerbar::formatPrice(*lo)));
        row("24h volume", juce::String(tickerbar::formatVolume(ctx.dayVolumeUsd)));
        if (ctx.marketCapUsd > 0) row("Market cap", juce::String(tickerbar::formatVolume(ctx.marketCapUsd)));
        // range buttons (radio group) + the feed-limits note line
        auto styleRangeBtn = [&](juce::TextButton& b, const char* label, tickerbar::DetailRange r) {
            b.setButtonText(label);
            b.setClickingTogglesState(true);
            b.setRadioGroupId(7701);
            b.setToggleState(range == r, juce::dontSendNotification);
            b.onClick = [this, r] { range = r; refreshRange(); };
            addAndMakeVisible(b);
        };
        styleRangeBtn(btn24, "24h", tickerbar::DetailRange::H24);
        styleRangeBtn(btn7, "7d", tickerbar::DetailRange::D7);
        styleRangeBtn(btn30, "30d", tickerbar::DetailRange::D30);
        noteLabel.setFont(10.f); noteLabel.setColour(juce::Label::textColourId, th.dim);
        addAndMakeVisible(noteLabel);
        alertsTitle.setText("Price alerts for " + sym, juce::dontSendNotification);
        alertsTitle.setFont(juce::Font(12.5f, juce::Font::bold)); alertsTitle.setColour(juce::Label::textColourId, th.accent);
        addAndMakeVisible(alertsTitle);
        auto lv = owner->levelsFor(sym.toStdString());
        auto styleEditor = [&](juce::TextEditor& e, double v) {
            e.setColour(juce::TextEditor::backgroundColourId, th.deepest);
            e.setColour(juce::TextEditor::textColourId, th.text);
            e.setColour(juce::TextEditor::outlineColourId, th.border);
            if (!std::isnan(v)) e.setText(juce::String(v, 6), juce::dontSendNotification);
            addAndMakeVisible(e);
        };
        aboveLabel.setText("Above", juce::dontSendNotification); belowLabel.setText("Below", juce::dontSendNotification);
        for (auto* l : { &aboveLabel, &belowLabel }) { l->setFont(11.f); l->setColour(juce::Label::textColourId, th.dim); addAndMakeVisible(l); }
        styleEditor(aboveEditor, lv.above); styleEditor(belowEditor, lv.below);
        setAlertsBtn.onClick = [this, sym] {
            auto parse = [](juce::TextEditor& e) {
                auto t = e.getText().trim();
                return t.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : t.getDoubleValue();
            };
            owner->setAlerts(sym.toStdString(), parse(aboveEditor), parse(belowEditor));
            if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss();
        };
        clearBtn.onClick = [this, sym] {
            owner->setAlerts(sym.toStdString(), std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN());
            if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss();
        };
        addAndMakeVisible(setAlertsBtn); addAndMakeVisible(clearBtn);
        refreshRange();
        startTimerHz(1);
        setSize(390, 512);
    }
    ~DetailComponent() override { stopTimer(); }
    void timerCallback() override { refreshRange(); }
    void refreshRange() {
        auto cs = owner->client().candlesFor(symbol.toStdString(), range);
        if (cs.empty()) owner->client().requestCandles(symbol.toStdString(), range);
        candles = std::move(cs);
        juce::String note = juce::String(owner->client().detailNote(symbol.toStdString(), range));
        if (note.isEmpty()) {
            if (candles.empty()) note = "loading...";
            else if (range == tickerbar::DetailRange::D30 &&
                     tickerbar::sourceForSymbol(symbol.toStdString()) == tickerbar::PriceSource::Coinbase)
                note = "Coinbase caps one candles request at 300 - 30d is daily candles.";
            else if (range == tickerbar::DetailRange::D7 &&
                     tickerbar::sourceForSymbol(symbol.toStdString()) == tickerbar::PriceSource::Coinbase)
                note = "Hourly candles (168).";
        }
        if (note != noteLabel.getText()) noteLabel.setText(note, juce::dontSendNotification);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        g.fillAll(th.panel);
        auto area = chartArea.toFloat().reduced(6.f);
        g.setColour(th.deepest); g.fillRoundedRectangle(area, 6.f);
        if (candles.size() > 1) {
            auto rangeInfo = tickerbar::candleRange(candles);
            if (rangeInfo) {
                // price pane = top ~74%, volume pane = bottom ~20%
                auto priceArea = area.withHeight(area.getHeight() * 0.74f);
                auto volArea = area.withTop(area.getBottom() - area.getHeight() * 0.20f);
                double lo = rangeInfo->lo, hi = rangeInfo->hi;
                if (!(hi > lo)) { const double pad = std::max(1e-9, hi * 0.005); hi += pad; lo -= pad; }
                const float step = priceArea.getWidth() / (float)candles.size();
                const float bodyW = std::max(1.0f, step * 0.62f);
                auto yFor = [&](double px) {
                    return priceArea.getBottom() - (float)((px - lo) / (hi - lo)) * priceArea.getHeight();
                };
                for (size_t i = 0; i < candles.size(); ++i) {
                    const auto& c = candles[i];
                    const float cx = priceArea.getX() + step * ((float)i + 0.5f);
                    const bool up = c.close >= c.open;
                    auto col = (c.close == c.open) ? th.dim : (up ? th.good : th.bad);
                    g.setColour(col);
                    g.drawLine(cx, yFor(c.high), cx, yFor(c.low), 1.f); // wick
                    const float yO = yFor(c.open), yC = yFor(c.close);
                    const float top = std::min(yO, yC), hgt = std::max(1.f, std::abs(yC - yO));
                    g.fillRect(cx - bodyW / 2.f, top, bodyW, hgt);       // body (flat tick for closes-only)
                    if (rangeInfo->hasVolume && rangeInfo->maxVolume > 0) {
                        const float vh = (float)(c.volume / rangeInfo->maxVolume) * volArea.getHeight();
                        g.setColour(col.withAlpha(0.55f));
                        g.fillRect(cx - bodyW / 2.f, volArea.getBottom() - vh, bodyW, vh);
                    }
                }
                g.setColour(th.dim); g.setFont(9.5f);
                g.drawText(juce::String(tickerbar::formatPrice(hi)), priceArea.reduced(4.f, 2.f), juce::Justification::topRight);
                g.drawText(juce::String(tickerbar::formatPrice(lo)), priceArea.reduced(4.f, 2.f), juce::Justification::bottomRight);
                if (!rangeInfo->hasVolume) {
                    g.setColour(th.dim);
                    g.drawText("no volume from this feed", volArea.reduced(4.f, 0.f), juce::Justification::centredRight);
                }
                return;
            }
        }
        g.setColour(th.dim); g.setFont(11.f);
        g.drawText("chart unavailable", area, juce::Justification::centred);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(14);
        title.setBounds(r.removeFromTop(22)); asOfLabel.setBounds(r.removeFromTop(15)); r.removeFromTop(6);
        auto rangeRow = r.removeFromTop(24);
        btn24.setBounds(rangeRow.removeFromLeft(58)); rangeRow.removeFromLeft(6);
        btn7.setBounds(rangeRow.removeFromLeft(58)); rangeRow.removeFromLeft(6);
        btn30.setBounds(rangeRow.removeFromLeft(58));
        noteLabel.setBounds(r.removeFromTop(15)); r.removeFromTop(4);
        chartArea = r.removeFromTop(150); r.removeFromTop(8);
        for (auto* l : rows) l->setBounds(r.removeFromTop(20));
        r.removeFromTop(8);
        alertsTitle.setBounds(r.removeFromTop(18)); r.removeFromTop(4);
        auto rowA = r.removeFromTop(24);
        aboveLabel.setBounds(rowA.removeFromLeft(44)); aboveEditor.setBounds(rowA.removeFromLeft(110));
        rowA.removeFromLeft(10);
        belowLabel.setBounds(rowA.removeFromLeft(44)); belowEditor.setBounds(rowA.removeFromLeft(110));
        r.removeFromTop(8);
        auto rowB = r.removeFromTop(26);
        setAlertsBtn.setBounds(rowB.removeFromLeft(110)); rowB.removeFromLeft(8); clearBtn.setBounds(rowB.removeFromLeft(110));
    }
private:
    MainComponent* owner;
    juce::String symbol;
    tickerbar::AssetCtx ctx;
    std::vector<tickerbar::Candle> candles;
    bool stale;
    tickerbar::DetailRange range = tickerbar::DetailRange::H24;
    juce::Label title, asOfLabel, alertsTitle, aboveLabel, belowLabel, noteLabel;
    juce::OwnedArray<juce::Label> rows;
    juce::TextEditor aboveEditor, belowEditor;
    juce::TextButton setAlertsBtn { "Set alerts" }, clearBtn { "Clear" };
    juce::TextButton btn24, btn7, btn30;
    juce::Rectangle<int> chartArea;
};

// =====================================================================
// Settings window content (MeterBar idiom: panels of small controls
// writing straight into the shared state, firing callbacks).
// =====================================================================
class SettingsContent : public juce::Component {
public:
    MainComponent* owner = nullptr;
    tickerbar::TickerState* st = nullptr;
    std::function<void()> onChanged;          // visual/window settings changed
    std::function<void()> onWatchlistChanged; // watchlist or alerts changed
    SettingsContent() { setSize(700, 620); }
    void build() {
        removeAllChildren(); owned.clear(); y_ = 8;
        if (!owner || !st) return;
        th_ = owner->theme();
        panel("Watchlist", [&]{
            auto names = owner->universeNames();
            for (auto& sym : st->watchlist) {
                auto* l = add(new juce::Label({}, juce::String(sym)));
                l->setFont(juce::Font(13.5f, juce::Font::bold)); l->setColour(juce::Label::textColourId, th_.text);
                l->setBounds(24, y_, 120, 22);
                auto* rm = add(new juce::TextButton("Remove"));
                rm->setBounds(150, y_, 90, 22);
                std::string s = sym;
                rm->onClick = [this, s]{
                    auto& wl = st->watchlist;
                    wl.erase(std::remove(wl.begin(), wl.end(), s), wl.end());
                    if (wl.empty()) wl.push_back("BTC"); // the bar is never empty
                    changedWatchlist();
                };
                y_ += 26;
            }
            auto* ed = add(new juce::TextEditor());
            ed->setColour(juce::TextEditor::backgroundColourId, th_.deepest);
            ed->setColour(juce::TextEditor::textColourId, th_.text);
            ed->setColour(juce::TextEditor::outlineColourId, th_.border);
            ed->setBounds(24, y_, 130, 24);
            auto* addBtn = add(new juce::TextButton("Add symbol"));
            addBtn->setBounds(162, y_, 110, 24);
            auto* err = add(new juce::Label({}, ""));
            err->setFont(11.f); err->setColour(juce::Label::textColourId, th_.warn);
            err->setBounds(284, y_, 400, 24);
            addBtn->onClick = [this, ed, err, names]{
                auto sym = ed->getText().trim().toUpperCase();
                if (sym.isEmpty()) return;
                if (names.isEmpty()) { err->setText("Coin list hasn't loaded yet - check the connection (status dot), then retry.", juce::dontSendNotification); return; }
                if (!names.contains(sym)) { err->setText(sym + " is not a supported symbol (Coinbase USD pairs + HYPE).", juce::dontSendNotification); return; }
                if (std::find(st->watchlist.begin(), st->watchlist.end(), sym.toStdString()) != st->watchlist.end()) {
                    err->setText(sym + " is already on the bar.", juce::dontSendNotification); return;
                }
                st->watchlist.push_back(sym.toStdString());
                ed->clear(); err->setText("", juce::dontSendNotification);
                changedWatchlist();
            };
            y_ += 30;
            note("Any Coinbase USD pair, plus HYPE (served by CoinGecko). Validated against the live coin list.");
        });
        panel("Alerts", [&]{
            toggle("Soft click sound on a cross (default off)", st->alertSound, [&](bool b){ st->alertSound = b; });
            toggle("Windows toast notification on a cross", st->alertToasts, [&](bool b){ st->alertToasts = b; });
            note("Levels are also editable by clicking a coin on the bar. A cross fires once, then re-arms when price returns.");
            for (auto& sym : st->watchlist) {
                auto lv = owner->levelsFor(sym);
                auto* l = add(new juce::Label({}, juce::String(sym)));
                l->setFont(juce::Font(12.5f, juce::Font::bold)); l->setColour(juce::Label::textColourId, th_.text);
                l->setBounds(24, y_, 56, 22);
                auto* lab = add(new juce::Label({}, "Above")); lab->setFont(11.f); lab->setColour(juce::Label::textColourId, th_.dim); lab->setBounds(84, y_, 40, 22);
                auto* ea = add(new juce::TextEditor());
                auto* lb = add(new juce::Label({}, "Below")); lb->setFont(11.f); lb->setColour(juce::Label::textColourId, th_.dim); lb->setBounds(240, y_, 40, 22);
                auto* eb = add(new juce::TextEditor());
                for (auto* e : { ea, eb }) {
                    e->setColour(juce::TextEditor::backgroundColourId, th_.deepest);
                    e->setColour(juce::TextEditor::textColourId, th_.text);
                    e->setColour(juce::TextEditor::outlineColourId, th_.border);
                }
                ea->setBounds(126, y_, 104, 22); eb->setBounds(282, y_, 104, 22);
                if (!std::isnan(lv.above)) ea->setText(juce::String(lv.above, 6), juce::dontSendNotification);
                if (!std::isnan(lv.below)) eb->setText(juce::String(lv.below, 6), juce::dontSendNotification);
                auto* ap = add(new juce::TextButton("Apply"));
                ap->setBounds(398, y_, 70, 22);
                std::string s = sym;
                ap->onClick = [this, s, ea, eb]{
                    auto parse = [](juce::TextEditor* e) {
                        auto t = e->getText().trim();
                        return t.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : t.getDoubleValue();
                    };
                    owner->setAlerts(s, parse(ea), parse(eb));
                };
                y_ += 26;
            }
        });
        panel("Appearance", [&]{
            combo("Background style", {"Clean Dark", "Midnight Kids", "Flat"}, (int)st->backgroundStyle,
                  [&](int v){ st->backgroundStyle = (tickerbar::BackgroundStyle)v; });
            slider("Background opacity % (15-100)", 15.0, 100.0, st->backgroundOpacity * 100.0,
                   [&](double v){ st->backgroundOpacity = (float)(v / 100.0); });
            note("Theme: Midnight Kids (MeterBar's palette family). Below 100% opacity the desktop shows through the bar's background.");
        });
        panel("Window", [&]{
            toggle("Always on top", st->pinned, [&](bool b){ st->pinned = b; });
            toggle("Reserve screen space when docked (Windows)", st->reserveScreenSpace, [&](bool b){ st->reserveScreenSpace = b; });
            note("While ON and the bar is docked flush at a monitor edge, Windows shrinks that monitor's work area so maximised windows stop at the bar. Same control as the bar's Reserve button; the Dock button on the bar picks the edge. Reserve is event-driven (v0.2.0): it reacts to window moves/resizes, never polls.");
            combo("Dock edge", {"Top", "Bottom", "Left", "Right"}, (int)st->dockEdge, [&](int v){
                st->dockEdge = (tickerbar::DockEdge)v;
                if (onChanged) onChanged();
                owner->snapToDockEdge();
            });
            combo("Poll interval", {"10 s", "20 s", "30 s", "60 s"},
                  st->pollSec == 10 ? 0 : st->pollSec == 20 ? 1 : st->pollSec == 30 ? 2 : 3,
                  [&](int v){ st->pollSec = v == 0 ? 10 : v == 1 ? 20 : v == 2 ? 30 : 60; });
            buttonRow("Arrange windows clear of the bar", "Restore arranged windows",
                      [this]{ owner->doArrange(); }, [this]{ owner->doRestore(); });
            note("Arrange slides every other (non-maximised) window on the bar's monitor clear of the bar; Restore puts them back. Screen space: " + owner->screenSpaceStatus());
        });
        panel("About", [&]{
            note("TickerBar v0.2.0 - prices are SPOT prices: Coinbase Exchange public API for Coinbase-listed coins, CoinGecko for HYPE. No account, no keys, read-only.");
            note("Detail chart: candlesticks + volume with 24h / 7d / 30d ranges. Coinbase caps one candles request at 300: 24h/7d are hourly, 30d is daily. CoinGecko (HYPE) supplies hourly closes only - no OHLC, no volume, no 30d - and the chart note says so.");
            note("Polls every N seconds (Window panel). If the feed drops, cells grey out and keep the last real prices, marked STALE - never fake-fresh numbers.");
            note("Context cells: Fear & Greed index (alternative.me) and BTC dominance (CoinGecko) poll separately every ~5 minutes and go stale on their own clock - they never block prices.");
            note("Made with JUCE.");
        });
        setSize(700, y_ + 8);
    }
    void resized() override {}
private:
    int y_ = 8;
    Theme th_;
    std::vector<std::unique_ptr<juce::Component>> owned;
    template <typename T> T* add(T* c) { owned.emplace_back(c); addAndMakeVisible(c); return c; }
    void changed() { if (onChanged) onChanged(); }
    void changedWatchlist() { if (onWatchlistChanged) onWatchlistChanged(); }
    void panel(const juce::String& title, std::function<void()> body) {
        auto* hdr = add(new juce::Label({}, title)); hdr->setFont(juce::Font(15.f, juce::Font::bold));
        hdr->setColour(juce::Label::textColourId, th_.accent);
        hdr->setBounds(12, y_, 300, 22); y_ += 24;
        body(); y_ += 10;
    }
    void combo(const juce::String& label, std::vector<juce::String> items, int sel, std::function<void(int)> fn) {
        auto* l = add(new juce::Label({}, label)); l->setBounds(24, y_, 200, 22);
        l->setColour(juce::Label::textColourId, th_.text);
        auto* c = add(new juce::ComboBox()); int id = 1; for (auto& s : items) c->addItem(s, id++);
        c->setSelectedItemIndex(juce::jlimit(0, (int)items.size() - 1, sel), juce::dontSendNotification);
        c->setBounds(230, y_, 260, 22);
        c->onChange = [this, c, fn]{ fn(c->getSelectedItemIndex()); changed(); };
        y_ += 26;
    }
    void slider(const juce::String& label, double mn, double mx, double val, std::function<void(double)> fn) {
        auto* l = add(new juce::Label({}, label)); l->setBounds(24, y_, 200, 22);
        l->setColour(juce::Label::textColourId, th_.text);
        auto* s = add(new juce::Slider()); s->setRange(mn, mx, 0.01); s->setValue(val, juce::dontSendNotification);
        s->setBounds(230, y_, 300, 22);
        s->onValueChange = [this, s, fn]{ fn(s->getValue()); changed(); };
        y_ += 26;
    }
    void toggle(const juce::String& label, bool val, std::function<void(bool)> fn) {
        auto* t = add(new juce::ToggleButton(label)); t->setToggleState(val, juce::dontSendNotification);
        t->setColour(juce::ToggleButton::textColourId, th_.text);
        t->setBounds(24, y_, 460, 22);
        t->onClick = [this, t, fn]{ fn(t->getToggleState()); changed(); };
        y_ += 26;
    }
    void buttonRow(const juce::String& a, const juce::String& b, std::function<void()> fa, std::function<void()> fb) {
        auto* ba = add(new juce::TextButton(a)); ba->setBounds(24, y_, 260, 22); ba->onClick = fa;
        auto* bb = add(new juce::TextButton(b)); bb->setBounds(296, y_, 260, 22); bb->onClick = fb;
        y_ += 26;
    }
    void note(const juce::String& text) {
        auto* l = add(new juce::Label({}, text)); l->setFont(11.f);
        l->setColour(juce::Label::textColourId, th_.dim);
        l->setBounds(24, y_, 660, 16); y_ += 18;
    }
};
class TickerSettingsWindow : public juce::DocumentWindow {
public:
    SettingsContent content;
    TickerSettingsWindow() : juce::DocumentWindow("TickerBar Settings", juce::Colour(0xff231f31), juce::DocumentWindow::closeButton) {
        content.build();
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        setContentNonOwned(&viewport, true);
        setSize(740, 660); setResizable(true, false); setUsingNativeTitleBar(true);
    }
    void closeButtonPressed() override { setVisible(false); }
private:
    juce::Viewport viewport;
};

// =====================================================================
// MainComponent
// =====================================================================
MainComponent::MainComponent() {
    state = settingsstore::load();
    theme_ = midnightKidsTheme();
    setOpaque(false); // the window is non-opaque too: Background Opacity shows the desktop through
    setTooltip("TickerBar - prices are spot prices (Coinbase Exchange / CoinGecko)");

    wordmark_.setText("TickerBar", juce::dontSendNotification);
    wordmark_.setFont(juce::Font(15.f, juce::Font::bold)); wordmark_.setColour(juce::Label::textColourId, theme_.accent);
    versionLabel_.setText(kVersion, juce::dontSendNotification);
    versionLabel_.setFont(10.f); versionLabel_.setColour(juce::Label::textColourId, theme_.dim);
    statusLabel_.setFont(10.5f); statusLabel_.setColour(juce::Label::textColourId, theme_.dim);
    statusLabel_.setText("connecting...", juce::dontSendNotification);
    for (auto* c : { (juce::Component*)&wordmark_, (juce::Component*)&versionLabel_, (juce::Component*)&statusLabel_ }) {
        addAndMakeVisible(c);
        c->setInterceptsMouseClicks(false, false); // clicks fall through to the bar drag
    }
    reserveBtn_.setClickingTogglesState(true);
    reserveBtn_.setToggleState(state.reserveScreenSpace, juce::dontSendNotification);
    reserveBtn_.setTooltip("Reserve screen space: Windows keeps maximised windows clear of the docked bar");
    reserveBtn_.onClick = [this]{ state.reserveScreenSpace = reserveBtn_.getToggleState(); saveSettings(); syncScreenSpace(); };
    dockBtn_.setButtonText(dockEdgeName(state.dockEdge));
    dockBtn_.setTooltip("Dock edge: click to cycle Top / Bottom / Left / Right - the bar snaps to that edge");
    dockBtn_.onClick = [this]{ cycleDockEdge(); };
    settingsBtn_.onClick = [this]{
        settingsWin_->content.build();
        settingsWin_->setVisible(true); settingsWin_->toFront(true);
    };
    minBtn_.onClick = [this]{ if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                                 if (auto* peer = w->getPeer()) peer->setMinimised(true); };
    quitBtn_.onClick = []{ juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
    for (auto* b : { &reserveBtn_, &dockBtn_, &settingsBtn_, &minBtn_, &quitBtn_ }) addAndMakeVisible(b);

    settingsWin_ = std::make_unique<TickerSettingsWindow>();
    settingsWin_->content.owner = this;
    settingsWin_->content.st = &state;
    settingsWin_->content.onChanged = [this]{ settingsChanged(false); };
    settingsWin_->content.onWatchlistChanged = [this]{ settingsChanged(true); };
    settingsWin_->content.build();

    fngCell_ = std::make_unique<ContextCell>(this);
    fngCell_->title = "FEAR & GREED";
    fngCell_->fngLink = true;
    fngCell_->setMouseCursor(juce::MouseCursor::PointingHandCursor);
    fngCell_->setTooltip("Crypto Fear & Greed Index - alternative.me. Updates about daily; polled every 5 minutes. Click to open the index page.");
    domCell_ = std::make_unique<ContextCell>(this);
    domCell_->title = "BTC DOMINANCE";
    domCell_->setTooltip("BTC share of total crypto market cap - CoinGecko global. Polled every 5 minutes.");
    addAndMakeVisible(fngCell_.get());
    addAndMakeVisible(domCell_.get());

    state.windowW = std::max(state.windowW, minimumBarWidth()); // supported-width floor from the first frame
    rebuildCells();
    client_.setWatchlist(state.watchlist);
    client_.setPollSec(state.pollSec);
    client_.onUpdate = [this]{ refreshData(); };
    client_.startPolling();
    contextClient_.onUpdate = [this]{ refreshContext(); };
    contextClient_.startPolling();
    refreshContext();
    syncScreenSpace();
    startTimerHz(4);
    setSize(state.windowW, state.windowH);
}

MainComponent::~MainComponent() {
    stopTimer();
    saveSettings();
    screenSpace_.release();
    contextClient_.stopThread(4500);
    client_.stopThread(4500);
}

void MainComponent::paint(juce::Graphics& g) {
    fillStyledBackground(g, getLocalBounds().toFloat(), state.backgroundStyle, theme_,
                         juce::jlimit(0.15f, 1.0f, state.backgroundOpacity));
    g.setColour(theme_.border);
    g.drawRect(getLocalBounds().toFloat(), 1.f);
    // status dot, painted here (position fixed in resized()) — colour by feed state
    auto dot = juce::Rectangle<float>(14.f, (float)getHeight() / 2.f - 5.f, 10.f, 10.f);
    auto c = feed_ == tickerbar::FeedStatus::Live ? theme_.good
           : feed_ == tickerbar::FeedStatus::Stale ? theme_.warn : theme_.bad;
    g.setColour(c.withAlpha(0.25f)); g.fillEllipse(dot.expanded(2.f));
    g.setColour(c); g.fillEllipse(dot);
}

void MainComponent::resized() {
    auto r = getLocalBounds().reduced(8, 6);
    r.removeFromLeft(24); // status dot (painted)
    auto left = r.removeFromLeft(96);
    wordmark_.setBounds(left.removeFromTop(20));
    versionLabel_.setBounds(left.removeFromTop(12));
    statusLabel_.setBounds(left);
    quitBtn_.setBounds(r.removeFromRight(26).withSizeKeepingCentre(26, 24));
    minBtn_.setBounds(r.removeFromRight(26).withSizeKeepingCentre(26, 24));
    settingsBtn_.setBounds(r.removeFromRight(76).withSizeKeepingCentre(72, 24));
    reserveBtn_.setBounds(r.removeFromRight(80).withSizeKeepingCentre(76, 24));
    dockBtn_.setBounds(r.removeFromRight(66).withSizeKeepingCentre(62, 24));
    // market-context group at the right of the coin cells
    if (domCell_ && fngCell_) {
        domCell_->setBounds(r.removeFromRight(112));
        r.removeFromRight(6);
        fngCell_->setBounds(r.removeFromRight(112));
        r.removeFromRight(6);
    }
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
        w->setResizeLimits(minimumBarWidth(), 56, 3840, 300);
    syncScreenSpace(); // v0.2.0: a resize is a reserve-machine event
    // cells share the middle equally
    const int n = (int)cells_.size();
    if (n > 0) {
        const int gap = 6;
        const int w = (r.getWidth() - gap * (n - 1)) / n;
        for (int i = 0; i < n; ++i) {
            auto cellR = r.removeFromLeft(w);
            cells_[(size_t)i]->setBounds(cellR);
            r.removeFromLeft(gap);
        }
    }
}

void MainComponent::mouseDown(const juce::MouseEvent& e) { windowDragStarted(e); }
void MainComponent::mouseDrag(const juce::MouseEvent& e) { windowDragged(e); }
void MainComponent::windowDragStarted(const juce::MouseEvent& e) {
    // ComponentDragger works in screen coordinates, so events forwarded
    // from child cells drag the top-level window correctly.
    if (auto* top = getTopLevelComponent()) dragger_.startDraggingComponent(top, e);
}
void MainComponent::windowDragged(const juce::MouseEvent& e) {
    if (auto* top = getTopLevelComponent()) dragger_.dragComponent(top, e, nullptr);
}

void MainComponent::rebuildCells() {
    cells_.clear();
    for (auto& sym : state.watchlist) {
        auto cell = std::make_unique<CoinCell>(this);
        cell->symbol = sym;
        cell->setTooltip(juce::String(sym) + " - click for detail. Prices: spot (Coinbase / CoinGecko).");
        addAndMakeVisible(cell.get());
        cells_.push_back(std::move(cell));
    }
    resized();
}

void MainComponent::refreshData() {
    auto snap = client_.snapshot();
    for (auto& cell : cells_) {
        if (const auto* ctx = snap.find(cell->symbol)) {
            // Staleness is per symbol now: each source (Coinbase ~20 s,
            // CoinGecko ~60 s) ages on its own clock with its own 3x
            // threshold, so a CoinGecko hiccup can never grey out the
            // Coinbase cells (and a healthy HYPE never flickers STALE).
            const bool stale = tickerbar::feedStatus(true, client_.ageSecFor(cell->symbol),
                                                     tickerbar::staleAfterSecFor(cell->symbol)) != tickerbar::FeedStatus::Live;
            cell->setData(*ctx, client_.candlesFor(cell->symbol), stale);
            auto& tracker = trackers_[cell->symbol];
            const auto lv = levelsFor(cell->symbol);
            unsigned ev = tracker.step(lv, ctx->price);
            if (ev != tickerbar::AlertNone) {
                cell->flash();
                if (state.alertSound) clickPlayer_.play();
                if (state.alertToasts) {
                    void* hwnd = nullptr;
                    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                        if (auto* peer = w->getPeer()) hwnd = peer->getNativeHandle();
                    toastNotifier_.show(hwnd, "TickerBar alert",
                                        tickerbar::alertNotificationText(cell->symbol, ev, lv, ctx->price));
                }
            }
        }
    }
    refreshStatus();
}

void MainComponent::refreshContext() {
    if (!fngCell_ || !domCell_) return;
    const auto fg = contextClient_.fng();
    if (fg.value) {
        const int band = tickerbar::fngBandIndex(fg.value->value);
        juce::Colour col = band == 0 ? theme_.bad : band == 1 ? theme_.warn
                         : band == 2 ? theme_.text : theme_.good;
        fngCell_->setData(juce::String(fg.value->value), juce::String(fg.value->classification), col,
                          tickerbar::contextStale(true, fg.ageSec));
    } else {
        fngCell_->setData("--", "loading...", theme_.dim, false);
    }
    const auto dom = contextClient_.dominance();
    if (dom.value) {
        domCell_->setData(juce::String(tickerbar::formatDominance(*dom.value)), "BTC share of market",
                          theme_.accent2, tickerbar::contextStale(true, dom.ageSec));
    } else {
        domCell_->setData("--", "loading...", theme_.dim, false);
    }
}

void MainComponent::refreshStatus() {
    // The bar's status is the worst watched symbol's status, each
    // judged on its own source's stale threshold.
    double worstAge = 0.0;
    if (!client_.hasData()) {
        feed_ = tickerbar::FeedStatus::Down;
    } else {
        feed_ = tickerbar::FeedStatus::Live;
        for (auto& sym : state.watchlist) {
            const double age = client_.ageSecFor(sym);
            worstAge = std::max(worstAge, age);
            if (tickerbar::feedStatus(true, age, tickerbar::staleAfterSecFor(sym)) != tickerbar::FeedStatus::Live)
                feed_ = tickerbar::FeedStatus::Stale;
        }
    }
    juce::String txt;
    if (feed_ == tickerbar::FeedStatus::Live) txt = "live";
    else if (feed_ == tickerbar::FeedStatus::Stale) {
        const int age = (int)worstAge;
        txt = age >= 86400 ? "stale"
            : age >= 90 ? "stale " + juce::String(age / 60) + "m"
                        : "stale " + juce::String(age) + "s";
    } else txt = client_.consecutiveFailures() > 0 ? "no connection" : "connecting...";
    statusLabel_.setText(txt, juce::dontSendNotification);
    repaint();
}

void MainComponent::timerCallback() {
    refreshStatus();
    refreshContext(); // age-based stale transitions need no new data to show
    // v0.2.0: NO screen-space work here. The v0.1 sync ran from this
    // 4 Hz timer and re-asserted the AppBar several times a second —
    // half of the flicker Jackson field-reported (MeterBar v0.18's
    // engine). The reserve machine is event-driven now:
    // notifyWindowGeometryChanged() feeds it window moved/resized.
    auto b = currentWindowBounds();
    if (b != lastSavedBounds_ && !b.isEmpty()) { lastSavedBounds_ = b; saveSettings(); }
}

juce::Rectangle<int> MainComponent::currentWindowBounds() const {
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) return w->getBounds();
    return {};
}

void MainComponent::saveSettings() {
    auto b = currentWindowBounds();
    if (!b.isEmpty()) { state.windowX = b.getX(); state.windowY = b.getY(); state.windowW = b.getWidth(); state.windowH = b.getHeight(); }
    settingsstore::save(state);
}

void MainComponent::settingsChanged(bool watchlistToo) {
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setAlwaysOnTop(state.pinned);
    client_.setPollSec(state.pollSec);
    if (watchlistToo) {
        client_.setWatchlist(state.watchlist);
        rebuildCells();
        rebuildSettingsDeferred();
    }
    if (reserveBtn_.getToggleState() != state.reserveScreenSpace)
        reserveBtn_.setToggleState(state.reserveScreenSpace, juce::dontSendNotification);
    dockBtn_.setButtonText(dockEdgeName(state.dockEdge));
    saveSettings();
    syncScreenSpace();
    repaint();
}

void MainComponent::rebuildSettingsDeferred() {
    // The settings content owns the very button whose click may have
    // triggered this (Remove/Add) — rebuilding it synchronously would
    // delete that button inside its own callback. Defer to the next
    // message-loop turn, when the click has fully unwound.
    juce::MessageManager::callAsync([this]{
        if (settingsWin_) settingsWin_->content.build();
    });
}

tickerbar::AlertLevels MainComponent::levelsFor(const std::string& symbol) const {
    auto it = state.alerts.find(symbol);
    return it == state.alerts.end() ? tickerbar::AlertLevels{} : it->second;
}
void MainComponent::setAlerts(const std::string& symbol, double above, double below) {
    tickerbar::AlertLevels lv; lv.above = above; lv.below = below;
    if (lv.anySet()) state.alerts[symbol] = lv; else state.alerts.erase(symbol);
    trackers_[symbol].reset();
    saveSettings();
}

juce::StringArray MainComponent::universeNames() const {
    juce::StringArray out;
    for (auto& s : client_.tradableSymbols()) out.add(juce::String(s));
    return out;
}

void MainComponent::openDetail(const juce::String& symbol) {
    auto snap = client_.snapshot();
    const auto* ctx = snap.find(symbol.toStdString());
    tickerbar::AssetCtx copy;
    if (ctx) copy = *ctx;
    const bool staleAll = tickerbar::feedStatus(client_.hasData(), client_.ageSecFor(symbol.toStdString()),
                                                tickerbar::staleAfterSecFor(symbol.toStdString())) != tickerbar::FeedStatus::Live;
    auto asOf = juce::Time::getCurrentTime().toString(false, true, true, false);
    auto* cell = [&]() -> CoinCell* {
        for (auto& c : cells_) if (c->symbol == symbol.toStdString()) return c.get();
        return nullptr;
    }();
    auto detail = std::make_unique<DetailComponent>(this, symbol, copy,
                                                    client_.candlesFor(symbol.toStdString()), staleAll, asOf);
    juce::Rectangle<int> anchor = cell ? cell->getScreenBounds() : getScreenBounds();
    juce::CallOutBox::launchAsynchronously(std::move(detail), anchor, nullptr);
}

// v0.2.0: on-bar Dock button — cycle the preferred edge and snap.
void MainComponent::cycleDockEdge() {
    state.dockEdge = (tickerbar::DockEdge)(((int)state.dockEdge + 1) % 4);
    dockBtn_.setButtonText(dockEdgeName(state.dockEdge));
    saveSettings();
    snapToDockEdge();
}

void MainComponent::snapToDockEdge() {
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    if (!w) return;
    auto display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(w->getBounds());
    if (display == nullptr) return;
    auto area = display->totalArea;
    auto b = w->getBounds();
    switch (state.dockEdge) {
        case tickerbar::DockEdge::Top:    b.setY(area.getY()); break;
        case tickerbar::DockEdge::Bottom: b.setY(area.getBottom() - b.getHeight()); break;
        case tickerbar::DockEdge::Left:   b.setX(area.getX()); break;
        case tickerbar::DockEdge::Right:  b.setX(area.getRight() - b.getWidth()); break;
    }
    b.setX(juce::jlimit(area.getX(), area.getRight() - b.getWidth(), b.getX()));
    b.setY(juce::jlimit(area.getY(), area.getBottom() - b.getHeight(), b.getY()));
    w->setBounds(b);
    syncScreenSpace();
}

// The supported-width floor: fixed chrome (dot + wordmark block +
// buttons + context cells, mirroring resized()) plus, per watched
// cell, core minCoinCellWidth() with the WORST-CASE % strings measured
// at the render fonts ("-100.00%" for 24h, "1h -100.00%" for 1h — the
// wider of the two governs). Below this width the layout rule cannot
// keep its promise, so the window simply may not be that narrow.
int MainComponent::minimumBarWidth() const {
    const juce::Font pctFont(12.5f, juce::Font::bold), h1Font(11.5f, juce::Font::bold), symFont(13.5f, juce::Font::bold);
    const int pctWorst = (int)std::ceil(std::max(pctFont.getStringWidthFloat("-100.00%"),
                                                 h1Font.getStringWidthFloat("1h -100.00%")));
    int cellsW = 0;
    for (auto& sym : state.watchlist) {
        const int symW = (int)std::ceil(symFont.getStringWidthFloat(juce::String(sym)));
        cellsW += tickerbar::minCoinCellWidth(symW, pctWorst);
    }
    if (!state.watchlist.empty()) cellsW += 6 * ((int)state.watchlist.size() - 1); // inter-cell gaps
    const int chrome = 16 /*outer pad*/ + 24 /*status dot*/ + 96 /*wordmark block*/
                     + 26 + 26 + 76 + 80 + 66 /*quit/min/settings/reserve/dock*/
                     + 112 + 6 + 112 + 6;     /*context cells + gaps*/
    return chrome + cellsW;
}

// MainWindow moved/resized feed — the reserve machine's event source
// (plus settings persistence for the new position).
void MainComponent::notifyWindowGeometryChanged() {
    syncScreenSpace();
    auto b = currentWindowBounds();
    if (b != lastSavedBounds_ && !b.isEmpty()) { lastSavedBounds_ = b; saveSettings(); }
}

// Reserve sync, v0.2.0 — MeterBar v0.18.1/v0.19.1 semantics, ported:
// the ReserveMachine (pure core, unit-tested) consumes geometry
// EVENTS and returns transitions; this function performs ONLY the
// returned action. Nothing here runs from a timer. Hysteresis: dock
// at gap <= 2 px, release only past 8 px — the 3..8 px band is a dead
// zone where repeat observations return None forever, so a 1 px rect
// wobble can never tear the AppBar down and rebuild it (the flicker
// Jackson field-reported: bar jumps, windows resize, bar jumps back).
void MainComponent::syncScreenSpace() {
    tickerbar::IntRect bar{}, mon{};
    long long monId = 0;
    void* hwnd = nullptr;
#if JUCE_WINDOWS
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    HWND h = (w && w->getPeer()) ? static_cast<HWND>(w->getPeer()->getNativeHandle()) : nullptr;
    if (h) {
        hwnd = h;
        RECT wr{}; GetWindowRect(h, &wr);
        HMONITOR hm = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof mi; GetMonitorInfoW(hm, &mi);
        bar = { wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top };
        mon = { mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top };
        monId = (long long)(intptr_t)hm;
    }
#else
    auto b = currentWindowBounds();
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect(b)) {
        auto a = d->totalArea;
        bar = { b.getX(), b.getY(), b.getWidth(), b.getHeight() };
        mon = { a.getX(), a.getY(), a.getWidth(), a.getHeight() };
        monId = (long long)a.getX() * 100000 + a.getY();
    }
#endif
    if (bar.w <= 0 || mon.w <= 0) return;
    tickerbar::EdgeGaps gaps{ bar.y - mon.y, mon.bottom() - bar.bottom(),
                              bar.x - mon.x, mon.right() - bar.right() };
    auto action = reserveMachine_.update(state.reserveScreenSpace, gaps, bar, mon, monId);
    using namespace tickerbar;
    switch (action) {
        case ReserveAction::None: break;
        case ReserveAction::Release: screenSpace_.release(); break;
        case ReserveAction::Switch: screenSpace_.release(); [[fallthrough]];
        case ReserveAction::Register: [[fallthrough]];
        case ReserveAction::Update: {
            auto strip = reserveMachine_.requestedRect();
            screenSpace_.reserve(hwnd, reserveMachine_.edge(), strip.x, strip.y, strip.w, strip.h);
            break;
        }
    }
    if (reserveBtn_.getToggleState() != state.reserveScreenSpace)
        reserveBtn_.setToggleState(state.reserveScreenSpace, juce::dontSendNotification);
}

void MainComponent::doArrange() {
#if JUCE_WINDOWS
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    HWND hwnd = (w && w->getPeer()) ? static_cast<HWND>(w->getPeer()->getNativeHandle()) : nullptr;
    if (hwnd) {
        RECT wr{}; GetWindowRect(hwnd, &wr);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof mi; GetMonitorInfoW(mon, &mi);
        const tickerbar::IntRect barR{ wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top };
        const tickerbar::IntRect monR{ mi.rcMonitor.left, mi.rcMonitor.top,
                                       mi.rcMonitor.right - mi.rcMonitor.left,
                                       mi.rcMonitor.bottom - mi.rcMonitor.top };
        tickerbar::EdgeGaps gaps{ barR.y - monR.y, monR.bottom() - barR.bottom(),
                                  barR.x - monR.x, monR.right() - barR.right() };
        const auto edge = reserveMachine_.reserved() ? reserveMachine_.edge()
                                                     : tickerbar::ReserveMachine::nearestEdge(gaps, barR);
        const auto stripRect = tickerbar::ReserveMachine::stripRect(edge, barR, monR);
        const int strip = (edge == tickerbar::ReserveEdge::Top || edge == tickerbar::ReserveEdge::Bottom)
                              ? stripRect.h : stripRect.w;
        screenSpace_.arrange(hwnd, strip, edge);
        return;
    }
#endif
    screenSpace_.arrange(nullptr, 0, tickerbar::ReserveEdge::Top); // stub: records the honest "unavailable" status
}
void MainComponent::doRestore() { screenSpace_.restore(); }
