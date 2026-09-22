#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace lightHostModern
{
// One audio writer; configuration and stop require a stopped device. The fixed
// histogram is read only after completion/stop, so the callback needs no locks,
// allocation, per-bin atomics, or a reader handshake. Quantile upper bounds have
// at most 1/256 relative error; the maximum and accumulated ticks are exact.
class CallbackMeasurement
{
public:
    enum class Phase : unsigned { disabled, armed, warmingUp, measuring, completed, interrupted };
    struct Snapshot
    {
        Phase phase = Phase::disabled;
        std::uint64_t frequency = 0, firstCallbackTick = 0, windowStartTick = 0, windowEndTick = 0;
        std::uint64_t callbacks = 0, samples = 0, totalTicks = 0, maximumTicks = 0;
        std::uint64_t p95UpperTicks = 0, p99UpperTicks = 0;
        bool quantilesAvailable = false;
    };

    void configure(std::uint64_t ticksPerSecond, unsigned warmupSeconds, unsigned durationSeconds)
    {
        if (!ticksPerSecond || ticksPerSecond > 1000000000000ull || warmupSeconds > 300
            || !durationSeconds || durationSeconds > 1800 || phase.load() != Phase::disabled)
            throw std::invalid_argument("Invalid or already armed callback measurement");
        frequency = ticksPerSecond;
        warmupTicks = ticksPerSecond * warmupSeconds;
        durationTicks = ticksPerSecond * durationSeconds;
        phase.store(Phase::armed, std::memory_order_release);
    }

    bool enabled() const noexcept
    {
        const auto value = phase.load(std::memory_order_relaxed);
        return value == Phase::armed || value == Phase::warmingUp || value == Phase::measuring;
    }

    void record(std::uint64_t started, std::uint64_t ended, unsigned sampleCount) noexcept
    {
        auto current = phase.load(std::memory_order_acquire);
        if (current == Phase::disabled || current == Phase::completed || current == Phase::interrupted || ended < started) return;
        if (current == Phase::armed)
        {
            firstCallbackTick.store(started, std::memory_order_relaxed);
            windowStartTick = started + warmupTicks;
            windowEndTick = windowStartTick + durationTicks;
            phase.store(Phase::warmingUp, std::memory_order_release);
        }
        if (started < windowStartTick) return;
        if (started >= windowEndTick) { phase.store(Phase::completed, std::memory_order_release); return; }
        const auto duration = ended - started;
        ++histogram[bucket(duration)];
        callbacks.fetch_add(1, std::memory_order_relaxed);
        samples.fetch_add(sampleCount, std::memory_order_relaxed);
        totalTicks.fetch_add(duration, std::memory_order_relaxed);
        if (duration > maximumTicks.load(std::memory_order_relaxed)) maximumTicks.store(duration, std::memory_order_relaxed);
        phase.store(Phase::measuring, std::memory_order_release);
    }

    // Called only after the audio device has joined its callback thread.
    void stop() noexcept
    {
        const auto value = phase.load(std::memory_order_relaxed);
        if (value == Phase::warmingUp || value == Phase::measuring) phase.store(Phase::interrupted, std::memory_order_release);
    }

    Snapshot snapshot() const noexcept
    {
        Snapshot result;
        result.phase = phase.load(std::memory_order_acquire);
        if (result.phase == Phase::disabled) return result;
        result.frequency = frequency;
        result.firstCallbackTick = firstCallbackTick.load(std::memory_order_relaxed);
        result.windowStartTick = result.firstCallbackTick ? result.firstCallbackTick + warmupTicks : 0;
        result.windowEndTick = result.windowStartTick ? result.windowStartTick + durationTicks : 0;
        result.callbacks = callbacks.load(std::memory_order_relaxed);
        result.samples = samples.load(std::memory_order_relaxed);
        result.totalTicks = totalTicks.load(std::memory_order_relaxed);
        result.maximumTicks = maximumTicks.load(std::memory_order_relaxed);
        result.quantilesAvailable = result.callbacks && (result.phase == Phase::completed || result.phase == Phase::interrupted);
        if (result.quantilesAvailable)
        {
            const auto rank95 = (result.callbacks * 95 + 99) / 100;
            const auto rank99 = (result.callbacks * 99 + 99) / 100;
            std::uint64_t sum = 0;
            bool found95 = false;
            for (std::size_t index = 0; index < histogram.size(); ++index)
            {
                sum += histogram[index];
                if (!found95 && sum >= rank95) { result.p95UpperTicks = upperBound(index); found95 = true; }
                if (sum >= rank99) { result.p99UpperTicks = upperBound(index); break; }
            }
        }
        return result;
    }

    static std::size_t bucket(std::uint64_t ticks) noexcept
    {
        if (ticks < 256) return static_cast<std::size_t>(ticks);
        unsigned exponent = 0;
#if defined(_MSC_VER) && defined(_M_X64)
        unsigned long highest = 0;
        _BitScanReverse64(&highest, ticks);
        exponent = highest;
#else
        for (auto value = ticks; value >>= 1;) ++exponent;
#endif
        return (exponent - 7) * 256 + static_cast<std::size_t>((ticks >> (exponent - 8)) - 256);
    }
    static std::uint64_t upperBound(std::size_t index) noexcept
    {
        if (index < 256) return index;
        const auto shift = index / 256 - 1;
        return ((256 + static_cast<std::uint64_t>(index % 256)) << shift) | ((std::uint64_t{1} << shift) - 1);
    }

private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Callback counters must be lock-free");
    std::array<std::uint64_t, 57 * 256> histogram{};
    std::atomic<Phase> phase{Phase::disabled};
    std::uint64_t frequency = 0, warmupTicks = 0, durationTicks = 0, windowStartTick = 0, windowEndTick = 0;
    std::atomic<std::uint64_t> firstCallbackTick{0}, callbacks{0}, samples{0}, totalTicks{0}, maximumTicks{0};
};
}
