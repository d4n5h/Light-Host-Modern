#include "DeviceController.h"
#include "GuardedAudioDeviceManager.h"
#include "ScenarioRunner.h"

void lightHostLog(const juce::String&) {}

namespace
{
struct Hardware
{
    StringArray names{"A", "B"};
    int creations = 0, opens = 0, scans = 0;
    bool fail = false;
    std::function<void()> opening;
};
class Device final : public AudioIODevice
{
public:
    Device(String name, Hardware& hardware, String backend) : AudioIODevice(name, backend), hardware(hardware) {}
    StringArray getOutputChannelNames() override { return {"Left", "Right", "Aux 1", "Aux 2"}; }
    StringArray getInputChannelNames() override { return {"Left", "Right", "Aux 1", "Aux 2"}; }
    Array<double> getAvailableSampleRates() override { return {44100, 48000, 96000}; }
    Array<int> getAvailableBufferSizes() override { return {64, 256, 1024}; }
    int getDefaultBufferSize() override { return 256; }
    String open(const BigInteger& in, const BigInteger& out, double rate, int block) override
    {
        ++hardware.opens;
        input = in; output = out; sampleRate = rate; blockSize = block;
        if (hardware.opening) { auto action = std::move(hardware.opening); action(); }
        opened = !hardware.fail;
        return opened ? String{} : "Simulated driver failure";
    }
    void close() override { opened = false; playing = false; }
    bool isOpen() override { return opened; }
    void start(AudioIODeviceCallback* callback) override { playing = opened; if (callback) callback->audioDeviceAboutToStart(this); }
    void stop() override { playing = false; }
    bool isPlaying() override { return playing; }
    String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    BigInteger getActiveOutputChannels() const override { return output; }
    BigInteger getActiveInputChannels() const override { return input; }
    int getOutputLatencyInSamples() override { return 16; }
    int getInputLatencyInSamples() override { return 8; }
private:
    Hardware& hardware;
    bool opened = false, playing = false;
    double sampleRate = 48000;
    int blockSize = 256;
    BigInteger input, output;
};
class DeviceType final : public AudioIODeviceType
{
public:
    explicit DeviceType(Hardware& hardware, String backend = "Simulated") : AudioIODeviceType(backend), hardware(hardware) {}
    void scanForDevices() override { ++hardware.scans; }
    StringArray getDeviceNames(bool) const override { return hardware.names; }
    int getDefaultDeviceIndex(bool) const override { return hardware.names.isEmpty() ? -1 : 0; }
    int getIndexOfDevice(AudioIODevice* device, bool) const override { return device ? hardware.names.indexOf(device->getName()) : -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    AudioIODevice* createDevice(const String& output, const String& input) override
    {
        ++hardware.creations;
        if ((!input.isEmpty() && !hardware.names.contains(input)) || (!output.isEmpty() && !hardware.names.contains(output))) return nullptr;
        return new Device(output.isEmpty() ? input : output, hardware, getTypeName());
    }
private:
    Hardware& hardware;
};
class Manager final : public AudioDeviceManager
{
public:
    Hardware hardware;
    Hardware alternative;
    bool includeAlternative = false;
    GuardedAudioDeviceType::Policy policy;
    void createAudioDeviceTypes(OwnedArray<AudioIODeviceType>& types) override
    {
        types.add(new GuardedAudioDeviceType(std::make_unique<DeviceType>(hardware),
            [this](const auto& backend, const auto& in, const auto& out) { return policy && policy(backend, in, out); }));
        if (includeAlternative) types.add(new GuardedAudioDeviceType(std::make_unique<DeviceType>(alternative, "Alternative"),
            [this](const auto& backend, const auto& in, const auto& out) { return policy && policy(backend, in, out); }));
    }
};
struct Scenario
{
    Manager manager;
    PropertySet settings;
    DeviceController::Clock::time_point now{};
    DeviceController controller{manager, settings, {}, {}, [this] { return now; }};
    Scenario(const String& mode)
    {
        manager.policy = [this](const auto& backend, const auto& in, const auto& out) { return controller.isAudioDeviceChoiceAllowed(backend, in, out); };
        settings.setValue("audioPersistenceMode", mode);
        settings.setValue("audioPersistenceRetrySeconds", 1);
        settings.setValue("audioPersistenceRetryAttempts", 3);
        for (const auto prefix : {"audioPersistenceLast", "audioPersistenceCustom"})
        {
            settings.setValue(String(prefix) + "Backend", "Simulated");
            settings.setValue(String(prefix) + "InputDevice", "A");
            settings.setValue(String(prefix) + "OutputDevice", "A");
        }
    }
    void advance() { now += std::chrono::seconds(1); controller.tick(); }
};
}

int main()
{
    using scenarios::require;
    scenarios::Runner tests;
    tests.run("all modes preserve explicit suspension and never open blocked drivers", [] {
        for (const auto mode : {"disabled", "lastSelected", "custom"})
        {
            Scenario scenario(mode);
            scenario.controller.start(false, true);
            for (int i = 0; i < 60; ++i) scenario.advance();
            require(scenario.manager.hardware.creations == 0, "Suspended profile touched driver creation");
            require(scenario.controller.setAudioBackendByIndex(0), "Explicit device selection failed");
            const auto generation = scenario.controller.getGeneration();
            require(scenario.controller.addBlockedAudioBackend("Simulated"), "Could not block backend");
            scenario.advance();
            const auto calls = scenario.manager.hardware.creations;
            for (int i = 0; i < 60; ++i) scenario.advance();
            require(scenario.manager.hardware.creations == calls, "Blocked state repeatedly accessed driver");
            require(!scenario.manager.getCurrentAudioDevice(), "Blocked device was left open");
            auto config = scenario.controller.getAudioDeviceConfiguration();
            require(config.currentInputDeviceIndex == -1 && config.currentOutputDeviceIndex == -1, "Blocked state did not show None");
            require(scenario.controller.getGeneration() > generation, "Policy did not invalidate previous generation");
            require(scenario.settings.getValue("audioPersistenceLastInputDevice") == "A", "Blocked device lost configured target");
        }
    });
    tests.run("disappearance, bounded retries and device return", [] {
        for (const auto mode : {"lastSelected", "custom"})
        {
            Scenario scenario(mode);
            scenario.manager.hardware.names = {"B"};
            scenario.controller.start(false, false);
            for (int i = 0; i < 8; ++i) scenario.advance();
            auto state = scenario.controller.createDiagnosticsSnapshot(scenario.manager);
            require(state.recoveryAttempt == 3 && state.recoveryState == "failed", "Retry limit ignored");
            require(state.recoveryTargetInputDevice == "A", "Missing target replaced by approximation");
            scenario.manager.hardware.names = {"A", "B"};
            scenario.controller.devicesChanged();
            scenario.advance();
            require(scenario.manager.getCurrentAudioDevice() && scenario.manager.getCurrentAudioDevice()->getName() == "A", "Device return did not restore target");
        }
    });
    tests.run("manual selection supersedes retry without a timing grace period", [] {
        for (const auto mode : {"lastSelected", "custom"})
        {
            Scenario scenario(mode);
            scenario.manager.hardware.names = {"B"};
            scenario.controller.start(false, false);
            scenario.advance();
            require(scenario.controller.setAudioBackendByIndex(0), "Manual B selection failed");
            scenario.manager.hardware.names = {"A", "B"};
            scenario.controller.devicesChanged();
            for (int i = 0; i < 25; ++i) scenario.advance();
            require(scenario.manager.getCurrentAudioDevice()->getName() == "B", "Obsolete retry replaced manual device");
        }
    });
    tests.run("complete setup preserves masks, format and stale completions", [] {
        Scenario scenario("lastSelected");
        scenario.controller.start(false, true);
        AudioDeviceManager::AudioDeviceSetup setup;
        setup.inputDeviceName = "A"; setup.outputDeviceName = "A";
        setup.sampleRate = 96000; setup.bufferSize = 64;
        setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
        setup.inputChannels.setBit(2); setup.outputChannels.setBit(3);
        require(scenario.controller.apply(scenario.manager, "Simulated", setup).isEmpty(), "Complete setup failed");
        auto* device = scenario.manager.getCurrentAudioDevice();
        require(device->getCurrentSampleRate() == 96000 && device->getCurrentBufferSizeSamples() == 64
            && device->getActiveInputChannels() == setup.inputChannels && device->getActiveOutputChannels() == setup.outputChannels, "Full configuration was not applied");
        scenario.manager.closeAudioDevice();
        scenario.manager.hardware.opening = [&] { scenario.controller.setAudioPersistenceMode("disabled"); };
        require(scenario.controller.apply(scenario.manager, "Simulated", setup) == "Audio configuration was superseded", "Obsolete completion was accepted");
    });
    tests.run("saved retry values keep their limits", [] {
        Scenario scenario("custom");
        scenario.controller.setAudioPersistenceRetrySeconds(0);
        scenario.controller.setAudioPersistenceRetryAttempts(200);
        require(scenario.controller.getAudioRecoveryConfiguration().retrySeconds == 1
            && scenario.controller.getAudioRecoveryConfiguration().retryAttempts == 100, "Retry bounds changed");
        scenario.controller.setAudioPersistenceRetrySeconds(60);
        scenario.controller.setAudioPersistenceRetryAttempts(1);
        require(scenario.controller.getAudioRecoveryConfiguration().retrySeconds == 60
            && scenario.controller.getAudioRecoveryConfiguration().retryAttempts == 1, "Valid saved limits were lost");
    });
    tests.run("custom recovery choices use their own backend without opening it", [] {
        Scenario scenario("disabled");
        scenario.manager.includeAlternative = true;
        scenario.manager.alternative.names = {"C", "D"};
        scenario.controller.start(false, true);
        require(scenario.controller.setAudioBackendByIndex(0), "Could not open effective backend");
        require(scenario.controller.setAudioPersistenceCustomBackendByIndex(1), "Could not select custom backend");
        const auto choices = scenario.controller.getAudioDeviceConfiguration();
        require(choices.customInputDeviceNames == std::vector<String>{"C", "D"} && choices.inputDeviceNames == std::vector<String>{"A", "B"}, "Custom choices leaked the effective backend");
        require(scenario.controller.setAudioPersistenceCustomInputByIndex(0) && scenario.controller.setAudioPersistenceCustomOutputByIndex(1), "Could not select custom ports");
        const auto saved = scenario.controller.getAudioRecoveryConfiguration();
        require(saved.customBackend == "Alternative" && saved.customInputDevice == "C" && saved.customOutputDevice == "D", "Custom backend was overwritten by effective backend");
        require(scenario.manager.getCurrentAudioDevice()->getTypeName() == "Simulated" && scenario.manager.alternative.creations == 0, "Editing recovery choices opened a driver");
    });
    tests.run("named configuration applies complete masks and reports actual driver format", [] {
        Scenario scenario("disabled"); scenario.controller.start(false, true);
        AudioDeviceSelection selected; selected.backend = "Simulated";
        selected.setup.inputDeviceName = "B"; selected.setup.outputDeviceName = "A";
        selected.setup.inputChannels.setBit(2); selected.setup.outputChannels.setBit(1); selected.setup.outputChannels.setBit(3);
        selected.setup.useDefaultInputChannels = selected.setup.useDefaultOutputChannels = false;
        selected.setup.sampleRate = 96000; selected.setup.bufferSize = 64;
        selected.expectedGeneration = scenario.controller.getGeneration();
        auto value = lightHost::audioSelection::setupJson(selected.backend, selected.setup);
        value.getDynamicObject()->setProperty("expectedGeneration", String(selected.expectedGeneration));
        AudioDeviceSelection parsed;
        require(lightHost::audioSelection::parse(value, parsed), "Complete named request was rejected");
        require(scenario.controller.selectConfiguration(parsed), "Named selection failed");
        const auto result = scenario.controller.selectionState();
        require(result["configured"]["input"].toString() == "B" && result["effective"]["output"].toString() == "A", "Names or direction were changed");
        require(result["effective"]["inputMask"].toString() == "100" && result["effective"]["outputMask"].toString() == "1010", "Channel masks were lost");
        require(static_cast<double>(result["effective"]["sampleRate"]) == 96000 && static_cast<int>(result["effective"]["bufferSize"]) == 64, "Effective format was not confirmed");
        const auto calls = scenario.manager.hardware.creations, opens = scenario.manager.hardware.opens;
        require(!scenario.controller.selectConfiguration(parsed) && scenario.manager.hardware.creations == calls && scenario.manager.hardware.opens == opens,
            "Stale generation accessed a driver");
        value.getDynamicObject()->setProperty("inputMask", String::repeatedString("1", 257));
        require(!lightHost::audioSelection::parse(value, parsed), "More than 256 channels were accepted");
        selected.expectedGeneration = scenario.controller.getGeneration();
        selected.setup.useDefaultInputChannels = selected.setup.useDefaultOutputChannels = true;
        selected.setup.inputChannels.clear(); selected.setup.outputChannels.clear();
        require(scenario.controller.selectConfiguration(selected), "Saved device pair could not be reopened");
        const auto restored = scenario.controller.selectionState();
        require(restored["effective"]["inputMask"].toString() == "100" && restored["effective"]["outputMask"].toString() == "1010", "Default selection lost the saved channel masks");
    });
    tests.run("named options and preferred draft commits never open a driver", [] {
        Scenario scenario("custom"); scenario.controller.start(false, true);
        scenario.manager.includeAlternative = true; scenario.manager.alternative.names = {"C", "D"};
        const auto initial = scenario.controller.getGeneration();
        const auto options = scenario.controller.optionsForBackend("Alternative");
        require(static_cast<bool>(options["available"]) && options["inputs"].getArray()->getFirst().toString() == "C", "Wrong named backend options");
        require(scenario.controller.getGeneration() == initial && scenario.manager.alternative.creations == 0, "Reading options mutated selection");
        require(scenario.controller.setPreferredDevice("Alternative", "C", "D", initial), "Atomic preferred selection failed");
        const auto recovery = scenario.controller.getAudioRecoveryConfiguration();
        require(recovery.customBackend == "Alternative" && recovery.customInputDevice == "C" && recovery.customOutputDevice == "D", "Preferred target was partially committed");
        require(scenario.manager.alternative.creations == 0 && scenario.manager.hardware.creations == 0, "Saving a preferred target opened a driver");
        require(!scenario.controller.setPreferredDevice("Simulated", "A", "A", initial), "Stale preferred edit replaced a newer one");
    });
    tests.run("opening a suspended backend resets obsolete default channel requirements", [] {
        Scenario scenario("disabled");
        require(scenario.manager.initialise(0, 2, nullptr, false).isEmpty(), "Could not seed an output-only manager");
        scenario.manager.closeAudioDevice(); scenario.controller.start(false, true);
        AudioDeviceSelection selected; selected.backend = "Simulated";
        selected.setup.inputDeviceName = selected.setup.outputDeviceName = "A";
        selected.setup.useDefaultInputChannels = selected.setup.useDefaultOutputChannels = true;
        selected.expectedGeneration = scenario.controller.getGeneration();
        require(scenario.controller.selectConfiguration(selected), "Suspended backend did not open");
        const auto effective = scenario.controller.selectionState()["effective"];
        require(effective["inputMask"].toString() == String::repeatedString("1", 256)
            && effective["outputMask"].toString() == String::repeatedString("1", 256), "Previous manager defaults disabled the selected input");
    });
    tests.run("a failed manual open preserves an editable named attempt without saving it", [] {
        Scenario scenario("disabled"); scenario.controller.start(false, true);
        scenario.manager.hardware.fail = true;
        AudioDeviceSelection selected; selected.backend = "Simulated";
        selected.setup.inputDeviceName = "B"; selected.setup.outputDeviceName = "A";
        selected.expectedGeneration = scenario.controller.getGeneration();
        require(!scenario.controller.selectConfiguration(selected), "Broken driver unexpectedly opened");
        const auto state = scenario.controller.selectionState();
        require(!static_cast<bool>(state["driverAvailable"]) && state["editable"]["backend"].toString() == "Simulated"
            && state["attempted"]["input"].toString() == "B", "Failed selection cannot be edited by name");
        const auto options = scenario.controller.getAudioDeviceConfiguration();
        require(options.currentBackendIndex == 0 && options.inputDeviceNames.size() == 2 && options.currentInputDeviceIndex == -1,
            "Failed backend lost its permitted input/output choices");
        require(scenario.settings.getValue("audioPersistenceLastInputDevice") == "A", "Failed selection overwrote saved preferences");
        scenario.controller.setAudioPersistenceMode("lastSelected");
        require(scenario.controller.selectionState()["attempted"].isVoid(), "Old manual attempt survived configuration invalidation");
    });
    tests.run("blocked named choices never create devices and explicit None remains suspended", [] {
        Scenario scenario("lastSelected"); scenario.controller.start(false, true);
        AudioDeviceSelection selected; selected.backend = "Simulated"; selected.setup.inputDeviceName = selected.setup.outputDeviceName = "A";
        scenario.controller.addBlockedAudioBackend("Simulated"); selected.expectedGeneration = scenario.controller.getGeneration();
        require(!scenario.controller.selectConfiguration(selected) && scenario.manager.hardware.creations == 0, "Blocked named selection reached driver creation");
        scenario.controller.removeBlockedAudioBackend(0); selected.expectedGeneration = scenario.controller.getGeneration();
        require(scenario.controller.selectConfiguration(selected), "Permitted named selection failed");
        AudioDeviceSelection none; none.expectedGeneration = scenario.controller.getGeneration();
        require(scenario.controller.selectConfiguration(none) && !scenario.manager.getCurrentAudioDevice(), "Explicit None left a device open");
        const auto calls = scenario.manager.hardware.creations;
        for (int i = 0; i < 5; ++i) scenario.advance();
        require(calls == scenario.manager.hardware.creations && scenario.settings.getBoolValue("audioSelectionSuspended"), "Explicit None restarted automatically");
    });
    return tests.result();
}
