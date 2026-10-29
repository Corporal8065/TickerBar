#include "MainComponent.h"
#include <tickerbar/Format.hpp>
#include <tickerbar/Stale.hpp>
#include <tickerbar/Layout.hpp>
#include <tickerbar/ChartMath.hpp>
#include <tickerbar/WindowGeom.hpp>
#include <BinaryData.h>
#include "SettingsStore.h"
#include "PresetIO.h"
#include "SettingsPalette.h"
#if JUCE_WINDOWS
 #include <windows.h> // native rects for the AppBar sync (physical pixels)
#endif
static const char* kVersion = "v0.3.1";
static const char* kVersionNum = "0.3.1";
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
// MascotView — the Anime-theme mascot cell (v0.3.0, item 18): the
// transparent-cutout character in her own cell at the END of the
// strip, hidden entirely outside the Anime theme. Ported from
// MeterBar's ModuleViews.h; the ARTWORK is MeterBar's own bundled
// files (anime-mascot-calm-clear.png / anime-mascot-excited-clear.png,
// byte-identical copies in this repo's assets/). MainComponent
// drives `excited` and `bobPx` from TickerBar's own triggers: alert
// crossings cheer, watched price ticks hop.
// =====================================================================
class MascotView : public juce::Component {
public:
    bool excited = false;
    float bobPx = 0.f; // <= 0 (up) during a hop
    MascotView() { setInterceptsMouseClicks(false, false); setOpaque(false); }
    void paint(juce::Graphics& g) override {
        ensureLoaded();
        auto& img = excited ? imgExcited : imgCalm;
        if (!img.isValid()) return;
        auto b = getLocalBounds().toFloat();
        float scale = std::min(b.getWidth() / (float)img.getWidth(), b.getHeight() / (float)img.getHeight());
        float w = img.getWidth() * scale, h = img.getHeight() * scale;
        float x = b.getCentreX() - w * 0.5f;
        float y = b.getBottom() - h + bobPx;
        g.setColour(juce::Colour(0x28000000));
        g.fillEllipse(b.getCentreX() - w * 0.30f, b.getBottom() - 6.f, w * 0.60f, 5.f);
        g.drawImage(img, x, y, w, h, 0, 0, img.getWidth(), img.getHeight());
    }
private:
    juce::Image imgCalm, imgExcited; bool loaded = false;
    void ensureLoaded() {
        if (loaded) return; loaded = true;
        imgCalm = juce::ImageFileFormat::loadFrom(BinaryData::animemascotcalmclear_png,
                                                  (size_t)BinaryData::animemascotcalmclear_pngSize);
        imgExcited = juce::ImageFileFormat::loadFrom(BinaryData::animemascotexcitedclear_png,
                                                     (size_t)BinaryData::animemascotexcitedclear_pngSize);
    }
};

// =====================================================================
// CellSplitter — the draggable divider between two cells (v0.3.0,
// item 10, MeterBar's ModuleSplitter pattern): a slim grab strip
// with two grip dots; dragging reports the axis delta to the owner,
// which adjusts the pair's widths through the core
// (Layout.hpp adjustPairWidths) and persists them.
// =====================================================================
class CellSplitter : public juce::Component {
public:
    MainComponent* owner = nullptr;
    int pairIndex = 0;      // divides cells pairIndex and pairIndex+1 (coin cells only)
    bool verticalBar = false; // bar is in vertical orientation: the strip is horizontal, drag is dy
    std::function<void(int, int)> onDragDelta; // (pairIndex, axis delta px)
    CellSplitter() { setRepaintsOnMouseActivity(true); }
    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        auto b = getLocalBounds().toFloat();
        g.setColour(isMouseOver() ? th.accent : th.dim.withAlpha(0.6f));
        const float cx = b.getCentreX(), cy = b.getCentreY();
        if (verticalBar) {
            g.fillEllipse(cx - 8.f, cy - 1.5f, 3.f, 3.f);
            g.fillEllipse(cx + 5.f, cy - 1.5f, 3.f, 3.f);
        } else {
            g.fillEllipse(cx - 1.5f, cy - 8.f, 3.f, 3.f);
            g.fillEllipse(cx - 1.5f, cy + 5.f, 3.f, 3.f);
        }
    }
    void mouseDown(const juce::MouseEvent&) override {
        lastPos_ = verticalBar ? getMouseXYRelative().getY() : getMouseXYRelative().getX();
        setMouseCursor(verticalBar ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        auto rel = e.getPosition();
        const int pos = verticalBar ? rel.getY() : rel.getX();
        const int delta = pos - lastPos_;
        lastPos_ = pos;
        if (delta != 0 && onDragDelta) onDragDelta(pairIndex, delta);
    }
private:
    int lastPos_ = 0;
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

    void mouseDown(const juce::MouseEvent& e) override { owner->cellDragBegin(this, e); }
    void mouseDrag(const juce::MouseEvent& e) override { owner->cellDragMove(this, e); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (owner->cellDragEnd(this, e)) return; // the gesture was a reorder
        if (e.getDistanceFromDragStart() < 5 && e.mouseWasClicked()) owner->openDetail(juce::String(symbol));
    }

    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        auto b = getLocalBounds().toFloat().reduced(1.f);
        const float alpha = stale ? 0.55f : 1.0f;
        g.setColour(th.panel.withAlpha((owner->fxActive() ? 0.55f : 0.92f) * alpha));
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
            g.setColour(owner->gainColour(*pct >= 0).withAlpha(alpha));
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
            g.setColour(owner->gainColour(*pct1h >= 0).withAlpha(alpha));
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
            auto lineCol = owner->gainColour(up).withAlpha(0.9f * alpha);
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
    void mouseDown(const juce::MouseEvent& e) override { owner->cellDragBegin(this, e); }
    void mouseDrag(const juce::MouseEvent& e) override { owner->cellDragMove(this, e); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (owner->cellDragEnd(this, e)) return;
        if (fngLink && e.getDistanceFromDragStart() < 5 && e.mouseWasClicked())
            juce::URL("https://alternative.me/crypto/fear-and-greed-index/").launchInDefaultBrowser();
    }
    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        auto b = getLocalBounds().toFloat().reduced(1.f);
        const float alpha = stale ? 0.55f : 1.0f;
        g.setColour(th.panel.withAlpha((owner->fxActive() ? 0.55f : 0.92f) * alpha));
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
                // v0.3.0 item 16: the 52-bar volume moving average +
                // 3-sigma flags (core ChartMath). The pane scales to
                // whichever is taller, the average or the tallest bar,
                // so the line is always in frame.
                std::vector<double> vols;
                for (auto& c : candles) vols.push_back(c.volume);
                auto vstats = tickerbar::volumeMaSd(vols);
                double vMaxMa = 0.0;
                for (auto& vp : vstats) vMaxMa = std::max(vMaxMa, vp.ma);
                const double vScale = std::max(rangeInfo->maxVolume, vMaxMa);
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
                    if (rangeInfo->hasVolume && vScale > 0) {
                        const float vh = (float)(c.volume / vScale) * volArea.getHeight();
                        const bool hot = i < vstats.size() && vstats[i].hot;
                        // hot bars (volume >= 52-bar average + 3 sigma):
                        // full-strength accent + a cap marker above.
                        g.setColour(hot ? th.accent : col.withAlpha(0.55f));
                        g.fillRect(cx - bodyW / 2.f, volArea.getBottom() - vh, bodyW, vh);
                        if (hot) g.fillRect(cx - bodyW / 2.f, volArea.getBottom() - vh - 3.f, bodyW, 2.f);
                    }
                }
                if (rangeInfo->hasVolume && vScale > 0) {
                    // the 52-bar average line across the volume pane
                    juce::Path maLine;
                    bool started = false;
                    for (size_t i = 0; i < vstats.size(); ++i) {
                        const float cx = priceArea.getX() + step * ((float)i + 0.5f);
                        const float y = volArea.getBottom() - (float)(vstats[i].ma / vScale) * volArea.getHeight();
                        if (!started) { maLine.startNewSubPath(cx, y); started = true; } else maLine.lineTo(cx, y);
                    }
                    g.setColour(th.accent2.withAlpha(0.9f));
                    g.strokePath(maLine, juce::PathStrokeType(1.2f));
                    g.setColour(th.dim); g.setFont(9.f);
                    g.drawText("volume: line = 52-bar average - pink cap = bar >= average + 3 sd",
                               volArea.reduced(4.f, 0.f), juce::Justification::topLeft);
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
    void paint(juce::Graphics& g) override { g.fillAll(settingsPalette::base()); } // v0.3.0: the fixed pink-plum sheet
    void build() {
        removeAllChildren(); owned.clear(); y_ = 8;
        if (!owner || !st) return;
        th_ = owner->theme();
        panel("Watchlist", [&]{
            auto names = owner->universeNames();
            for (auto& sym : st->watchlist) {
                auto* l = add(new juce::Label({}, juce::String(sym)));
                l->setFont(juce::Font(13.5f, juce::Font::bold)); l->setColour(juce::Label::textColourId, settingsPalette::text());
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
            ed->setColour(juce::TextEditor::backgroundColourId, settingsPalette::well());
            ed->setColour(juce::TextEditor::textColourId, settingsPalette::text());
            ed->setColour(juce::TextEditor::outlineColourId, settingsPalette::border());
            ed->setBounds(24, y_, 130, 24);
            auto* addBtn = add(new juce::TextButton("Add symbol"));
            addBtn->setBounds(162, y_, 110, 24);
            auto* err = add(new juce::Label({}, ""));
            err->setFont(11.f); err->setColour(juce::Label::textColourId, juce::Colour(0xfffad927));
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
        panel("Context cells", [&]{
            toggle("Show Fear & Greed cell", st->showFearGreed, [&](bool b){ st->showFearGreed = b; });
            toggle("Show BTC dominance cell", st->showBtcDominance, [&](bool b){ st->showBtcDominance = b; });
            note("The two market-context cells at the end of the bar (Vishona's request). Drag the divider beside a context cell to change its width - the width is remembered, like the coin cells.");
        });
        panel("Alerts", [&]{
            toggle("Soft click sound on a cross (default off)", st->alertSound, [&](bool b){ st->alertSound = b; });
            toggle("Windows toast notification on a cross", st->alertToasts, [&](bool b){ st->alertToasts = b; });
            note("Levels are also editable by clicking a coin on the bar. A cross fires once, then re-arms when price returns.");
            for (auto& sym : st->watchlist) {
                auto lv = owner->levelsFor(sym);
                auto* l = add(new juce::Label({}, juce::String(sym)));
                l->setFont(juce::Font(12.5f, juce::Font::bold)); l->setColour(juce::Label::textColourId, settingsPalette::text());
                l->setBounds(24, y_, 56, 22);
                auto* lab = add(new juce::Label({}, "Above")); lab->setFont(11.f); lab->setColour(juce::Label::textColourId, settingsPalette::dimText()); lab->setBounds(84, y_, 40, 22);
                auto* ea = add(new juce::TextEditor());
                auto* lb = add(new juce::Label({}, "Below")); lb->setFont(11.f); lb->setColour(juce::Label::textColourId, settingsPalette::dimText()); lb->setBounds(240, y_, 40, 22);
                auto* eb = add(new juce::TextEditor());
                for (auto* e : { ea, eb }) {
                    e->setColour(juce::TextEditor::backgroundColourId, settingsPalette::well());
                    e->setColour(juce::TextEditor::textColourId, settingsPalette::text());
                    e->setColour(juce::TextEditor::outlineColourId, settingsPalette::border());
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
            combo("Theme", {"Midnight Kids", "Midnight Pink", "Deep Teal", "Daylight", "Anime", "Clean Dark"},
                  juce::StringArray(themeNameList()).indexOf(juce::String(st->themeName)),
                  [&](int v){
                      auto names = themeNameList();
                      st->themeName = names[juce::jlimit(0, names.size() - 1, v)].toStdString();
                      if (st->themeName == "Anime") st->backgroundStyle = tickerbar::BackgroundStyle::Pastel; // MeterBar's pairing
                  });
            combo("Background style", {"Clean Dark", "Midnight Kids", "Flat", "Pastel"}, (int)st->backgroundStyle,
                  [&](int v){ st->backgroundStyle = (tickerbar::BackgroundStyle)v; });
            slider("Background opacity % (15-100)", 15.0, 100.0, st->backgroundOpacity * 100.0,
                   [&](double v){ st->backgroundOpacity = (float)(v / 100.0); });
            combo("Data colour scheme", {"Family", "Plasma", "Inferno", "Viridis", "Magma"}, st->dataColorScheme,
                  [&](int v){ st->dataColorScheme = v; });
            note("Data colours drive the gain/loss text and the sparklines. Family = the theme's own green/red; the others are pairs of stops from MeterBar's colour maps.");
            combo("Background effect", {"Styled background", "Visualizer particles", "Reactive glow"}, st->bgEffect,
                  [&](int v){ st->bgEffect = v; });
            note("Visualizer: MeterBar's particle brushes (transient-fired emitters shedding strands of dots). Reactive glow: the MiniMeters-style blurred bloom - linked to MeterBar it pulses with the music; on its own it breathes gently. Both render UNDER the cells.");
            slider("Effect palette hue", 0.0, 1.0, st->vizHue, [&](double v){ st->vizHue = (float)v; });
            slider("Effect palette saturation", 0.0, 1.0, st->vizSat, [&](double v){ st->vizSat = (float)v; });
            slider("Effect palette spread", 0.0, 1.0, st->vizSpread, [&](double v){ st->vizSpread = (float)v; });
            slider("Effect cycle seconds (0 = static)", 0.0, 60.0, st->vizCycle, [&](double v){ st->vizCycle = (float)v; });
        });
        panel("Sound", [&]{
            toggle("UI click sounds on the bar's buttons", st->clickSounds, [&](bool b){ st->clickSounds = b; });
            note("MeterBar's synthesized clicks (pop / bloop / sparkle), played on button presses. Default off. The price-alert click above is separate.");
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
            button("Stack all linked bars as one block", [this]{ owner->doStackAll(); });
            note("Stack is the one-click form of Link + Dock: it turns Link on, snaps this bar to its dock edge, and every other linked bar lines up flush against it. The Stack button on the bar does the same.");
            note("Arrange slides every other (non-maximised) window on the bar's monitor clear of the bar; Restore puts them back. Screen space: " + owner->screenSpaceStatus());
        });
        panel("About", [&]{
            button("Check for updates", [this]{ owner->checkForUpdates(); });
            note("TickerBar v0.3.1 - prices are SPOT prices: Coinbase Exchange public API for Coinbase-listed coins, CoinGecko for HYPE. No account, no keys, read-only.");
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
        hdr->setColour(juce::Label::textColourId, settingsPalette::accent());
        hdr->setBounds(12, y_, 300, 22); y_ += 24;
        body(); y_ += 10;
    }
    void combo(const juce::String& label, std::vector<juce::String> items, int sel, std::function<void(int)> fn) {
        auto* l = add(new juce::Label({}, label)); l->setBounds(24, y_, 200, 22);
        l->setColour(juce::Label::textColourId, settingsPalette::text());
        auto* c = add(new juce::ComboBox()); int id = 1; for (auto& s : items) c->addItem(s, id++);
        c->setSelectedItemIndex(juce::jlimit(0, (int)items.size() - 1, sel), juce::dontSendNotification);
        c->setBounds(230, y_, 260, 22);
        c->onChange = [this, c, fn]{ fn(c->getSelectedItemIndex()); changed(); };
        y_ += 26;
    }
    void slider(const juce::String& label, double mn, double mx, double val, std::function<void(double)> fn) {
        auto* l = add(new juce::Label({}, label)); l->setBounds(24, y_, 200, 22);
        l->setColour(juce::Label::textColourId, settingsPalette::text());
        auto* s = add(new juce::Slider()); s->setRange(mn, mx, 0.01); s->setValue(val, juce::dontSendNotification);
        s->setBounds(230, y_, 300, 22);
        s->onValueChange = [this, s, fn]{ fn(s->getValue()); changed(); };
        y_ += 26;
    }
    void toggle(const juce::String& label, bool val, std::function<void(bool)> fn) {
        auto* t = add(new juce::ToggleButton(label)); t->setToggleState(val, juce::dontSendNotification);
        t->setColour(juce::ToggleButton::textColourId, settingsPalette::text());
        t->setBounds(24, y_, 460, 22);
        t->onClick = [this, t, fn]{ fn(t->getToggleState()); changed(); };
        y_ += 26;
    }
    void button(const juce::String& a, std::function<void()> fa) {
        auto* ba = add(new juce::TextButton(a)); ba->setBounds(24, y_, 300, 22); ba->onClick = fa;
        y_ += 26;
    }
    void buttonRow(const juce::String& a, const juce::String& b, std::function<void()> fa, std::function<void()> fb) {
        auto* ba = add(new juce::TextButton(a)); ba->setBounds(24, y_, 260, 22); ba->onClick = fa;
        auto* bb = add(new juce::TextButton(b)); bb->setBounds(296, y_, 260, 22); bb->onClick = fb;
        y_ += 26;
    }
    void note(const juce::String& text) {
        auto* l = add(new juce::Label({}, text)); l->setFont(11.f);
        l->setColour(juce::Label::textColourId, settingsPalette::dimText());
        l->setBounds(24, y_, 660, 16); y_ += 18;
    }
};
class TickerSettingsWindow : public juce::DocumentWindow {
public:
    SettingsLookAndFeel settingsLnf; // declared before content so it outlives it
    SettingsContent content;
    TickerSettingsWindow() : juce::DocumentWindow("TickerBar Settings", settingsPalette::base(), juce::DocumentWindow::closeButton) {
        content.setLookAndFeel(&settingsLnf);
        viewport.setLookAndFeel(&settingsLnf);
        content.build();
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        setContentNonOwned(&viewport, true);
        setSize(740, 660); setResizable(true, false); setUsingNativeTitleBar(true);
    }
    ~TickerSettingsWindow() override {
        content.setLookAndFeel(nullptr);
        viewport.setLookAndFeel(nullptr);
    }
    void visibilityChanged() override {
        if (isVisible()) settingsPalette::applyDarkTitleBar(this);
        juce::DocumentWindow::visibilityChanged();
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
    // v0.3.0 migration: the new top toolbar strip needs ~30 px the
    // pre-0.3.0 bar didn't have — grow the saved height ONCE (uiVer).
    if (state.uiVer < 3) {
        state.windowH = juce::jlimit(52, 1200, state.windowH + 30);
        state.uiVer = 3;
    }
    theme_ = themeByName(state.themeName);
    setOpaque(false); // the window is non-opaque too: Background Opacity shows the desktop through
    setTooltip("TickerBar - prices are spot prices (Coinbase Exchange / CoinGecko)");

    wordmark_.setText("TickerBar", juce::dontSendNotification);
    wordmark_.setFont(juce::Font(15.f, juce::Font::bold));
    versionLabel_.setText(kVersion, juce::dontSendNotification);
    versionLabel_.setFont(10.f);
    statusLabel_.setFont(10.5f);
    statusLabel_.setText("connecting...", juce::dontSendNotification);
    for (auto* c : { (juce::Component*)&wordmark_, (juce::Component*)&versionLabel_, (juce::Component*)&statusLabel_ }) {
        addAndMakeVisible(c);
        c->setInterceptsMouseClicks(false, false); // clicks fall through to the bar drag
    }
    reserveBtn_.setClickingTogglesState(true);
    reserveBtn_.setToggleState(state.reserveScreenSpace, juce::dontSendNotification);
    reserveBtn_.setTooltip("Reserve screen space: Windows keeps maximised windows clear of the docked bar");
    reserveBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop); state.reserveScreenSpace = reserveBtn_.getToggleState(); saveSettings(); syncScreenSpace(); };
    linkBtn_.setClickingTogglesState(true);
    linkBtn_.setToggleState(state.linkEnabled, juce::dontSendNotification);
    linkBtn_.setTooltip("Link: snap, drag, dock and share the background with the other family bars (MeterBar / SessionBar) when their Link is on too");
    linkBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop); state.linkEnabled = linkBtn_.getToggleState(); saveSettings(); };
    arrangeBtn_.setTooltip("Arrange: slide every other window on this monitor clear of the bar");
    arrangeBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop); doArrange(); };
    restoreBtn_.setTooltip("Restore windows moved by Arrange");
    restoreBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop); doRestore(); };
    presetBtn_.setTooltip("Presets: named full configurations - appearance, watchlist AND window position");
    presetBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop); showPresetMenu(); };
    stackBtn_.setTooltip("Stack: turn Link on, snap this bar to its dock edge, and line every linked bar up flush against it as one block");
    stackBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Sparkle); doStackAll(); };
    dockBtn_.setButtonText(dockEdgeName(state.dockEdge));
    dockBtn_.setTooltip("Dock edge: click to cycle Top / Bottom / Left / Right - the bar snaps to that edge");
    dockBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop); cycleDockEdge(); };
    settingsBtn_.onClick = [this]{
        uiClick(ClickSounds::Which::Pop);
        settingsWin_->content.build();
        settingsWin_->setVisible(true); settingsWin_->toFront(true);
    };
    // v0.3.0 item 1: the update indicator starts hidden and lights
    // only when a check finds a newer release (MeterBar's pattern).
    updateBtn_.setVisible(false);
    updateBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop);
        if (pendingRelease_.ok && pendingRelease_.newer) showUpdatePanel(pendingRelease_); };
    minBtn_.onClick = [this]{ if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                                 if (auto* peer = w->getPeer()) peer->setMinimised(true); };
    quitBtn_.onClick = []{ juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
    for (auto* b : { &reserveBtn_, &linkBtn_, &arrangeBtn_, &restoreBtn_, &presetBtn_, &stackBtn_,
                     &dockBtn_, &settingsBtn_, &updateBtn_, &minBtn_, &quitBtn_ }) addAndMakeVisible(b);
    applyChrome();
    // v0.2.0 (k): family link wiring. Bounds are the WINDOW's (the bar
    // is the window); the background packet is the family style
    // ordinal + opacity; the dock ordinal is the reserve machine's.
    linkClient_.getBounds = [this]{ return currentWindowBounds(); };
    linkClient_.setBounds = [this](juce::Rectangle<int> r){
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setBounds(r);
    };
    linkClient_.getBg = [this]{ return BarLinkClient::Bg{ (int)state.backgroundStyle, state.backgroundOpacity }; };
    linkClient_.applyBg = [this](const BarLinkClient::Bg& bg){
        state.backgroundStyle = (tickerbar::BackgroundStyle)juce::jlimit(0, 3, bg.style);
        state.backgroundOpacity = juce::jlimit(0.05f, 1.0f, bg.opacity);
        saveSettings(); repaint();
    };
    linkClient_.getDockedEdge = [this]{
        return reserveMachine_.reserved() ? (int)reserveMachine_.edge() : -1;
    };
    linkClient_.isLinked = [this]{ return state.linkEnabled; };
    linkClient_.start();

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

    // Post-v0.3.0: dividers for the context cells — A between the
    // coin block and the nearest visible context cell, B between
    // Fear & Greed and BTC dominance. Same CellSplitter +
    // adjustPairWidths machinery as the coin dividers; resized()
    // places them and drives visibility from the show* toggles.
    ctxSplitA_ = std::make_unique<CellSplitter>();
    ctxSplitA_->owner = this;
    ctxSplitA_->onDragDelta = [this](int, int delta) { onContextSplitterDrag(true, delta); };
    addChildComponent(ctxSplitA_.get());
    ctxSplitB_ = std::make_unique<CellSplitter>();
    ctxSplitB_->owner = this;
    ctxSplitB_->onDragDelta = [this](int, int delta) { onContextSplitterDrag(false, delta); };
    addChildComponent(ctxSplitB_.get());

    mascot_ = std::make_unique<MascotView>();
    addChildComponent(mascot_.get()); // Anime theme only (applyTheme drives visibility)
    fxPump_.onTick = [this]{ stepFx(); };

    applyTheme();
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
    syncFxPump();
    startTimerHz(4);
    setSize(state.windowW, state.windowH);
    // v0.3.0 item 1: silent background update check on launch.
    if (tbupdate::configured()) startUpdateCheck(true);
}

MainComponent::~MainComponent() {
    stopTimer();
    saveSettings();
    screenSpace_.release();
    contextClient_.stopThread(4500);
    client_.stopThread(4500);
}

void MainComponent::paint(juce::Graphics& g) {
    if (state.bgEffect == 1) drawVisualizerBackground(g);
    else if (state.bgEffect == 2) drawGlowBackground(g);
    else fillStyledBackground(g, getLocalBounds().toFloat(), state.backgroundStyle, theme_,
                              juce::jlimit(0.15f, 1.0f, state.backgroundOpacity));
    g.setColour(theme_.border);
    g.drawRect(getLocalBounds().toFloat(), 1.f);
    // status dot (position set by resized: the toolbar strip, or the
    // wordmark row in vertical mode) — colour by feed state
    auto c = feed_ == tickerbar::FeedStatus::Live ? theme_.good
           : feed_ == tickerbar::FeedStatus::Stale ? theme_.warn : theme_.bad;
    g.setColour(c.withAlpha(0.25f)); g.fillEllipse(dotRect_.expanded(2.f));
    g.setColour(c); g.fillEllipse(dotRect_);
}

void MainComponent::resized() {
    auto area = getLocalBounds();
    const bool sideDock = state.dockEdge == tickerbar::DockEdge::Left || state.dockEdge == tickerbar::DockEdge::Right;
    vertical_ = sideDock && area.getHeight() > area.getWidth();
    // The control cluster, in MeterBar's order (item 11).
    struct BtnW { juce::TextButton* b; int w; };
    const BtnW cluster[] = {
        { &reserveBtn_, 76 }, { &linkBtn_, 52 }, { &arrangeBtn_, 76 }, { &restoreBtn_, 76 },
        { &presetBtn_, 64 }, { &stackBtn_, 56 }, { &dockBtn_, 62 }, { &settingsBtn_, 72 },
        { &updateBtn_, 68 }, { &minBtn_, 26 }, { &quitBtn_, 46 },
    };
    auto btnVisible = [this](juce::TextButton* b) { return b != &updateBtn_ || updateAvailable_; };
    updateBtn_.setVisible(updateAvailable_);
    auto r = area.reduced(8, 6);
    const int n = (int)cells_.size();
    const bool anime = state.themeName == "Anime";
    // Context-cell visibility (post-v0.3.0 toggles): hidden cells
    // and their dividers take no space in either orientation.
    const bool showF = state.showFearGreed && fngCell_ != nullptr;
    const bool showD = state.showBtcDominance && domCell_ != nullptr;
    if (fngCell_) fngCell_->setVisible(showF);
    if (domCell_) domCell_->setVisible(showD);
    if (ctxSplitA_) ctxSplitA_->setVisible((showF || showD) && n > 0);
    if (ctxSplitB_) ctxSplitB_->setVisible(showF && showD);
    if (!vertical_) {
        // ---- horizontal: 30 px toolbar strip across the top ----
        auto strip = r.removeFromTop(30);
        dotRect_ = { (float)strip.getX() + 6.f, (float)strip.getY() + 10.f, 10.f, 10.f };
        int clusterW = 0;
        for (auto& bw : cluster) if (btnVisible(bw.b)) clusterW += bw.w + 4;
        auto cl = strip.removeFromRight(clusterW);
        for (int i = (int)std::size(cluster) - 1; i >= 0; --i) {
            if (!btnVisible(cluster[i].b)) continue;
            auto cellRect = cl.removeFromRight(cluster[i].w);
            cluster[i].b->setBounds(cellRect.withSizeKeepingCentre(cluster[i].w, 24));
            cl.removeFromRight(4);
        }
        strip.removeFromLeft(24); // the status dot's space
        wordmark_.setBounds(strip.removeFromLeft(92).withSizeKeepingCentre(92, 20));
        versionLabel_.setBounds(strip.removeFromLeft(44).withSizeKeepingCentre(44, 14));
        statusLabel_.setBounds(strip.withSizeKeepingCentre(strip.getWidth(), 14));
        r.removeFromTop(4);
        // ---- cells row ----
        if (anime && mascot_) { mascot_->setBounds(r.removeFromRight(72)); r.removeFromRight(6); }
        // Context cells at the right end (DOM outermost, then
        // FNG), each visible one at its effective width with a
        // draggable divider on its inner side — the coin-divider
        // idiom (16 px boundary: 3 air + 10 grip + 3 air), placed
        // right-to-left. A divides coins from the nearest context
        // cell; B divides FNG from DOM.
        auto ctxBoundary = [&](CellSplitter* sp) {
            r.removeFromRight(3);
            sp->verticalBar = false;
            auto sb = r.removeFromRight(10);
            sp->setBounds(sb.getX(), r.getY(), 10, r.getHeight());
            r.removeFromRight(3);
        };
        if (showD) {
            domCell_->setBounds(r.removeFromRight(tickerbar::contextCellExtent(state.domCellWidth, false)));
            if (showF) ctxBoundary(ctxSplitB_.get());
            else if (n > 0) ctxBoundary(ctxSplitA_.get());
        }
        if (showF) {
            fngCell_->setBounds(r.removeFromRight(tickerbar::contextCellExtent(state.fngCellWidth, false)));
            if (n > 0) ctxBoundary(ctxSplitA_.get());
        }
        if (n > 0) {
            const int boundary = 16; // 10 px splitter + 6 px air
            std::vector<int> expl((size_t)n, 0);
            for (int i = 0; i < n; ++i) {
                auto it = state.cellWidths.find(cells_[(size_t)i]->symbol);
                if (it != state.cellWidths.end()) expl[(size_t)i] = it->second;
            }
            auto widths = tickerbar::solveCellWidths(r.getWidth(), boundary, expl, 90);
            int x = r.getX();
            for (int i = 0; i < n; ++i) {
                cells_[(size_t)i]->setBounds(x, r.getY(), widths[(size_t)i], r.getHeight());
                x += widths[(size_t)i];
                if (i + 1 < n && i < (int)splitters_.size()) {
                    splitters_[(size_t)i]->verticalBar = false;
                    splitters_[(size_t)i]->setBounds(x + 3, r.getY(), 10, r.getHeight());
                }
                x += boundary;
            }
        }
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setResizeLimits(minimumBarWidth(), 92, 3840, 1200);
    } else {
        // ---- vertical: wordmark row, cluster grid, cells stacked ----
        auto wm = r.removeFromTop(24);
        dotRect_ = { (float)wm.getX() + 6.f, (float)wm.getY() + 7.f, 10.f, 10.f };
        wm.removeFromLeft(24);
        wordmark_.setBounds(wm.removeFromLeft(92).withSizeKeepingCentre(92, 20));
        versionLabel_.setBounds(wm.removeFromLeft(44).withSizeKeepingCentre(44, 14));
        statusLabel_.setBounds(wm.withSizeKeepingCentre(wm.getWidth(), 14));
        r.removeFromTop(4);
        int bx = r.getX(), by = r.getY();
        for (auto& bw : cluster) {
            if (!btnVisible(bw.b)) continue;
            if (bx + bw.w > r.getRight()) { bx = r.getX(); by += 28; }
            bw.b->setBounds(bx, by, bw.w, 24);
            bx += bw.w + 4;
        }
        r.removeFromTop(by - r.getY() + 28);
        // cells stacked full-width; persisted widths become heights
        const int boundary = 16;
        auto placeRow = [&](juce::Component* c, int h) {
            c->setBounds(r.getX(), r.getY(), r.getWidth(), h);
            r.removeFromTop(h);
        };
        // Each visible context row reserves its divider boundary
        // (16) + its effective height + the 6 px trailing gap from
        // the coin solver's share of the stack.
        const int fngH = tickerbar::contextCellExtent(state.fngCellWidth, true);
        const int domH = tickerbar::contextCellExtent(state.domCellWidth, true);
        const int ctxReserve = (showF ? 16 + fngH + 6 : 0) + (showD ? 16 + domH + 6 : 0);
        if (n > 0) {
            std::vector<int> expl((size_t)n, 0);
            for (int i = 0; i < n; ++i) {
                auto it = state.cellWidths.find(cells_[(size_t)i]->symbol);
                if (it != state.cellWidths.end()) expl[(size_t)i] = it->second;
            }
            auto heights = tickerbar::solveCellWidths(r.getHeight() - ctxReserve, boundary, expl, 56);
            for (int i = 0; i < n; ++i) {
                placeRow(cells_[(size_t)i].get(), heights[(size_t)i]);
                if (i + 1 < n && i < (int)splitters_.size()) {
                    splitters_[(size_t)i]->verticalBar = true;
                    splitters_[(size_t)i]->setBounds(r.getX(), r.getY() + 3, r.getWidth(), 10);
                }
                r.removeFromTop(boundary);
            }
        }
        // Context rows under the coins, each visible one preceded
        // by its divider (dragging it adjusts row heights — the
        // stored width doubles as height, like the coin cells).
        auto placeCtxRow = [&](ContextCell* cell, int h, CellSplitter* sp) {
            sp->verticalBar = true;
            sp->setBounds(r.getX(), r.getY() + 3, r.getWidth(), 10);
            r.removeFromTop(16);
            placeRow(cell, h);
            r.removeFromTop(6);
        };
        if (showF) placeCtxRow(fngCell_.get(), fngH, ctxSplitA_.get());
        if (showD) placeCtxRow(domCell_.get(), domH, showF ? ctxSplitB_.get() : ctxSplitA_.get());
        if (anime && mascot_) mascot_->setBounds(r.removeFromTop(96));
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setResizeLimits(200, 340, 1200, 3840);
    }
    syncScreenSpace(); // v0.2.0: a resize is a reserve-machine event
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
        cell->setTooltip(juce::String(sym) + " - click for detail, drag sideways to reorder. Prices: spot (Coinbase / CoinGecko).");
        addAndMakeVisible(cell.get());
        cells_.push_back(std::move(cell));
    }
    rebuildSplitters();
    resized();
}

void MainComponent::rebuildSplitters() {
    splitters_.clear();
    for (int i = 0; i + 1 < (int)cells_.size(); ++i) {
        auto sp = std::make_unique<CellSplitter>();
        sp->owner = this;
        sp->pairIndex = i;
        sp->onDragDelta = [this](int pair, int delta) {
            if (pair < 0 || pair + 1 >= (int)cells_.size()) return;
            const int w1 = vertical_ ? cells_[(size_t)pair]->getHeight() : cells_[(size_t)pair]->getWidth();
            const int w2 = vertical_ ? cells_[(size_t)pair + 1]->getHeight() : cells_[(size_t)pair + 1]->getWidth();
            auto pr = tickerbar::adjustPairWidths(w1, w2, delta, vertical_ ? 56 : 90);
            state.cellWidths[cells_[(size_t)pair]->symbol] = pr.first;
            state.cellWidths[cells_[(size_t)pair + 1]->symbol] = pr.second;
            saveSettings();
            resized();
        };
        addAndMakeVisible(sp.get());
        splitters_.push_back(std::move(sp));
    }
}

// --- context-cell dividers (post-v0.3.0, Vishona's request). A
// pairs the LAST coin cell with the nearest visible context cell
// (Fear & Greed, or BTC dominance when Fear & Greed is hidden); B
// pairs Fear & Greed with BTC dominance. Same pair math as the
// coin dividers: the coin side persists into cellWidths, the
// context side into the cell's own width field; both reflow via
// resized(). The axis extent doubles as height in vertical mode,
// exactly like the coin widths. ---
void MainComponent::onContextSplitterDrag(bool coinsPair, int delta) {
    if (coinsPair) {
        const bool useFng = state.showFearGreed;
        if (cells_.empty() || (!useFng && !state.showBtcDominance)) return;
        auto* coin = cells_.back().get();
        auto* ctx = useFng ? (juce::Component*)fngCell_.get() : (juce::Component*)domCell_.get();
        const int w1 = vertical_ ? coin->getHeight() : coin->getWidth();
        const int w2 = vertical_ ? ctx->getHeight() : ctx->getWidth();
        auto pr = tickerbar::adjustPairWidths(w1, w2, delta, tickerbar::kContextCellMin);
        const int coinMin = vertical_ ? 56 : 90; // the coin solver's floors, mirrored
        if (pr.first < coinMin) { // the coin keeps its floor; the context cell absorbs the difference
            pr.second -= coinMin - pr.first;
            pr.first = coinMin;
            if (pr.second < tickerbar::kContextCellMin) pr.second = tickerbar::kContextCellMin;
        }
        state.cellWidths[coin->symbol] = pr.first;
        (useFng ? state.fngCellWidth : state.domCellWidth) = pr.second;
    } else {
        if (!state.showFearGreed || !state.showBtcDominance) return;
        const int w1 = vertical_ ? fngCell_->getHeight() : fngCell_->getWidth();
        const int w2 = vertical_ ? domCell_->getHeight() : domCell_->getWidth();
        auto pr = tickerbar::adjustPairWidths(w1, w2, delta, tickerbar::kContextCellMin);
        state.fngCellWidth = pr.first;
        state.domCellWidth = pr.second;
    }
    saveSettings();
    resized();
}

// --- cell drag arbitration (v0.3.0 item 9): the press decides by
// direction — mostly-along-the-bar past a threshold becomes a cell
// reorder; anything else becomes the ordinary window drag (started
// at the decision point, so there is no jump, just a few px of dead
// zone). Context cells never reorder (they are not in cells_). ---
void MainComponent::cellDragBegin(juce::Component* cell, const juce::MouseEvent& e) {
    dragCell_ = cell;
    dragMode_ = 0;
    dragStartScreen_ = e.getScreenPosition();
    dragCellStartBounds_ = cell->getBounds();
}
void MainComponent::cellDragMove(juce::Component* cell, const juce::MouseEvent& e) {
    if (cell != dragCell_) return;
    const auto now = e.getScreenPosition();
    const int dx = now.x - dragStartScreen_.x, dy = now.y - dragStartScreen_.y;
    const int axis = vertical_ ? dy : dx, cross = vertical_ ? dx : dy;
    bool isCoin = false;
    for (auto& c : cells_) if (c.get() == cell) isCoin = true;
    if (dragMode_ == 0) {
        if (isCoin && std::abs(axis) > 14 && std::abs(axis) > 1.6 * std::abs(cross)) {
            dragMode_ = 2;
            cell->toFront(false);
        } else if (std::abs(dx) + std::abs(dy) > 6) {
            dragMode_ = 1;
            windowDragStarted(e);
            windowDragged(e);
            return;
        } else return;
    }
    if (dragMode_ == 1) { windowDragged(e); return; }
    auto b = dragCellStartBounds_;
    if (vertical_) b.setY(b.getY() + dy); else b.setX(b.getX() + dx);
    cell->setBounds(b);
}
bool MainComponent::cellDragEnd(juce::Component* cell, const juce::MouseEvent&) {
    const bool wasReorder = dragMode_ == 2 && cell == dragCell_;
    dragCell_ = nullptr;
    const int mode = dragMode_;
    dragMode_ = 0;
    if (!wasReorder) return false;
    int from = -1;
    for (int i = 0; i < (int)cells_.size(); ++i) if (cells_[(size_t)i].get() == cell) from = i;
    if (from < 0) { resized(); return true; }
    std::vector<int> centres;
    for (int i = 0; i < (int)cells_.size(); ++i) {
        if (i == from) continue;
        auto b = cells_[(size_t)i]->getBounds();
        centres.push_back(vertical_ ? b.getCentreY() : b.getCentreX());
    }
    auto db = cell->getBounds();
    const int slot = tickerbar::dropSlot(centres, vertical_ ? db.getCentreY() : db.getCentreX());
    auto order = tickerbar::indexOrderAfterMove((int)cells_.size(), from, slot);
    std::vector<std::unique_ptr<CoinCell>> newCells(cells_.size());
    std::vector<std::string> newWatch;
    for (int i = 0; i < (int)order.size(); ++i) {
        newCells[(size_t)i] = std::move(cells_[(size_t)order[(size_t)i]]);
        newWatch.push_back(newCells[(size_t)i]->symbol);
    }
    cells_ = std::move(newCells);
    if (newWatch != state.watchlist) {
        state.watchlist = newWatch;
        client_.setWatchlist(state.watchlist);
        saveSettings();
    }
    rebuildSplitters();
    resized();
    return true;
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
                mascotAnim_.triggerExcited(juce::Time::currentTimeMillis(), 2000); // item 18: a cross cheers
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
    // item 18: a watched price actually moving hops the mascot.
    bool ticked = false;
    for (auto& cell : cells_) {
        if (const auto* ctx = snap.find(cell->symbol)) {
            auto it = lastPrices_.find(cell->symbol);
            if (it != lastPrices_.end() && std::abs(it->second - ctx->price) > 1e-12) ticked = true;
            lastPrices_[cell->symbol] = ctx->price;
        }
    }
    if (ticked) mascotAnim_.triggerBob(juce::Time::currentTimeMillis());
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
    pollUpdater();
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
    applyTheme();
    linkClient_.notifyLocalSettings();
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setAlwaysOnTop(state.pinned);
    client_.setPollSec(state.pollSec);
    if (watchlistToo) {
        client_.setWatchlist(state.watchlist);
        rebuildCells();
        rebuildSettingsDeferred();
    }
    if (reserveBtn_.getToggleState() != state.reserveScreenSpace)
        reserveBtn_.setToggleState(state.reserveScreenSpace, juce::dontSendNotification);
    if (linkBtn_.getToggleState() != state.linkEnabled)
        linkBtn_.setToggleState(state.linkEnabled, juce::dontSendNotification);
    dockBtn_.setButtonText(dockEdgeName(state.dockEdge));
    saveSettings();
    syncScreenSpace();
    syncFxPump();
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
    if (!state.watchlist.empty()) cellsW += 16 * ((int)state.watchlist.size() - 1); // splitter boundaries
    // Context cells (post-v0.3.0): each VISIBLE one contributes
    // its effective width plus its 16 px divider boundary; a
    // hidden cell contributes nothing — the floor shrinks with it.
    int ctxW = 0;
    if (state.showFearGreed) ctxW += tickerbar::contextCellExtent(state.fngCellWidth, false) + 16;
    if (state.showBtcDominance) ctxW += tickerbar::contextCellExtent(state.domCellWidth, false) + 16;
    const int chrome = 16 /*outer pad*/ + 24 /*status dot*/ + 92 + 44 + 70 /*wordmark + version + status*/
                     + 76 + 52 + 76 + 76 + 64 + 56 + 62 + 72 + 68 + 26 + 46 + 44 /*the v0.3.0 cluster + its gaps*/
                     + ctxW;
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

// =====================================================================
// v0.3.0: theme application + chrome
// =====================================================================
void MainComponent::applyTheme() {
    theme_ = themeByName(state.themeName);
    applyChrome();
    if (mascot_) mascot_->setVisible(state.themeName == "Anime");
    for (auto& c : cells_) c->repaint();
    if (fngCell_) fngCell_->repaint();
    if (domCell_) domCell_->repaint();
    resized(); // the mascot cell (Anime) and cell softening depend on the theme
    repaint();
}
void MainComponent::applyChrome() {
    wordmark_.setColour(juce::Label::textColourId, theme_.accent);
    versionLabel_.setColour(juce::Label::textColourId, theme_.dim);
    statusLabel_.setColour(juce::Label::textColourId, theme_.dim);
    for (auto* b : { &reserveBtn_, &linkBtn_, &arrangeBtn_, &restoreBtn_, &presetBtn_, &stackBtn_,
                     &dockBtn_, &settingsBtn_, &minBtn_, &quitBtn_ }) {
        b->setColour(juce::TextButton::buttonColourId, theme_.panel);
        b->setColour(juce::TextButton::buttonOnColourId, theme_.accent);
        b->setColour(juce::TextButton::textColourOffId, theme_.text);
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }
    updateBtn_.setColour(juce::TextButton::buttonColourId, theme_.accent);
    updateBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    updateBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}
void MainComponent::uiClick(ClickSounds::Which w) {
    if (state.clickSounds) clicks_.play(w); // the toggle is the gate (item 5)
}
juce::Colour MainComponent::gainColour(bool up) const {
    if (state.dataColorScheme == 0) return up ? theme_.good : theme_.bad;
    auto pr = tickerbar::dataColorPair(state.dataColorScheme);
    return dataRgb(up ? pr.up : pr.down);
}

// =====================================================================
// v0.3.0 item 12: one-click stack. Link on (if it wasn't), snap to
// the dock edge, then stamp the request into the shared block; the
// other bars' clients run the core planner and move themselves.
// =====================================================================
void MainComponent::doStackAll() {
    if (!state.linkEnabled) {
        state.linkEnabled = true;
        linkBtn_.setToggleState(true, juce::dontSendNotification);
        saveSettings();
    }
    snapToDockEdge();
    linkClient_.requestStackAll((int)state.dockEdge, currentWindowBounds());
}

// =====================================================================
// v0.3.0 item 2: named presets (MeterBar's menu pattern). A preset
// is a FULL configuration — window position included — and loading
// one moves the bar (clamped on-screen via the core guard).
// =====================================================================
void MainComponent::showPresetMenu() {
    enum { idNew = 1, idSave, idSaveAs, idOpenFolder, idLoadBase = 100 };
    juce::PopupMenu m;
    m.addItem(idNew, "New Preset (defaults)");
    m.addItem(idSave, "Save", lastPresetName_.isNotEmpty());
    m.addItem(idSaveAs, "Save As...");
    m.addItem(idOpenFolder, "Open Preset Folder");
    m.addSeparator();
    auto files = presetio::listPresets();
    for (int i = 0; i < files.size(); ++i) m.addItem(idLoadBase + i, files[i].getFileNameWithoutExtension());
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetBtn_), [this, files](int r) {
        if (r == 1) {
            auto keepTheme = state.themeName;
            state = tickerbar::TickerState{};
            state.themeName = keepTheme;
            lastPresetName_.clear();
            settingsChanged(true);
        }
        else if (r == 2) savePresetInteractive(false);
        else if (r == 3) savePresetInteractive(true);
        else if (r == 4) presetio::presetFolder().revealToUser();
        else if (r >= 100 && r < 100 + files.size()) loadPresetFile(files[r - 100]);
    });
}
void MainComponent::savePresetInteractive(bool saveAs) {
    saveSettings(); // refresh the live window rect into state first — a preset includes position
    if (!saveAs && lastPresetName_.isNotEmpty()) { presetio::save(state, lastPresetName_); return; }
    auto w = std::make_shared<juce::AlertWindow>("Save Preset", "Preset name:", juce::AlertWindow::NoIcon);
    w->addTextEditor("name", lastPresetName_.isEmpty() ? juce::String("My Preset") : lastPresetName_);
    w->addButton("Save", 1); w->addButton("Cancel", 0);
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        if (r == 1) {
            lastPresetName_ = w->getTextEditorContents("name");
            presetio::save(state, lastPresetName_);
        }
    }));
}
void MainComponent::loadPresetFile(const juce::File& f) {
    tickerbar::TickerState loaded;
    juce::String name;
    if (!presetio::load(f, loaded, name)) return;
    state = loaded;
    lastPresetName_ = name;
    const auto rect = juce::Rectangle<int>(state.windowX, state.windowY, state.windowW, state.windowH);
    settingsChanged(true); // applies everything; its saveSettings() re-stamps the CURRENT window rect...
    state.windowX = rect.getX(); state.windowY = rect.getY(); // ...so restore the preset's rect, then move
    state.windowW = rect.getWidth(); state.windowH = rect.getHeight();
    applyWindowRectFromState();
    saveSettings();
    uiClick(ClickSounds::Which::Sparkle);
}
void MainComponent::applyWindowRectFromState() {
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    if (!w) return;
    std::vector<tickerbar::IntRect> areas;
    for (auto& d : juce::Desktop::getInstance().getDisplays().displays) {
        auto ua = d.userArea;
        areas.push_back({ ua.getX(), ua.getY(), ua.getWidth(), ua.getHeight() });
    }
    auto clamped = tickerbar::clampRectToWorkAreas(
        { state.windowX, state.windowY, state.windowW, state.windowH }, areas);
    w->setBounds(clamped.x, clamped.y, clamped.w, clamped.h);
}

// =====================================================================
// v0.3.0 item 1: the in-app updater — MeterBar's flow ported
// (check -> notes -> download -> size/SHA-256 verify -> install ->
// relaunch), driven by GitHub's HTTP answers: a public repo performs
// the full flow; an auth-blocked answer lands in the honest
// private-repo fallback with a releases-page button.
// =====================================================================
void MainComponent::startUpdateCheck(bool fromLaunch) {
    if (updateCheckPending_) return;
    updateState_ = std::make_shared<tbupdate::SharedState>();
    updateCheckPending_ = true;
    updateCheckFromLaunch_ = fromLaunch;
    tbupdate::checkLatestAsync(updateState_, juce::String(kVersionNum));
}
void MainComponent::checkForUpdates() {
    if (!tbupdate::configured()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Updates",
            "TickerBar " + juce::String(kVersion) + " - updates are not configured in this build.");
        return;
    }
    if (updateCheckPending_) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Updates",
            "An update check is already running - its result will appear when it finishes.");
        return;
    }
    startUpdateCheck(false);
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Updates",
        "Checking GitHub for the latest TickerBar release...\nThe result will pop up here when the check finishes.");
}
void MainComponent::pollUpdater() {
    if (updateCheckPending_ && updateState_ && updateState_->done.load(std::memory_order_acquire)) {
        updateCheckPending_ = false;
        const bool wasLaunch = updateCheckFromLaunch_;
        updateCheckFromLaunch_ = false;
        const auto r = updateState_->result;
        if (wasLaunch) {
            if (r.ok && r.newer) { // launch checks are silent otherwise
                pendingRelease_ = r;
                updateAvailable_ = true;
                resized();
                repaint();
            }
        } else {
            presentUpdateResult(r);
        }
    }
    if (updateDownloadPending_ && updateDownload_ && updateDownload_->isFinished()) {
        updateDownloadPending_ = false;
        const int dlStatus = updateDownload_->statusCode();
        const bool dlOk = !updateDownload_->hadError() && dlStatus == 200;
        updateDownload_.reset();
        if (!dlOk) {
            if (tickerbar::httpStatusBlocksAuth(dlStatus)) {
                showPrivateRepoFallback("The installer download itself was refused (HTTP "
                    + juce::String(dlStatus) + ") - the same repository-visibility limit as the check.");
                return;
            }
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Update",
                "The update download did not complete.\nTickerBar has NOT been changed - you are still on " + juce::String(kVersion) + ".");
            return;
        }
        if (pendingRelease_.assetSize > 0 && updateInstallerFile_.getSize() != pendingRelease_.assetSize) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Update",
                "The downloaded installer is the wrong size (expected " + juce::String(pendingRelease_.assetSize)
                + " bytes, got " + juce::String(updateInstallerFile_.getSize()) + ").\nIt will NOT be run. TickerBar has not been changed.");
            return;
        }
        if (pendingRelease_.assetDigest.isNotEmpty()) {
            juce::String want = pendingRelease_.assetDigest;
            if (want.startsWithIgnoreCase("sha256:")) want = want.substring(7);
            const juce::String got = tbupdate::sha256HexOfFile(updateInstallerFile_);
            if (!got.equalsIgnoreCase(want)) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Update",
                    "The downloaded installer's SHA-256 does not match the one GitHub published for this release.\n"
                    "It will NOT be run. TickerBar has not been changed.");
                return;
            }
        }
        juce::ChildProcess installer;
        const bool launched = installer.start({ updateInstallerFile_.getFullPathName(),
                                                "/SILENT", "/CLOSEAPPLICATIONS", "/RESTARTAPPLICATIONS", "/NORESTART" });
        if (!launched) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Update",
                "The installer was downloaded to\n" + updateInstallerFile_.getFullPathName()
                + "\nbut could not be started. You can run it by hand;\nTickerBar is still on " + juce::String(kVersion) + ".");
            return;
        }
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
}
void MainComponent::presentUpdateResult(const tbupdate::ReleaseInfo& r) {
    pendingRelease_ = r;
    if (r.authBlocked) {
        showPrivateRepoFallback("TickerBar asked GitHub for the latest release, but GitHub "
            "answers only signed-in users for a repository it treats as private - and TickerBar "
            "deliberately carries no sign-in token.");
        return;
    }
    if (!r.ok) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Updates",
            "Update check failed.\n" + r.error + "\nYou are still on TickerBar " + juce::String(kVersion) + ".");
        return;
    }
    if (!r.newer) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Updates",
            "You are up to date.\nTickerBar " + juce::String(kVersion) + " is the latest release (" + r.tag + ").");
        return;
    }
    updateAvailable_ = true;
    resized();
    showUpdatePanel(r);
}
void MainComponent::showUpdatePanel(const tbupdate::ReleaseInfo& r) {
    auto* w = new juce::AlertWindow("Update available",
        "TickerBar " + r.tag + " is available (you are on " + juce::String(kVersion) + ").\nRelease notes:",
        juce::AlertWindow::NoIcon);
    auto* notesBox = new juce::TextEditor();
    notesBox->setMultiLine(true, true);
    notesBox->setReadOnly(true);
    notesBox->setScrollbarsShown(true);
    notesBox->setCaretVisible(false);
    notesBox->setText(r.notes.trim().isNotEmpty() ? r.notes : "(This release was published without notes.)", false);
    notesBox->setSize(520, 190);
    w->addCustomComponent(notesBox);
    w->addTextBlock("\nDownload & install will close TickerBar, run the installer,\n"
                    "and relaunch at the new version. Your settings and presets\n"
                    "are stored outside the app folder and are kept.");
    w->addButton("Download & install", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Later", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
        if (result == 1) startUpdateDownload();
    }), true);
}
void MainComponent::showPrivateRepoFallback(const juce::String& detail) {
    auto* w = new juce::AlertWindow("Updates",
        "Automatic update check isn't available while the repository reads as private.\n\n"
        + detail + "\n\n"
        "You are still on TickerBar " + juce::String(kVersion) + ". Releases can be downloaded "
        "from the releases page in your browser instead:",
        juce::AlertWindow::InfoIcon);
    w->addButton("Open releases page", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Close", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([](int result) {
        if (result == 1) juce::URL(tbupdate::releasesPageUrl()).launchInDefaultBrowser();
    }), true);
}
void MainComponent::startUpdateDownload() {
    if (updateDownloadPending_) return;
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("TickerBar-Update");
    dir.createDirectory();
    updateInstallerFile_ = dir.getChildFile("TickerBar-Setup.exe");
    updateInstallerFile_.deleteFile();
    updateDownload_ = juce::URL(pendingRelease_.assetUrl).downloadToFile(updateInstallerFile_, juce::URL::DownloadTaskOptions());
    if (updateDownload_ == nullptr) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Update",
            "The update download could not be started.\nTickerBar has NOT been changed - you are still on " + juce::String(kVersion) + ".");
        return;
    }
    updateDownloadPending_ = true;
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Update",
        "Downloading TickerBar " + pendingRelease_.tag + "...\n"
        "When the download finishes, TickerBar will close, install,\nand relaunch itself at the new version.");
}

// =====================================================================
// v0.3.0 items 7/17/18: animated backgrounds + mascot drive. The
// 30 Hz pump steps the pure cores; band energy comes from MeterBar's
// BarLink broadcast when linked (item 17), else from the core's
// ambient driver, so the bar breathes gently on its own.
// =====================================================================
tickerbar::VisualizerSettings MainComponent::vizSettings() const {
    tickerbar::VisualizerSettings vs;
    vs.enabled = true;
    vs.baseHue = state.vizHue;
    vs.saturation = state.vizSat;
    vs.paletteSpread = state.vizSpread;
    vs.cycleSeconds = state.vizCycle;
    return vs;
}
tickerbar::GlowSettings MainComponent::glowSettings() const {
    tickerbar::GlowSettings gs;
    gs.baseHue = state.vizHue;
    gs.saturation = state.vizSat;
    gs.spread = state.vizSpread;
    gs.cycleSeconds = state.vizCycle;
    return gs;
}
void MainComponent::syncFxPump() {
    fxPump_.setRunning(state.bgEffect != 0 || (mascot_ && mascot_->isVisible()));
}
void MainComponent::stepFx() {
    const float dt = 1.f / 30.f;
    fxClock_ += dt;
    float bands[3] = { 0.f, 0.f, 0.f }, level = 0.f;
    float pkt[4];
    if (state.linkEnabled && linkClient_.peerAudio(tickerbar::kLinkAppMeter, pkt)) {
        bands[0] = pkt[0]; bands[1] = pkt[1]; bands[2] = pkt[2]; level = pkt[3];
    } else {
        tickerbar::ambientBands(fxClock_, bands, level);
    }
    if (state.bgEffect == 1) viz_.update(dt, bands, vizSettings());
    else if (state.bgEffect == 2) glow_.update(dt, bands, level);
    if (mascot_ && mascot_->isVisible()) {
        const long long now = juce::Time::currentTimeMillis();
        mascot_->excited = mascotAnim_.excited(now);
        mascot_->bobPx = mascotAnim_.bobPx(now);
        mascot_->repaint();
    }
    if (state.bgEffect != 0) repaint();
}
void MainComponent::drawVisualizerBackground(juce::Graphics& g) {
    // MeterBar's composite: black -> blurred particle layer (the
    // colour wash covers the whole background) -> crisp dots.
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colours::black);
    if (bounds.isEmpty()) return;
    const int iw = juce::jlimit(1, 480, bounds.getWidth() / 2);
    const int ih = juce::jlimit(1, 270, bounds.getHeight() / 2);
    if (!vizCache_.isValid() || vizCache_.getWidth() != iw || vizCache_.getHeight() != ih) {
        vizCache_ = juce::Image(juce::Image::RGB, iw, ih, true);
        vizBlur_ = juce::Image(juce::Image::RGB, iw, ih, true);
    }
    auto vs = vizSettings();
    {
        juce::Graphics ig(vizCache_);
        ig.fillAll(juce::Colours::black);
        for (auto& pt : viz_.particles()) {
            auto c = viz_.colorFor(pt, vs);
            ig.setColour(juce::Colour(c.r, c.g, c.b));
            const float px = pt.x * (float)iw, py = pt.y * (float)ih;
            const float r = pt.size;
            ig.fillEllipse(px - r, py - r, r * 2.f, r * 2.f);
        }
    }
    vizBlur_ = vizCache_;
    vizBlur_.duplicateIfShared();
    juce::ImageConvolutionKernel kernel(5);
    kernel.createGaussianBlur(1.8f);
    kernel.applyToImage(vizBlur_, vizCache_, vizBlur_.getBounds());
    const float op = juce::jlimit(0.15f, 1.0f, state.backgroundOpacity);
    g.setOpacity(op);
    g.drawImage(vizBlur_, bounds.toFloat());
    g.setOpacity(0.9f * op);
    g.drawImage(vizCache_, bounds.toFloat());
    g.setOpacity(1.0f);
}
void MainComponent::drawGlowBackground(juce::Graphics& g) {
    // The reactive glow: sample the core field in GROUP space (the
    // union of the linked bars' screen rects), so a linked block
    // reads as one continuous surface; unlinked, the group is the
    // bar itself. Near-black base, soft blooms, pinstripes on top.
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colours::black);
    if (bounds.isEmpty()) return;
    const int iw = juce::jlimit(8, 240, bounds.getWidth() / 4);
    const int ih = juce::jlimit(8, 135, bounds.getHeight() / 4);
    if (!glowCache_.isValid() || glowCache_.getWidth() != iw || glowCache_.getHeight() != ih)
        glowCache_ = juce::Image(juce::Image::RGB, iw, ih, true);
    auto group = linkClient_.groupRect();
    auto own = getScreenBounds();
    if (group.isEmpty() || own.isEmpty()) group = own;
    auto gs = glowSettings();
    {
        juce::Image::BitmapData bd(glowCache_, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < ih; ++y) {
            const float sy = (float)own.getY() + (y + 0.5f) / (float)ih * (float)own.getHeight();
            const float gy = group.getHeight() > 0 ? (sy - (float)group.getY()) / (float)group.getHeight() : 0.5f;
            for (int x = 0; x < iw; ++x) {
                const float sx = (float)own.getX() + (x + 0.5f) / (float)iw * (float)own.getWidth();
                const float gx = group.getWidth() > 0 ? (sx - (float)group.getX()) / (float)group.getWidth() : 0.5f;
                auto c = glow_.sample(gx, gy, gs);
                bd.setPixelColour(x, y, juce::Colour(c.r, c.g, c.b));
            }
        }
    }
    g.setOpacity(juce::jlimit(0.15f, 1.0f, state.backgroundOpacity));
    g.drawImage(glowCache_, bounds.toFloat());
    const float groupW = group.getWidth() > 0 ? (float)group.getWidth() : 1.f;
    g.setColour(juce::Colours::white);
    for (int lx = 0; lx < bounds.getWidth(); lx += 2) {
        const float gx = ((float)own.getX() + (float)lx - (float)group.getX()) / groupW;
        const float a = tickerbar::GlowField::pinstripe(gx);
        if (a > 0.f) { g.setOpacity(a); g.drawVerticalLine(lx, 0.f, (float)bounds.getHeight()); }
    }
    g.setOpacity(1.0f);
}
