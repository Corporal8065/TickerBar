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
// visual background: the toolbar labels and the (text) Update!
// button. (v0.6.10: the ticker cells and context cells no longer
// use the pop — they draw single via drawSingle below, per
// Jackson's 2026-11-03 cell-photo instruction.) Emoji / icon
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

// WHITE RULE (v0.6.9 — Jackson, 2026-11-02 21:19 PT): "for all
// the text if the front text is white make the back white." When
// a label's own colour is white or near-white, the offset BACK
// copy is drawn PURE WHITE — white text never gets a tinted or
// contrasting backing. (The themes' text colours are tinted
// whites, not pure white — without this rule the back copy
// showed that tint behind the white face.) Coloured labels keep
// their own colour as the back copy; the corrected geometry is
// otherwise unchanged. Alpha always follows the caller's colour.
inline bool isWhiteText(juce::Colour c) {
    const int r = (int)c.getRed(), g = (int)c.getGreen(), b = (int)c.getBlue();
    const int lo = juce::jmin(r, juce::jmin(g, b)), hi = juce::jmax(r, juce::jmax(g, b));
    return lo >= 0xE0 && (hi - lo) <= 0x30;
}
inline juce::Colour backColourFor(juce::Colour colour) {
    return isWhiteText(colour) ? juce::Colours::white.withAlpha(colour.getFloatAlpha())
                               : colour;
}

// drawText with the pop. The caller sets the font first, exactly
// as with Graphics::drawText — the offset derives from it. Both
// copies keep the text colour's own alpha, so a dimmed (stale)
// readout pops proportionally.
inline void draw(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                 juce::Justification j, juce::Colour colour) {
    const float off = offsetFor(g.getCurrentFont().getHeight());
    g.setColour(backColourFor(colour));
    g.drawText(text, r.translated(off, off), j);
    g.setColour(juce::Colours::white.withAlpha(colour.getFloatAlpha()));
    g.drawText(text, r, j);
}
inline void draw(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                 juce::Justification j, juce::Colour colour) {
    draw(g, text, r.toFloat(), j, colour);
}

// SINGLE DRAW (v0.6.10 — Jackson, 2026-11-03, with a photo of the
// BTC cell): "remove the double text for each cell in the title %
// return and every other element." Inside the ticker cells and the
// context cells the pop's two copies overlap into illegibility at
// cell text sizes, so cell text draws ONCE, in its own colour, with
// no offset back copy. The caller sets the font first, exactly as
// with draw above; the colour's own alpha still applies, so a
// dimmed (stale) readout dims proportionally. The pop itself stays
// for the toolbar labels / Update! button, which are not cells.
inline void drawSingle(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                       juce::Justification j, juce::Colour colour) {
    g.setColour(colour);
    g.drawText(text, r, j);
}
inline void drawSingle(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                       juce::Justification j, juce::Colour colour) {
    drawSingle(g, text, r.toFloat(), j, colour);
}

// drawFittedText with the pop (toolbar labels / the Update!
// button, whose stock LookAndFeel draws fitted text).
inline void drawFitted(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                       juce::Justification j, int maxLines, float minScale, juce::Colour colour) {
    const int off = juce::roundToInt(offsetFor(g.getCurrentFont().getHeight()));
    g.setColour(backColourFor(colour));
    g.drawFittedText(text, r.translated(off, off), j, maxLines, minScale);
    g.setColour(juce::Colours::white.withAlpha(colour.getFloatAlpha()));
    g.drawFittedText(text, r, j, maxLines, minScale);
}

} // namespace textpop
