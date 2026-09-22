#include "AudioMeters.h"
#include "MeterScale.h"
#include "ProcessMetrics.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
int main()
{
    using namespace lightHostModern;
    try
    {
        CpuUsageSampler cpu;
        require(!cpu.sample(0, 1000, 4), "CPU needs a previous observation");
        require(cpu.sample(10000000, 2000, 4) == 25.0, "One CPU second on four processors is 25 percent");
        require(cpu.sample(20000000, 2500, 4) == 25.0, "CPU sampler must not publish faster than 1 Hz");
        require(cpu.sample(20000000, 3000, 4) == 25.0, "CPU interval includes all process work");
        require(cpu.sample(20000000, 4000, 4) == 0.0, "Idle worker CPU returns to zero");
        require(!cpu.sample({}, 5000, 4), "Unavailable counters must not look like zero CPU");
        AudioMeters meter;
        meter.prepare(1000, 2);
        std::vector<float> first(300, .5f), second(300, .25f);
        const float* data[]{first.data(), second.data()};
        meter.process(data, 2, 300);
        auto result = meter.snapshot();
        require(std::abs(result.aggregate.rms - .5f) < 1e-6 && result.channelCount == 2, "300 ms RMS and max-channel aggregation");
        std::fill(first.begin(), first.end(), 0.0f); std::fill(second.begin(), second.end(), 0.0f);
        meter.process(data, 2, 150);
        require(std::abs(meter.snapshot().aggregate.rms - std::sqrt(.125f)) < 1e-6, "Sliding RMS retains exactly half the 300 ms history");
        meter.process(data, 2, 150);
        require(meter.snapshot().aggregate.rms == 0, "Expired RMS history leaves the window");
        second[0] = 2.0f;
        meter.process(data, 2, 1);
        require(meter.snapshot().aggregate.peak == 2.0f && meter.snapshot().channels[1].clipped, "Above-full-scale samples are not clamped before measurement");
        second[0] = 0;
        for (int index = 0; index < 1499; ++index) meter.process(data, 2, 1);
        require(meter.snapshot().aggregate.peakHold == 2.0f, "Peak hold lasts 1.5 seconds");
        meter.process(data, 2, 1);
        require(meter.snapshot().aggregate.peakHold == 0 && meter.snapshot().aggregate.clipped, "Hold expires independently of clipping");
        meter.resetClipping(0);
        require(meter.snapshot().channels[1].clipped, "Individual reset cannot clear another channel");
        meter.resetClipping(1);
        require(!meter.snapshot().aggregate.clipped, "Reset works without another callback");
        first[0] = std::numeric_limits<float>::quiet_NaN();
        meter.process(data, 2, 1);
        require(meter.snapshot().channels[0].clipped && std::isfinite(meter.snapshot().aggregate.rms), "Nonfinite plugin samples do not poison the RMS history");
        meter.prepare(48000, 2);
        require(meter.snapshot().channels[0].clipped, "Preparation preserves persistent clipping");
        meter.resetClipping();
        require(!meter.snapshot().aggregate.clipped, "Global clipping reset");
        for (int channels : {2, 32, 64, 256})
        {
            meter.prepare(48000, channels);
            std::vector<float> sine(14400);
            for (size_t i = 0; i < sine.size(); ++i) sine[i] = .5f * std::sin(static_cast<float>(i * 2 * 3.141592653589793 * 1000 / 48000));
            std::vector<const float*> pointers(static_cast<size_t>(channels), sine.data());
            meter.process(pointers.data(), channels, static_cast<int>(sine.size()));
            require(meter.snapshot().channelCount == channels && std::abs(meter.snapshot().aggregate.rms - .353553f) < 1e-5, "Large callback and 2/32/64/256-channel sine RMS");
            meter.process(nullptr, 0, static_cast<int>(sine.size()));
            require(meter.snapshot().aggregate.rms < 1e-6, "Disconnected input advances silence history");
        }
        require(lightHostModern::meterSegments(lightHostModern::amplitudeDb(.001)) == 0, "-60 dBFS floor");
        require(lightHostModern::meterSegments(lightHostModern::amplitudeDb(1)) == 28, "0 dBFS ceiling");
        require(std::abs(lightHostModern::amplitudeDb(2)-6.020599913)<1e-6, "Overload dB was clamped");
        require(!std::isfinite(lightHostModern::amplitudeDb(0)), "Silence is not minus infinity");
        lightHostModern::PresentationPeak presentation;
        presentation.process(1, 64, 48000);
        for (int i=0;i<30;++i) presentation.process(0,64,48000);
        require(presentation.read()==1, "Transient was lost between 20Hz UI samples");
        for (int i=0;i<50;++i) presentation.process(0,64,48000);
        require(presentation.read()==0, "Presentation retains a stale maximum");
        std::cout << "RMS, peak hold, per-channel clipping and 256-channel meter scenarios passed\n";
    }
    catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
