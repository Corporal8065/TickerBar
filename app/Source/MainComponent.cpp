#include "MainComponent.h"
#include <tickerbar/Format.hpp>
#include <tickerbar/Stale.hpp>
#include "SettingsStore.h"
#if JUCE_WINDOWS
 #include <windows.h> // native rects for the AppBar sync (physical pixels)
#endif
static const char* kVersion = "v0.1.1";

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
        // top row: symbol (left) + change chip (right)
        auto top = r.removeFromTop(18.f);
        g.setColour(th.text.withAlpha(alpha)); g.setFont(juce::Font(13.5f, juce::Font::bold));
        g.drawText(juce::String(symbol), top.removeFromLeft(64.f), juce::Justification::centredLeft);
        auto pct = has ? tickerbar::changePct(ctx.price, ctx.open24h) : std::nullopt;
        if (pct) {
            g.setColour((*pct >= 0 ? th.good : th.bad).withAlpha(alpha));
            g.setFont(juce::Font(12.5f, juce::Font::bold));
            g.drawText(juce::String(tickerbar::formatPct(*pct)), top, juce::Justification::centredRight);
        }
        // price row
        auto mid = r.removeFromTop(24.f);
        g.setColour(th.text.withAlpha(alpha)); g.setFont(juce::Font(16.5f, juce::Font::bold));
        g.drawText(has ? juce::String(tickerbar::formatPrice(ctx.price)) : juce::String("--"),
                   mid, juce::Justification::centredLeft);
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
    explicit ContextCell(MainComponent* o) : owner(o) {}
    void setData(const juce::String& v, const juce::String& sub, juce::Colour col, bool isStale) {
        if (v == valueText && sub == subText && col == valueColour && isStale == stale && has) { repaint(); return; }
        valueText = v; subText = sub; valueColour = col; stale = isStale; has = true; repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override { owner->windowDragStarted(e); }
    void mouseDrag(const juce::MouseEvent& e) override { owner->windowDragged(e); }
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
// Detail view (CallOutBox content): bigger chart + full stats + the
// alert editors for this symbol.
// =====================================================================
class DetailComponent : public juce::Component {
public:
    DetailComponent(MainComponent* o, const juce::String& sym, const tickerbar::AssetCtx& c,
                    std::vector<tickerbar::Candle> cs, bool isStale, const juce::String& asOf)
        : owner(o), ctx(c), candles(std::move(cs)), stale(isStale) {
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
        // 24h range: the source's own stats fields when it provides
        // them (both do), candles as the fallback.
        auto hi = ctx.high24h > 0 ? std::optional<double>(ctx.high24h) : tickerbar::candlesHigh(candles);
        auto lo = ctx.low24h > 0 ? std::optional<double>(ctx.low24h) : tickerbar::candlesLow(candles);
        auto row = [&](const juce::String& k, const juce::String& v, juce::Colour vc = juce::Colour()) {
            juce::String s = k + "  " + v;
            auto* l = new juce::Label({}, s);
            l->setFont(juce::Font(13.f)); l->setColour(juce::Label::textColourId, vc.getARGB() ? vc : th.text);
            rows.add(l); addAndMakeVisible(l);
        };
        row("Price (spot)", juce::String(tickerbar::formatPrice(ctx.price)));
        if (pct) row("24h change", juce::String(tickerbar::formatPct(*pct)), *pct >= 0 ? th.good : th.bad);
        if (hi) row("24h high", juce::String(tickerbar::formatPrice(*hi)));
        if (lo) row("24h low", juce::String(tickerbar::formatPrice(*lo)));
        row("24h volume", juce::String(tickerbar::formatVolume(ctx.dayVolumeUsd)));
        // No funding row: these are spot sources — a funding rate does
        // not exist here and is never fabricated. CoinGecko rows carry
        // a market cap instead.
        if (ctx.marketCapUsd > 0) row("Market cap", juce::String(tickerbar::formatVolume(ctx.marketCapUsd)));
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
        setSize(360, 452);
    }
    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        g.fillAll(th.panel);
        // big chart in the reserved band
        auto area = chartArea.toFloat().reduced(6.f);
        g.setColour(th.deepest); g.fillRoundedRectangle(area, 6.f);
        if (candles.size() > 1) {
            std::vector<double> closes; for (auto& c : candles) closes.push_back(c.close);
            auto norm = tickerbar::normalizeSeries(closes);
            juce::Path p;
            for (size_t i = 0; i < norm.size(); ++i) {
                float x = area.getX() + area.getWidth() * (float)i / (float)(norm.size() - 1);
                float y = area.getBottom() - 4.f - norm[i] * (area.getHeight() - 8.f);
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            auto pct = tickerbar::changePct(ctx.price, ctx.open24h);
            auto col = (pct && *pct >= 0) ? th.good : th.bad;
            juce::Path fill = p;
            fill.lineTo(area.getRight(), area.getBottom()); fill.lineTo(area.getX(), area.getBottom()); fill.closeSubPath();
            g.setColour(col.withAlpha(0.15f)); g.fillPath(fill);
            g.setColour(col); g.strokePath(p, juce::PathStrokeType(1.8f));
        } else {
            g.setColour(th.dim); g.setFont(11.f);
            g.drawText("chart unavailable", area, juce::Justification::centred);
        }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(14);
        title.setBounds(r.removeFromTop(22)); asOfLabel.setBounds(r.removeFromTop(15)); r.removeFromTop(6);
        chartArea = r.removeFromTop(130); r.removeFromTop(8);
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
    tickerbar::AssetCtx ctx;
    std::vector<tickerbar::Candle> candles;
    bool stale;
    juce::Label title, asOfLabel, alertsTitle, aboveLabel, belowLabel;
    juce::OwnedArray<juce::Label> rows;
    juce::TextEditor aboveEditor, belowEditor;
    juce::TextButton setAlertsBtn { "Set alerts" }, clearBtn { "Clear" };
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
            note("While ON and the bar is docked flush at its monitor edge, Windows shrinks that monitor's work area so maximised windows stop at the bar. Same control as the bar's Reserve button.");
            combo("Dock edge", {"Top", "Bottom"}, (int)st->dockEdge, [&](int v){
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
            note("TickerBar v0.1.1 - prices are SPOT prices: Coinbase Exchange public API for Coinbase-listed coins, CoinGecko for HYPE. No account, no keys, read-only.");
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
    settingsBtn_.onClick = [this]{
        settingsWin_->content.build();
        settingsWin_->setVisible(true); settingsWin_->toFront(true);
    };
    minBtn_.onClick = [this]{ if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                                 if (auto* peer = w->getPeer()) peer->setMinimised(true); };
    quitBtn_.onClick = []{ juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
    for (auto* b : { &reserveBtn_, &settingsBtn_, &minBtn_, &quitBtn_ }) addAndMakeVisible(b);

    settingsWin_ = std::make_unique<TickerSettingsWindow>();
    settingsWin_->content.owner = this;
    settingsWin_->content.st = &state;
    settingsWin_->content.onChanged = [this]{ settingsChanged(false); };
    settingsWin_->content.onWatchlistChanged = [this]{ settingsChanged(true); };
    settingsWin_->content.build();

    fngCell_ = std::make_unique<ContextCell>(this);
    fngCell_->title = "FEAR & GREED";
    fngCell_->setTooltip("Crypto Fear & Greed Index - alternative.me. Updates about daily; polled every 5 minutes.");
    domCell_ = std::make_unique<ContextCell>(this);
    domCell_->title = "BTC DOMINANCE";
    domCell_->setTooltip("BTC share of total crypto market cap - CoinGecko global. Polled every 5 minutes.");
    addAndMakeVisible(fngCell_.get());
    addAndMakeVisible(domCell_.get());

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
    // market-context group at the right of the coin cells
    if (domCell_ && fngCell_) {
        domCell_->setBounds(r.removeFromRight(112));
        r.removeFromRight(6);
        fngCell_->setBounds(r.removeFromRight(112));
        r.removeFromRight(6);
    }
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
            unsigned ev = tracker.step(levelsFor(cell->symbol), ctx->price);
            if (ev != tickerbar::AlertNone) {
                cell->flash();
                if (state.alertSound) clickPlayer_.play();
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
    syncScreenSpace();
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

void MainComponent::snapToDockEdge() {
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    if (!w) return;
    auto display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(w->getBounds());
    if (display == nullptr) return;
    auto area = display->totalArea;
    auto b = w->getBounds();
    b.setY(state.dockEdge == tickerbar::DockEdge::Top ? area.getY() : area.getBottom() - b.getHeight());
    b.setX(juce::jlimit(area.getX(), area.getRight() - b.getWidth(), b.getX()));
    w->setBounds(b);
    syncScreenSpace();
}

// Reserve sync (MeterBar v0.18 pattern): while the toggle is ON and the
// bar sits flush at its docked edge of its monitor, register as an
// AppBar. Physical pixels from GetWindowRect on Windows; the ~4x/sec
// timer poll is what makes moves/resizes/edge changes take effect.
void MainComponent::syncScreenSpace() {
#if JUCE_WINDOWS
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    HWND hwnd = (w && w->getPeer()) ? static_cast<HWND>(w->getPeer()->getNativeHandle()) : nullptr;
    if (hwnd) {
        RECT wr{}; GetWindowRect(hwnd, &wr);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof mi; GetMonitorInfoW(mon, &mi);
        const bool top = state.dockEdge == tickerbar::DockEdge::Top;
        const bool docked = top ? std::abs(wr.top - mi.rcMonitor.top) <= 2
                                : std::abs(wr.bottom - mi.rcMonitor.bottom) <= 2;
        if (state.reserveScreenSpace && docked)
            screenSpace_.reserve(hwnd, top ? 0 : 1, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top);
        else
            screenSpace_.release();
    }
#else
    screenSpace_.release();
#endif
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
        const bool top = state.dockEdge == tickerbar::DockEdge::Top;
        const int strip = top ? (wr.bottom - mi.rcMonitor.top) : (mi.rcMonitor.bottom - wr.top);
        screenSpace_.arrange(hwnd, strip, top ? 0 : 1);
        return;
    }
#endif
    screenSpace_.arrange(nullptr, 0, 0); // stub: records the honest "unavailable" status
}
void MainComponent::doRestore() { screenSpace_.restore(); }
