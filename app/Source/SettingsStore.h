#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <tickerbar/State.hpp>
// Settings persistence: ONE JSON file in the per-user app data folder
// (<AppData>/TickerBar/settings.json on Windows), via JUCE's built-in
// JSON — the MeterBar presets idiom, minus the multi-preset machinery
// (a ticker bar has one config, not a preset library). Missing keys
// fall back to TickerState defaults, so a settings file from an older
// build can never lose a field or invent a value.
namespace settingsstore {
inline juce::File settingsFile() {
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                 .getChildFile("TickerBar").getChildFile("settings.json");
    f.getParentDirectory().createDirectory();
    return f;
}
inline juce::String levelsToString(double v) {
    return std::isnan(v) ? juce::String() : juce::String(v, 8);
}
inline juce::var toVar(const tickerbar::TickerState& s) {
    auto* o = new juce::DynamicObject();
    auto set = [&](const char* k, juce::var v){ o->setProperty(k, v); };
    juce::StringArray wl; for (auto& sym : s.watchlist) wl.add(juce::String(sym));
    set("watchlist", wl);
    auto* alertsObj = new juce::DynamicObject();
    for (auto& kv : s.alerts) {
        auto* lv = new juce::DynamicObject();
        lv->setProperty("above", levelsToString(kv.second.above));
        lv->setProperty("below", levelsToString(kv.second.below));
        alertsObj->setProperty(juce::String(kv.first), juce::var(lv));
    }
    set("alerts", juce::var(alertsObj));
    set("alertSound", s.alertSound);
    set("alertToasts", s.alertToasts);
    set("linkEnabled", s.linkEnabled);
    set("theme", juce::String(s.themeName));
    set("bgStyle", (int)s.backgroundStyle);
    set("bgOpacity", s.backgroundOpacity);
    set("pinned", s.pinned);
    set("reserveSpace", s.reserveScreenSpace);
    set("dockEdge", (int)s.dockEdge);
    set("winX", s.windowX); set("winY", s.windowY); set("winW", s.windowW); set("winH", s.windowH);
    set("pollSec", s.pollSec);
    set("uiVer", s.uiVer);
    set("clickSounds", s.clickSounds);
    set("dataColorScheme", s.dataColorScheme);
    set("bgEffect", s.bgEffect);
    set("vizHue", s.vizHue); set("vizSat", s.vizSat); set("vizSpread", s.vizSpread); set("vizCycle", s.vizCycle);
    auto* widthsObj = new juce::DynamicObject();
    for (auto& kv : s.cellWidths) widthsObj->setProperty(juce::String(kv.first), kv.second);
    set("cellWidths", juce::var(widthsObj));
    return juce::var(o);
}
inline tickerbar::TickerState fromVar(const juce::var& v) {
    tickerbar::TickerState s; // defaults first; stored values override
    if (!v.isObject()) return s;
    auto g = [&](const char* k){ return v.getProperty(k, {}); };
    auto gb = [&](const char* k, bool d){ auto x = g(k); return x.isVoid() ? d : (bool)x; };
    auto gi = [&](const char* k, int d){ auto x = g(k); return x.isVoid() ? d : (int)x; };
    auto gf = [&](const char* k, float d){ auto x = g(k); return x.isVoid() ? d : (float)(double)x; };
    if (auto* arr = g("watchlist").getArray()) {
        s.watchlist.clear();
        for (auto& item : *arr) {
            auto sym = item.toString().trim().toUpperCase();
            if (sym.isNotEmpty()) s.watchlist.push_back(sym.toStdString());
        }
        if (s.watchlist.empty()) s.watchlist = tickerbar::TickerState{}.watchlist;
    }
    if (auto* aObj = g("alerts").getDynamicObject()) {
        for (auto& prop : aObj->getProperties()) {
            tickerbar::AlertLevels lv;
            auto parse = [&](const juce::var& x) {
                auto str = x.toString();
                return str.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : str.getDoubleValue();
            };
            if (prop.value.isObject()) {
                lv.above = parse(prop.value.getProperty("above", {}));
                lv.below = parse(prop.value.getProperty("below", {}));
            }
            s.alerts[prop.name.toString().toStdString()] = lv;
        }
    }
    s.alertSound = gb("alertSound", s.alertSound);
    s.alertToasts = gb("alertToasts", s.alertToasts);
    s.linkEnabled = gb("linkEnabled", s.linkEnabled);
    s.themeName = g("theme").toString().toStdString();
    if (s.themeName.empty()) s.themeName = "Midnight Kids";
    s.backgroundStyle = (tickerbar::BackgroundStyle)juce::jlimit(0, 3, gi("bgStyle", (int)s.backgroundStyle));
    s.backgroundOpacity = juce::jlimit(0.15f, 1.0f, gf("bgOpacity", s.backgroundOpacity));
    s.pinned = gb("pinned", s.pinned);
    s.reserveScreenSpace = gb("reserveSpace", s.reserveScreenSpace);
    s.dockEdge = (tickerbar::DockEdge)juce::jlimit(0, 3, gi("dockEdge", (int)s.dockEdge));
    s.windowX = gi("winX", s.windowX); s.windowY = gi("winY", s.windowY);
    s.windowW = juce::jlimit(160, 3840, gi("winW", s.windowW));
    s.windowH = juce::jlimit(52, 1200, gi("winH", s.windowH));
    s.pollSec = juce::jlimit(5, 300, gi("pollSec", s.pollSec));
    s.uiVer = gi("uiVer", 0); // files written before v0.3.0 carry no marker — the app migrates them once
    s.clickSounds = gb("clickSounds", s.clickSounds);
    s.dataColorScheme = juce::jlimit(0, 4, gi("dataColorScheme", s.dataColorScheme));
    s.bgEffect = juce::jlimit(0, 2, gi("bgEffect", s.bgEffect));
    s.vizHue = gf("vizHue", s.vizHue); s.vizSat = gf("vizSat", s.vizSat);
    s.vizSpread = gf("vizSpread", s.vizSpread); s.vizCycle = gf("vizCycle", s.vizCycle);
    if (auto* wObj = g("cellWidths").getDynamicObject()) {
        for (auto& prop : wObj->getProperties()) {
            const int w = (int)prop.value;
            if (w >= 40 && w <= 1200) s.cellWidths[prop.name.toString().toStdString()] = w;
        }
    }
    return s;
}
inline tickerbar::TickerState load() {
    auto f = settingsFile();
    if (!f.existsAsFile()) return tickerbar::TickerState{};
    auto parsed = juce::JSON::parse(f.loadFileAsString());
    return fromVar(parsed);
}
inline void save(const tickerbar::TickerState& s) {
    settingsFile().replaceWithText(juce::JSON::toString(toVar(s)));
}
} // namespace settingsstore
