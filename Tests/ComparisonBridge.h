#pragma once
#include "CallbackMeasurement.h"
#include "RealtimeAudit.h"

// Compiled only in the isolated comparison copy. Forward to the unmodified
// JUCE player used by the frozen build; never substitute the new host adapter.
class ComparisonAudioPlayer final : public juce::AudioProcessorPlayer
{
public:
    lightHost::CallbackMeasurement measurement;
    void audioDeviceStopped() override
    {
        measurement.stop();
        juce::AudioProcessorPlayer::audioDeviceStopped();
    }
    void audioDeviceIOCallbackWithContext(const float* const* input, int inputs,
        float* const* output, int outputs, int samples, const juce::AudioIODeviceCallbackContext& context) override
    {
        lightHost::realtimeAudit::Scope audit(lightHost::realtimeAudit::Origin::host);
        struct TimedCallback
        {
            lightHost::CallbackMeasurement& measurement;
            bool active;
            uint64 started;
            unsigned samples;
            TimedCallback(lightHost::CallbackMeasurement& value, int count) noexcept
                : measurement(value), active(value.enabled()), started(active ? static_cast<uint64>(juce::Time::getHighResolutionTicks()) : 0), samples(static_cast<unsigned>(juce::jmax(0, count))) {}
            ~TimedCallback() { if (active) measurement.record(started, static_cast<uint64>(juce::Time::getHighResolutionTicks()), samples); }
        } timed(measurement, samples);
        juce::AudioProcessorPlayer::audioDeviceIOCallbackWithContext(input, inputs, output, outputs, samples, context);
    }
};

inline juce::var comparisonMeasurementReply(const lightHost::CallbackMeasurement::Snapshot& data)
{
    using namespace juce;
    auto* result = new DynamicObject(); result->setProperty("status", "ok");
    static const char* phases[] = {"disabled", "armed", "warmingUp", "measuring", "completed", "interrupted"};
    result->setProperty("phase", phases[static_cast<unsigned>(data.phase)]);
    result->setProperty("frequency", String(data.frequency));
    result->setProperty("firstCallbackTick", String(data.firstCallbackTick));
    result->setProperty("windowStartTick", String(data.windowStartTick));
    result->setProperty("windowEndTick", String(data.windowEndTick));
    result->setProperty("callbacks", String(data.callbacks)); result->setProperty("samples", String(data.samples));
    result->setProperty("totalTicks", String(data.totalTicks)); result->setProperty("maximumTicks", String(data.maximumTicks));
    result->setProperty("p95UpperTicks", data.quantilesAvailable ? var(String(data.p95UpperTicks)) : var());
    result->setProperty("p99UpperTicks", data.quantilesAvailable ? var(String(data.p99UpperTicks)) : var());
    result->setProperty("quantileRelativeErrorBound", 1.0 / 256.0);
    result->setProperty("hostAllocationAuditAvailable", lightHost::realtimeAudit::available.load());
    result->setProperty("hostAllocations", lightHost::realtimeAudit::available.load() ? var(String(lightHost::realtimeAudit::hostAllocations.load())) : var());
    result->setProperty("hostFrees", lightHost::realtimeAudit::available.load() ? var(String(lightHost::realtimeAudit::hostFrees.load())) : var());
    result->setProperty("thirdPartyAllocationAuditAvailable", false);
    return var(result);
}
