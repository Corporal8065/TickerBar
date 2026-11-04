#pragma once
#include <string>
#include "Json.hpp"
namespace tickerbar {
// v0.6.16: SAVING — named presets + settings export/import, the
// validation half. Jackson picked "both" (presets AND export) when
// asked what MeterBar's saving feature should mean for TickerBar.
//
// Storage shapes (the app layer writes them; this header grades
// them, so the rules are core-tested, not buried in UI code):
//   PRESET  — one JSON file per preset in Documents/TickerBar/
//             Presets (the v0.3.0 shape, MeterBar's exact idiom):
//             the settings mapping plus "name". v0.6.16 appends
//             three envelope keys: "app", "presetSchema",
//             "savedByVer". Preset files written before v0.6.16
//             carry none of them and stay loadable (schema 0: every
//             key they carry is a key this build knows — the
//             family's appended-keys rule, not a half-apply).
//   EXPORT  — one JSON file the user picks: the SAME settings
//             mapping (no parallel schema) plus "app",
//             "exportSchema", "savedByVer" and a "presets" array
//             holding the whole preset library, so one file moves
//             the full setup between machines.
//
// Refusal rules — a document that fails is refused WHOLE, with a
// user-facing reason; nothing is ever partially applied:
//   - not a single JSON object;
//   - an "app" marker naming a different app (foreign file);
//   - a schema stamp from a NEWER build (presetSchema /
//     exportSchema above this build's) — the newer-build preset
//     must fail honestly here, never silently drop its new fields;
//   - an import with no TickerBar identity: an export envelope
//     ("app"/"exportSchema") or, for a raw settings.json copied
//     by hand, the "watchlist" key no sibling app writes;
//   - an import carrying no settings keys at all;
//   - an import whose embedded preset list is not a list, or any
//     embedded preset failing the preset rules.
// A schema stamp that is present but not a number is malformed
// and refused, never guessed at.
inline constexpr int kPresetSchema = 1;
inline constexpr int kExportSchema = 1;
inline constexpr const char* kSavingAppMarker = "TickerBar";

struct SavingCheck {
    bool ok = false;
    std::string reason; // user-facing when !ok; empty when ok
};

namespace savingdetail {
// A representative set of keys only TickerBar's settings mapping
// writes — presence of any one is evidence a document carries
// TickerBar settings. Deliberately NOT the identity test on its
// own (sibling apps share a few key names like "theme"); it is
// the "carries settings" test, and import identity additionally
// needs the envelope or the watchlist key (see below).
inline bool isKnownSettingsKey(const std::string& k) {
    static const char* const keys[] = {
        "watchlist", "alerts", "theme", "bgStyle", "bgOpacity", "bgEffect",
        "vizStyle", "vizHue", "vizSat", "vizSpread", "vizCycle",
        "dataColorScheme", "dockEdge", "pollSec", "cellWidths",
        "rotationSection", "linkEnabled", "linkBackground", "pinned",
        "reserveSpace", "clickSounds", "alertSound", "alertToasts",
        "showFearGreed", "showBtcDominance", "winX", "winY", "winW", "winH",
        "customStocks", // v0.6.15 (Stocks section's own tickers)
    };
    for (auto* key : keys) if (k == key) return true;
    return false;
}
inline bool carriesSettings(const Json& v) {
    if (!v.isObject()) return false;
    for (auto& kv : v.object) if (isKnownSettingsKey(kv.first)) return true;
    return false;
}
// absent -> 0 (a pre-stamp document); present-but-not-a-number ->
// malformed, which the callers refuse rather than guess.
inline int schemaOf(const Json& v, const char* key, bool& malformed) {
    malformed = false;
    const Json* s = v.find(key);
    if (!s) return 0;
    auto n = s->asNumber();
    if (!n) { malformed = true; return 0; }
    return (int)*n;
}
inline SavingCheck checkAppMarker(const Json& v) {
    const Json* app = v.find("app");
    if (!app) return { true, {} };
    auto s = app->asString();
    if (!s || *s != kSavingAppMarker)
        return { false, "it is marked as a different app's file" };
    return { true, {} };
}
} // namespace savingdetail

inline SavingCheck checkPresetDocument(const Json& v) {
    if (!v.isObject())
        return { false, "not a JSON object — a preset is a single JSON object" };
    auto marker = savingdetail::checkAppMarker(v);
    if (!marker.ok)
        return { false, "not a TickerBar preset: " + marker.reason };
    bool malformed = false;
    const int schema = savingdetail::schemaOf(v, "presetSchema", malformed);
    if (malformed)
        return { false, "its presetSchema stamp is not a number" };
    if (schema > kPresetSchema)
        return { false, "this preset was saved by a newer TickerBar (preset schema " +
                        std::to_string(schema) + " — this build understands up to " +
                        std::to_string(kPresetSchema) + "); update TickerBar to use it" };
    if (!savingdetail::carriesSettings(v) && !v.find("name"))
        return { false, "no TickerBar settings found in this file" };
    return { true, {} };
}

inline SavingCheck checkImportDocument(const Json& v) {
    if (!v.isObject())
        return { false, "not a JSON object — a TickerBar settings file is a single JSON object" };
    auto marker = savingdetail::checkAppMarker(v);
    if (!marker.ok)
        return { false, "not a TickerBar settings file: " + marker.reason };
    bool malformed = false;
    const int schema = savingdetail::schemaOf(v, "exportSchema", malformed);
    if (malformed)
        return { false, "its exportSchema stamp is not a number" };
    if (schema > kExportSchema)
        return { false, "this file was exported by a newer TickerBar (export schema " +
                        std::to_string(schema) + " — this build understands up to " +
                        std::to_string(kExportSchema) + "); update TickerBar to import it" };
    // Identity: an export envelope, or a raw settings.json (the
    // app's own file, copied by hand) — the watchlist key is the
    // tell no sibling app shares.
    const bool isEnvelope = v.find("app") != nullptr || v.find("exportSchema") != nullptr;
    if (!isEnvelope && !v.find("watchlist"))
        return { false, "not a TickerBar settings file (no TickerBar marker and no watchlist)" };
    if (!savingdetail::carriesSettings(v))
        return { false, "no TickerBar settings found in this file" };
    if (const Json* presets = v.find("presets")) {
        if (!presets->isArray())
            return { false, "its presets entry is not a list — refusing the whole file" };
        for (size_t i = 0; i < presets->array.size(); ++i) {
            auto pc = checkPresetDocument(presets->array[i]);
            if (!pc.ok)
                return { false, "preset " + std::to_string(i + 1) +
                                " inside this file is not importable: " + pc.reason };
        }
    }
    return { true, {} };
}
} // namespace tickerbar
