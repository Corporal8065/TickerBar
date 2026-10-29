#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <tickerbar/State.hpp>
#include <tickerbar/Alerts.hpp>
#include <tickerbar/Model.hpp>
#include <map>
#include <vector>
#include "Theme.h"
#include "PriceClient.h"
#include "ContextClient.h"
#include "ScreenSpace.h"
#include "ClickPlayer.h"
class CoinCell;
class ContextCell;
class TickerSettingsWindow;

// The bar itself: wordmark + feed status on the left, one cell per
// watched coin across the middle, Reserve / Settings / minimise / quit
// on the right. Borderless and always-on-top (state.pinned); drag any
// non-interactive surface to move it; position + every setting persist
// to the settings JSON on change.
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
    void openDetail(const juce::String& symbol);
    tickerbar::AlertLevels levelsFor(const std::string& symbol) const;
    void setAlerts(const std::string& symbol, double above, double below);
    void settingsChanged(bool watchlistToo); // apply + persist; watchlistToo also rebuilds cells and the client list
    void rebuildSettingsDeferred();
    void snapToDockEdge();
    void doArrange();
    void doRestore();
    juce::String screenSpaceStatus() const { return juce::String(screenSpace_.statusInfo()); }
    juce::StringArray universeNames() const;

private:
    void timerCallback() override;
    void rebuildCells();
    void refreshData();
    void refreshContext();
    void refreshStatus();
    void syncScreenSpace();
    void saveSettings();
    juce::Rectangle<int> currentWindowBounds() const;

    Theme theme_ = midnightKidsTheme();
    PriceClient client_;
    ContextClient contextClient_;
    std::unique_ptr<ContextCell> fngCell_, domCell_;
    ScreenSpace screenSpace_;
    ClickPlayer clickPlayer_;
    std::vector<std::unique_ptr<CoinCell>> cells_;
    std::map<std::string, tickerbar::AlertTracker> trackers_;
    std::unique_ptr<TickerSettingsWindow> settingsWin_;

    juce::Label wordmark_, versionLabel_, statusLabel_;
    juce::TextButton reserveBtn_ { "Reserve" }, settingsBtn_ { "Settings" },
                     minBtn_ { "-" }, quitBtn_ { "X" };
    juce::ComponentDragger dragger_;
    juce::Rectangle<int> lastSavedBounds_;
    tickerbar::FeedStatus feed_ = tickerbar::FeedStatus::Down;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
