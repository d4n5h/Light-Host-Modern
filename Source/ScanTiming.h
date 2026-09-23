#pragma once
#include "VerboseLog.h"
#include <chrono>

namespace lightHostModern::scan
{
// Scanner/control threads only. Disabled captures do not sample the clock.
class StageTiming
{
public:
    using Clock = std::chrono::steady_clock;
    StageTiming(const char* stage, std::string context = {})
        : enabled(verbose::logger().active()), name(stage), fields(std::move(context))
    { if (enabled) started = Clock::now(); }

    ~StageTiming() noexcept
    {
        if (!enabled) return;
        try {
            verbose::log("scan.timing", "stage=" + name + " durationMs="
                + std::to_string(milliseconds(Clock::now() - started)) + " " + fields);
        } catch (...) {}
    }

    bool active() const noexcept { return enabled; }
    double elapsedMs() const { return enabled ? milliseconds(Clock::now() - started) : 0.0; }
    void add(const char* key, double value)
    { if (enabled) fields += " " + std::string(key) + "=" + std::to_string(value); }
    template<class Duration> static double milliseconds(Duration duration)
    { return std::chrono::duration<double, std::milli>(duration).count(); }

private:
    bool enabled;
    std::string name, fields;
    Clock::time_point started{};
};
}
