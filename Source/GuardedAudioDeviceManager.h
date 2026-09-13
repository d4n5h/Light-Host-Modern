#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>

// Guard the driver creation boundary, including JUCE's own fallback and restart
// paths. Enumeration remains unfiltered so Settings can re-enable devices.
class GuardedAudioDeviceType final : public juce::AudioIODeviceType,
                                     private juce::AudioIODeviceType::Listener
{
public:
    using Policy = std::function<bool(const juce::String&, const juce::String&, const juce::String&)>;
    GuardedAudioDeviceType(std::unique_ptr<juce::AudioIODeviceType> type, Policy policy)
        : AudioIODeviceType(type->getTypeName()), wrapped(std::move(type)), allowed(std::move(policy))
    {
        wrapped->addListener(this);
    }
    ~GuardedAudioDeviceType() override { wrapped->removeListener(this); }
    void scanForDevices() override { wrapped->scanForDevices(); }
    juce::StringArray getDeviceNames(bool input = false) const override { return wrapped->getDeviceNames(input); }
    int getDefaultDeviceIndex(bool input) const override
    {
        const auto names = getDeviceNames(input);
        const auto permits = [&](int index) {
            if (index < 0 || index >= names.size() || !allowed) return false;
            if (!hasSeparateInputsAndOutputs()) return allowed(getTypeName(), names[index], names[index]);
            return allowed(getTypeName(), input ? names[index] : juce::String(), input ? juce::String() : names[index]);
        };
        const int preferred = wrapped->getDefaultDeviceIndex(input);
        if (permits(preferred)) return preferred;
        for (int index = 0; index < names.size(); ++index)
            if (permits(index)) return index;
        return -1;
    }
    int getIndexOfDevice(juce::AudioIODevice* device, bool input) const override { return wrapped->getIndexOfDevice(device, input); }
    bool hasSeparateInputsAndOutputs() const override { return wrapped->hasSeparateInputsAndOutputs(); }
    juce::AudioIODevice* createDevice(const juce::String& output, const juce::String& input) override
    {
        if (!allowed || !allowed(getTypeName(), input, output)) return nullptr;
        return wrapped->createDevice(output, input);
    }
private:
    void audioDeviceListChanged() override { callDeviceChangeListeners(); }
    std::unique_ptr<juce::AudioIODeviceType> wrapped;
    Policy allowed;
};

class GuardedAudioDeviceManager : public juce::AudioDeviceManager
{
public:
    GuardedAudioDeviceType::Policy allowed;
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
        juce::OwnedArray<juce::AudioIODeviceType> drivers;
        juce::AudioDeviceManager::createAudioDeviceTypes(drivers);
        while (!drivers.isEmpty())
        {
            std::unique_ptr<juce::AudioIODeviceType> driver(drivers.removeAndReturn(0));
            types.add(new GuardedAudioDeviceType(std::move(driver), allowed));
        }
    }
};
