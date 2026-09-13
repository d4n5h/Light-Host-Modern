#include "GuardedAudioDeviceManager.h"
#include <iostream>

class SimulatedType final : public juce::AudioIODeviceType
{
public:
    explicit SimulatedType(int& attemptsIn) : AudioIODeviceType("Simulated"), attempts(attemptsIn) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return {"Blocked", "Allowed"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override
    {
        ++attempts;
        return nullptr; // Simulate a driver that fails at creation, without hardware.
    }
private:
    int& attempts;
};

class SimulatedManager final : public juce::AudioDeviceManager
{
public:
    int attempts = 0;
    bool allBlocked = false;
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
        types.add(new GuardedAudioDeviceType(std::make_unique<SimulatedType>(attempts),
            [this](const auto&, const auto& input, const auto& output) {
                return !allBlocked && input != "Blocked" && output != "Blocked";
            }));
    }
};

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
int main()
{
    try
    {
        SimulatedManager manager;
        auto* type = manager.getAvailableDeviceTypes()[0];
        require(type->getDeviceNames(false).contains("Blocked"), "blocked devices must remain visible in Settings");
        require(type->getDefaultDeviceIndex(false) == 1, "default selection must skip blocked devices");
        require(!type->createDevice("Blocked", "Allowed") && manager.attempts == 0, "blocked output must not reach driver");
        require(!type->createDevice("Allowed", "Blocked") && manager.attempts == 0, "blocked input must not reach driver");
        type->createDevice("Allowed", "Allowed");
        require(manager.attempts == 1, "allowed pair must delegate exactly once");
        manager.allBlocked = true;
        require(type->getDefaultDeviceIndex(false) == -1, "no permitted default must remain None");
        type->createDevice("Allowed", "Allowed");
        require(manager.attempts == 1, "updated policy must apply at creation time");

        juce::XmlElement state("DEVICESETUP");
        state.setAttribute("deviceType", "Simulated");
        state.setAttribute("audioInputDeviceName", "Blocked");
        state.setAttribute("audioOutputDeviceName", "Blocked");
        manager.initialise(2, 2, &state, true);
        manager.restartLastAudioDevice();
        require(manager.attempts == 1, "JUCE restore, fallback and restart must not reach a prohibited driver");
        manager.allBlocked = false;
        state.setAttribute("audioInputDeviceName", "Allowed");
        state.setAttribute("audioOutputDeviceName", "Allowed");
        manager.initialise(2, 2, &state, false);
        require(manager.attempts == 2, "explicit retry after re-enabling must reach driver");
        std::cout << "Device policy regressions passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
