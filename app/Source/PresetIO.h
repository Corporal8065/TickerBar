#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <tickerbar/State.hpp>
#include "SettingsStore.h"
// Named presets (v0.3.0, MeterBar parity): ONE JSON file per preset in
// <Documents>/TickerBar/Presets, written through the SAME toVar/fromVar
// mapping as the live settings file — so a preset is a full
// configuration, window position included (winX/winY/winW/winH ride
// along in the state), plus a "name" property the settings file
// itself doesn't carry.
namespace presetio {
inline juce::File presetFolder() {
    auto f = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                 .getChildFile("TickerBar").getChildFile("Presets");
    f.createDirectory(); return f;
}
inline juce::Array<juce::File> listPresets() {
    juce::Array<juce::File> out;
    for (auto& f : presetFolder().findChildFiles(juce::File::findFiles, false, "*.json"))
        out.add(f);
    return out;
}
inline bool save(const tickerbar::TickerState& s, const juce::String& name) {
    auto v = settingsstore::toVar(s);
    if (auto* o = v.getDynamicObject()) o->setProperty("name", name);
    auto f = presetFolder().getChildFile(juce::File::createLegalFileName(name) + ".json");
    return f.replaceWithText(juce::JSON::toString(v, true));
}
inline bool load(const juce::File& f, tickerbar::TickerState& into, juce::String& nameOut) {
    auto parsed = juce::JSON::parse(f.loadFileAsString());
    if (parsed.isVoid()) return false;
    into = settingsstore::fromVar(parsed);
    nameOut = parsed.getProperty("name", f.getFileNameWithoutExtension()).toString();
    return true;
}
} // namespace presetio
