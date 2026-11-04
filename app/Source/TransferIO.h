#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <tickerbar/Saving.hpp>
#include <tickerbar/State.hpp>
#include "PresetIO.h"
#include "SettingsStore.h"
#include "TickerBarVersion.h"
// v0.6.16: settings EXPORT / IMPORT — the second half of Jackson's
// "saving feature as MeterBar" pick (both presets AND export).
//
// The export is ONE user-chosen JSON file holding the app's own
// settings mapping (settingsstore::toVar — no parallel schema to
// drift), plus the envelope: "app", "exportSchema", "savedByVer",
// and "presets": the whole preset library, each entry a preset
// document in its own right. One file therefore moves the full
// setup — settings, custom tickers, presets — between machines
// (laptop <-> PC) or into a backup.
//
// Import validates the WHOLE file through the core rules
// (tickerbar::checkImportDocument) before anything is touched:
// malformed, foreign, or newer-schema files — and a file whose
// embedded presets contain a single bad one — are refused whole,
// with the reason, and nothing is applied. Only after validation
// are the embedded presets written to the Presets folder and the
// settings handed back for the caller's live apply. Geometry is
// the caller's call: MainComponent keeps this machine's window
// rect on import (an export's rect describes another display).
namespace transferio {
inline juce::var buildExportVar(const tickerbar::TickerState& s) {
    auto v = settingsstore::toVar(s);
    juce::Array<juce::var> presets;
    for (auto& f : presetio::listPresets()) {
        auto pv = juce::JSON::parse(f.loadFileAsString());
        if (pv.isObject()) presets.add(pv); // an unreadable preset file is skipped, never fatal
    }
    if (auto* o = v.getDynamicObject()) {
        o->setProperty("app", juce::String(tickerbar::kSavingAppMarker));
        o->setProperty("exportSchema", tickerbar::kExportSchema);
        o->setProperty("savedByVer", juce::String(tickerbar::kVersionString));
        o->setProperty("presets", juce::var(presets));
    }
    return v;
}
inline bool exportToFile(const tickerbar::TickerState& s, const juce::File& dest, int& presetCountOut) {
    auto v = buildExportVar(s);
    presetCountOut = 0;
    if (auto* arr = v.getProperty("presets", {}).getArray()) presetCountOut = arr->size();
    return dest.replaceWithText(juce::JSON::toString(v, true));
}
struct ImportData {
    bool ok = false;
    juce::String reason;              // user-facing when !ok
    tickerbar::TickerState state;     // the imported settings (geometry NOT yet applied — caller's call)
    int presetsWritten = 0;
};
inline ImportData importFromFile(const juce::File& f) {
    ImportData out;
    const auto text = f.loadFileAsString();
    const auto coreDoc = tickerbar::parseJson(text.toStdString());
    if (!coreDoc) { out.reason = "the file is not readable JSON"; return out; }
    const auto check = tickerbar::checkImportDocument(*coreDoc);
    if (!check.ok) { out.reason = juce::String(check.reason); return out; }
    auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject()) { out.reason = "the file is not readable JSON"; return out; }
    // Whole file validated. Embedded presets land in the library
    // first; the settings apply only when this returns ok, so a
    // refused import changes nothing the user can see.
    if (auto* arr = parsed.getProperty("presets", {}).getArray()) {
        int unnamed = 0;
        for (auto& pv : *arr) {
            auto doc = pv;
            juce::String name = doc.getProperty("name", {}).toString().trim();
            if (name.isEmpty()) { // valid per the core rules (it carries settings) — give it a shelf name
                name = unnamed == 0 ? juce::String("Imported Preset")
                                    : "Imported Preset " + juce::String(unnamed + 1);
                ++unnamed;
                if (auto* o = doc.getDynamicObject()) o->setProperty("name", name);
            }
            juce::String written;
            if (!presetio::saveDocument(doc, written)) {
                out.reason = "preset '" + name + "' could not be written to the Presets folder";
                return out;
            }
            ++out.presetsWritten;
        }
    }
    out.state = settingsstore::fromVar(parsed);
    out.ok = true;
    return out;
}
} // namespace transferio
