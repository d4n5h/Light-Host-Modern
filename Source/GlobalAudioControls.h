#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "DryDelay.h"

// Runtime-only state: no session writer persists these flags. prepare() is called
// by the controller with callbacks suspended; capture/mix touch prepared storage.
class GlobalAudioControls
{
public:
    bool setMuted(bool value) noexcept { return muted.exchange(value) != value; }
    bool setBypassed(bool value) noexcept { return bypassed.exchange(value) != value; }
    bool isMuted() const noexcept { return muted.load(); }
    bool isBypassed() const noexcept { return bypassed.load(); }

    bool prepare(int channels, int blockSize, int latency, double sampleRate)
    {
        step = (float) (1.0 / juce::jmax(1.0, sampleRate * 0.005));
        return dryDelay.prepare(channels, blockSize, latency, sampleRate);
    }

    void capture(const juce::AudioBuffer<float>& input) noexcept
    {
        dryDelay.capture(input);
    }

    void applyMute(juce::AudioBuffer<float>& wet) noexcept
    {
        const bool silence = isMuted();
        for (int i = 0; i < wet.getNumSamples(); ++i)
        {
            muteGain = silence ? juce::jmax(0.0f, muteGain - step) : juce::jmin(1.0f, muteGain + step);
            if (muteGain == 1.0f) continue;
            for (int ch = 0; ch < wet.getNumChannels(); ++ch)
                wet.getWritePointer(ch)[i] *= muteGain;
        }
    }

    void mix(juce::AudioBuffer<float>& wet) noexcept
    {
        const bool useDry = isBypassed(), silence = isMuted();
        for (int i = 0; i < wet.getNumSamples(); ++i)
        {
            dryMix = useDry ? juce::jmin(1.0f, dryMix + step) : juce::jmax(0.0f, dryMix - step);
            muteGain = silence ? juce::jmax(0.0f, muteGain - step) : juce::jmin(1.0f, muteGain + step);
            for (int ch = 0; ch < wet.getNumChannels(); ++ch)
            {
                auto& sample = wet.getWritePointer(ch)[i];
                const auto clean = dryDelay.output().getSample(ch, i);
                const auto mixed = dryMix == 1.0f ? clean : (dryMix == 0.0f ? sample : sample * (1.0f - dryMix) + clean * dryMix);
                sample = muteGain == 0.0f ? 0.0f : mixed * muteGain;
            }
        }
    }

private:
    std::atomic<bool> muted { false }, bypassed { false };
    DryDelay dryDelay;
    float step = 1.0f, dryMix = 0.0f, muteGain = 1.0f;
};
