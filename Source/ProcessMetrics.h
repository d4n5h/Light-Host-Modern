#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

namespace lightHost
{
// Shared by the host controller, callback and scanner accounting. The UI owns
// its process-local copy and never samples CPU while the host setting is off.
inline std::atomic<bool> diagnosticsCollectionEnabled{true};
// Windows process times are accumulated user+kernel time in 100 ns units.
// Percentages use all logical processors, matching the 0..100 process scale.
class CpuUsageSampler
{
public:
    void reset() { previous.reset(); cached.reset(); previousWall = 0; }
    std::optional<double> sample(std::optional<uint64_t> ticks, uint64_t wallMilliseconds, unsigned processors)
    {
        if (!ticks || processors == 0) { previous.reset(); cached.reset(); return {}; }
        if (!previous || *ticks < *previous || wallMilliseconds < previousWall)
        { previous = ticks; previousWall = wallMilliseconds; cached.reset(); return {}; }
        const auto elapsed = wallMilliseconds - previousWall;
        if (elapsed < 1000) return cached;
        cached = (std::clamp)(static_cast<double>(*ticks - *previous) / (elapsed * 10000.0 * processors) * 100.0, 0.0, 100.0);
        previous = ticks; previousWall = wallMilliseconds;
        return cached;
    }
private:
    std::optional<uint64_t> previous;
    uint64_t previousWall = 0;
    std::optional<double> cached;
};
inline std::atomic<uint64_t> workerCpuTicks{0};
#if defined(_WIN32)
inline std::optional<uint64_t> processCpuTicks(HANDLE process = GetCurrentProcess())
{
    FILETIME created{}, ended{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &ended, &kernel, &user)) return {};
    const auto ticks = [](FILETIME time) { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return ticks(kernel) + ticks(user);
}
inline unsigned processorCount() { return GetActiveProcessorCount(ALL_PROCESSOR_GROUPS); }
#endif
}
