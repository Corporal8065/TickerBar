#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
// The alert "soft click": a short synthesized blip (no sample files),
// played through the default output ONLY when the user enabled alert
// sound (default OFF). Best-effort by design: if the audio device
// can't open, playback silently doesn't happen — a sound must never be
// able to break a price alert. The device opens lazily on first use so
// a sound-off user never pays for an audio device at all.
//
// v0.6.8: the render now follows the same spec as the UI clicks'
// renderClick (ClickSounds.h — Jackson's intake 2026-10-31 item 3:
// centred stereo, quieter, high cut + a touch of reverb), so EVERY
// click sound in the app sounds like family. The blip is synthesized
// mono and the playback below dual-writes it to both channels (the
// centred image — kept from the original design); the one-pole
// high-cut (~4 kHz, renderClick's approach), the short-tap reverb
// (renderClick's tap table) and the softer level are baked into the
// buffer at construction.
class ClickPlayer : private juce::AudioSource {
public:
    ClickPlayer() {
        // 70 ms decaying sine blip at ~1.5 kHz — a soft tick, not a beep.
        const int sr = 44100, n = 3095;
        // Reverb tail room (renderClick extends its buffer the same way).
        const int tail = (int)(0.10 * sr);
        click.setSize(1, n + tail);
        click.clear();
        auto* w = click.getWritePointer(0);
        // Dry blip, softer than the pre-v0.6.8 render (peak 0.22 ->
        // 0.13, in line with renderClick's overall ~0.19-of-sample
        // level once its player gain is included).
        for (int i = 0; i < n; ++i) {
            const double t = i / (double)sr;
            w[i] = (float)(0.13 * std::sin(2.0 * 3.141592653589793 * 1500.0 * t) * std::exp(-t * 55.0));
        }
        // High-cut: one-pole lowpass, fc ~4 kHz — renderClick's
        // exact coefficient formula; takes the brittle edge off.
        const float a = 1.0f - std::exp(-2.0f * 3.14159265f * 4000.0f / (float)sr);
        float y = 0.f;
        for (int i = 0; i < n; ++i) { y += a * (w[i] - y); w[i] = y; }
        // A touch of reverb: renderClick's short feedforward taps
        // (small-room fingerprint), wet kept low, into the tail.
        // Like renderClick, the taps read the DRY buffer and add
        // into a separate output, so they never feed each other.
        juce::AudioBuffer<float> dry;
        dry.makeCopyOf(click);
        const auto* s = dry.getReadPointer(0);
        static const float taps[][2] = { {17.f, 0.20f}, {27.f, 0.15f}, {39.f, 0.11f}, {53.f, 0.07f} };
        for (auto& t : taps) {
            const int off = (int)(t[0] * 0.001f * (float)sr);
            const float g = t[1];
            for (int i = 0; i < n; ++i) w[i + off] += g * s[i];
        }
    }
    ~ClickPlayer() override {
        sourcePlayer.setSource(nullptr);
        deviceManager.removeAudioCallback(&sourcePlayer);
    }
    void play() {
        if (!initialised) {
            if (deviceManager.initialiseWithDefaultDevices(0, 2).isNotEmpty()) return; // no device: stay silent
            sourcePlayer.setSource(this);
            deviceManager.addAudioCallback(&sourcePlayer);
            initialised = true;
        }
        pos.store(0);
    }
    void prepareToPlay(int, double) override {}
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        info.clearActiveBufferRegion();
        int p = pos.load();
        if (p < 0) return;
        const int avail = click.getNumSamples() - p;
        const int n = std::min(avail, info.numSamples);
        if (n <= 0) { pos.store(-1); return; }
        // Centred stereo image: the mono blip dual-writes to every
        // output channel (unchanged from the original design).
        for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
            info.buffer->copyFrom(ch, info.startSample, click, 0, p, n);
        pos.store(p + n >= click.getNumSamples() ? -1 : p + n);
    }
private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioBuffer<float> click;
    std::atomic<int> pos { -1 };
    bool initialised = false;
};
