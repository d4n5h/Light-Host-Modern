#include "CallbackMeasurement.h"
#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
    using Measurement = lightHostModern::CallbackMeasurement;
    try
    {
        for (unsigned exponent = 0; exponent < 64; ++exponent)
            for (auto offset : {0ull, 1ull, 17ull, 255ull})
            {
                const auto value = (std::uint64_t{1} << exponent) | offset;
                const auto upper = Measurement::upperBound(Measurement::bucket(value));
                require(upper >= value && static_cast<long double>(upper - value) <= value / 256.0L, "Quantile bucket exceeded its error bound");
            }
        require(Measurement::upperBound(Measurement::bucket(UINT64_MAX)) == UINT64_MAX, "Maximum tick value overflowed");
        Measurement meter;
        require(!meter.enabled() && meter.snapshot().phase == Measurement::Phase::disabled, "Measurement started without explicit configuration");
        meter.configure(1000, 1, 2);
        meter.record(100, 900, 64); // Warmup's longest callback must be excluded.
        meter.record(1099, 1100, 64);
        for (unsigned i = 0; i < 100; ++i) meter.record(1100 + i * 20, 1101 + i * 20 + i, 128);
        require(!meter.snapshot().quantilesAvailable, "A live histogram was exposed to a reader");
        meter.record(3100, 4100, 256); // The end boundary also must be excluded.
        const auto result = meter.snapshot();
        require(result.phase == Measurement::Phase::completed && result.quantilesAvailable, "Measurement did not complete");
        require(result.callbacks == 100 && result.samples == 12800 && result.totalTicks == 5050, "Warmup/end boundaries changed the actual work count");
        require(result.p95UpperTicks == 95 && result.p99UpperTicks == 99 && result.maximumTicks == 100, "Percentiles or maximum are incorrect");
        bool rejected = false;
        try { meter.configure(1000, 0, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "A completed measurement was overwritten");
        Measurement concurrent;
        concurrent.configure(100000, 0, 1);
        std::thread writer([&] { for (unsigned i = 1; i <= 100001; ++i) concurrent.record(i, i + 2, 32); });
        bool incompleteRead = false;
        while (concurrent.enabled())
        {
            const auto partial = concurrent.snapshot();
            if (partial.quantilesAvailable && partial.p99UpperTicks != 2) incompleteRead = true;
        }
        writer.join();
        require(!incompleteRead, "A concurrent reader observed an incomplete histogram");
        require(concurrent.snapshot().callbacks == 100000, "Concurrent snapshot reads lost callbacks");
        Measurement stopped;
        stopped.configure(1000, 0, 1);
        stopped.record(100, 108, 64);
        stopped.stop();
        require(stopped.snapshot().phase == Measurement::Phase::interrupted && stopped.snapshot().maximumTicks == 8, "Interrupted measurements were reported as complete");
        std::cout << "Callback window, histogram precision, work counters and concurrent snapshots passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
