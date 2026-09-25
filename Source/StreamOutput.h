#pragma once
#include "MixCapture.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

class StreamOutput : private juce::AudioIODeviceCallback
{
public:
    juce::StringArray deviceNames();
    juce::String start(MixCapture& capture, const juce::String& deviceName, const juce::String& liveName, double sampleRate);
    void stop();
    bool running() const noexcept { return device != nullptr; }

private:
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    MixCapture* capture = nullptr;
    std::vector<float> left, right;
};
