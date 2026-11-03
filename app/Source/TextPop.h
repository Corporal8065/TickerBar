#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// TEXT POP (v0.6.8) — mirrors MeterBar's app/Source/TextPop.h in
// structure, with the CORRECTED geometry Jackson specified
// 2026-11-02 19:40 PT (superseding the arrangement MeterBar
// v0.26.1 shipped): the text's own COLOUR copy is drawn FIRST at
// the rect translated (+off, +off) — bottom-right, BEHIND — and
// then a WHITE copy at the untranslated rect — top-left relative
// to the colour copy, IN FRONT. The visible letters read white
// with a hard colour edge at the bottom-right, exactly the
// pictured effect in his corrected description. The offset is
// ~20% of the font height, clamped 2–7 px (MeterBar's scale: it
// always clears the glyph strokes, and large captions don't fly
// away from their letters).
//
// Applied to the text TickerBar draws over its transparent /
// visual background: the ticker cells, the context cells, the
// toolbar labels and the (text) Update! button. Emoji / icon
// GLYPH buttons (Link / Settings / Menu) are NOT routed through
// here — Jackson: the emoji text was good as it was — and nothing
// inside the opaque Settings sheet or the detail popup uses it.
namespace textpop {

// Offset for a given font height: ~20% of the glyph, clamped so
// small readouts still clear their strokes and large captions
// don't fly away from their letters.
inline float offsetFor(float fontHeight) {
    return juce::jlimit(2.f, 7.f, fontHeight * 0.20f);
}

// drawText with the pop. The caller sets the font first, exactly
// as with Graphics::drawText — the offset derives from it. Both
// copies keep the text colour's own alpha, so a dimmed (stale)
// readout pops proportionally.
inline void draw(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                 juce::Justification j, juce::Colour colour) {
    const float off = offsetFor(g.getCurrentFont().getHeight());
    g.setColour(colour);
    g.drawText(text, r.translated(off, off), j);
    g.setColour(juce::Colours::white.withAlpha(colour.getFloatAlpha()));
    g.drawText(text, r, j);
}
inline void draw(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                 juce::Justification j, juce::Colour colour) {
    draw(g, text, r.toFloat(), j, colour);
}

// drawFittedText with the pop (toolbar labels / the Update!
// button, whose stock LookAndFeel draws fitted text).
inline void drawFitted(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                       juce::Justification j, int maxLines, float minScale, juce::Colour colour) {
    const int off = juce::roundToInt(offsetFor(g.getCurrentFont().getHeight()));
    g.setColour(colour);
    g.drawFittedText(text, r.translated(off, off), j, maxLines, minScale);
    g.setColour(juce::Colours::white.withAlpha(colour.getFloatAlpha()));
    g.drawFittedText(text, r, j, maxLines, minScale);
}

} // namespace textpop
