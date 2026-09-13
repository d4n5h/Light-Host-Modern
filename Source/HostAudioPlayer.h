#pragma once
#include "RealtimeHostProcessor.h"
#include "CallbackMeasurement.h"

// The JUCE convenience player constructs channel views and collects MIDI in its
// callback. This adapter prepares storage at device start and segments unexpected
// large driver blocks without resizing or taking a callback mutex.
class HostAudioPlayer final : public juce::AudioIODeviceCallback
{
public:
    void setProcessor(RealtimeHostProcessor* value) noexcept { processor = value; }
    lightHost::CallbackMeasurement& callbackMeasurement() noexcept { return measurement; }
    const lightHost::CallbackMeasurement& callbackMeasurement() const noexcept { return measurement; }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        if (!processor || !device) return;
        const int inputs = device->getActiveInputChannels().countNumberOfSetBits();
        const int outputs = device->getActiveOutputChannels().countNumberOfSetBits();
        channels = juce::jmax(inputs, outputs);
        blockSize = juce::jmax(1, device->getCurrentBufferSizeSamples());
        if (channels < 1 || channels > RealtimeHostProcessor::maxScratchChannels) { channels = 0; return; }
        storage.setSize(channels, blockSize);
        view.setDataToReferTo(storage.getArrayOfWritePointers(), channels, blockSize);
        processor->setPlayConfigDetails(inputs, outputs, device->getCurrentSampleRate(), blockSize);
        processor->prepareToPlay(device->getCurrentSampleRate(), blockSize);
        processor->prepareMidiBuffer(midi);
    }

    void audioDeviceStopped() override
    {
        measurement.stop();
        if (processor) processor->releaseResources();
        channels = 0;
    }

    void audioDeviceIOCallbackWithContext(const float* const* input, int inputChannels,
        float* const* output, int outputChannels, int samples, const juce::AudioIODeviceCallbackContext&) override
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
        if (!processor || channels == 0 || inputChannels > channels || outputChannels > channels)
        {
            for (int channel = 0; channel < outputChannels; ++channel)
                if (output[channel]) juce::FloatVectorOperations::clear(output[channel], samples);
            return;
        }
        for (int offset = 0; offset < samples; offset += blockSize)
        {
            const int count = juce::jmin(blockSize, samples - offset);
            for (int channel = 0; channel < channels; ++channel)
            {
                if (channel < inputChannels && input[channel]) storage.copyFrom(channel, 0, input[channel] + offset, count);
                else storage.clear(channel, 0, count);
            }
            view.setDataToReferTo(storage.getArrayOfWritePointers(), channels, count);
            midi.clear();
            processor->processBlock(view, midi);
            for (int channel = 0; channel < outputChannels; ++channel)
                if (output[channel]) juce::FloatVectorOperations::copy(output[channel] + offset, storage.getReadPointer(channel), count);
        }
    }

private:
    RealtimeHostProcessor* processor = nullptr;
    int channels = 0, blockSize = 1;
    juce::AudioBuffer<float> storage, view;
    juce::MidiBuffer midi;
    lightHost::CallbackMeasurement measurement;
};
