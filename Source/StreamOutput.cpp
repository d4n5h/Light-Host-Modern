#include "StreamOutput.h"

juce::StringArray StreamOutput::deviceNames()
{
    juce::StringArray names;
    std::unique_ptr<juce::AudioIODeviceType> wasapi(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
    if (!wasapi) return names;
    wasapi->scanForDevices();
    for (const auto& name : wasapi->getDeviceNames(false)) names.add(name);
    return names;
}

juce::String StreamOutput::start(MixCapture& source, const juce::String& deviceName, const juce::String& liveName, double sampleRate)
{
    stop();
    if (deviceName.isEmpty()) return "Choose a stream device";
    if (deviceName == liveName) return "Stream device must not be the live output";
    type.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
    if (!type) return "WASAPI is unavailable";
    type->scanForDevices();
    device.reset(type->createDevice({}, deviceName));
    if (!device) return "Could not open " + deviceName;
    juce::BigInteger outputs;
    outputs.setRange(0, juce::jmin(2, device->getOutputChannelNames().size()), true);
    const auto error = device->open({}, outputs, sampleRate, device->getDefaultBufferSize());
    if (error.isNotEmpty()) { device.reset(); return error; }
    capture = &source;
    left.assign(16384, 0.0f);
    right.assign(16384, 0.0f);
    source.setStream(true);
    device->start(this);
    return {};
}

void StreamOutput::stop()
{
    if (device) { device->stop(); device->close(); device.reset(); }
    if (capture) capture->setStream(false);
    capture = nullptr;
    type.reset();
}

void StreamOutput::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* output, int outputs, int frames, const juce::AudioIODeviceCallbackContext&)
{
    if (outputs <= 0 || output == nullptr || output[0] == nullptr) return;
    const int n = juce::jmin(frames, (int) left.size());
    const int got = capture ? capture->popStream(left.data(), right.data(), n) : 0;
    juce::FloatVectorOperations::clear(output[0], frames);
    if (outputs > 1 && output[1]) juce::FloatVectorOperations::clear(output[1], frames);
    if (got > 0)
    {
        juce::FloatVectorOperations::copy(output[0], left.data(), got);
        if (outputs > 1 && output[1]) juce::FloatVectorOperations::copy(output[1], right.data(), got);
    }
}
