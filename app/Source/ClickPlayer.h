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
class ClickPlayer : private juce::AudioSource {
public:
    ClickPlayer() {
        // 70 ms decaying sine blip at ~1.5 kHz — a soft tick, not a beep.
        const int sr = 44100, n = 3095;
        click.setSize(1, n);
        auto* w = click.getWritePointer(0);
        for (int i = 0; i < n; ++i) {
            const double t = i / (double)sr;
            w[i] = (float)(0.22 * std::sin(2.0 * 3.141592653589793 * 1500.0 * t) * std::exp(-t * 55.0));
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
