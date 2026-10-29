#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "Visualizer.hpp" // VizRgb + vizHsvToRgb
namespace tickerbar {
// REACTIVE GLOW FIELD (v0.3.0, items 7/17) — the pure model behind
// the MiniMeters-style background Jackson referenced (photo:
// your_files/bar-family/reactive-bg-reference-minimeters.png — one
// frame; the real effect cycles colours as the music plays).
//
// The picture, per the reference + Jackson's description:
//  - near-black base, always;
//  - two/three LARGE, soft colour blooms bleeding in from the edges,
//    never a loud wash — a bloom's centre alpha stays tasteful;
//  - hue is DRIVEN: each frequency band owns a bloom, its hue sits
//    at an offset from the palette base hue, and its strength tracks
//    the band's (smoothed) energy, pumped by an onset envelope —
//    the "pumping heartbeat": a beat slams the envelope to full and
//    it relaxes over ~half a second;
//  - a subtle vertical pinstripe texture over everything.
//
// Coordinates are GROUP space: (0,0) is the top-left of the union of
// all linked bars' screen rects (or the bar's own rect when
// unlinked), (1,1) the bottom-right — so linked bars sampling the
// same field through their own slice of the union read as ONE
// surface. Every bar steps the same deterministic model from the
// same broadcast band energies, so the fields agree.
//
// The standalone driver (no MeterBar linked) is ambientBands(): slow
// deterministic swells — the bar breathes gently on its own and
// blooms for real when MeterBar's broadcast arrives.

struct GlowSettings {
    float baseHue = 0.83f;     // palette start (family pink/magenta)
    float saturation = 0.80f;  // 0..1
    float spread = 0.35f;      // hue distance between band blooms (in wheel turns)
    float cycleSeconds = 24.f; // slow hue drift of the whole field; <= 0 = static
    float intensity = 1.0f;    // master strength scaler (the app's taste cap)
};

struct GlowBloom {
    float cx = 0.5f, cy = 0.5f; // group space
    float radius = 0.4f;        // gaussian sigma is radius * 0.5
    float hue = 0.83f;          // 0..1 wheel position (may exceed 1; vizHsvToRgb wraps)
    float strength = 0.f;       // 0..~0.85 centre weight before the taste cap
};

class GlowField {
public:
    // Advance the model. bands = [low, mid, high] energies 0..1,
    // level = overall loudness 0..1 (drives onset detection).
    void update(float dt, const float bands[3], float level) {
        if (dt <= 0) return;
        time_ += dt;
        // Band smoothing: fast attack, slow release — blooms surge on
        // a hit and melt away instead of flickering per frame.
        for (int i = 0; i < 3; ++i) {
            const float target = bands[i] < 0.f ? 0.f : bands[i] > 1.f ? 1.f : bands[i];
            const float rate = target > smooth_[i] ? 10.f : 1.6f;
            float a = rate * dt; if (a > 1.f) a = 1.f;
            smooth_[i] += (target - smooth_[i]) * a;
        }
        // Onset pump: a level jump slams the envelope; it then decays
        // with a ~0.45 s half-life (the heartbeat relax).
        const float lv = level < 0.f ? 0.f : level > 1.f ? 1.f : level;
        if (lv > prevLevel_ + 0.07f) pump_ = 1.f;
        prevLevel_ = lv;
        pump_ *= std::exp(-dt * 1.55f);
        if (pump_ < 0.f) pump_ = 0.f;
    }

    // The three blooms for the current state. Centres are fixed in
    // group space (low = left, mid = centre, high = right), bleeding
    // from the vertical middle like the reference frame's clouds.
    std::array<GlowBloom, 3> blooms(const GlowSettings& st) const {
        float cycle = 0.f;
        if (st.cycleSeconds > 0.01f) cycle = (float)std::fmod(time_ / (double)st.cycleSeconds, 1.0);
        const float pumpGain = 0.35f + 0.65f * pump_;
        std::array<GlowBloom, 3> out;
        const float xs[3] = { 0.22f, 0.52f, 0.80f };
        const float ys[3] = { 0.58f, 0.42f, 0.55f };
        const float rs[3] = { 0.46f, 0.40f, 0.46f };
        for (int i = 0; i < 3; ++i) {
            out[(size_t)i].cx = xs[i]; out[(size_t)i].cy = ys[i]; out[(size_t)i].radius = rs[i];
            out[(size_t)i].hue = st.baseHue + cycle + (float)(i - 1) * st.spread * 0.5f;
            float s = smooth_[i] * pumpGain * st.intensity;
            out[(size_t)i].strength = s > 0.85f ? 0.85f : s;
        }
        return out;
    }

    // Sample the field at group-space (x, y): near-black base plus
    // the blooms' additive colour. The 0.5 taste cap keeps even a
    // full-energy field a dark bloom, never a wash (the reference
    // frame's brightest cloud sits well under mid-grey).
    VizRgb sample(float x, float y, const GlowSettings& st) const {
        double r = 8.0, g = 8.0, b = 12.0; // near-black blue-ish base
        for (auto& bl : blooms(st)) {
            const double dx = x - bl.cx, dy = y - bl.cy;
            const double sigma = bl.radius * 0.5;
            const double fall = std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma));
            const double w = fall * bl.strength * 0.5;
            if (w <= 0.001) continue;
            const VizRgb c = vizHsvToRgb(bl.hue, st.saturation, 1.0f);
            r += c.r * w; g += c.g * w; b += c.b * w;
        }
        auto cl = [](double v) { return (std::uint8_t)std::lround(v < 0.0 ? 0.0 : v > 255.0 ? 255.0 : v); };
        return VizRgb{ cl(r), cl(g), cl(b) };
    }

    // The pinstripe texture: faint vertical lines, period 1/56 of the
    // group width, ~14% duty — returns an alpha 0..~0.05 (the app
    // composites white at this alpha). Matches the reference frame's
    // subtle streaks.
    static float pinstripe(float groupX) {
        const float ph = groupX * 56.f;
        const float frac = ph - std::floor(ph);
        return frac < 0.14f ? 0.05f : 0.0f;
    }

    float pump() const { return pump_; }
    float band(int i) const { return smooth_[i]; }
    double time() const { return time_; }

private:
    float smooth_[3] = { 0.f, 0.f, 0.f };
    float pump_ = 0.f;
    float prevLevel_ = 0.f;
    double time_ = 0.0;
};

// Standalone band driver for bars with no audio of their own: slow,
// deterministic swells (all values well under the visualizer's burst
// trigger — this is breathing, not a light show). When MeterBar is
// linked and broadcasting, the app uses ITS bands instead.
inline void ambientBands(double tSec, float out[3], float& level) {
    out[0] = 0.34f + 0.24f * (float)std::sin(tSec * 0.23);
    out[1] = 0.30f + 0.26f * (float)std::sin(tSec * 0.31 + 2.1);
    out[2] = 0.26f + 0.22f * (float)std::sin(tSec * 0.47 + 4.2);
    level = 0.30f + 0.20f * (float)std::sin(tSec * 0.11 + 1.0);
}
} // namespace tickerbar
