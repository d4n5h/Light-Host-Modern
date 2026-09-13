#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// This VST3 is a test fixture, never an internal effect or distribution payload.
// Every instance performs the same bounded gain operation and has independent
// parameter/state storage. It lets UI tests use actual loaded processors while
// keeping third-party fixtures out of packaged builds.
class ScenarioProcessor final : public juce::AudioProcessor
{
public:
    ScenarioProcessor() : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        gain = new juce::AudioParameterFloat(juce::ParameterID{"gain", 1}, "Gain", 0.0f, 1.0f, 0.999f);
        addParameter(gain);
    }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet() == layout.getMainOutputChannelSet()
            && (layout.getMainInputChannelSet() == juce::AudioChannelSet::mono() || layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals guard;
        audio.applyGain(gain->get());
    }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    void getStateInformation(juce::MemoryBlock& destination) override
    {
        std::array<std::uint32_t, 3> state{0x4c485354, 1, 0};
        const auto value = gain->get();
        std::memcpy(&state[2], &value, sizeof(value));
        destination.replaceAll(state.data(), sizeof(state));
    }
    void setStateInformation(const void* data, int size) override
    {
        std::array<std::uint32_t, 3> state{};
        if (size != sizeof(state)) return;
        std::memcpy(state.data(), data, sizeof(state));
        if (state[0] != 0x4c485354 || state[1] != 1) return;
        float value = 0;
        std::memcpy(&value, &state[2], sizeof(value));
        if (std::isfinite(value) && value >= 0 && value <= 1) gain->setValueNotifyingHost(value);
    }
private:
    juce::AudioParameterFloat* gain = nullptr;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ScenarioProcessor(); }
