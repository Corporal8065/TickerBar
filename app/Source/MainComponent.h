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
// control cluster on the right in MeterBar's order:
// Reserve | Link | Arrange | Restore | Preset | Stack | Dock |
// Settings | Update! | _ | Quit. Below it, one cell per watched coin
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
    void openDetail(const juce::String& symbol);
    tickerbar::AlertLevels levelsFor(const std::string& symbol) const;
    void setAlerts(const std::string& symbol, double above, double below);
    void settingsChanged(bool watchlistToo); // apply + persist; watchlistToo also rebuilds cells and the client list
    void rebuildSettingsDeferred();
    void notifyWindowGeometryChanged(); // MainWindow moved/resized feed (reserve-machine events, v0.2.0)
    int minimumBarWidth() const;        // supported-width floor: every cell's % fully visible (Layout.hpp)
    void cycleDockEdge();               // the on-bar Dock button: Top -> Bottom -> Left -> Right
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
    // presets (v0.3.0 item 2)
    void showPresetMenu();
    void savePresetInteractive(bool saveAs);
    void loadPresetFile(const juce::File& f);
    void applyWindowRectFromState(); // preset/startup position apply, clamped on-screen (core WindowGeom)
    // animated backgrounds (v0.3.0 items 7/17) + mascot (item 18)
    void stepFx();
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
    std::map<std::string, tickerbar::AlertTracker> trackers_;
    std::map<std::string, double> lastPrices_; // mascot tick triggers (item 18)
    std::unique_ptr<TickerSettingsWindow> settingsWin_;

    juce::Label wordmark_, versionLabel_, statusLabel_;
    juce::TextButton reserveBtn_ { "Reserve" }, linkBtn_ { "Link" }, arrangeBtn_ { "Arrange" },
                     restoreBtn_ { "Restore" }, presetBtn_ { "Preset" }, stackBtn_ { "Stack" },
                     dockBtn_ { "Top" }, settingsBtn_ { "Settings" }, updateBtn_ { "Update!" },
                     minBtn_ { "_" }, quitBtn_ { "Quit" };
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
    // fx state
    FxPump fxPump_;
    tickerbar::VisualizerEngine viz_;
    tickerbar::GlowField glow_;
    tickerbar::MascotAnim mascotAnim_;
    juce::Image vizCache_, vizBlur_, glowCache_;
    double fxClock_ = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
