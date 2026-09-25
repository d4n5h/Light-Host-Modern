#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <vector>

class AudioEngine;

inline float mackieFaderToDb(int bend)
{
    bend = juce::jlimit(0, 16383, bend);
    return -60.0f + (bend / 16383.0f) * 72.0f;
}

inline int dbToMackieFader(float db)
{
    db = juce::jlimit(-60.0f, 12.0f, db);
    return (int) std::lround((db + 60.0f) / 72.0f * 16383.0f);
}

inline float applyVpot(float pan, int value)
{
    const int steps = value & 0x3f;
    if (steps == 0) return pan;
    const float delta = (value & 0x40 ? -1.0f : 1.0f) * steps * 0.01f;
    return juce::jlimit(-1.0f, 1.0f, pan + delta);
}

inline int nextBank(int bank, int strips, int direction)
{
    const int banks = juce::jmax(1, (strips + 7) / 8);
    return (bank + (direction < 0 ? banks - 1 : 1)) % banks;
}

class MackieSurface : private juce::MidiInputCallback, private juce::Timer
{
public:
    explicit MackieSurface(AudioEngine& engine);
    ~MackieSurface() override;
    juce::StringArray inputNames() const;
    juce::String open(int deviceIndex);
    void close();

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void dispatch(const juce::MidiMessage&);
    void timerCallback() override;
    void note(int number, bool down);
    void sendFader(int channel, float db);
    void sendButton(int note, bool on);
    AudioEngine& engine;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    std::vector<bool> touch;
    int bank = 0;
    int panSerial = 0;
    int flushedPan = 0;
    bool dirtyPan[8] {};
};
