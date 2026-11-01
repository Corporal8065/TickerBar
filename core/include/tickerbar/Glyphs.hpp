#pragma once
// Glyphs -- the toolbar icon glyphs as EXPLICIT UTF-8 byte escapes
// (v0.6.2, pure core header: std-only, unit-tested in test_core).
//
// Why this exists: up to v0.6.1 the icon buttons were constructed
// from raw non-ASCII literals in MainComponent.h. The MSVC build
// decoded those literals as Windows-1252, so the button TEXT itself
// arrived mangled -- Jackson's v0.22.1 MeterBar screenshot showed
// mojibake (a Latin-1 eth or a-circumflex followed by
// empty boxes), not tofu: 0xF0 is the first byte of a 4-byte
// UTF-8 sequence and 0xE2 of a 3-byte one, each read as a
// single-byte character. The v0.6.1 Segoe UI Emoji font change
// was necessary but could never repair wrong codepoints.
//
// Here every glyph is written as \x escapes -- pure ASCII source
// bytes that no compiler source-encoding setting can mangle. The
// app wraps them in juce::String::fromUTF8 at the button. The root
// CMakeLists also passes /utf-8 to MSVC as belt and braces for any
// future literal. test_core pins every byte two ways: against an
// independent codepoint->UTF-8 encoder and against these sequences.
//
// Each function returns a NUL-terminated string holding exactly the
// glyph's UTF-8 bytes (none of the sequences contains a NUL byte).

namespace tickerbar {

// U+1F517 LINK SYMBOL -- the Link button. UTF-8: F0 9F 94 97.
inline const char* glyphLinkUtf8() { return "\xF0\x9F\x94\x97"; }

// U+2699 GEAR -- the Settings button. UTF-8: E2 9A 99.
inline const char* glyphSettingsUtf8() { return "\xE2\x9A\x99"; }

// U+2630 TRIGRAM FOR HEAVEN -- the Menu (burger) button. UTF-8: E2 98 B0.
inline const char* glyphMenuUtf8() { return "\xE2\x98\xB0"; }

} // namespace tickerbar
