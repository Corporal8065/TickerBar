#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <tickerbar/Saving.hpp>
#include <tickerbar/State.hpp>
#include "SettingsStore.h"
#include "TickerBarVersion.h"
// Named presets (v0.3.0, MeterBar parity): ONE JSON file per preset in
// <Documents>/TickerBar/Presets, written through the SAME toVar/fromVar
// mapping as the live settings file — so a preset is a full
// configuration, window position included (winX/winY/winW/winH ride
// along in the state), plus a "name" property the settings file
// itself doesn't carry.
//
// v0.6.16: save() also stamps the envelope — "app", "presetSchema",
// "savedByVer" (core tickerbar/Saving.hpp) — and load() validates
// the document through the core rules BEFORE anything is applied:
// a preset stamped by a newer build is refused whole, with the
// reason, never half-applied by silently dropping its new fields.
// Files written before v0.6.16 carry no stamp (schema 0) and load
// exactly as they always have.
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
inline void stamp(juce::var& v, const juce::String& name) {
    if (auto* o = v.getDynamicObject()) {
        o->setProperty("name", name);
        o->setProperty("app", juce::String(tickerbar::kSavingAppMarker));
        o->setProperty("presetSchema", tickerbar::kPresetSchema);
        o->setProperty("savedByVer", juce::String(tickerbar::kVersionString));
    }
}
inline bool save(const tickerbar::TickerState& s, const juce::String& name) {
    auto v = settingsstore::toVar(s);
    stamp(v, name);
    auto f = presetFolder().getChildFile(juce::File::createLegalFileName(name) + ".json");
    return f.replaceWithText(juce::JSON::toString(v, true));
}
// Write an already-formed preset document (the import path hands
// over a validated one). The file name comes from the document's
// own "name"; a document with no usable name is not written.
inline bool saveDocument(const juce::var& doc, juce::String& nameOut) {
    nameOut = doc.getProperty("name", {}).toString().trim();
    if (nameOut.isEmpty()) return false;
    auto f = presetFolder().getChildFile(juce::File::createLegalFileName(nameOut) + ".json");
    return f.replaceWithText(juce::JSON::toString(doc, true));
}
inline bool load(const juce::File& f, tickerbar::TickerState& into, juce::String& nameOut,
                 juce::String& reasonOut) {
    reasonOut.clear();
    const auto text = f.loadFileAsString();
    const auto coreDoc = tickerbar::parseJson(text.toStdString());
    if (!coreDoc) { reasonOut = "the file is not readable JSON"; return false; }
    const auto check = tickerbar::checkPresetDocument(*coreDoc);
    if (!check.ok) { reasonOut = juce::String(check.reason); return false; }
    auto parsed = juce::JSON::parse(text);
    if (parsed.isVoid()) { reasonOut = "the file is not readable JSON"; return false; }
    into = settingsstore::fromVar(parsed);
    nameOut = parsed.getProperty("name", f.getFileNameWithoutExtension()).toString();
    return true;
}
inline bool load(const juce::File& f, tickerbar::TickerState& into, juce::String& nameOut) {
    juce::String ignored;
    return load(f, into, nameOut, ignored);
}
} // namespace presetio
