#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <tickerbar/State.hpp>
// Theme values are MeterBar's palettes VERBATIM (meters-app/app/Source/
// Theme.h) so the bars look like siblings on one screen. v0.3.0: the
// full MeterBar set — Midnight Kids, Midnight Pink, Deep Teal,
// Daylight, Anime — plus TickerBar's legacy "Clean Dark" theme kept
// so a saved choice never silently changes under its owner.
struct Theme {
    juce::Colour bg, panel, deepest, accent, accent2, text, dim, good, warn, bad;
    juce::Colour border, grid;
    juce::String name;
};
inline Theme midnightKidsTheme() {
    return { juce::Colour(0xff17141f), juce::Colour(0xff231f31), juce::Colour(0xff0e0c15),
        juce::Colour(0xffe04c89), juce::Colour(0xff40aac0), juce::Colour(0xfff6eff3), juce::Colour(0xffa79fc0),
        juce::Colour(0xff2fd68a), juce::Colour(0xfffad927), juce::Colour(0xfffe5a3c),
        juce::Colour(0xff3d3654), juce::Colour(0xff494164), "Midnight Kids" };
}
inline Theme cleanDarkTheme() {
    return { juce::Colour(0xff14171c), juce::Colour(0xff1d2129), juce::Colour(0xff0b0d10),
        juce::Colour(0xff40aac0), juce::Colour(0xffe04c89), juce::Colour(0xffeef2f5), juce::Colour(0xff8b95a5),
        juce::Colour(0xff2fd68a), juce::Colour(0xfffad927), juce::Colour(0xfffe5a3c),
        juce::Colour(0xff2c313a), juce::Colour(0xff232830), "Clean Dark" };
}
inline Theme themeByName(const juce::String& n) {
    if (n == "Midnight Pink") return { juce::Colour(0xff1e1b24), juce::Colour(0xff26232e), juce::Colour(0xff161519),
        juce::Colour(0xffe04c89), juce::Colour(0xff40aac0), juce::Colour(0xfff2e8ee), juce::Colour(0xff9a8fa0),
        juce::Colour(0xff40aac0), juce::Colour(0xfffad927), juce::Colour(0xfffe8a0a),
        juce::Colour(0xff3a3547), juce::Colour(0xff45405a), "Midnight Pink" };
    if (n == "Deep Teal") return { juce::Colour(0xff0d1f22), juce::Colour(0xff14333a), juce::Colour(0xff081417),
        juce::Colour(0xff40aac0), juce::Colour(0xffe04c89), juce::Colour(0xffe8f4f6), juce::Colour(0xff7fa3ab),
        juce::Colour(0xff40c060), juce::Colour(0xfffad927), juce::Colour(0xfffe5a3c),
        juce::Colour(0xff1e4a52), juce::Colour(0xff2a5a64), "Deep Teal" };
    if (n == "Anime") return { juce::Colour(0xfff6e3ee), juce::Colour(0xfffdf3f8), juce::Colour(0xfff3d9e8),
        juce::Colour(0xffe77fb1), juce::Colour(0xff8fb8e8), juce::Colour(0xff5a4356), juce::Colour(0xffa58ba0),
        juce::Colour(0xff3fae6a), juce::Colour(0xffd9a92c), juce::Colour(0xffe5533c),
        juce::Colour(0xffe3c8dd), juce::Colour(0xffedd8ea), "Anime" };
    if (n == "Daylight") return { juce::Colour(0xffe9e9ee), juce::Colour(0xffffffff), juce::Colour(0xffd8d8e0),
        juce::Colour(0xff3050c0), juce::Colour(0xffc22a6d), juce::Colour(0xff181824), juce::Colour(0xff606078),
        juce::Colour(0xff1a9e4b), juce::Colour(0xffc9a227), juce::Colour(0xffd23c28),
        juce::Colour(0xffb9b9c6), juce::Colour(0xffc6c6d2), "Daylight" };
    if (n == "Clean Dark") return cleanDarkTheme();
    return midnightKidsTheme();
}
inline Theme themeByName(const std::string& n) { return themeByName(juce::String(n)); }
// The Settings theme list: MeterBar's five first, then the legacy one.
inline juce::StringArray themeNameList() {
    return { "Midnight Kids", "Midnight Pink", "Deep Teal", "Daylight", "Anime", "Clean Dark" };
}
// Background styles (MeterBar v0.16 helper): CleanDark = near-black
// blue-grey gradient; MidnightKids = violet/plum gradient; Pastel =
// the Anime family's pale-pink gradient; Flat = plain theme.bg.
// Painted at alpha=op.
inline void fillStyledBackground(juce::Graphics& g, juce::Rectangle<float> r,
                                 tickerbar::BackgroundStyle style, const Theme& th, float alpha) {
    alpha = juce::jlimit(0.15f, 1.0f, alpha);
    juce::Colour top, bottom;
    switch (style) {
    case tickerbar::BackgroundStyle::CleanDark:
        top = juce::Colour(0xff272c36); bottom = juce::Colour(0xff101216); break;
    case tickerbar::BackgroundStyle::MidnightKids:
        top = juce::Colour(0xff2b1a4d); bottom = juce::Colour(0xff150f24); break;
    case tickerbar::BackgroundStyle::Pastel:
        top = juce::Colour(0xfffdeef5); bottom = juce::Colour(0xfff6d3e4); break;
    default: // Flat
        g.fillAll(th.bg.withAlpha(alpha)); return;
    }
    juce::ColourGradient grad(top.withAlpha(alpha), r.getX(), r.getY(),
                              bottom.withAlpha(alpha), r.getX(), r.getBottom(), false);
    if (style == tickerbar::BackgroundStyle::MidnightKids)
        grad.addColour(0.55, juce::Colour(0xff3d1d43).withAlpha(alpha)); // magenta-plum mid-stop
    g.setGradientFill(grad);
    g.fillRect(r);
}
