#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// Controller-owned allocation; capture() only accesses prepared storage. The
// ring retains recent input even at zero latency, allowing short live changes.
class DryDelay
{
public:
    bool prepare(int channels, int blockSize, int latency, double sampleRate)
    {
        latency = juce::jmax(0, latency);
        const int transition = juce::jmax(1, static_cast<int>(sampleRate * 0.005));
        if (channels == history.getNumChannels() && blockSize == dry.getNumSamples()
            && latency == delaySamples && rate == sampleRate) return true;
        const int known = validSamples.load(std::memory_order_acquire);
        const bool compatible = rate == sampleRate && channels == history.getNumChannels()
            && latency <= known;
        const int retainedDelay = compatible ? delaySamples : latency;
        const int capacity = juce::jmax(latency, retainedDelay) + blockSize + transition + 1;
        juce::AudioBuffer<float> replacement(channels, capacity);
        replacement.clear();
        const int retained = rate == sampleRate ? juce::jmin(known, capacity - 1) : 0;
        for (int channel = 0; channel < juce::jmin(channels, history.getNumChannels()); ++channel)
            for (int age = 1; age <= retained; ++age)
                replacement.setSample(channel, capacity - age, history.getSample(channel,
                    (position + history.getNumSamples() - age) % history.getNumSamples()));
        history = std::move(replacement);
        pendingLatency.store(-1, std::memory_order_relaxed);
        validSamples.store(retained, std::memory_order_release);
        position = 0;
        oldDelaySamples = delaySamples;
        transitionRemaining = compatible && latency != delaySamples ? transition : 0;
        transitionLength = transition;
        delaySamples = latency;
        rate = sampleRate;
        dry.setSize(channels, blockSize, false, false, true);
        dry.clear();
        return compatible;
    }

    bool requestLatency(int latency) noexcept
    {
        latency = juce::jmax(0, latency);
        if (history.getNumChannels() == 0 || dry.getNumChannels() != history.getNumChannels() || rate == 0.0)
            return false;
        const int transition = juce::jmax(1, static_cast<int>(rate * 0.005));
        const int needed = latency + dry.getNumSamples() + transition + 1;
        if (latency > validSamples.load(std::memory_order_acquire) || needed > history.getNumSamples())
            return false;
        pendingLatency.store(latency, std::memory_order_release);
        return true;
    }

    void capture(const juce::AudioBuffer<float>& input) noexcept
    {
        applyPending();
        const int capacity = history.getNumSamples();
        const int samples = input.getNumSamples();
        const float transitionStep = 1.0f / static_cast<float>(transitionLength);
        for (int channel = 0; channel < input.getNumChannels(); ++channel)
        {
            const auto* source = input.getReadPointer(channel);
            auto* target = dry.getWritePointer(channel);
            auto* ring = history.getWritePointer(channel);
            if (delaySamples == 0 && transitionRemaining == 0)
            {
                juce::FloatVectorOperations::copy(target, source, samples);
                const int first = juce::jmin(samples, capacity - position);
                juce::FloatVectorOperations::copy(ring + position, source, first);
                juce::FloatVectorOperations::copy(ring, source + first, samples - first);
                continue;
            }
            int write = position, read = (position + capacity - delaySamples) % capacity;
            int oldRead = transitionRemaining > 0 ? (position + capacity - oldDelaySamples) % capacity : 0;
            int remaining = transitionRemaining;
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto current = delaySamples == 0 ? source[sample] : ring[read];
                if (remaining > 0)
                {
                    const auto old = oldDelaySamples == 0 ? source[sample] : ring[oldRead];
                    const auto weight = static_cast<float>(remaining--) * transitionStep;
                    target[sample] = current * (1.0f - weight) + old * weight;
                }
                else target[sample] = current;
                ring[write] = source[sample];
                if (++write == capacity) write = 0;
                if (++read == capacity) read = 0;
                if (++oldRead == capacity) oldRead = 0;
            }
        }
        position = (position + samples) % capacity;
        transitionRemaining = juce::jmax(0, transitionRemaining - samples);
        validSamples.store(juce::jmin(capacity - 1, validSamples.load(std::memory_order_relaxed) + samples), std::memory_order_release);
    }
    const juce::AudioBuffer<float>& output() const noexcept { return dry; }
    int channels() const noexcept { return dry.getNumChannels(); }
    size_t allocatedSamples() const noexcept { return static_cast<size_t>(history.getNumChannels()) * history.getNumSamples(); }

private:
    void applyPending() noexcept
    {
        const int next = pendingLatency.exchange(-1, std::memory_order_acq_rel);
        if (next < 0 || next == delaySamples) return;
        oldDelaySamples = delaySamples;
        transitionLength = juce::jmax(1, static_cast<int>(rate * 0.005));
        transitionRemaining = transitionLength;
        delaySamples = next;
    }

    juce::AudioBuffer<float> history, dry;
    std::atomic<int> pendingLatency { -1 };
    std::atomic<int> validSamples { 0 };
    int position = 0, delaySamples = 0, oldDelaySamples = 0;
    int transitionRemaining = 0, transitionLength = 1;
    double rate = 0;
};
