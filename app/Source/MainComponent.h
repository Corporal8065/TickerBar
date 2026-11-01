#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <tickerbar/State.hpp>
#include <tickerbar/Alerts.hpp>
#include <tickerbar/Model.hpp>
#include <tickerbar/ReserveState.hpp>
#include <tickerbar/DataColors.hpp>
#include <tickerbar/Visualizer.hpp>
#include <tickerbar/Glow.hpp>
#include <tickerbar/Mascot.hpp>
#include <tickerbar/Glyphs.hpp> // v0.6.2: icon glyphs as explicit UTF-8 bytes
#include "TickerBarVersion.h" // v0.5.3: generated — single-source version
#include <map>
#include <memory>
#include <vector>
#include "Theme.h"
#include "PriceClient.h"
#include "ContextClient.h"
#include "ScreenSpace.h"
#include "ClickPlayer.h"
#include "ClickSounds.h"
#include "ToastNotifier.h"
#include "BarLink.h" // v0.2.0 (k): family link client
#include "Updater.h"
#include "UpdaterConfig.h"
class CoinCell;
class ContextCell;
class TickerSettingsWindow;
class MascotView;
class CellSplitter;

// v0.6.1: the icon buttons' private LookAndFeel — stock V4 in every
// respect EXCEPT the font. The v0.6.0 glyph buttons rendered as
// tofu on real Windows: the default button typeface has no emoji
// glyphs. Segoe UI Emoji carries them (Segoe UI Symbol is the
// fallback face); if neither exists at runtime the default sans is
// used, which is the pre-fix behaviour. Applied per button, so
// every other button (Update! included) keeps the existing font.
class IconButtonLookAndFeel : public juce::LookAndFeel_V4 {
public:
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override {
        const float h = juce::jmin(16.0f, (float)buttonHeight * 0.62f);
        for (auto* name : { "Segoe UI Emoji", "Segoe UI Symbol" })
            if (juce::Font::findAllTypefaceNames().contains(name))
                return juce::Font(juce::FontOptions(juce::String(name), h, juce::Font::plain));
        return juce::Font(juce::FontOptions(h));
    }
};

// 30 Hz pump for the animated backgrounds + mascot (v0.3.0): the
// bar's own timer runs at 4 Hz for data/stale bookkeeping, far too
// slow for particles — this pump runs only while an effect is on or
// the mascot is visible.
class FxPump : private juce::Timer {
public:
    std::function<void()> onTick;
    void setRunning(bool run) {
        if (run) { if (!isTimerRunning()) startTimerHz(30); }
        else stopTimer();
    }
private:
    void timerCallback() override { if (onTick) onTick(); }
};

// The bar itself: a MeterBar-style TOP TOOLBAR STRIP (v0.3.0 — the
// v0.2.1 cluster sat vertically centred mid-bar; Jackson's field
// report) holding wordmark + version + status on the left and the
// control cluster on the right. v0.6.0: the cluster is ICON-ONLY —
// Link | Settings | Update! | Menu (☰) — and Presets, Arrange,
// Restore, Stack, Reposition, Minimize and Quit live in the Menu.
// (The pre-v0.6.0 text cluster ran Link | Arrange | Restore |
// Preset | Reposition | Stack | Dock | Settings | Update! | _ |
// Quit; Reserve had already moved into Reposition at v0.5.5.)
// Below the strip, one cell per watched coin
// plus the context cells and (Anime theme) the mascot. Borderless
// and always-on-top (state.pinned); drag any non-interactive surface
// to move it; drag a coin cell sideways to reorder it (v0.3.0);
// position + every setting persist to the settings JSON on change.
class MainComponent : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;
    tickerbar::TickerState state; // loaded in the ctor; MainWindow positions itself from it

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    // --- services for the child components (cells / detail / settings) ---
    const Theme& theme() const { return theme_; }
    PriceClient& client() { return client_; }
    void windowDragStarted(const juce::MouseEvent& e);
    void windowDragged(const juce::MouseEvent& e);
    // Cell drag arbitration (v0.3.0): a press on a cell may become a
    // window drag OR a cell reorder, decided by the drag direction.
    void cellDragBegin(juce::Component* cell, const juce::MouseEvent& e);
    void cellDragMove(juce::Component* cell, const juce::MouseEvent& e);
    bool cellDragEnd(juce::Component* cell, const juce::MouseEvent& e); // true = the gesture was a reorder (suppress the click)
    // Divider drags (v0.5.4): a press on any CellSplitter holds the
    // overflow marquee still for the drag (the stepFx advance gate
    // reads splitterDragHold_ alongside dragMode_).
    void splitterDragBegin();
    void splitterDragEnd();
    void openDetail(const juce::String& symbol);
    tickerbar::AlertLevels levelsFor(const std::string& symbol) const;
    void setAlerts(const std::string& symbol, double above, double below);
    void settingsChanged(bool watchlistToo); // apply + persist; watchlistToo also rebuilds cells and the client list
    void resetLayoutToDefaults();       // v0.3.2: Settings button — layout fields only (core resetLayoutToDefaults)
    int  symbolCellWidth(const std::string& symbol) const; // v0.5.6: Sizes panel read — stored explicit width, 0 = auto
    void setSymbolCellWidth(const std::string& symbol, int px); // v0.5.6: Sizes panel write — the SAME cellWidths store the dividers drag
    void resetSymbolCellWidths();       // v0.5.6: Sizes panel reset — every symbol back to auto width (v0.5.9: context widths too)
    int  contextCellWidth(bool fng) const; // v0.5.9: Sizes panel read — stored fng/dom cell width, 0 = default extent
    void setContextCellWidth(bool fng, int px); // v0.5.9: Sizes panel write — the SAME fng/dom fields the context dividers drag
    void onBgLinkToggled();             // v0.5.6: Settings "Link background" flipped — re-stamp for peers when it turns on
    void rebuildSettingsDeferred();
    void notifyWindowGeometryChanged(); // MainWindow moved/resized feed (reserve-machine events, v0.2.0)
    int minimumBarWidth() const;        // supported-width floor: every cell's % fully visible (Layout.hpp)
    void snapToDockEdge();
    void doArrange();
    void doRestore();
    void doStackAll();                  // v0.3.0 item 12: one-click stack of the linked family
    void checkForUpdates();             // v0.3.0 item 1: manual update check (Settings > About)
    juce::Colour gainColour(bool up) const; // v0.3.0 item 6: data colour-scheme pair
    juce::Colour dataRgb(const tickerbar::Rgb& c) const { return juce::Colour((juce::uint8)c.r, (juce::uint8)c.g, (juce::uint8)c.b); }
    bool fxActive() const { return state.bgEffect != 0; } // cells soften their panels over an animated background
    bool vertical() const { return vertical_; }
    void uiClick(ClickSounds::Which w); // v0.3.0 item 5: settings-toggle-gated UI click
    juce::String screenSpaceStatus() const { return juce::String(screenSpace_.statusInfo()); }
    juce::StringArray universeNames() const;

private:
    void timerCallback() override;
    void rebuildCells();
    void rebuildSplitters();
    void onContextSplitterDrag(bool coinsPair, int delta); // coinsPair: A (coins<->context); else B (FNG<->DOM)
    void onTrailingSplitterDrag(int delta); // v0.3.2: the bar-end divider — resizes the window along the bar axis
    void refreshData();
    void refreshContext();
    void refreshStatus();
    void syncScreenSpace();
    void saveSettings();
    void applyTheme();
    void applyChrome();
    juce::Rectangle<int> currentWindowBounds() const;
    // updater (v0.3.0 item 1, MeterBar's flow ported)
    void startUpdateCheck(bool fromLaunch);
    void pollUpdater();
    void presentUpdateResult(const tbupdate::ReleaseInfo& r);
    void showUpdatePanel(const tbupdate::ReleaseInfo& r);
    void showPrivateRepoFallback(const juce::String& detail);
    void startUpdateDownload();
    // v0.6.0: the burger Menu — holds the compressed toolbar:
    // the Presets submenu (v0.3.0 item 2), Arrange / Restore /
    // Stack items, the Reposition submenu (the v0.5.5 dropdown —
    // dock edges, pin, move-to-display, and the Reserve toggle),
    // then Minimize / Quit. The builders reproduce the old
    // standalone menus verbatim; showMainMenu's callback handles
    // the unified item ids.
    void showMainMenu();
    juce::PopupMenu buildPresetSubMenu(juce::Array<juce::File>& filesOut);
    juce::PopupMenu buildRepositionSubMenu();
    void doMoveToDisplay(int idx);
    void savePresetInteractive(bool saveAs);
    void loadPresetFile(const juce::File& f);
    void applyWindowRectFromState(); // preset/startup position apply, clamped on-screen (core WindowGeom)
    // animated backgrounds (v0.3.0 items 7/17) + mascot (item 18)
    void stepFx();
    // overflow marquee (v0.4.0): when the strip's natural content is
    // wider than the bar, it scrolls (core Layout marquee math).
    // stripItems() is the flow order shared by resized() and the
    // per-frame repositioning; positionStrip() applies one frame.
    struct StripItem { juce::Component* c; int extent; };
    std::vector<StripItem> stripItems() const;
    void positionStrip();
    void drawVisualizerBackground(juce::Graphics& g);
    void drawGlowBackground(juce::Graphics& g);
    tickerbar::VisualizerSettings vizSettings() const;
    tickerbar::GlowSettings glowSettings() const;
    void syncFxPump();

    Theme theme_ = midnightKidsTheme();
    PriceClient client_;
    ContextClient contextClient_;
    std::unique_ptr<ContextCell> fngCell_, domCell_;
    std::unique_ptr<MascotView> mascot_;
    BarLinkClient linkClient_{ tickerbar::kLinkAppTicker }; // v0.2.0 (k)
    ScreenSpace screenSpace_;
    tickerbar::ReserveMachine reserveMachine_;
    ToastNotifier toastNotifier_;
    ClickPlayer clickPlayer_;
    ClickSounds clicks_;
    std::vector<std::unique_ptr<CoinCell>> cells_;
    std::vector<std::unique_ptr<CellSplitter>> splitters_;
    // Context-cell dividers (post-v0.3.0): A divides the coin block
    // from the nearest visible context cell, B divides Fear & Greed
    // from BTC dominance. Created once; resized() places them and
    // drives their visibility from the show* toggles.
    std::unique_ptr<CellSplitter> ctxSplitA_, ctxSplitB_;
    // Trailing divider (v0.3.2): sits right of the last coin cell
    // when NO context cell follows it; dragging it resizes the
    // window along the bar axis and the solver's fill rule hands
    // the change to the last coin cell.
    std::unique_ptr<CellSplitter> trailSplit_;
    std::map<std::string, tickerbar::AlertTracker> trackers_;
    std::map<std::string, double> lastPrices_; // mascot tick triggers (item 18)
    std::unique_ptr<TickerSettingsWindow> settingsWin_;

    juce::Label wordmark_, versionLabel_, statusLabel_;
    IconButtonLookAndFeel iconLnf_; // v0.6.1: declared before the buttons so it outlives them
    // v0.6.0: icon-only cluster (see the class comment). The
    // Update! indicator stays text on purpose — it is temporary
    // and must be unmissable.
    // v0.6.2: glyph text is built from the core's explicit UTF-8
    // byte escapes via fromUTF8 — the raw literals used up to
    // v0.6.1 were decoded as Windows-1252 by MSVC and reached the
    // buttons as mojibake (see tickerbar/Glyphs.hpp).
    juce::TextButton linkBtn_ { juce::String::fromUTF8(tickerbar::glyphLinkUtf8()) },
                     settingsBtn_ { juce::String::fromUTF8(tickerbar::glyphSettingsUtf8()) },
                     burgerBtn_ { juce::String::fromUTF8(tickerbar::glyphMenuUtf8()) },
                     updateBtn_ { "Update!" };
    juce::ComponentDragger dragger_;
    juce::Rectangle<int> lastSavedBounds_;
    juce::Rectangle<float> dotRect_ { 14.f, 10.f, 10.f, 10.f }; // status dot (painted; set by resized)
    tickerbar::FeedStatus feed_ = tickerbar::FeedStatus::Down;
    bool vertical_ = false;
    juce::String lastPresetName_;
    // reorder gesture state
    juce::Component* dragCell_ = nullptr;
    juce::Rectangle<int> dragCellStartBounds_;
    juce::Point<int> dragStartScreen_;
    int dragMode_ = 0; // 0 undecided, 1 window drag, 2 cell reorder
    // updater state
    std::shared_ptr<tbupdate::SharedState> updateState_;
    bool updateCheckPending_ = false, updateCheckFromLaunch_ = false, updateAvailable_ = false;
    tbupdate::ReleaseInfo pendingRelease_;
    std::unique_ptr<juce::URL::DownloadTask> updateDownload_;
    bool updateDownloadPending_ = false;
    juce::File updateInstallerFile_;
    // marquee state (v0.4.0): stripRect_ is the cell strip's area as
    // computed by resized(); the offset advances in stepFx() while
    // the content overflows and no drag is in progress.
    juce::Rectangle<int> stripRect_;
    bool marqueeActive_ = false;
    int marqueeCycle_ = 0;
    double marqueeOffset_ = 0.0;
    bool splitterDragHold_ = false; // v0.5.4: a divider drag is in progress — the marquee holds still for it
    // fx state
    FxPump fxPump_;
    tickerbar::VisualizerEngine viz_;
    tickerbar::VisualizerLines vizLines_; // v0.5.8: the Lines style's ribbon model (pure core)
    tickerbar::GlowField glow_;
    tickerbar::MascotAnim mascotAnim_;
    juce::Image vizCache_, vizBlur_, glowCache_;
    double fxClock_ = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
