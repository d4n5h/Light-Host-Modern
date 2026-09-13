#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace lightHost
{
struct AudioMeasurement
{
    float rms = 0, peak = 0, peakHold = 0;
    bool clipped = false;
};
struct MeterSnapshot
{
    static constexpr int maximumChannels = 256;
    int channelCount = 0;
    std::array<AudioMeasurement, maximumChannels> channels{};
    AudioMeasurement aggregate;
};

class AudioMeters
{
public:
    static constexpr int maximumChannels = MeterSnapshot::maximumChannels;
    // Controller only, with the callback excluded. Keep clipping latches, but
    // never mix RMS/hold history from before a diagnostics pause with new audio.
    void clearHistory() noexcept
    {
        std::fill(history.begin(), history.end(), 0.0);
        std::fill(states.begin(), states.end(), Channel{});
        writePosition = 0; sampleClock = 0;
        for (auto& channel : published)
        { channel.rms.store(0); channel.peak.store(0); channel.hold.store(0); }
    }
    // Controller only, under callback exclusion. Clipping survives preparation.
    void prepare(double sampleRate, int channelCount)
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 0 || sampleRate > 10000000)
            throw std::invalid_argument("Invalid audio meter sample rate");
        const int nextChannels = (std::clamp)(channelCount, 0, maximumChannels);
        const auto nextWindow = static_cast<size_t>((std::max)(1.0, std::ceil(sampleRate * 0.300)));
        const auto nextHold = static_cast<uint64_t>((std::max)(1.0, std::ceil(sampleRate * 1.500)));
        if (nextChannels == preparedChannels && nextWindow == window && nextHold == holdSamples) return;
        history.assign(nextWindow * static_cast<size_t>(nextChannels), 0.0);
        states.assign(static_cast<size_t>(nextChannels), {});
        preparedChannels = nextChannels; window = nextWindow; holdSamples = nextHold;
        writePosition = 0; sampleClock = 0;
        for (auto& channel : published)
        { channel.rms.store(0); channel.peak.store(0); channel.hold.store(0); }
        activeChannels.store(nextChannels, std::memory_order_release);
    }

    // Audio callback only. Prepared storage, complete sample history and no heap
    // or ownership operations. Null inputs represent silent channels.
    float process(const float* const* data, int channelCount, int samples) noexcept
    {
        if (samples <= 0 || window == 0) return 0;
        float blockPeak = 0;
        for (int channel = 0; channel < preparedChannels; ++channel)
        {
            auto& state = states[static_cast<size_t>(channel)];
            auto* ring = history.data() + static_cast<size_t>(channel) * window;
            const auto* input = data && channel < channelCount ? data[channel] : nullptr;
            size_t position = writePosition;
            double sum = state.sum;
            float peak = 0;
            bool clipped = false;
            for (int index = 0; index < samples; ++index)
            {
                const float raw = input ? input[index] : 0;
                const bool finite = std::isfinite(raw);
                const float amplitude = finite ? std::abs(raw) : 0;
                clipped = clipped || !finite || amplitude >= 1.0f;
                peak = (std::max)(peak, amplitude);
                const double square = static_cast<double>(amplitude) * amplitude;
                sum += square - ring[position]; ring[position] = square;
                if (++position == window) position = 0;
                const auto clock = sampleClock + static_cast<uint64_t>(index);
                if (amplitude >= state.hold || clock >= state.holdUntil)
                { state.hold = amplitude; state.holdUntil = clock + holdSamples; }
            }
            state.sum = (std::max)(0.0, sum);
            auto& output = published[static_cast<size_t>(channel)];
            output.rms.store(static_cast<float>(std::sqrt(state.sum / window)), std::memory_order_relaxed);
            output.peak.store(peak, std::memory_order_relaxed);
            output.hold.store(state.hold, std::memory_order_relaxed);
            if (clipped) output.clipped.store(true, std::memory_order_release);
            blockPeak = (std::max)(blockPeak, peak);
        }
        writePosition = (writePosition + static_cast<size_t>(samples)) % window;
        sampleClock += static_cast<uint64_t>(samples);
        return blockPeak;
    }

    MeterSnapshot snapshot() const noexcept
    {
        MeterSnapshot result;
        result.channelCount = activeChannels.load(std::memory_order_acquire);
        for (int index = 0; index < result.channelCount; ++index)
        {
            const auto& source = published[static_cast<size_t>(index)];
            auto& channel = result.channels[static_cast<size_t>(index)];
            channel.rms = source.rms.load(std::memory_order_relaxed);
            channel.peak = source.peak.load(std::memory_order_relaxed);
            channel.peakHold = source.hold.load(std::memory_order_relaxed);
            channel.clipped = source.clipped.load(std::memory_order_acquire);
            result.aggregate.rms = (std::max)(result.aggregate.rms, channel.rms);
            result.aggregate.peak = (std::max)(result.aggregate.peak, channel.peak);
            result.aggregate.peakHold = (std::max)(result.aggregate.peakHold, channel.peakHold);
            result.aggregate.clipped = result.aggregate.clipped || channel.clipped;
        }
        return result;
    }
    void resetClipping(int channel = -1) noexcept
    {
        if (channel >= 0 && channel < maximumChannels) published[static_cast<size_t>(channel)].clipped.store(false);
        else if (channel == -1) for (auto& value : published) value.clipped.store(false);
    }
private:
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
    struct Channel { double sum = 0; float hold = 0; uint64_t holdUntil = 0; };
    struct Published { std::atomic<float> rms{0}, peak{0}, hold{0}; std::atomic<bool> clipped{false}; };
    std::array<Published, maximumChannels> published;
    std::atomic<int> activeChannels{0};
    std::vector<double> history;
    std::vector<Channel> states;
    int preparedChannels = 0;
    size_t window = 0, writePosition = 0;
    uint64_t sampleClock = 0, holdSamples = 1;
};
}
