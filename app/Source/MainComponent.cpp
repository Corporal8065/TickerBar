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
// v0.5.3: the version string comes from the GENERATED header — root
// CMakeLists project(VERSION) is the single source of truth (the
// MeterBar v0.19 pattern, via MainComponent.h's TickerBarVersion.h
// include). Before this, the literals here (kVersion / kVersionNum)
// and getApplicationVersion() in Main.cpp were hard-coded and last
// bumped at v0.4.0: every later build reported 0.4.0 to the updater
// and in the Settings sheet, so the updater offered the newest
// release forever — Fiona's stuck-on-0.4.0 report. Her manual
// install of the 0.5.0 Setup.exe had actually worked; the app was
// misreporting its own version.
static const juce::String kVersion = juce::String("v") + tickerbar::kVersionString;
#include "CrashHandler.h" // v0.6.13: hard-crash record (installed in Main.cpp initialise, first thing)
// v0.6.13 (family crash-log round): TickerBar's Documents folder —
// crash records (crash-*.txt + .dmp), presets and the folder the
// "Open Log Folder" actions open.
static juce::File tickerBarDocsDir() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TickerBar");
}
void MainComponent::openLogFolder() {
    auto dir = tickerBarDocsDir();
    dir.createDirectory();
    dir.revealToUser();
}
// (dockEdgeName went with the Dock button at v0.6.0 — the edge is
// picked by name in the Reposition submenu now.)

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
        if (owner) owner->splitterDragBegin(); // v0.5.4: hold the marquee still for this drag
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (owner) owner->splitterDragEnd();
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
        // v0.4.0 (Jackson's ask): NO panel fill — each symbol's cell
        // background is transparent, so the bar's own background
        // (styled gradient / blurred fx) shows straight through.
        // The border alone carries the cell's shape.
        if (flashing()) {
            const bool phase = (juce::Time::currentTimeMillis() / 400) % 2 == 0;
            g.setColour((phase ? th.warn : th.accent).withAlpha(0.95f));
            g.drawRoundedRectangle(b, 8.f, 2.f);
        } else {
            g.setColour(th.border.withAlpha(alpha));
            g.drawRoundedRectangle(b, 8.f, 1.f);
        }
        // v0.6.13 (DESIGN §7): the linked pulse spends itself on the
        // BORDER only (TickerBar cells are border-only by design —
        // no panel fill is added): a brighter border in the pulse
        // colour while the envelope/pump is live. The alert flash
        // above keeps precedence.
        if (!flashing()) {
            const float spend = owner->cellPulseSpend();
            if (spend > 0.01f) {
                g.setColour(owner->cellPulseColour().withAlpha(0.5f * spend * alpha));
                g.drawRoundedRectangle(b.reduced(0.5f), 8.f, 1.4f);
            }
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
        // v0.6.10: cell text draws SINGLE, in its own colour — no
        // text pop inside cells (Jackson, 2026-11-03, BTC-cell
        // photo: the doubled copies were illegible at cell sizes).
        g.setFont(symFont);
        textpop::drawSingle(g, juce::String(symbol), top.removeFromLeft((float)lay.symbolW), juce::Justification::centredLeft, th.text.withAlpha(alpha));
        top.removeFromLeft((float)lay.gap);
        if (pct) {
            g.setFont(pctFont);
            textpop::drawSingle(g, pctText, top.removeFromLeft((float)lay.pctW), juce::Justification::centredRight, owner->gainColour(*pct >= 0).withAlpha(alpha));
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
        g.setFont(priceFont);
        textpop::drawSingle(g, priceText, mid.removeFromLeft((float)lay2.symbolW), juce::Justification::centredLeft, th.text.withAlpha(alpha));
        mid.removeFromLeft((float)lay2.gap);
        if (pct1h) {
            g.setFont(h1Font);
            textpop::drawSingle(g, h1Text, mid.removeFromLeft((float)lay2.pctW), juce::Justification::centredRight, owner->gainColour(*pct1h >= 0).withAlpha(alpha));
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
            g.setFont(10.5f);
            textpop::drawSingle(g, has ? juce::String("chart loading...") : juce::String("waiting for data..."),
                          r, juce::Justification::centredLeft, th.dim.withAlpha(alpha));
        }
        if (stale && has) {
            g.setFont(juce::Font(9.f, juce::Font::bold));
            textpop::drawSingle(g, juce::String("STALE"), b.reduced(7.f, 4.f), juce::Justification::bottomRight, th.warn);
        }
    }
private:
    juce::int64 flashUntilMs = 0;
};

// QuoteCell — one rotation-section cell (v0.6.11): an index/ETF/
// stock quote, a FRED yield, or a computed Internals proxy. Same
// visual language and stale honesty as a CoinCell, but the change
// figure is the DAY change (the crypto cells' "1h" figure comes
// from hourly candles these sources do not have — nothing here may
// wear that label). Text arrives pre-formatted from
// MainComponent::refreshSections; every element draws SINGLE
// (v0.6.10 rule). A press drags the window via the same arbitration
// as the context cells used; quote cells have no click action.
// =====================================================================
class QuoteCell : public juce::Component, public juce::SettableTooltipClient {
public:
    MainComponent* owner = nullptr;
    juce::String title, valueText, pctText, subText;
    juce::Colour pctColour, lineColour;
    std::vector<double> series;
    bool has = false, stale = false;
    explicit QuoteCell(MainComponent* o) : owner(o) {}
    void setData(const juce::String& v, const juce::String& pct, juce::Colour pctCol,
                 const juce::String& sub, std::vector<double> ser, juce::Colour lineCol, bool isStale) {
        if (v == valueText && pct == pctText && sub == subText && pctCol == pctColour &&
            lineCol == lineColour && ser == series && isStale == stale && has) { repaint(); return; }
        valueText = v; pctText = pct; pctColour = pctCol; subText = sub;
        series = std::move(ser); lineColour = lineCol; stale = isStale; has = true; repaint();
    }
    void setWaiting(const juce::String& sub) {
        if (!has && sub == subText) { repaint(); return; }
        has = false; stale = false; valueText.clear(); pctText.clear();
        subText = sub; series.clear(); repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override { owner->cellDragBegin(this, e); }
    void mouseDrag(const juce::MouseEvent& e) override { owner->cellDragMove(this, e); }
    void mouseUp(const juce::MouseEvent& e) override { owner->cellDragEnd(this, e); }
    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        auto b = getLocalBounds().toFloat().reduced(1.f);
        const float alpha = stale ? 0.55f : 1.0f;
        // v0.4.0 rule, shared with the coin cells: border only.
        g.setColour(th.border.withAlpha(alpha));
        g.drawRoundedRectangle(b, 8.f, 1.f);
        // v0.6.13 (DESIGN §7): linked-pulse border brightening, same
        // spend as the coin cells (border only — no panel fill).
        {
            const float spend = owner->cellPulseSpend();
            if (spend > 0.01f) {
                g.setColour(owner->cellPulseColour().withAlpha(0.5f * spend * alpha));
                g.drawRoundedRectangle(b.reduced(0.5f), 8.f, 1.4f);
            }
        }
        auto r = b.reduced(9.f, 5.f);
        // top row: title (left) + day-change % (right), the core
        // never-truncate layout — the % reserves in full, the title clips.
        auto top = r.removeFromTop(18.f);
        const juce::Font titleFont(13.5f, juce::Font::bold), pctFont(12.5f, juce::Font::bold);
        auto lay = tickerbar::coinCellTopRow((int)top.getWidth(),
                                             (int)std::ceil(titleFont.getStringWidthFloat(title)),
                                             pctText.isEmpty() ? 0 : (int)std::ceil(pctFont.getStringWidthFloat(pctText)));
        g.setFont(titleFont);
        textpop::drawSingle(g, title, top.removeFromLeft((float)lay.symbolW), juce::Justification::centredLeft, th.text.withAlpha(alpha));
        top.removeFromLeft((float)lay.gap);
        if (pctText.isNotEmpty()) {
            g.setFont(pctFont);
            textpop::drawSingle(g, pctText, top.removeFromLeft((float)lay.pctW), juce::Justification::centredRight, pctColour.withAlpha(alpha));
        }
        // value row
        g.setFont(juce::Font(16.5f, juce::Font::bold));
        textpop::drawSingle(g, has ? valueText : juce::String("--"), r.removeFromTop(24.f),
                            juce::Justification::centredLeft, (has ? th.text : th.dim).withAlpha(alpha));
        // bottom: sparkline when a series exists (the sub line — a
        // yield's observation date — keeps a small strip under it),
        // else the sub line alone (day range / "computed proxy" /
        // waiting state).
        if (series.size() >= 2) {
            auto area = r;
            if (subText.isNotEmpty()) area.removeFromBottom(11.f);
            auto norm = tickerbar::normalizeSeries(series);
            juce::Path p;
            auto drawArea = area.reduced(0.f, 1.f);
            for (size_t i = 0; i < norm.size(); ++i) {
                float x = drawArea.getX() + drawArea.getWidth() * (float)i / (float)(norm.size() - 1);
                float y = drawArea.getBottom() - norm[i] * drawArea.getHeight();
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            auto lineCol = lineColour.withAlpha(0.9f * alpha);
            juce::Path fill = p;
            fill.lineTo(drawArea.getRight(), drawArea.getBottom()); fill.lineTo(drawArea.getX(), drawArea.getBottom()); fill.closeSubPath();
            g.setColour(lineCol.withAlpha(0.16f)); g.fillPath(fill);
            g.setColour(lineCol); g.strokePath(p, juce::PathStrokeType(1.4f));
            if (subText.isNotEmpty()) {
                g.setFont(8.5f);
                textpop::drawSingle(g, subText, r.removeFromBottom(11.f), juce::Justification::centredLeft, th.dim.withAlpha(alpha));
            }
        } else {
            g.setFont(10.5f);
            textpop::drawSingle(g, has ? subText : (subText.isEmpty() ? juce::String("waiting for data...") : subText),
                                r, juce::Justification::centredLeft, th.dim.withAlpha(alpha));
        }
        if (stale && has) {
            g.setFont(juce::Font(9.f, juce::Font::bold));
            textpop::drawSingle(g, juce::String("STALE"), b.reduced(7.f, 4.f), juce::Justification::bottomRight, th.warn);
        }
    }
};

// =====================================================================
// TitleContextCell — the v0.6.11 title-bar context rotator: Fear &
// Greed and BTC dominance merged into ONE small cell living in the
// title bar between the left cluster and the top-right icons (they
// left the main row — Jackson's ask). The two readings rotate on
// the core dwell (kTitleContextRotateSec); the face + dwell are
// owned by the core TitleRotator state machine (v0.6.12 — this
// widget only renders the face it is handed), and when only one
// reading is enabled in Settings the cell is static on it. Same stale honesty as every cell: dimmed
// at 0.55 alpha with a STALE tag on the context clock. On the
// Fear & Greed face a click still opens the index page (the
// v0.2.0 click-through behaviour, preserved). Single-draw text
// (v0.6.10 rule — it is a cell, not toolbar furniture).
// =====================================================================
class TitleContextCell : public juce::Component, public juce::SettableTooltipClient {
public:
    MainComponent* owner = nullptr;
    explicit TitleContextCell(MainComponent* o) : owner(o) { syncFaceChrome(); }
    void setReading(bool fng, const juce::String& v, const juce::String& sub, juce::Colour col,
                    bool hasValue, bool isStale) {
        Face& f = fng ? fng_ : dom_;
        f.value = v; f.sub = sub; f.colour = col; f.has = hasValue; f.stale = isStale;
        if (fng == showingFng_) repaint();
    }
    void setIncluded(bool fngIn, bool domIn) {
        fngIn_ = fngIn; domIn_ = domIn;
        syncFaceChrome(); repaint();
    }
    // v0.6.12: the FACE is owned by the core TitleRotator now (the
    // owner pushes it here on every context refresh and on every
    // flip). v0.6.11 kept the face in this widget, the dwell in the
    // owner and the inclusion flags in both — the split is what let
    // the face freeze in the field. This widget only renders.
    void setFace(bool fng) {
        if (showingFng_ == fng) return;
        showingFng_ = fng;
        syncFaceChrome(); repaint();
    }
    bool showingFng() const { return showingFng_; }
    void mouseDown(const juce::MouseEvent& e) override { owner->cellDragBegin(this, e); }
    void mouseDrag(const juce::MouseEvent& e) override { owner->cellDragMove(this, e); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (owner->cellDragEnd(this, e)) return;
        if (showingFng_ && fng_.has && e.getDistanceFromDragStart() < 5 && e.mouseWasClicked())
            juce::URL("https://alternative.me/crypto/fear-and-greed-index/").launchInDefaultBrowser();
    }
    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        const Face& f = showingFng_ ? fng_ : dom_;
        auto b = getLocalBounds().toFloat().reduced(1.f);
        const float alpha = f.stale ? 0.55f : 1.0f;
        g.setColour(th.border.withAlpha(alpha));
        g.drawRoundedRectangle(b, 6.f, 1.f);
        const juce::String title = showingFng_ ? "FEAR & GREED" : "BTC DOMINANCE";
        const juce::String value = f.has ? f.value : "--";
        const juce::String sub = f.has ? f.sub : "loading...";
        auto r = b.reduced(8.f, 2.f);
        if (getHeight() >= 28) {
            // two-line form (the 30 px horizontal toolbar strip)
            g.setFont(juce::Font(8.5f, juce::Font::bold));
            textpop::drawSingle(g, title, r.removeFromTop(10.f), juce::Justification::centredLeft, th.dim.withAlpha(alpha));
            auto row = r;
            const juce::Font valFont(13.5f, juce::Font::bold);
            const int vw = (int)std::ceil(valFont.getStringWidthFloat(value)) + 2;
            g.setFont(valFont);
            textpop::drawSingle(g, value, row.removeFromLeft((float)vw), juce::Justification::centredLeft, (f.has ? f.colour : th.dim).withAlpha(alpha));
            g.setFont(9.5f);
            textpop::drawSingle(g, sub, row, juce::Justification::centredLeft, th.dim.withAlpha(alpha));
        } else {
            // single-line form (the vertical stack's title row)
            const juce::Font titleFont(8.5f, juce::Font::bold), valFont(12.5f, juce::Font::bold);
            const int tw = (int)std::ceil(titleFont.getStringWidthFloat(title)) + 7;
            const int vw = (int)std::ceil(valFont.getStringWidthFloat(value)) + 7;
            g.setFont(titleFont);
            textpop::drawSingle(g, title, r.removeFromLeft((float)tw), juce::Justification::centredLeft, th.dim.withAlpha(alpha));
            g.setFont(valFont);
            textpop::drawSingle(g, value, r.removeFromLeft((float)vw), juce::Justification::centredLeft, (f.has ? f.colour : th.dim).withAlpha(alpha));
            g.setFont(9.5f);
            textpop::drawSingle(g, sub, r, juce::Justification::centredLeft, th.dim.withAlpha(alpha));
        }
        if (f.stale && f.has) {
            g.setFont(juce::Font(8.f, juce::Font::bold));
            textpop::drawSingle(g, juce::String("STALE"), b.reduced(5.f, 2.f), juce::Justification::bottomRight, th.warn);
        }
    }
private:
    struct Face { juce::String value, sub; juce::Colour colour; bool has = false, stale = false; };
    Face fng_, dom_;
    bool showingFng_ = true, fngIn_ = true, domIn_ = true;
    void syncFaceChrome() {
        if (showingFng_) {
            setTooltip("Crypto Fear & Greed Index - alternative.me. Updates about daily; polled every 5 minutes. Click to open the index page.");
            setMouseCursor(fng_.has ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        } else {
            setTooltip("BTC share of total crypto market cap - CoinGecko global. Polled every 5 minutes.");
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }
};

// =====================================================================
// ModeSelector — v0.6.14: the rotation-section switcher ON the bar,
// at the left end of the main strip (Jackson's ask — since v0.6.11
// the sections lived only in Settings > Rotation and Menu >
// Rotation, and he never found them there). Three surfaces, ONE
// state: this selector, the Settings combo and the Menu submenu all
// read/write MainComponent::rotationSection()/setRotationSection()
// over the same persisted TickerState::rotationSection field — no
// new settings keys. Short labels (core rotationSectionShortName);
// full names ride the tooltips and the section menu. The form
// (expanded segments / collapsed current-mode button / hidden) is
// the core room rule's verdict, handed in by resized(); text draws
// SINGLE (the v0.6.10 cell rule — this is a cell-like control, not
// toolbar furniture). A press here is a click, never a window drag
// (the toolbar buttons set the precedent).
// =====================================================================
class ModeSelector : public juce::Component, public juce::SettableTooltipClient {
public:
    MainComponent* owner = nullptr;
    explicit ModeSelector(MainComponent* o) : owner(o) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void setForm(tickerbar::ModeSelectorForm f) {
        if (f == form_) return;
        form_ = f; hoverSeg_ = -1; repaint();
    }
    tickerbar::ModeSelectorForm form() const { return form_; }
    // Measured extents at the paint font — resized() feeds these to
    // the core room rule (the titleContextExtent pattern).
    int naturalWidth() const {
        std::vector<int> lw;
        for (int i = 0; i < tickerbar::kRotationSectionCount; ++i)
            lw.push_back((int)std::ceil(segFont().getStringWidthFloat(
                juce::String(tickerbar::rotationSectionShortName(i)))));
        return tickerbar::modeSelectorNaturalWidth(lw);
    }
    int collapsedWidth() const {
        const int lw = (int)std::ceil(segFont().getStringWidthFloat(
            juce::String(tickerbar::rotationSectionShortName(owner->rotationSection()))));
        return tickerbar::modeSelectorCollapsedWidth(lw);
    }
    void paint(juce::Graphics& g) override {
        const auto& th = owner->theme();
        const int active = owner->rotationSection();
        auto b = getLocalBounds().toFloat().reduced(1.f);
        g.setColour(th.border);
        g.drawRoundedRectangle(b, 6.f, 1.f);
        if (form_ == tickerbar::ModeSelectorForm::Expanded) {
            const int w = getWidth();
            for (int i = 0; i < tickerbar::kRotationSectionCount; ++i) {
                const float x0 = (float)tickerbar::modeSegmentX0(i, w, tickerbar::kRotationSectionCount);
                const float x1 = (float)tickerbar::modeSegmentX1(i, w, tickerbar::kRotationSectionCount);
                juce::Rectangle<float> seg(x0 + 2.f, b.getY() + 2.f, x1 - x0 - 4.f, b.getHeight() - 4.f);
                if (i == active) {
                    g.setColour(th.accent.withAlpha(0.30f));
                    g.fillRoundedRectangle(seg, 4.f);
                    g.setColour(th.accent);
                    g.fillRect(seg.getX() + 4.f, seg.getBottom() - 2.f, seg.getWidth() - 8.f, 2.f);
                } else if (i == hoverSeg_) {
                    g.setColour(th.text.withAlpha(0.08f));
                    g.fillRoundedRectangle(seg, 4.f);
                }
                if (i > 0) {
                    g.setColour(th.border.withAlpha(0.6f));
                    g.drawVerticalLine((int)x0, b.getY() + 5.f, b.getBottom() - 5.f);
                }
                g.setFont(segFont());
                textpop::drawSingle(g, juce::String(tickerbar::rotationSectionShortName(i)), seg,
                                    juce::Justification::centred,
                                    i == active ? th.text : th.dim);
            }
        } else { // Collapsed: current mode + dropdown triangle
            const juce::String label(tickerbar::rotationSectionShortName(active));
            auto r = b.reduced(4.f, 1.f);
            auto triZone = r.removeFromRight((float)tickerbar::kModeSelectorChevronPx);
            g.setFont(segFont());
            textpop::drawSingle(g, label, r, juce::Justification::centred, th.text);
            juce::Path tri;
            const float cx = triZone.getCentreX(), cy = triZone.getCentreY();
            tri.addTriangle(cx - 4.f, cy - 2.5f, cx + 4.f, cy - 2.5f, cx, cy + 3.5f);
            g.setColour(th.dim);
            g.fillPath(tri);
        }
    }
    void mouseMove(const juce::MouseEvent& e) override {
        juce::String tip;
        int hov = -1;
        if (form_ == tickerbar::ModeSelectorForm::Expanded) {
            hov = tickerbar::modeSegmentAt(e.getPosition().x, getWidth(), tickerbar::kRotationSectionCount);
            if (hov >= 0)
                tip = juce::String(tickerbar::rotationSectionName(hov))
                    + (hov == owner->rotationSection() ? " - current section" : " - click to switch");
        } else if (form_ == tickerbar::ModeSelectorForm::Collapsed) {
            tip = "Section: " + juce::String(tickerbar::rotationSectionName(owner->rotationSection()))
                + " - click to choose";
        }
        if (tip != lastTip_) { lastTip_ = tip; setTooltip(tip); }
        if (hov != hoverSeg_) { hoverSeg_ = hov; repaint(); }
    }
    void mouseExit(const juce::MouseEvent&) override {
        if (hoverSeg_ != -1) { hoverSeg_ = -1; repaint(); }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mouseWasClicked()) return;
        if (form_ == tickerbar::ModeSelectorForm::Expanded) {
            const int i = tickerbar::modeSegmentAt(e.getPosition().x, getWidth(),
                                                    tickerbar::kRotationSectionCount);
            if (i >= 0 && i != owner->rotationSection()) {
                owner->uiClick(ClickSounds::Which::Pop);
                owner->setRotationSection(i);
            }
        } else if (form_ == tickerbar::ModeSelectorForm::Collapsed) {
            owner->uiClick(ClickSounds::Which::Pop);
            showSectionMenu();
        }
    }
private:
    tickerbar::ModeSelectorForm form_ = tickerbar::ModeSelectorForm::Expanded;
    int hoverSeg_ = -1;
    juce::String lastTip_;
    static juce::Font segFont() { return juce::Font(12.0f, juce::Font::bold); }
    // The collapsed form's menu: the five FULL names, the active one
    // ticked — the same choice the Menu > Rotation submenu offers,
    // landing in the same setRotationSection entry.
    void showSectionMenu() {
        juce::PopupMenu m;
        for (int i = 0; i < tickerbar::kRotationSectionCount; ++i)
            m.addItem(i + 1, tickerbar::rotationSectionName(i), true, i == owner->rotationSection());
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [this](int r) {
                            if (r >= 1 && r <= tickerbar::kRotationSectionCount)
                                owner->setRotationSection(r - 1);
                        });
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
// supplies hourly closes only (flat ticks, no volume bars, no 30d),
// so since v0.6.12 the drill-in candles for CoinGecko-routed
// symbols (HYPE) come from Hyperliquid's candleSnapshot first —
// full OHLC + volume at every range — with CoinGecko as the
// fallback. The note line under the range buttons says which feed
// drew the chart, in words, from PriceClient::detailNote +
// detailSourceName.
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
        // v0.6.12: the chart names the feed that actually DREW it
        // (PriceClient::detailSourceName — the feed that supplied
        // these cached candles, which for a CoinGecko-priced symbol
        // is Hyperliquid when the candleSnapshot route served the
        // range and CoinGecko after a fallback). Notes that already
        // open with the feed's name ("Hyperliquid candles: ...",
        // "CoinGecko supplies ...") stand as they are; anything else
        // gets the source prefixed so no chart is ever unlabelled.
        const std::string srcName = owner->client().detailSourceName(symbol.toStdString(), range);
        if (!srcName.empty() && !candles.empty() && !note.startsWith(juce::String(srcName)))
            note = "Candles: " + juce::String(srcName) + (note.isEmpty() || note == "loading..."
                        ? juce::String() : ". " + note);
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
        panel("Rotation", [&]{
            combo("Section", {"Crypto (watchlist)", "ETFs & Indexes", "Top 100 S&P stocks", "Bonds", "Market Internals"},
                  owner->rotationSection(),
                  [&](int v){ owner->setRotationSection(v); });
            note("What the main strip shows (also on the bar itself - the mode selector at the left of the strip - and in the bar's Menu > Rotation; all three switch the same setting). The non-crypto sections are keyless public data: index / ETF / stock quotes and bond ETFs from CNBC, Treasury yields from FRED (daily - each yield cell prints its observation date), sparklines from Yahoo daily closes. Market Internals figures are computed proxies from the Top 100 quotes, not official exchange breadth.");
        });
        panel("Title bar context", [&]{
            toggle("Show Fear & Greed in the title bar", st->showFearGreed, [&](bool b){ st->showFearGreed = b; });
            toggle("Show BTC dominance in the title bar", st->showBtcDominance, [&](bool b){ st->showBtcDominance = b; });
            note("Fear & Greed and BTC dominance share one small cell in the title bar, rotating every few seconds when both are on (v0.6.11 - they used to be full cells at the end of the bar). If the title bar gets too narrow the cell hides rather than crushing the wordmark or the icons. A click on the Fear & Greed face still opens the index page.");
            button("Reset layout to defaults", [this]{ owner->resetLayoutToDefaults(); });
            note("Layout only: every cell shown again, all dragged cell widths forgotten, coin order back to BTC / ETH / HYPE. Your coins, alerts, theme and poll settings are kept.");
        });
        panel("Sizes", [&]{
            note("One slider per coin - the same widths the dividers on the bar drag, so the two always agree. Applies live, whether the bar is scrolling or still. A cell with no width set shares the space automatically (auto).");
            for (auto& sym : st->watchlist) {
                const int stored = owner->symbolCellWidth(sym);
                auto* l = add(new juce::Label({}, juce::String(sym)));
                l->setFont(juce::Font(12.5f, juce::Font::bold));
                l->setColour(juce::Label::textColourId, settingsPalette::text());
                l->setBounds(24, y_, 70, 22);
                auto* s = add(new juce::Slider());
                s->setRange(tickerbar::kSymbolCellMinW, tickerbar::kSymbolCellMaxW, 1.0);
                s->setValue(stored > 0 ? stored : 160, juce::dontSendNotification);
                s->setBounds(102, y_, 380, 22);
                auto* ro = add(new juce::Label({}, stored > 0 ? juce::String(stored) + " px" : juce::String("auto")));
                ro->setFont(11.f);
                ro->setColour(juce::Label::textColourId, settingsPalette::dimText());
                ro->setBounds(492, y_, 90, 22);
                s->onValueChange = [this, s, ro, sym]{
                    const int px = (int)(s->getValue() + 0.5);
                    owner->setSymbolCellWidth(sym, px);
                    ro->setText(juce::String(px) + " px", juce::dontSendNotification);
                };
                y_ += 26;
            }
            button("Reset all cell widths to auto", [this]{ owner->resetSymbolCellWidths(); });
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
            combo("Visualizer style", {"Bubbles (particles)", "Lines (flowing ribbons)", "Field (wandering vortices)"}, st->vizStyle,
                  [&](int v){ st->vizStyle = v; }); // v0.5.8: Jackson's second style, beside the effect choice
            toggle("Link background (share style, opacity and effect with linked bars)", st->linkBackground,
                   [&](bool b){ st->linkBackground = b; owner->onBgLinkToggled(); });
            note("Visualizer: MeterBar's particle brushes (transient-fired emitters shedding strands of dots) — or the Lines style: three flowing ribbons, one per band, spring-smoothed and near-still in quiet. Reactive glow: the MiniMeters-style blurred bloom - linked to MeterBar it pulses with the music; on its own it breathes gently. Both render UNDER the cells.");
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
        panel("Reset", [&]{
            button("Reset settings to defaults", [this]{ owner->resetSettingsToDefaults(); });
            note("ALL settings back to factory defaults - watchlist, alerts, theme, background style / opacity / effect, link toggles, poll interval and every cell width - applied immediately. (\"Reset layout to defaults\" above only restores the layout.) Preset files on disk are not deleted.");
        });
        panel("About", [&]{
            buttonRow("Check for updates", "GitHub",
                      [this]{ owner->checkForUpdates(); },
                      []{ juce::URL("https://github.com/Corporal8065/TickerBar").launchInDefaultBrowser(); });
            button("Open Log Folder", [this]{ owner->openLogFolder(); }); // v0.6.13: crash records live in Documents\\TickerBar
            note("Crash records: if TickerBar ever hard-crashes it leaves a crash-*.txt + .dmp in Documents\\TickerBar - send both to Milo.");
            note("TickerBar " + kVersion + " - prices are SPOT prices: Coinbase Exchange public API for Coinbase-listed coins, CoinGecko for HYPE. No account, no keys, read-only.");
            note("Detail chart: candlesticks + volume with 24h / 7d / 30d ranges. Coinbase caps one candles request at 300: 24h/7d are hourly, 30d is daily. CoinGecko (HYPE) supplies hourly closes only - no OHLC, no volume, no 30d - and the chart note says so.");
            note("Polls every N seconds (Window panel). If the feed drops, cells grey out and keep the last real prices, marked STALE - never fake-fresh numbers.");
            note("Title bar: Fear & Greed index (alternative.me) and BTC dominance (CoinGecko) share one rotating cell in the title bar, poll separately every ~5 minutes and go stale on their own clock - they never block prices.");
            note("Rotation sections: ETFs & Indexes, Top 100 S&P stocks, Bonds and Market Internals ride keyless public sources (CNBC quotes, FRED yields, Yahoo daily closes) on their own poller - a section outage never blocks prices. Market Internals are computed proxies from the Top 100 quotes, labelled as such - never official exchange breadth.");
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
    // v0.6.1 heal (Jackson's field report: BTC dominance stuck
    // huge): explicit widths stored by the pre-cap solver era —
    // before v0.5.9 capped solveCellWidths, an explicit width
    // could balloon to thousands of px — are honoured verbatim by
    // every reader except the layout clamps, so the raw store
    // stayed poisoned (the Sizes panel readout sat outside its
    // own slider band; the marquee strip carried a 600 px cell).
    // Clamp every stored explicit width into its store's writable
    // band once, here at load, and persist when anything moved.
    // 0 keeps its meaning (unset / auto) in both stores. Context
    // widths use their exact drag band [kContextCellMin,
    // kContextCellMax]; coin widths clamp at the same 600 ceiling
    // with the floor left at 56 — the vertical drag path's own
    // floor (adjustPairWidths is called with 56 in vertical
    // mode), so a legitimately dragged 56-89 px vertical width is
    // NOT rewritten; only the ballooned top end heals.
    {
        bool healed = false;
        for (auto& kv : state.cellWidths) {
            if (kv.second <= 0) continue;
            const int c = juce::jlimit(56, tickerbar::kSymbolCellMaxW, kv.second);
            if (c != kv.second) { kv.second = c; healed = true; }
        }
        for (int* w : { &state.fngCellWidth, &state.domCellWidth }) {
            if (*w <= 0) continue;
            const int c = juce::jlimit(tickerbar::kContextCellMin, tickerbar::kContextCellMax, *w);
            if (c != *w) { *w = c; healed = true; }
        }
        if (healed) saveSettings();
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
    // v0.6.8: the toolbar labels get the text pop (they sit over
    // the bar's transparent / visual background). The glyph
    // buttons below keep their single-draw icon LookAndFeel.
    wordmark_.setLookAndFeel(&popLabelLnf_);
    versionLabel_.setLookAndFeel(&popLabelLnf_);
    statusLabel_.setLookAndFeel(&popLabelLnf_);
    // v0.5.5: the standalone Reserve button is GONE — Reserve lives
    // in the Reposition dropdown now (Jackson's ask), as the ticked
    // "Reserve Screen Space" item. state.reserveScreenSpace itself
    // (and its settings key) is untouched, so a bar that had Reserve
    // on keeps it on.
    // v0.6.0: the toolbar is icon-only. Reposition / Arrange /
    // Restore / Presets / Stack / Dock / Minimize / Quit have no
    // buttons any more — they are items in the burger Menu
    // (showMainMenu), calling the same handlers; the tooltips below
    // preserve the old buttons' hover text on the surviving glyphs.
    linkBtn_.setClickingTogglesState(true);
    linkBtn_.setToggleState(state.linkEnabled, juce::dontSendNotification);
    linkBtn_.setTooltip("Link: snap, drag, dock and share the background with the other family bars (MeterBar / SessionBar) when their Link is on too");
    linkBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop); state.linkEnabled = linkBtn_.getToggleState(); saveSettings(); };
    settingsBtn_.setTooltip("Settings");
    settingsBtn_.onClick = [this]{
        uiClick(ClickSounds::Which::Pop);
        settingsWin_->content.build();
        settingsWin_->setVisible(true); settingsWin_->toFront(true);
    };
    burgerBtn_.setTooltip("Menu: presets, arrange / restore windows, stack linked bars, reposition, minimize, quit");
    burgerBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop); showMainMenu(); };
    // v0.6.1: the glyph buttons wear the emoji typeface (see
    // IconButtonLookAndFeel) — everything else keeps the stock font.
    for (auto* b : { &linkBtn_, &settingsBtn_, &burgerBtn_ }) b->setLookAndFeel(&iconLnf_);
    // v0.3.0 item 1: the update indicator starts hidden and lights
    // only when a check finds a newer release (MeterBar's pattern).
    updateBtn_.setVisible(false);
    updateBtn_.setLookAndFeel(&popButtonLnf_); // v0.6.8: text pop for the Update! TEXT button (not a glyph button)
    updateBtn_.onClick = [this]{ uiClick(ClickSounds::Which::Pop);
        if (pendingRelease_.ok && pendingRelease_.newer) showUpdatePanel(pendingRelease_); };
    for (auto* b : { &linkBtn_, &settingsBtn_, &updateBtn_, &burgerBtn_ }) addAndMakeVisible(b);
    applyChrome();
    // v0.2.0 (k): family link wiring. Bounds are the WINDOW's (the bar
    // is the window); the background packet is the family style
    // ordinal + opacity; the dock ordinal is the reserve machine's.
    linkClient_.getBounds = [this]{ return currentWindowBounds(); };
    linkClient_.setBounds = [this](juce::Rectangle<int> r){
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) {
            // v0.5.2: EVERY remote apply (stack / follow / snap /
            // group-dock) passes the visibility floor (core
            // WindowGeom, ensureRectVisible). Before this, the link
            // could move the bar off-screen a tick after the
            // (clamped) startup restore, and the position-save feed
            // kept it there — the defect behind Jackson's
            // 2026-10-31 "stuck somewhere i cannot see upon load
            // up" report on SessionBar, shared here by construction.
            // Meaningfully-visible targets pass through untouched.
            std::vector<tickerbar::IntRect> areas;
            for (auto& d : juce::Desktop::getInstance().getDisplays().displays) {
                auto ua = d.userArea;
                areas.push_back({ ua.getX(), ua.getY(), ua.getWidth(), ua.getHeight() });
            }
            auto safe = tickerbar::ensureRectVisible(
                { r.getX(), r.getY(), r.getWidth(), r.getHeight() }, areas);
            w->setBounds(safe.x, safe.y, safe.w, safe.h);
        }
    };
    linkClient_.getBg = [this]{
        // BLK7 (DESIGN §2/§3): theme ordinal = index into the shared
        // themeNameList (0-5 = the family list); scheme rides
        // verbatim (0-4 = the family list); palette floats verbatim.
        auto names = themeNameList();
        const int themeOrd = juce::jmax(0, names.indexOf(juce::String(state.themeName)));
        return BarLinkClient::Bg{ (int)state.backgroundStyle, state.backgroundOpacity, state.bgEffect == 1 ? (state.vizStyle == 1 ? 3 : state.vizStyle == 2 ? 4 : 1) : state.bgEffect,
                                  themeOrd, state.dataColorScheme,
                                  state.vizHue, state.vizSat, state.vizSpread, state.vizCycle };
    }; // BLK4: the visualizer style rides the effect ordinal (1 Bubbles / 3 Lines); BLK5 (v0.6.5): 4 = Field
    linkClient_.applyBg = [this](const BarLinkClient::Bg& bg){
        // Follower apply order (DESIGN §3): background (existing
        // mapping preserved exactly), then theme, then scheme, then
        // palette.
        state.backgroundStyle = (tickerbar::BackgroundStyle)juce::jlimit(0, 3, bg.style);
        state.backgroundOpacity = juce::jlimit(0.15f, 1.0f, bg.opacity); // v0.6.13 (DESIGN §8): stored clamp aligned 0.05 -> 0.15 (loader + every render path + the family)
        // BLK4: effect 3 = the visualizer with the Lines style,
        // effect 1 = the visualizer with Bubbles; 0/2 as before.
        if (bg.effect == 3) { state.bgEffect = 1; state.vizStyle = 1; }
        else if (bg.effect == 4) { state.bgEffect = 1; state.vizStyle = 2; } // BLK5: Field
        else if (bg.effect == 1) { state.bgEffect = 1; state.vizStyle = 0; }
        else state.bgEffect = juce::jlimit(0, 2, bg.effect);
        // Theme: ordinal -> local name via the shared list; an
        // out-of-range ordinal leaves the theme unchanged.
        if (bg.theme >= 0) {
            auto names = themeNameList();
            if (bg.theme < names.size()) { state.themeName = names[bg.theme].toStdString(); applyTheme(); }
        }
        // Scheme: family 0-4 apply verbatim; 5-7 (MeterBar-only
        // extensions) keep this bar's own scheme (DESIGN §2).
        if (bg.scheme >= 0 && bg.scheme <= 4) state.dataColorScheme = bg.scheme;
        // Palette: plain field assignment (read live per fx frame).
        state.vizHue = bg.palHue; state.vizSat = bg.palSat; state.vizSpread = bg.palSpread; state.vizCycle = bg.palCycle;
        saveSettings(); repaint(); syncFxPump(); // an adopted visualizer/glow must actually start (or stop)
    };
    // v0.6.13 (DESIGN §6d): appearance snapshot/restore on link
    // edges. ON (any cause — toggle, Link All): snapshot BEFORE the
    // first adoption can land. OFF: restore through the normal
    // settings-changed path (the file returns to the pre-link
    // appearance values) and, for a Link All-formed link, move back
    // to the pre-Link-All rect.
    linkClient_.onLinkEdge = [this](bool on){
        if (on) {
            if (!linkSnap_.held) {
                linkSnap_.held = true;
                linkSnap_.backgroundStyle = (int)state.backgroundStyle;
                linkSnap_.backgroundOpacity = state.backgroundOpacity;
                linkSnap_.bgEffect = state.bgEffect; linkSnap_.vizStyle = state.vizStyle;
                linkSnap_.themeName = state.themeName;
                linkSnap_.dataColorScheme = state.dataColorScheme;
                linkSnap_.vizHue = state.vizHue; linkSnap_.vizSat = state.vizSat;
                linkSnap_.vizSpread = state.vizSpread; linkSnap_.vizCycle = state.vizCycle;
            }
        } else {
            const bool hadSnap = linkSnap_.held;
            if (hadSnap) {
                state.backgroundStyle = (tickerbar::BackgroundStyle)juce::jlimit(0, 3, linkSnap_.backgroundStyle);
                state.backgroundOpacity = linkSnap_.backgroundOpacity;
                state.bgEffect = linkSnap_.bgEffect; state.vizStyle = linkSnap_.vizStyle;
                state.themeName = linkSnap_.themeName;
                state.dataColorScheme = linkSnap_.dataColorScheme;
                state.vizHue = linkSnap_.vizHue; state.vizSat = linkSnap_.vizSat;
                state.vizSpread = linkSnap_.vizSpread; state.vizCycle = linkSnap_.vizCycle;
                linkSnap_.held = false;
            }
            const bool hadRect = linkAllRectHeld_;
            const auto rect = linkAllRect_;
            linkAllRectHeld_ = false;
            if (hadSnap) settingsChanged(false); // normal path: applyTheme + persist + repaint
            if (hadRect) {
                // Move back AFTER the settings path (its save re-reads
                // the live rect); the bounds-change save that follows
                // the move persists the restored rect.
                if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setBounds(rect);
            }
        }
    };
    linkClient_.onLinkAllForming = [this]{
        if (!linkAllRectHeld_) { linkAllRect_ = currentWindowBounds(); linkAllRectHeld_ = true; }
    };
    linkClient_.setLinkEnabled = [this](bool on){
        // Exactly the Link toggle's own writes (state + persist).
        if (state.linkEnabled != on) {
            state.linkEnabled = on;
            linkBtn_.setToggleState(on, juce::dontSendNotification);
            saveSettings();
            syncFxPump(); // v0.6.13: the fx pump runs while linked (cell pulse)
        }
    };
    linkClient_.getDockedEdge = [this]{
        return reserveMachine_.reserved() ? (int)reserveMachine_.edge() : -1;
    };
    linkClient_.isLinked = [this]{ return state.linkEnabled; };
    linkClient_.bgLinkEnabled = [this]{ return state.linkBackground; }; // v0.5.6
    linkClient_.start();

    settingsWin_ = std::make_unique<TickerSettingsWindow>();
    settingsWin_->content.owner = this;
    settingsWin_->content.st = &state;
    settingsWin_->content.onChanged = [this]{ settingsChanged(false); };
    settingsWin_->content.onWatchlistChanged = [this]{ settingsChanged(true); };
    settingsWin_->content.build();

    // v0.6.11: the two context readings live in the TITLE BAR now,
    // merged into one small rotating cell (Jackson's ask) — the
    // row cells and their dividers (post-v0.3.0) are gone. The
    // show* toggles survive and govern which readings the rotator
    // includes; resized() places the cell and hides it when the
    // title bar is too narrow (core titleContextExtent).
    titleCtx_ = std::make_unique<TitleContextCell>(this);
    addAndMakeVisible(titleCtx_.get());

    // v0.6.14: the mode selector at the left end of the main strip —
    // the rotation sections made visible on the bar itself.
    // resized() decides its form (core modeSelectorFormH/V) and
    // placement; the component reads/writes rotationSection()
    // through the same entry as Settings and the Menu.
    modeSel_ = std::make_unique<ModeSelector>(this);
    addAndMakeVisible(modeSel_.get());

    // v0.3.2: the trailing divider — right of the last coin cell
    // (v0.6.11: the standing case, now that no context cell can
    // follow the coins). Dragging it resizes the window along the
    // bar axis; resized() places it and drives its visibility.
    trailSplit_ = std::make_unique<CellSplitter>();
    trailSplit_->owner = this;
    trailSplit_->onDragDelta = [this](int, int delta) { onTrailingSplitterDrag(delta); };
    addChildComponent(trailSplit_.get());

    mascot_ = std::make_unique<MascotView>();
    addChildComponent(mascot_.get()); // Anime theme only (applyTheme drives visibility)
    fxPump_.onTick = [this]{ stepFx(); };

    applyTheme();
    state.windowW = std::max(state.windowW, 160); // v0.4.0: free resize (Jackson's ask) — the old supported-width floor is gone; overflow scrolls instead
    rebuildCells();
    rebuildSectionCells(); // v0.6.11: no-op in the Crypto section
    client_.setWatchlist(state.watchlist);
    client_.setPollSec(state.pollSec);
    client_.onUpdate = [this]{ refreshData(); };
    client_.startPolling();
    contextClient_.onUpdate = [this]{ refreshContext(); };
    contextClient_.startPolling();
    sectionClient_.onUpdate = [this]{ refreshSections(); };
    sectionClient_.startPolling();
    refreshContext();
    refreshSections();
    syncScreenSpace();
    syncFxPump();
    startTimerHz(4);
    setSize(state.windowW, state.windowH);
    // v0.3.0 item 1: silent background update check on launch.
    if (tbupdate::configured()) startUpdateCheck(true);
    // v0.6.13: NEXT-LAUNCH CRASH NOTICE (family crash-log contract).
    // The crash handler (CrashHandler.h, installed in Main.cpp)
    // leaves crash-*.txt + .dmp in Documents\\TickerBar; a crash
    // file without a ".seen" sibling earns exactly one notice,
    // however many launches it survives. Deferred via callAsync so
    // the bar is fully up before the dialog appears. Either button
    // marks every unseen crash seen.
    juce::MessageManager::callAsync([this]{
        auto dir = tickerBarDocsDir();
        dir.createDirectory();
        // FALLBACK HARVEST (route C, DESIGN §9 amendment): heap-
        // corruption / fast-fail deaths bypass BOTH in-process
        // handlers (MeterBar v0.28.3 field finding) — but WER's own
        // dump lands in %LOCALAPPDATA%\CrashDumps. Copy any of this
        // app's dumps not already harvested into Documents\TickerBar
        // so the notice + folder flow below surfaces them too.
        {
            auto crashDumps = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                  .getChildFile("AppData").getChildFile("Local").getChildFile("CrashDumps");
            std::vector<std::string> docsNames;
            for (auto& f : dir.findChildFiles(juce::File::findFiles, false))
                docsNames.push_back(f.getFileName().toStdString());
            for (auto& f : crashDumps.findChildFiles(juce::File::findFiles, false)) {
                const std::string name = f.getFileName().toStdString();
                if (tickerbar::shouldHarvestDump(name, docsNames))
                    f.copyFileTo(dir.getChildFile(f.getFileName()));
            }
        }
        std::vector<std::string> names;
        for (auto& f : dir.findChildFiles(juce::File::findFiles, false))
            names.push_back(f.getFileName().toStdString());
        auto unseen = tickerbar::unseenCrashes(names);
        auto unseenDumps = tickerbar::unseenHarvestedDumps(names);
        // Dumps already paired with a crash-*.txt record are counted
        // by that record; the harvest notice covers ORPHAN dumps
        // (the filter-bypassed crashes — no .txt exists for them).
        if (unseen.empty() && unseenDumps.empty()) return;
        const int count = (int)unseen.size() + (int)unseenDumps.size();
        auto w = std::make_shared<juce::AlertWindow>("TickerBar crashed",
            "TickerBar crashed " + juce::String(count) + (count == 1 ? " time" : " times") +
            " since you last saw this notice.\nThe crash record" + (count == 1 ? " is" : "s are") +
            " in Documents\\TickerBar (crash-*.txt + .dmp).\nOpen the folder to send them to Milo?",
            juce::AlertWindow::WarningIcon);
        w->addButton("Open Log Folder", 1); w->addButton("Dismiss", 0);
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, unseen, unseenDumps](int r){
            for (auto& n : unseen) {
                auto seen = tickerBarDocsDir().getChildFile(juce::String(tickerbar::seenMarkerFor(n)));
                seen.create();
            }
            for (auto& n : unseenDumps) { // dismiss-once per harvested file
                auto seen = tickerBarDocsDir().getChildFile(juce::String(n) + ".seen");
                seen.create();
            }
            if (r == 1) openLogFolder();
        }));
    });
}

MainComponent::~MainComponent() {
    stopTimer();
    saveSettings();
    screenSpace_.release();
    contextClient_.stopThread(4500);
    sectionClient_.stopThread(4500);
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
        { &linkBtn_, 40 }, { &settingsBtn_, 40 },
        { &updateBtn_, 68 }, { &burgerBtn_, 40 },
    }; // v0.6.0: icon-only cluster (the pre-v0.6.0 text row ran Link 52 | Arrange 76 | Restore 76 | Preset 64 | Reposition 96 | Stack 56 | Dock 62 | Settings 72 | Update! 68 | _ 26 | Quit 46 — all but Link/Settings/Update! live in the Menu now)
    auto btnVisible = [this](juce::TextButton* b) { return b != &updateBtn_ || updateAvailable_; };
    updateBtn_.setVisible(updateAvailable_);
    auto r = area.reduced(8, 6);
    const bool crypto = rotationSection() == (int)tickerbar::RotationSection::Crypto;
    const int n = crypto ? (int)cells_.size() : 0;
    const bool anime = state.themeName == "Anime";
    // v0.6.11: only the active section's cells show — the coin
    // machinery (cells + dividers) in Crypto, the quote cells in a
    // rotation section. The splitters start hidden here; the
    // fitted/marquee placement below re-shows the live ones. The
    // context readings' visibility is the title rotator's business
    // (the title-strip layout further down), driven by the same
    // show* toggles as before.
    for (auto& c : cells_) c->setVisible(crypto);
    for (auto& sp : splitters_) sp->setVisible(false);
    for (auto& c : quoteCells_) c->setVisible(!crypto);
    if (trailSplit_) trailSplit_->setVisible(crypto && n > 0); // v0.3.2: nothing follows the coins any more
    const bool rotWanted = titleCtx_ != nullptr && (state.showFearGreed || state.showBtcDominance);
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
        // v0.6.11: the title-bar context rotator sits in the middle
        // span, against the icon cluster, when the core room rule
        // grants it space (titleContextExtent — below the minimum
        // it hides rather than crushing either cluster); the
        // status label keeps what remains of the span.
        if (rotWanted) {
            const int ext = tickerbar::titleContextExtent(strip.getWidth());
            if (ext > 0) {
                titleCtx_->setVisible(true);
                titlePlaced_ = true; // v0.6.12: the rotator's placed gate is this verdict
                titleCtx_->setBounds(strip.removeFromRight(ext).withSizeKeepingCentre(ext, 26));
                strip.removeFromRight(6);
            } else { titleCtx_->setVisible(false); titlePlaced_ = false; }
        } else if (titleCtx_) { titleCtx_->setVisible(false); titlePlaced_ = false; }
        statusLabel_.setBounds(strip.withSizeKeepingCentre(strip.getWidth(), 14));
        r.removeFromTop(4);
        // ---- cells row ----
        // v0.6.14: the mode selector claims the strip's LEFT end
        // before any cell layout (fitted or marquee) runs — chrome,
        // not content, so it never scrolls. The core room rule picks
        // the form from the strip width: expanded while the strip
        // keeps its working remainder, collapsed to the current-mode
        // button below that, hidden when even that would crush the
        // strip (Settings/Menu still switch).
        if (modeSel_) {
            const int natW = modeSel_->naturalWidth();
            const int colW = modeSel_->collapsedWidth();
            const auto form = tickerbar::modeSelectorFormH(r.getWidth(), natW, colW);
            const int ext = tickerbar::modeSelectorExtent(form, natW, colW);
            modeSel_->setForm(form);
            if (ext > 0) {
                modeSel_->setVisible(true);
                const auto selRect = r.removeFromLeft(ext);
                modeSel_->setBounds(selRect.withSizeKeepingCentre(ext, std::min(30, selRect.getHeight())));
                r.removeFromLeft(8);
            } else modeSel_->setVisible(false);
        }
        if (anime && mascot_) { mascot_->setBounds(r.removeFromRight(72)); r.removeFromRight(6); }
        // v0.4.0 overflow marquee: if the strip's natural content is
        // wider than the room left here, the whole strip scrolls
        // left in a seamless loop (positionStrip, stepped by the fx
        // pump) and the fitted layout below is skipped entirely.
        // v0.5.4: the COIN dividers stay live in the scroll —
        // positionStrip places them between their pairs and a drag
        // holds the scroll — after Jackson's field report that the
        // update had taken per-symbol sizing away (the v0.4.0 rule
        // hid every splitter for the whole marquee phase). The
        // trailing divider still hides: its sizing returns with
        // the fitted layout when the strip fits again. (v0.6.11:
        // the context dividers are gone with their cells.)
        {
            auto items = stripItems();
            std::vector<int> ext;
            for (auto& it : items) ext.push_back(it.extent);
            marqueeActive_ = tickerbar::marqueeNeeded(ext, 16, r.getWidth());
            marqueeCycle_ = marqueeActive_ ? tickerbar::marqueeCycle(ext, 16) : 0;
            if (marqueeActive_) {
                stripRect_ = r;
                if (trailSplit_) trailSplit_->setVisible(false);
                positionStrip();
            }
        }
        if (!marqueeActive_) {
        // The trailing divider at the coin block's right edge, in
        // the coin-divider idiom (16 px boundary: 3 air + 10 grip
        // + 3 air). v0.6.11: it is the standing case now (the
        // context cells it used to alternate with live in the
        // title bar), reserving the same 16 px boundary before
        // the coins solve, so the last coin fills up to IT.
        auto ctxBoundary = [&](CellSplitter* sp) {
            r.removeFromRight(3);
            sp->verticalBar = false;
            auto sb = r.removeFromRight(10);
            sp->setBounds(sb.getX(), r.getY(), 10, r.getHeight());
            r.removeFromRight(3);
        };
        if (crypto && n > 0) ctxBoundary(trailSplit_.get());
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
                    splitters_[(size_t)i]->setVisible(true); // back from a marquee phase
                    splitters_[(size_t)i]->setBounds(x + 3, r.getY(), 10, r.getHeight());
                }
                x += boundary;
            }
        }
        // v0.6.11: a rotation section's fitted layout — the quote
        // cells share the strip like coins (the core solver, no
        // stored widths, no dividers), each CAPPED at 190 px: a
        // 4-cell section on an ultrawide must not sprawl.
        if (!crypto && !quoteCells_.empty()) {
            const int m = (int)quoteCells_.size();
            std::vector<int> zeros((size_t)m, 0);
            auto widths = tickerbar::solveCellWidths(r.getWidth(), 16, zeros, 110);
            int x = r.getX();
            for (int i = 0; i < m; ++i) {
                const int w = std::min(widths[(size_t)i], 190);
                quoteCells_[(size_t)i]->setBounds(x, r.getY(), w, r.getHeight());
                x += w + 16;
            }
        }
        } // !marqueeActive_
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setResizeLimits(160, 48, 7680, 2160); // v0.4.0: free resize (was the minimumBarWidth()/92 supported floor)
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
        // v0.6.11: the title-bar context rotator gets its own row
        // under the cluster when the bar is wide enough to hold it
        // (the same core minimum as the horizontal strip; narrower
        // and it hides rather than crushing the row).
        if (rotWanted && r.getWidth() >= tickerbar::kTitleContextMinPx) {
            titleCtx_->setVisible(true);
            titlePlaced_ = true; // v0.6.12: as above
            const int tw = std::min(r.getWidth(), tickerbar::kTitleContextMaxPx);
            titleCtx_->setBounds(r.getX(), r.getY(), tw, 24);
            r.removeFromTop(28);
        } else if (titleCtx_) { titleCtx_->setVisible(false); titlePlaced_ = false; }
        // v0.6.14: the mode selector gets its own row under the
        // cluster / title-rotator rows — full-width row, so the core
        // room rule gates on the row's own width: expanded when all
        // five segments fit the row, collapsed to the current-mode
        // button on a narrow side bar, hidden below that.
        if (modeSel_) {
            const int natW = modeSel_->naturalWidth();
            const int colW = modeSel_->collapsedWidth();
            const auto form = tickerbar::modeSelectorFormV(r.getWidth(), natW, colW);
            const int ext = tickerbar::modeSelectorExtent(form, natW, colW);
            modeSel_->setForm(form);
            if (ext > 0) {
                modeSel_->setVisible(true);
                modeSel_->setBounds(r.getX(), r.getY(), ext, 26);
                r.removeFromTop(30);
            } else modeSel_->setVisible(false);
        }
        // v0.4.0 overflow marquee, vertical form: when the stack's
        // natural height overflows the room, it scrolls in a loop
        // (positionStrip) and the fitted stack below is skipped.
        {
            auto items = stripItems();
            std::vector<int> ext;
            for (auto& it : items) ext.push_back(it.extent);
            const int avail = r.getHeight() - ((anime && mascot_) ? 96 : 0);
            marqueeActive_ = tickerbar::marqueeNeeded(ext, 16, avail);
            marqueeCycle_ = marqueeActive_ ? tickerbar::marqueeCycle(ext, 16) : 0;
            if (marqueeActive_) {
                stripRect_ = { r.getX(), r.getY(), r.getWidth(), avail };
                // v0.5.4: coin dividers stay live in the scroll, as
                // in the horizontal branch — positionStrip places
                // them; only the trailing divider hides.
                if (trailSplit_) trailSplit_->setVisible(false);
                positionStrip();
                if (anime && mascot_) mascot_->setBounds(r.getX(), r.getBottom() - 96, r.getWidth(), 96);
            }
        }
        if (!marqueeActive_) {
        // cells stacked full-width; persisted widths become heights
        const int boundary = 16;
        auto placeRow = [&](juce::Component* c, int h) {
            c->setBounds(r.getX(), r.getY(), r.getWidth(), h);
            r.removeFromTop(h);
        };
        // v0.6.11: the trailing divider is the standing end of
        // the coin stack (the context rows it used to alternate
        // with live in the title bar); reserve its 16 px boundary
        // from the coin solver, and it lands in the strip the
        // loop leaves after the last row.
        const bool showTrail = crypto && n > 0;
        const int trailReserve = showTrail ? 16 : 0;
        if (n > 0) {
            std::vector<int> expl((size_t)n, 0);
            for (int i = 0; i < n; ++i) {
                auto it = state.cellWidths.find(cells_[(size_t)i]->symbol);
                if (it != state.cellWidths.end()) expl[(size_t)i] = it->second;
            }
            auto heights = tickerbar::solveCellWidths(r.getHeight() - trailReserve, boundary, expl, 56);
            for (int i = 0; i < n; ++i) {
                placeRow(cells_[(size_t)i].get(), heights[(size_t)i]);
                if (i + 1 < n && i < (int)splitters_.size()) {
                    splitters_[(size_t)i]->verticalBar = true;
                    splitters_[(size_t)i]->setVisible(true); // back from a marquee phase
                    splitters_[(size_t)i]->setBounds(r.getX(), r.getY() + 3, r.getWidth(), 10);
                } else if (showTrail && i == n - 1 && trailSplit_) {
                    trailSplit_->verticalBar = true;
                    trailSplit_->setBounds(r.getX(), r.getY() + 3, r.getWidth(), 10);
                }
                r.removeFromTop(boundary);
            }
        }
        // v0.6.11: a rotation section's fitted stack — full-width
        // quote rows sharing the height (the core solver), each
        // capped at 76 px so a short section does not sprawl.
        if (!crypto && !quoteCells_.empty()) {
            const int m = (int)quoteCells_.size();
            std::vector<int> zeros((size_t)m, 0);
            auto heights = tickerbar::solveCellWidths(r.getHeight(), boundary, zeros, 48);
            for (int i = 0; i < m; ++i) {
                const int h = std::min(heights[(size_t)i], 76);
                quoteCells_[(size_t)i]->setBounds(r.getX(), r.getY(), r.getWidth(), h);
                r.removeFromTop(h + boundary);
            }
        }
        if (anime && mascot_) mascot_->setBounds(r.removeFromTop(96));
        } // !marqueeActive_
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setResizeLimits(160, 240, 2160, 7680); // v0.4.0: free resize
    }
    syncScreenSpace(); // v0.2.0: a resize is a reserve-machine event
    syncFxPump();      // v0.4.0: a resize can start/stop the marquee, which rides the fx pump
}

// --- overflow marquee (v0.4.0). The strip's flow order — in the
// Crypto section, coins in watchlist order; in a v0.6.11 rotation
// section, the section's quote cells in table order. A coin's
// natural extent is its dragged width when one is stored, else a
// comfortable default; quote cells use their core natural extent.
// (The context readings no longer ride the strip — v0.6.11 moved
// them into the title bar.)
std::vector<MainComponent::StripItem> MainComponent::stripItems() const {
    std::vector<StripItem> out;
    if (rotationSection() != (int)tickerbar::RotationSection::Crypto) {
        for (auto& c : quoteCells_)
            out.push_back({ c.get(), vertical_ ? tickerbar::kQuoteCellExtentV : tickerbar::kQuoteCellExtentH });
        return out;
    }
    const int defCoin = vertical_ ? 96 : 160;
    const int minCoin = vertical_ ? 56 : 90; // the solver floors, mirrored
    for (auto& c : cells_) {
        int e = defCoin;
        auto it = state.cellWidths.find(c->symbol);
        if (it != state.cellWidths.end() && it->second > 0) e = std::max(it->second, minCoin);
        out.push_back({ c.get(), e });
    }
    return out;
}

// One marquee frame: every item at its scrolled position from the
// core math. Items outside the strip rect are simply clipped by the
// window; over one cycle each crosses the visible strip once.
// v0.5.4: the divider between coin items i and i+1 rides the scroll
// with its pair (core marqueeSplitterRel) and stays visible — the
// per-symbol sizing handles survive the marquee (Jackson's field
// report: the v0.4.0 marquee hid them all, so an overflowing strip
// could never be sized back down). A splitter whose pair straddles
// the loop seam or sits off-strip is simply clipped/not grabbable
// until the scroll brings it in, like the cells themselves.
void MainComponent::positionStrip() {
    if (!marqueeActive_ || marqueeCycle_ <= 0) return;
    auto items = stripItems();
    const int gap = 16, off = (int)std::floor(marqueeOffset_);
    const int nCoins = rotationSection() == (int)tickerbar::RotationSection::Crypto ? (int)cells_.size() : 0;
    int base = 0, idx = 0;
    for (auto& it : items) {
        const int rel = tickerbar::marqueePosition(base, it.extent, off, marqueeCycle_);
        if (vertical_) it.c->setBounds(stripRect_.getX(), stripRect_.getY() + rel, stripRect_.getWidth(), it.extent);
        else it.c->setBounds(stripRect_.getX() + rel, stripRect_.getY(), it.extent, stripRect_.getHeight());
        if (idx + 1 < nCoins && idx < (int)splitters_.size()) {
            auto* sp = splitters_[(size_t)idx].get();
            const int srel = tickerbar::marqueeSplitterRel(rel, it.extent, gap, 10);
            sp->verticalBar = vertical_;
            if (vertical_) sp->setBounds(stripRect_.getX(), stripRect_.getY() + srel, stripRect_.getWidth(), 10);
            else sp->setBounds(stripRect_.getX() + srel, stripRect_.getY(), 10, stripRect_.getHeight());
            sp->setVisible(true);
        }
        base += it.extent + gap;
        ++idx;
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

// --- trailing divider (v0.3.2). Shown only when no context cell
// follows the coin block, so its pair is the last coin cell
// against the bar end itself: dragging resizes the WINDOW along
// the bar axis (clamped to the same limits resized() hands the
// window), and the solver's fill rule hands the gained/lost
// extent to the last coin cell. The window's resized notification
// persists the new bounds (notifyWindowGeometryChanged) — no
// extra save is needed here. ---
void MainComponent::onTrailingSplitterDrag(int delta) {
    if (delta == 0) return;
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    if (w == nullptr) return;
    auto b = w->getBounds();
    if (vertical_) b.setHeight(tickerbar::trailingWindowExtent(b.getHeight(), delta, 240, 7680));
    else b.setWidth(tickerbar::trailingWindowExtent(b.getWidth(), delta, 160, 7680)); // v0.4.0: the free-resize limits
    w->setBounds(b);
}

// --- divider drag hold (v0.5.4): any CellSplitter press holds the
// overflow marquee still for the duration of the drag (stepFx's
// advance gate), so a divider grabbed in the scroll stays under the
// pointer while its pair's widths change. Outside the marquee the
// flag is simply never read. ---
void MainComponent::splitterDragBegin() { splitterDragHold_ = true; }
void MainComponent::splitterDragEnd() { splitterDragHold_ = false; }

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
    if (!titleCtx_) return;
    titleCtx_->setIncluded(state.showFearGreed, state.showBtcDominance);
    // v0.6.12: the rendered face always comes from the core
    // rotator — pushed here on every refresh (4 Hz timer + every
    // context poll) so the widget can never sit on a stale face.
    titleRotator_.setIncluded(state.showFearGreed, state.showBtcDominance);
    titleCtx_->setFace(titleRotator_.showingFng());
    const auto fg = contextClient_.fng();
    if (fg.value) {
        const int band = tickerbar::fngBandIndex(fg.value->value);
        juce::Colour col = band == 0 ? theme_.bad : band == 1 ? theme_.warn
                         : band == 2 ? theme_.text : theme_.good;
        titleCtx_->setReading(true, juce::String(fg.value->value), juce::String(fg.value->classification), col,
                              true, tickerbar::contextStale(true, fg.ageSec));
    } else {
        titleCtx_->setReading(true, "--", "loading...", theme_.dim, false, false);
    }
    const auto dom = contextClient_.dominance();
    if (dom.value) {
        titleCtx_->setReading(false, juce::String(tickerbar::formatDominance(*dom.value)), "BTC share of market",
                              theme_.accent2, true, tickerbar::contextStale(true, dom.ageSec));
    } else {
        titleCtx_->setReading(false, "--", "loading...", theme_.dim, false, false);
    }
}

// --- v0.6.11 rotation sections --------------------------------------
// The switcher's single entry (Settings combo + Menu > Rotation —
// and since v0.6.14 the on-bar mode selector): persist, rebuild the
// section's cells when the section changed, reflow. Every surface
// reads the same state.rotationSection, so a change made on any one
// shows on the others (the selector paints rotationSection() and is
// repainted below; the combo and the submenu read it when built).
// A preset load lands here too, via settingsChanged's section
// guard below.
void MainComponent::setRotationSection(int idx) {
    state.rotationSection = juce::jlimit(0, tickerbar::kRotationSectionCount - 1, idx);
    if (state.rotationSection != builtSection_) {
        rebuildSectionCells();
        refreshSections();
    }
    saveSettings();
    resized();
    if (modeSel_) modeSel_->repaint(); // v0.6.14: the active mark follows at once
    repaint();
}

void MainComponent::rebuildSectionCells() {
    quoteCells_.clear();
    sectionDefs_ = tickerbar::sectionCells(rotationSection());
    builtSection_ = rotationSection();
    for (auto& def : sectionDefs_) {
        auto cell = std::make_unique<QuoteCell>(this);
        cell->title = juce::String(def.display);
        juce::String tip = juce::String(def.label);
        switch (def.kind) {
            case tickerbar::SectionCellKind::Quote:
                tip += (!def.cnbc.empty() && def.cnbc[0] == '.')
                           ? " - index reading, CNBC public quotes. Day change vs the previous close."
                           : " - CNBC public quotes. Day change vs the previous close.";
                break;
            case tickerbar::SectionCellKind::Yield:
                tip += " - FRED daily series; the cell prints the observation date (publication lags about a business day). Change is vs the previous observation, in basis points.";
                break;
            default:
                tip += " - computed from the Top 100 quote batch: a proxy, not an official exchange figure.";
                break;
        }
        cell->setTooltip(tip);
        addAndMakeVisible(cell.get());
        quoteCells_.push_back(std::move(cell));
    }
    resized();
}

// SectionClient -> quote cells. Every reading keeps the family's
// honesty shapes: a value greys + STALE on its own source's clock
// (3x its poll), a source that has never landed shows "--", and
// after repeated failures the cell says "unavailable" outright.
void MainComponent::refreshSections() {
    if (builtSection_ != rotationSection()) rebuildSectionCells();
    using namespace tickerbar;
    for (size_t i = 0; i < quoteCells_.size() && i < sectionDefs_.size(); ++i) {
        auto& cell = *quoteCells_[i];
        const auto& def = sectionDefs_[i];
        switch (def.kind) {
            case SectionCellKind::Quote: {
                auto st = sectionClient_.quote(def.display);
                if (!st.value) { cell.setWaiting(st.attempted && st.failures >= 2 ? "unavailable" : "loading..."); break; }
                const auto& q = *st.value;
                const bool isIndex = !def.cnbc.empty() && def.cnbc[0] == '.';
                const juce::String val = isIndex ? juce::String(formatIndexValue(q.last))
                                                 : juce::String(formatPrice(q.last));
                auto pct = changePct(q.last, q.prevClose);
                const juce::String pctText = pct ? juce::String(formatPct(*pct)) : juce::String();
                const juce::Colour pctCol = pct ? gainColour(*pct >= 0) : theme_.dim;
                juce::String sub;
                if (q.high > 0 && q.low > 0)
                    sub = "H " + juce::String(isIndex ? formatIndexValue(q.high) : formatPrice(q.high)) +
                          "  L " + juce::String(isIndex ? formatIndexValue(q.low) : formatPrice(q.low));
                else sub = juce::String(q.name);
                const bool stale = feedStatus(true, st.ageSec,
                                              builtSection_ == (int)RotationSection::Top100 ? kTop100StaleSec
                                                                                             : kSectionStaleSec) != FeedStatus::Live;
                cell.setData(val, pctText, pctCol, sub, q.dailyCloses,
                             pct ? gainColour(*pct >= 0) : theme_.accent2, stale);
                break;
            }
            case SectionCellKind::Yield: {
                auto st = sectionClient_.yieldFor(def.fredId);
                if (!st.value) { cell.setWaiting(st.attempted && st.failures >= 2 ? "unavailable" : "loading..."); break; }
                const auto& y = *st.value;
                juce::String pctText;
                juce::Colour pctCol = theme_.dim;
                if (y.prevPct > 0) {
                    const long bp = std::llround((y.valuePct - y.prevPct) * 100.0);
                    pctText = (bp > 0 ? "+" : "") + juce::String(bp) + " bp";
                    pctCol = gainColour(bp >= 0);
                }
                const bool stale = feedStatus(true, st.ageSec, kFredStaleSec) != FeedStatus::Live;
                cell.setData(juce::String(y.valuePct, 2) + "%", pctText, pctCol,
                             juce::String(y.date), y.history, theme_.accent2, stale);
                break;
            }
            case SectionCellKind::InternalsAdv:
            case SectionCellKind::InternalsMedian:
            case SectionCellKind::InternalsEqWt: {
                auto st = sectionClient_.internals();
                if (!st.value) { cell.setWaiting(st.attempted && st.failures >= 2 ? "unavailable" : "loading..."); break; }
                const auto& p = *st.value;
                const bool stale = feedStatus(true, st.ageSec, kTop100StaleSec) != FeedStatus::Live;
                if (def.kind == SectionCellKind::InternalsAdv) {
                    cell.setData(juce::String(p.advancersPct, 1) + "%", "", theme_.dim,
                                 "computed proxy - " + juce::String(p.advancers) + " of " + juce::String(p.valid) + " up",
                                 {}, theme_.accent2, stale);
                } else {
                    const double v = def.kind == SectionCellKind::InternalsMedian ? p.medianChangePct
                                                                                   : p.equalWeightChangePct;
                    cell.setData(juce::String(formatPct(v)), "", theme_.dim,
                                 "computed proxy - Top 100 quotes", {}, gainColour(v >= 0), stale);
                }
                break;
            }
        }
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
    // v0.6.13: keep the crash handler's state snapshot current —
    // the last state the app can honestly know (feed, link, section).
    {
        juce::String snap;
        snap << "feed: " << (feed_ == tickerbar::FeedStatus::Live ? "live" : feed_ == tickerbar::FeedStatus::Stale ? "stale" : "down") << "\n";
        snap << "link: " << (state.linkEnabled ? "on" : "off") << "\n";
        snap << "watchlist: " << juce::String((int)state.watchlist.size()) << " symbols\n";
        snap << "rotation section: " << juce::String(rotationSection()) << "\n";
        crashhandler::setStateSnapshot(snap.toRawUTF8());
    }
    pollUpdater();
    refreshStatus();
    refreshContext(); // age-based stale transitions need no new data to show
    refreshSections(); // v0.6.11: same for the section cells
    // v0.6.12: the title rotator's dwell runs through the core
    // TitleRotator, fed REAL elapsed time (clamped to 1 s so a
    // stalled message thread cannot fast-forward faces when it
    // recovers). The gates are the two facts that matter — both
    // readings included, and layout placed the cell (titlePlaced_,
    // resized()'s own verdict) — instead of v0.6.11's conjunction
    // of separately-maintained flags (owner accumulator + the
    // widget's re-read visibility flag + the widget's duplicate
    // inclusion flags), where any one false flag froze the face
    // forever: Jackson's v0.6.11 field report. refreshContext()
    // (above, every tick) already pushed the machine's face into
    // the cell; a flip here pushes the new face at once.
    {
        const juce::int64 nowMs = juce::Time::currentTimeMillis();
        double dtSec = titleLastTickMs_ > 0 ? (nowMs - titleLastTickMs_) / 1000.0 : 0.25;
        titleLastTickMs_ = nowMs;
        dtSec = juce::jlimit(0.0, 1.0, dtSec);
        titleRotator_.setIncluded(state.showFearGreed, state.showBtcDominance);
        if (titleRotator_.advance(dtSec, state.showFearGreed && state.showBtcDominance,
                                  titlePlaced_ && titleCtx_ != nullptr))
            titleCtx_->setFace(titleRotator_.showingFng());
    }
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
    // v0.6.11: a section change that arrived by any path other
    // than setRotationSection (a preset load, the factory reset)
    // still rebuilds the section's cells.
    if (state.rotationSection != builtSection_) {
        rebuildSectionCells();
        refreshSections();
    }
    if (linkBtn_.getToggleState() != state.linkEnabled)
        linkBtn_.setToggleState(state.linkEnabled, juce::dontSendNotification);
    saveSettings();
    syncScreenSpace();
    syncFxPump();
    repaint();
}

// v0.3.2: Settings > "Reset layout to defaults" — the core helper
// restores only the layout fields (cells visible, widths default,
// factory coin order); the watchlist-too apply rebuilds the cells
// in the restored order, persists, and defers a settings-sheet
// rebuild so the sheet shows the reset state.
// v0.5.6: the Settings > Sizes panel's read/write path into
// state.cellWidths — the single store the on-bar dividers also
// write (adjustPairWidths path above), so panel and dividers
// always reflect each other. The write tail mirrors the divider
// drag exactly: persist, then reflow.
int MainComponent::symbolCellWidth(const std::string& symbol) const {
    auto it = state.cellWidths.find(symbol);
    return it == state.cellWidths.end() ? 0 : it->second;
}
void MainComponent::setSymbolCellWidth(const std::string& symbol, int px) {
    state.cellWidths[symbol] = tickerbar::clampSymbolCellWidth(px);
    saveSettings();
    resized();
}
void MainComponent::resetSymbolCellWidths() {
    state.cellWidths.clear();
    state.fngCellWidth = 0; // v0.5.9: the reset covers the context widths too
    state.domCellWidth = 0;
    saveSettings();
    resized();
    rebuildSettingsDeferred(); // the sheet's sliders/readouts re-read the cleared store
}
// v0.5.6: "Link background" flipped in Settings. Turning it ON
// re-stamps the current background so linked peers adopt it now;
// turning it OFF needs nothing (the client's publish/adopt paths
// consult the toggle). Persistence rides settingsChanged's save.
void MainComponent::onBgLinkToggled() {
    linkClient_.notifyBgLinkChanged();
}

void MainComponent::resetLayoutToDefaults() {
    tickerbar::resetLayoutToDefaults(state);
    settingsChanged(true);
}

// v0.6.8: Settings > Reset > "Reset settings to defaults" — the
// FULL factory reset (core resetAllSettingsToDefaults), applied
// live through the ordinary settings-changed path: theme, cells,
// poll interval, link toggles and the fx pump all re-derive from
// the defaulted state, the result persists, and the deferred
// sheet rebuild (inside settingsChanged's watchlist path) makes
// the open sheet show the factory values. The bar's current
// window rect survives: saveSettings re-reads it into the state
// as it persists, exactly like the Menu's "New Preset (defaults)".
void MainComponent::resetSettingsToDefaults() {
    tickerbar::resetAllSettingsToDefaults(state);
    lastPresetName_.clear();
    settingsChanged(true);
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

// (The v0.2.0 on-bar Dock button — cycle the preferred edge and
// snap — went at v0.6.0: the Reposition submenu's ticked edge
// items are the same set-and-snap in direct form.)

void MainComponent::snapToDockEdge() {
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    if (!w) return;
    auto display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(w->getBounds());
    if (display == nullptr) return;
    auto area = display->totalArea;
    auto b = w->getBounds();
    switch (state.dockEdge) {
        // v0.6.10 (Jackson, 2026-11-03): Reposition to Top/Bottom
        // must also stretch the bar to the FULL WIDTH of the screen
        // it is on — x to the screen's left edge, width to the
        // screen's width — not just move it to the edge at whatever
        // width it happened to have. Height unchanged; Left/Right
        // keep their existing behaviour.
        case tickerbar::DockEdge::Top:    b.setY(area.getY());
                                          b.setX(area.getX());
                                          b.setSize(area.getWidth(), b.getHeight()); break;
        case tickerbar::DockEdge::Bottom: b.setY(area.getBottom() - b.getHeight());
                                          b.setX(area.getX());
                                          b.setSize(area.getWidth(), b.getHeight()); break;
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
    // v0.6.11: the context readings contribute nothing here any
    // more — they live in the title bar, whose own room rule
    // (core titleContextExtent) hides the rotator rather than
    // letting it crush the strip. The trailing divider's 16 px
    // boundary is the row's only fixed extra.
    const int ctxW = 16;
    const int chrome = 16 /*outer pad*/ + 24 /*status dot*/ + 92 + 44 + 70 /*wordmark + version + status*/
                     + 76 + 52 + 76 + 76 + 64 + 56 + 62 + 72 + 68 + 26 + 46 + 44 /*the v0.3.0 cluster + its gaps*/
                     + ctxW;
    return chrome + cellsW;
}

// MainWindow moved/resized feed — the reserve machine's event source
// (plus settings persistence for the new position).
// v0.6.7 DRAG-LEAD FIX: this feed never called the link client's
// geometry notify — the ONLY place a link drag session is stamped —
// so TickerBar could FOLLOW a linked drag (its link timer does
// that) but could never LEAD one: dragging TickerBar moved it
// alone while dragging MeterBar moved the stack (Jackson's field
// report 2026-11-01, against his standing rule that dragging ANY
// linked bar moves the whole stack). MeterBar and SessionBar call
// the notify in their twins of this function; now TickerBar does.
void MainComponent::notifyWindowGeometryChanged() {
    syncScreenSpace();
    linkClient_.notifyLocalGeometry();
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
    if (titleCtx_) titleCtx_->repaint();
    for (auto& c : quoteCells_) c->repaint();
    resized(); // the mascot cell (Anime) and cell softening depend on the theme
    repaint();
}
void MainComponent::applyChrome() {
    wordmark_.setColour(juce::Label::textColourId, theme_.accent);
    versionLabel_.setColour(juce::Label::textColourId, theme_.dim);
    statusLabel_.setColour(juce::Label::textColourId, theme_.dim);
    for (auto* b : { &linkBtn_, &settingsBtn_, &burgerBtn_ }) {
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
// v0.6.13 (DESIGN §6): LINK ALL — one click links all three bars
// and stacks them MeterBar -> TickerBar -> SessionBar at the top
// or bottom edge, all at the same full width, MeterBar as link
// parent. This host only stamps the request (§6a): the area is the
// bounds (user area) of the display containing MeterBar's current
// slot rect when MeterBar's slot is live, else the display this
// bar is on. Every client (incl. this one) consumes it on its tick.
// Menu surface (DESIGN §6e as amended by Jackson 2026-11-03
// ~14:03 PT): option B ONLY — "Link All → Top" / "Link All →
// Bottom"; no auto-edge (MeterBar's edge) item.
// =====================================================================
void MainComponent::doLinkAll(int edgeOrdinal) {
    if (edgeOrdinal != 0 && edgeOrdinal != 1) return;
    juce::Rectangle<int> area;
    juce::Rectangle<int> meterRect;
    auto& displays = juce::Desktop::getInstance().getDisplays();
    if (linkClient_.peerSlotRect(tickerbar::kLinkAppMeter, meterRect)) {
        area = displays.getDisplayContaining(meterRect.getCentre()).userArea;
    } else {
        auto b = currentWindowBounds();
        area = displays.getDisplayContaining(b.getCentre()).userArea;
    }
    if (area.isEmpty()) return;
    linkClient_.requestLinkAll(edgeOrdinal, area);
}

// =====================================================================
// v0.3.0 item 2: named presets (MeterBar's menu pattern). A preset
// is a FULL configuration — window position included — and loading
// one moves the bar (clamped on-screen via the core guard).
// =====================================================================
// --- the burger Menu (v0.6.0) — the compressed toolbar. Its two
// submenus are the old standalone dropdowns, verbatim:
// --- reposition dropdown (v0.5.5) — MeterBar's showRepositionMenu
// ported (Jackson: "i like the drop down box you did for meter bar
// can you add that to session and ticker?"), mapped item by item:
//   MeterBar "At the Top" / "At the Bottom" -> the same two items,
//     plus "At the Left" / "At the Right": this bar docks to all
//     four edges (MeterBar's world has two), each ticked when it is
//     the current dock edge. Picking one sets the edge, saves, and
//     snaps (the retired Dock button cycled to the same place).
//   MeterBar "Pin Above Other Windows" -> identical: state.pinned
//     + setAlwaysOnTop + save.
//   MeterBar "Move to Display" submenu -> identical shape; the
//     move centres the bar on the target display, then
//     snapToDockEdge settles it onto the current dock edge there.
//   MeterBar "Stick" -> NOT ported: there is no stick state in
//     this bar (its pinned + dock model is the equivalent surface).
//   MeterBar "Show Reposition Hint" -> NOT ported: this bar has
//     no hint label; the strip's middle is the feed status label.
//   NEW, all three bars (Jackson's ask): "Reserve Screen Space",
//     ticked by state.reserveScreenSpace — the standalone Reserve
//     button is retired; the item toggles the same state through
//     the same syncScreenSpace path, and the persisted setting is
//     the same key, so an existing Reserve-on bar keeps it on.
// Like MeterBar's menu, the Reposition (and Stack) actions are
// deferred until the popup has fully closed (its 2026-10-13 field
// lesson: moving the window from inside the menu callback wedges
// it). One result space covers the whole menu tree, so the ids
// below are unified (the old standalone menus each started at 1).
enum { mnArrange = 1, mnRestore, mnStack, mnMinimize, mnQuit, mnCheckUpdates,
       mnPresetNew = 10, mnPresetSave, mnPresetSaveAs, mnPresetFolder, mnPresetLoadBase = 1000,
       mnEdgeTop = 20, mnEdgeBottom, mnEdgeLeft, mnEdgeRight, mnPin, mnReserve, mnDisplayBase = 2000,
       mnSectionBase = 3000, // v0.6.11: + RotationSection ordinal
       mnOpenLogFolder = 4000, mnLinkAllTop = 4001, mnLinkAllBottom = 4002 }; // v0.6.13
juce::PopupMenu MainComponent::buildRepositionSubMenu() {
    juce::PopupMenu m;
    const int edge = (int)state.dockEdge;
    m.addItem(mnEdgeTop, "At the Top", true, edge == (int)tickerbar::DockEdge::Top);
    m.addItem(mnEdgeBottom, "At the Bottom", true, edge == (int)tickerbar::DockEdge::Bottom);
    m.addItem(mnEdgeLeft, "At the Left", true, edge == (int)tickerbar::DockEdge::Left);
    m.addItem(mnEdgeRight, "At the Right", true, edge == (int)tickerbar::DockEdge::Right);
    m.addItem(mnPin, "Pin Above Other Windows", true, state.pinned);
    m.addSeparator();
    m.addItem(mnReserve, "Reserve Screen Space", true, state.reserveScreenSpace);
    juce::PopupMenu disp;
    auto displays = juce::Desktop::getInstance().getDisplays().displays;
    for (int i = 0; i < displays.size(); ++i) disp.addItem(mnDisplayBase + i, "Display " + juce::String(i + 1));
    m.addSubMenu("Move to Display", disp);
    return m;
}
void MainComponent::doMoveToDisplay(int idx) {
    auto displays = juce::Desktop::getInstance().getDisplays().displays;
    if (idx < 0 || idx >= displays.size()) return;
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    if (!w) return;
    auto b = w->getBounds();
    b.setCentre(displays[(size_t)idx].userArea.getCentre());
    w->setBounds(b);
    snapToDockEdge(); // settle onto the current dock edge of the new display
}

juce::PopupMenu MainComponent::buildPresetSubMenu(juce::Array<juce::File>& filesOut) {
    juce::PopupMenu m;
    m.addItem(mnPresetNew, "New Preset (defaults)");
    m.addItem(mnPresetSave, "Save", lastPresetName_.isNotEmpty());
    m.addItem(mnPresetSaveAs, "Save As...");
    m.addItem(mnPresetFolder, "Open Preset Folder");
    m.addSeparator();
    filesOut = presetio::listPresets();
    for (int i = 0; i < filesOut.size(); ++i) m.addItem(mnPresetLoadBase + i, filesOut[i].getFileNameWithoutExtension());
    return m;
}
void MainComponent::showMainMenu() {
    juce::PopupMenu m;
    juce::Array<juce::File> presetFiles;
    m.addSubMenu("Presets", buildPresetSubMenu(presetFiles));
    m.addItem(mnArrange, "Arrange Windows");
    m.addItem(mnRestore, "Restore Windows");
    m.addItem(mnStack, "Stack Linked Bars");
    // v0.6.13 (DESIGN §6e, option B per Jackson 2026-11-03): the
    // Link All submenu — exactly two items.
    {
        juce::PopupMenu la;
        la.addItem(mnLinkAllTop, juce::String::fromUTF8("Link All \xe2\x86\x92 Top"));
        la.addItem(mnLinkAllBottom, juce::String::fromUTF8("Link All \xe2\x86\x92 Bottom"));
        m.addSubMenu("Link All", la);
    }
    m.addSubMenu("Reposition", buildRepositionSubMenu());
    // v0.6.11: the rotation-section switcher (the Settings >
    // Rotation combo is the same choice over the same field).
    {
        juce::PopupMenu rot;
        for (int i = 0; i < tickerbar::kRotationSectionCount; ++i)
            rot.addItem(mnSectionBase + i, tickerbar::rotationSectionName(i), true, i == rotationSection());
        m.addSubMenu("Rotation", rot);
    }
    m.addSeparator();
    m.addItem(mnCheckUpdates, "Check for updates");
    m.addItem(mnOpenLogFolder, "Open Log Folder"); // v0.6.13: crash records, reachable outside Settings
    m.addItem(mnMinimize, "Minimize");
    m.addItem(mnQuit, "Quit");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(burgerBtn_), [this, presetFiles](int r) {
        // Preset actions run directly, as the old Preset dropdown's
        // callback did.
        if (r == mnPresetNew) {
            auto keepTheme = state.themeName;
            state = tickerbar::TickerState{};
            state.themeName = keepTheme;
            lastPresetName_.clear();
            settingsChanged(true);
        }
        else if (r == mnPresetSave) savePresetInteractive(false);
        else if (r == mnPresetSaveAs) savePresetInteractive(true);
        else if (r == mnPresetFolder) presetio::presetFolder().revealToUser();
        else if (r >= mnPresetLoadBase && r < mnPresetLoadBase + presetFiles.size()) loadPresetFile(presetFiles[r - mnPresetLoadBase]);
        else if (r >= mnSectionBase && r < mnSectionBase + tickerbar::kRotationSectionCount) setRotationSection(r - mnSectionBase);
        else if (r == mnArrange) doArrange();
        else if (r == mnRestore) doRestore();
        else if (r == mnCheckUpdates) checkForUpdates();
        else if (r == mnOpenLogFolder) openLogFolder();
        else if (r == mnLinkAllTop || r == mnLinkAllBottom) {
            // Deferred like Stack: stamping from inside the popup
            // callback can move windows while the menu is closing.
            const int edge = (r == mnLinkAllTop) ? 0 : 1;
            juce::MessageManager::callAsync([this, edge]{ doLinkAll(edge); });
        }
        else if (r == mnMinimize) { if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
                                        if (auto* peer = w->getPeer()) peer->setMinimised(true); }
        else if (r == mnQuit) juce::JUCEApplication::getInstance()->systemRequestedQuit();
        else if (r == mnStack || (r >= mnEdgeTop && r <= mnReserve) || r >= mnDisplayBase) {
            // Stack + every Reposition action defer until the popup
            // has fully closed — the old dropdown's discipline.
            juce::MessageManager::callAsync([this, r] {
                if (r >= mnDisplayBase) { doMoveToDisplay(r - mnDisplayBase); return; }
                switch (r) {
                    case mnEdgeTop: case mnEdgeBottom: case mnEdgeLeft: case mnEdgeRight:
                        state.dockEdge = (tickerbar::DockEdge)(r - mnEdgeTop);
                        saveSettings();
                        snapToDockEdge();
                        break;
                    case mnPin:
                        state.pinned = !state.pinned;
                        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setAlwaysOnTop(state.pinned);
                        saveSettings();
                        break;
                    case mnReserve:
                        state.reserveScreenSpace = !state.reserveScreenSpace;
                        saveSettings();
                        syncScreenSpace();
                        break;
                    case mnStack:
                        doStackAll();
                        break;
                    default: break;
                }
            });
        }
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
    tbupdate::checkLatestAsync(updateState_, tickerbar::kVersionString);
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
    // v0.6.13 (DESIGN §7): the pump also runs while LINKED — the
    // cell pulse is driven from MeterBar's packet even when no
    // background effect is selected (SessionBar's gate shape).
    fxPump_.setRunning(state.bgEffect != 0 || state.linkEnabled || (mascot_ && mascot_->isVisible()) || marqueeActive_);
}
void MainComponent::stepFx() {
    const float dt = 1.f / 30.f;
    fxClock_ += dt;
    float bands[3] = { 0.f, 0.f, 0.f }, level = 0.f;
    float pkt[5];
    float balance = 0.f; // v0.6.5: MeterBar's stereo balance steers the distributed emitters
    const bool havePeerAudio = state.linkEnabled && linkClient_.peerAudio(tickerbar::kLinkAppMeter, pkt);
    if (havePeerAudio) {
        bands[0] = pkt[0]; bands[1] = pkt[1]; bands[2] = pkt[2]; level = pkt[3]; balance = pkt[4];
    } else {
        tickerbar::ambientBands(fxClock_, bands, level);
    }
    // v0.6.13 (DESIGN §7): the CELLS' pulse envelope — fed ONLY
    // from the real linked packet (ambient bands never move the
    // cells, so unlinked rendering is untouched), through core
    // CellPulse's attack/release smoothing; plus the BLK7 discrete
    // pulse edge: a fresh pulseSeq from MeterBar's slot fires a
    // coloured impulse. Cells repaint while lit, and once as it dies.
    {
        const float silent[3] = { 0.f, 0.f, 0.f };
        cellPulse_.update(dt, havePeerAudio ? bands : silent, havePeerAudio ? level : 0.f);
        unsigned pseq = 0; int pcol = 0; float pstr = 0.f;
        if (state.linkEnabled && linkClient_.peerPulse(tickerbar::kLinkAppMeter, pseq, pcol, pstr)
            && pseq != seenPulseSeq_) {
            seenPulseSeq_ = pseq;
            cellPulse_.fire(pcol, pstr);
        }
        const bool lit = cellPulse_.glow() > 0.01f || cellPulse_.pump() > 0.01f;
        if (lit || cellPulseWasLit_) {
            for (auto& c : cells_) c->repaint();
            for (auto& c : quoteCells_) c->repaint();
        }
        cellPulseWasLit_ = lit;
    }
    // v0.5.8: advance whichever visualizer style is selected —
    // the particle engine (Bubbles) or the ribbon model (Lines).
    if (state.bgEffect == 1) { if (state.vizStyle == 1) vizLines_.update(dt, bands); else if (state.vizStyle == 2) vizField_.update(dt, bands, vizSettings(), balance); else viz_.update(dt, bands, vizSettings(), balance); }
    else if (state.bgEffect == 2) glow_.update(dt, bands, level);
    if (mascot_ && mascot_->isVisible()) {
        const long long now = juce::Time::currentTimeMillis();
        mascot_->excited = mascotAnim_.excited(now);
        mascot_->bobPx = mascotAnim_.bobPx(now);
        mascot_->repaint();
    }
    // v0.4.0: advance the overflow marquee (~45 px/s, news-ticker
    // pace). Paused while a drag gesture owns the strip, so a cell
    // reorder or a divider drag never fights the scroll — v0.5.4:
    // divider drags are real marquee citizens now (the dividers
    // stay live in the scroll), so their hold joins the gate.
    if (marqueeActive_ && dragMode_ == 0 && !splitterDragHold_) {
        marqueeOffset_ += 45.0 * (double)dt;
        if (marqueeCycle_ > 0 && marqueeOffset_ >= (double)marqueeCycle_) marqueeOffset_ -= (double)marqueeCycle_;
        positionStrip();
    }
    if (state.bgEffect != 0) repaint();
}
void MainComponent::drawVisualizerBackground(juce::Graphics& g) {
    // MeterBar's composite: black -> blurred particle layer (the
    // colour wash covers the whole background) -> crisp dots.
    auto bounds = getLocalBounds();
    if (bounds.isEmpty()) return;
    // v0.5.0: LINKED, the particle field spans the GROUP — the
    // union of the linked bars' screen rects — and this bar draws
    // only its slice of it (core fieldMap; the glow below has
    // sampled in group space since item 17). Unlinked or alone the
    // map is the bar-local one and the render is v0.4.0's, unchanged.
    auto group = linkClient_.groupRect();
    auto own = getScreenBounds();
    if (group.isEmpty() || own.isEmpty()) group = own;
    // v0.6.6: LINKED, the base under the effect is the SHARED styled
    // background at the shared opacity — the same base the parent
    // paints — so the stack reads as one surface under the particles
    // (SessionBar v0.4.14 twin). Before this the base was an opaque
    // black fillAll: the adopted style was dropped entirely in
    // effect mode and the adopted opacity could never show through.
    // Unlinked/alone the base is the historical opaque black,
    // byte-for-byte the old render.
    // v0.6.8: "linked" is decided by LINK STATE (a live linked peer
    // exists — BarLinkClient::hasLiveLinkedPeer), not by the
    // v0.6.6 geometry test (group != own): a peer whose rect
    // momentarily coincided with this bar's, or was not yet
    // stamped, dropped the bar back to opaque black even though
    // the adopted background had landed in state.
    const bool linkedGroup = linkClient_.hasLiveLinkedPeer();
    if (linkedGroup)
        fillStyledBackground(g, bounds.toFloat(), state.backgroundStyle, theme_,
                             juce::jlimit(0.15f, 1.0f, state.backgroundOpacity));
    else
        g.fillAll(juce::Colours::black);
    const int iw = juce::jlimit(1, 480, bounds.getWidth() / 2);
    const int ih = juce::jlimit(1, 270, bounds.getHeight() / 2);
    if (!vizCache_.isValid() || vizCache_.getWidth() != iw || vizCache_.getHeight() != ih) {
        vizCache_ = juce::Image(juce::Image::RGB, iw, ih, true);
        vizBlurFrame_ = 0; // v0.6.4: layer resized — force a blur recompute below
    }
    auto vs = vizSettings();
    const auto fm = tickerbar::fieldMap(
        tickerbar::IntRect{ own.getX(), own.getY(), own.getWidth(), own.getHeight() },
        tickerbar::IntRect{ group.getX(), group.getY(), group.getWidth(), group.getHeight() }, iw, ih);
    {
        juce::Graphics ig(vizCache_);
        ig.fillAll(juce::Colours::black);
        if (state.vizStyle == 1) {
            // v0.5.8 LINES: three filled ribbons from the core
            // model — top edge along centreY - halfThick, back
            // along centreY + halfThick — mapped through the same
            // group field map as the dots, so a linked strip reads
            // as one set of lines across the seams. Alpha is the
            // model's silence gate: quiet = nothing drawn.
            const float alpha = vizLines_.visibility();
            if (alpha > 0.01f) {
                constexpr int n = tickerbar::VisualizerLines::kPoints;
                for (int band = 0; band < 3; ++band) {
                    auto c = vizLines_.colorFor(band, vs);
                    juce::Path path;
                    for (int i = 0; i < n; ++i) {
                        float px, py;
                        tickerbar::fieldToLocal(fm, (float)i / (float)(n - 1),
                                                vizLines_.centreY(band, i) - vizLines_.halfThick(band, i), px, py);
                        if (i == 0) path.startNewSubPath(px, py); else path.lineTo(px, py);
                    }
                    for (int i = n - 1; i >= 0; --i) {
                        float px, py;
                        tickerbar::fieldToLocal(fm, (float)i / (float)(n - 1),
                                                vizLines_.centreY(band, i) + vizLines_.halfThick(band, i), px, py);
                        path.lineTo(px, py);
                    }
                    path.closeSubPath();
                    ig.setColour(juce::Colour(c.r, c.g, c.b).withAlpha(alpha * 0.85f));
                    ig.fillPath(path);
                }
            }
        } else if (state.vizStyle == 2) {
            // v0.6.5 FIELD: the vortex dots, drawn through the
            // exact same dot path (and blur envelope) as Bubbles.
            for (auto& pt : vizField_.particles()) {
                auto c = vizField_.colorFor(pt, vs);
                ig.setColour(juce::Colour(c.r, c.g, c.b));
                float px, py;
                tickerbar::fieldToLocal(fm, pt.x, pt.y, px, py);
                const float r = pt.size * 1.35f;
                if (!tickerbar::fieldDotVisible(px, py, r, iw, ih)) continue; // another bar's slice of the field
                ig.fillEllipse(px - r, py - r, r * 2.f, r * 2.f);
            }
        } else {
            for (auto& pt : viz_.particles()) {
                auto c = viz_.colorFor(pt, vs);
                ig.setColour(juce::Colour(c.r, c.g, c.b));
                float px, py;
                tickerbar::fieldToLocal(fm, pt.x, pt.y, px, py);
                const float r = pt.size * 1.35f; // v0.4.0: plumper dots — softer, rounder blobs after the blur
                if (!tickerbar::fieldDotVisible(px, py, r, iw, ih)) continue; // another bar's slice of the field
                ig.fillEllipse(px - r, py - r, r * 2.f, r * 2.f);
            }
        }
    }
    // v0.6.4 (LAG FIX — MeterBar v0.23.1 twin): the blur used to be a
    // full 15x15 Gaussian over the whole half-res layer EVERY frame,
    // inside paint — with the synced background on, that constant
    // paint cost is what starves drag events across the linked stack
    // (it runs whether or not audio is flowing; pausing MeterBar does
    // not stop it). Now the convolution runs on a QUARTER-res copy
    // (capped 240x135) with the spatially equivalent kernel (9 taps /
    // sigma 2.75 == the old 15 / 5.5 at half scale), every 2nd frame,
    // held between recomputes and upscaled on draw. The crisp pass
    // below still renders every frame.
    {
        const int bw = juce::jmax(1, iw / 2), bh = juce::jmax(1, ih / 2);
        const bool blurSizeChanged = !vizBlurSrc_.isValid() || vizBlurSrc_.getWidth() != bw || vizBlurSrc_.getHeight() != bh
                                  || !vizBlur_.isValid() || vizBlur_.getWidth() != bw || vizBlur_.getHeight() != bh;
        if (blurSizeChanged) {
            vizBlurSrc_ = juce::Image(juce::Image::RGB, bw, bh, true);
            vizBlur_ = juce::Image(juce::Image::RGB, bw, bh, true);
        }
        if (blurSizeChanged || (vizBlurFrame_++ % 2) == 0) {
            {
                juce::Graphics bg(vizBlurSrc_);
                bg.drawImage(vizCache_, 0, 0, bw, bh, 0, 0, iw, ih); // downsample
            }
            vizBlur_ = vizBlurSrc_;
            vizBlur_.duplicateIfShared(); // the convolution reads source while writing dest — they must not share pixels
            juce::ImageConvolutionKernel kernel(9);
            kernel.createGaussianBlur(2.75f); // v0.6.4: == v0.5.8's 15/5.5 melt, at quarter res
            kernel.applyToImage(vizBlur_, vizBlurSrc_, vizBlur_.getBounds());
        }
    }
    const float op = juce::jlimit(0.15f, 1.0f, state.backgroundOpacity);
    g.setOpacity(op);
    g.drawImage(vizBlur_, bounds.toFloat());
    g.setOpacity(0.30f * op); // v0.5.8: crisp pass backed off further (was 0.45) so the wash, not the dots, is what reads
    g.drawImage(vizCache_, bounds.toFloat());
    g.setOpacity(1.0f);
    // v0.4.0: a dark scrim over the whole effect — the darker,
    // moodier tone Jackson asked for; text stays on top of it.
    // v0.6.8: LINKED, the scrim scales with the shared opacity —
    // it is drawn OVER the shared base, so at a low parent opacity
    // an unscaled scrim kept darkening the bar as if opacity were
    // still high and the bars stopped lining up. Unlinked the
    // scrim is the historical 0.18, unchanged.
    g.setColour(juce::Colours::black.withAlpha(linkedGroup ? 0.18f * op : 0.18f));
    g.fillRect(bounds);
}
void MainComponent::drawGlowBackground(juce::Graphics& g) {
    // The reactive glow: sample the core field in GROUP space (the
    // union of the linked bars' screen rects), so a linked block
    // reads as one continuous surface; unlinked, the group is the
    // bar itself. Near-black base, soft blooms, pinstripes on top.
    auto bounds = getLocalBounds();
    if (bounds.isEmpty()) return;
    auto group = linkClient_.groupRect();
    auto own = getScreenBounds();
    if (group.isEmpty() || own.isEmpty()) group = own;
    // v0.6.6: LINKED, the base under the glow is the shared styled
    // background at the shared opacity (see drawVisualizerBackground;
    // SessionBar v0.4.14 twin); unlinked/alone it stays the
    // historical opaque black.
    // v0.6.8: "linked" from LINK STATE, not group geometry (see
    // drawVisualizerBackground).
    const bool linkedGroup = linkClient_.hasLiveLinkedPeer();
    const float bgOp = juce::jlimit(0.15f, 1.0f, state.backgroundOpacity);
    if (linkedGroup)
        fillStyledBackground(g, bounds.toFloat(), state.backgroundStyle, theme_, bgOp);
    else
        g.fillAll(juce::Colours::black);
    const int iw = juce::jlimit(8, 240, bounds.getWidth() / 4);
    const int ih = juce::jlimit(8, 135, bounds.getHeight() / 4);
    if (!glowCache_.isValid() || glowCache_.getWidth() != iw || glowCache_.getHeight() != ih)
        glowCache_ = juce::Image(juce::Image::RGB, iw, ih, true);
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
    g.setOpacity(bgOp);
    g.drawImage(glowCache_, bounds.toFloat());
    const float groupW = group.getWidth() > 0 ? (float)group.getWidth() : 1.f;
    g.setColour(juce::Colours::white);
    for (int lx = 0; lx < bounds.getWidth(); lx += 2) {
        const float gx = ((float)own.getX() + (float)lx - (float)group.getX()) / groupW;
        const float a = tickerbar::GlowField::pinstripe(gx);
        // v0.6.8: LINKED, the pinstripes (drawn OVER the shared
        // base, like the visualizer's scrim) scale with the shared
        // opacity too — unscaled they stayed at full strength at a
        // low parent opacity. Unlinked: historical strength.
        if (a > 0.f) { g.setOpacity(linkedGroup ? a * bgOp : a); g.drawVerticalLine(lx, 0.f, (float)bounds.getHeight()); }
    }
    g.setOpacity(1.0f);
}
