#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
// v0.3.0: the pink-plum Settings palette + scoped LookAndFeel, ported
// VERBATIM from MeterBar's Settings.h (v0.19) — Settings is decoupled
// from the bar theme in the whole family: one fixed soft pink-dark
// sheet (#2A1E2E base, #E04C89 accent) with a dark title bar, whatever
// theme the bar itself wears. Applied to the Settings content + its
// viewport ONLY, never to bar components.
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dwmapi.h>
 #ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
  #define DWMWA_USE_IMMERSIVE_DARK_MODE 20
 #endif
#endif
// v0.19 SETTINGS PALETTE — Settings is DECOUPLED from the bar theme.
// Before this, SettingsContent painted nothing and coloured nothing:
// the window inherited theme.panel via MainComponent::applyTheme, which
// is near-white (0xfffdf3f8) in the Anime theme and pure white
// (0xffffffff) in Daylight — Jackson: "blinding when I hit Settings" —
// and its controls fell back to stock JUCE LookAndFeel_V4 whites/slates
// in every theme. Settings now always uses this one fixed soft
// pink-dark palette, identical in all five bar themes. (Plan:
// docs/V019_PLAN.md section 1.2.)
namespace settingsPalette {
    inline juce::Colour base()       { return juce::Colour(0xff2a1e2e); } // window/content background
    inline juce::Colour surface()    { return juce::Colour(0xff3a2438); } // combos, buttons, slider text boxes
    inline juce::Colour well()       { return juce::Colour(0xff241722); } // slider tracks, tick boxes, scrollbar track
    inline juce::Colour border()     { return juce::Colour(0xff57304d); } // outlines, dividers
    inline juce::Colour text()       { return juce::Colour(0xfff6e3ee); } // body text (~13:1 on base)
    inline juce::Colour dimText()    { return juce::Colour(0xffc9a3bb); } // notes (~7:1 on base)
    inline juce::Colour accent()     { return juce::Colour(0xffe04c89); } // brand pink: thumbs, ticks, highlights
    inline juce::Colour softPink()   { return juce::Colour(0xffe77fb1); } // combo arrows, secondary accents
    inline void applyDarkTitleBar(juce::Component* c) {
#if defined(_WIN32)
        if (c == nullptr) return;
        if (auto* peer = c->getPeer()) {
            if (auto* hwnd = (HWND) peer->getNativeHandle()) {
                BOOL dark = TRUE;
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            }
        }
#else
        (void) c;
#endif
    }
} // namespace settingsPalette
// Scoped LookAndFeel for the Settings window ONLY (the main bar keeps
// stock JUCE behaviour — this is set on the Settings content + its
// viewport, never on any bar component). Seeds every colour the stock
// V4 look would otherwise take from its own white/slate defaults;
// the builder helpers in SettingsContent ALSO set explicit colours, so
// the palette holds even for a control that misses the L&F chain.
class SettingsLookAndFeel : public juce::LookAndFeel_V4 {
public:
    SettingsLookAndFeel() {
        using namespace settingsPalette;
        setColour(juce::Label::textColourId, text());
        setColour(juce::ComboBox::backgroundColourId, surface());
        setColour(juce::ComboBox::textColourId, text());
        setColour(juce::ComboBox::arrowColourId, softPink());
        setColour(juce::ComboBox::outlineColourId, border());
        setColour(juce::ComboBox::buttonColourId, surface());
        setColour(juce::PopupMenu::backgroundColourId, base());
        setColour(juce::PopupMenu::textColourId, text());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent());
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xffffffff));
        setColour(juce::PopupMenu::headerTextColourId, dimText());
        setColour(juce::Slider::backgroundColourId, well());
        setColour(juce::Slider::trackColourId, well());
        setColour(juce::Slider::thumbColourId, accent());
        setColour(juce::Slider::textBoxTextColourId, text());
        setColour(juce::Slider::textBoxBackgroundColourId, surface());
        setColour(juce::Slider::textBoxOutlineColourId, border());
        setColour(juce::ToggleButton::textColourId, text());
        setColour(juce::ToggleButton::tickColourId, accent());
        setColour(juce::ToggleButton::tickDisabledColourId, dimText());
        setColour(juce::TextButton::buttonColourId, surface());
        setColour(juce::TextButton::buttonOnColourId, accent());
        setColour(juce::TextButton::textColourOffId, text());
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));
        setColour(juce::ScrollBar::backgroundColourId, well());
        setColour(juce::ScrollBar::thumbColourId, border());
        setColour(juce::ScrollBar::trackColourId, well());
    }
};
