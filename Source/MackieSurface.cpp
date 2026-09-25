#include "MackieSurface.h"
#include "AudioEngine.h"

MackieSurface::MackieSurface(AudioEngine& engineIn) : engine(engineIn), touch(9, false) {}
MackieSurface::~MackieSurface() { close(); }

juce::StringArray MackieSurface::inputNames() const
{
    juce::StringArray names;
    for (const auto& device : juce::MidiInput::getAvailableDevices()) names.add(device.name);
    return names;
}

juce::String MackieSurface::open(int deviceIndex)
{
    close();
    const auto inputs = juce::MidiInput::getAvailableDevices();
    if (!juce::isPositiveAndBelow(deviceIndex, (int) inputs.size())) return "Choose a MIDI input";
    input = juce::MidiInput::openDevice(inputs[(size_t) deviceIndex].identifier, this);
    if (!input) return "Could not open " + inputs[(size_t) deviceIndex].name;
    input->start();
    const auto outputs = juce::MidiOutput::getAvailableDevices();
    for (const auto& device : outputs)
        if (device.name == inputs[(size_t) deviceIndex].name)
            output = juce::MidiOutput::openDevice(device.identifier);
    bank = 0;
    startTimer(50);
    return {};
}

void MackieSurface::close()
{
    stopTimer();
    if (input) input->stop();
    input.reset();
    output.reset();
}

void MackieSurface::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    juce::MessageManager::callAsync([this, message] {
        if (input) dispatch(message);
    });
}

void MackieSurface::dispatch(const juce::MidiMessage& message)
{
    const auto strips = engine.chainStrips();
    if (message.isPitchWheel())
    {
        const int channel = message.getChannel() - 1;
        if (channel < 0 || channel > 8 || !touch[(size_t) channel]) return;
        const float db = mackieFaderToDb(message.getPitchWheelValue());
        if (channel == 8) engine.setMasterGainLive(db);
        else if (const int index = bank * 8 + channel; index < (int) strips.size())
            engine.setStripGainLive(strips[(size_t) index].id, db);
        return;
    }
    if (message.isController() && message.getControllerNumber() >= 16 && message.getControllerNumber() <= 23)
    {
        const int channel = message.getControllerNumber() - 16;
        const int index = bank * 8 + channel;
        if (index >= (int) strips.size()) return;
        const float pan = applyVpot(strips[(size_t) index].pan, message.getControllerValue());
        engine.setStripPanLive(strips[(size_t) index].id, pan);
        dirtyPan[channel] = true;
        ++panSerial;
        return;
    }
    if (message.isNoteOn() || message.isNoteOff())
        note(message.getNoteNumber(), message.isNoteOn() && message.getVelocity() > 0);
}

void MackieSurface::note(int number, bool down)
{
    if (number >= 104 && number <= 112)
    {
        const int channel = number - 104;
        const bool was = touch[(size_t) channel];
        touch[(size_t) channel] = down;
        if (was && !down)
        {
            const auto strips = engine.chainStrips();
            if (channel == 8) engine.setMasterGain(engine.masterGainDb());
            else if (const int index = bank * 8 + channel; index < (int) strips.size())
                engine.setStripGain(strips[(size_t) index].id, strips[(size_t) index].gainDb);
        }
        return;
    }
    if (!down) return;
    const auto strips = engine.chainStrips();
    if (number == 46 || number == 47)
    {
        bank = nextBank(bank, (int) strips.size(), number == 47 ? 1 : -1);
        return;
    }
    int channel = -1;
    if (number >= 8 && number <= 15) channel = number - 8;
    else if (number >= 16 && number <= 23) channel = number - 16;
    const int index = bank * 8 + channel;
    if (channel < 0 || index >= (int) strips.size()) return;
    if (number >= 16) engine.setStripMuted(strips[(size_t) index].id, !strips[(size_t) index].muted);
    else engine.setStripSolo(strips[(size_t) index].id, !strips[(size_t) index].solo);
}

void MackieSurface::sendFader(int channel, float db)
{
    if (output && !touch[(size_t) channel])
        output->sendMessageNow(juce::MidiMessage::pitchWheel(channel + 1, dbToMackieFader(db)));
}

void MackieSurface::sendButton(int noteNumber, bool on)
{
    if (output) output->sendMessageNow(juce::MidiMessage::noteOn(1, noteNumber, on ? (juce::uint8) 127 : (juce::uint8) 0));
}

void MackieSurface::timerCallback()
{
    const auto strips = engine.chainStrips();
    for (int channel = 0; channel < 8; ++channel)
    {
        const int index = bank * 8 + channel;
        if (index >= (int) strips.size()) continue;
        const auto& strip = strips[(size_t) index];
        sendFader(channel, strip.gainDb);
        sendButton(16 + channel, strip.muted);
        sendButton(8 + channel, strip.solo);
        if (dirtyPan[channel] && panSerial == flushedPan)
        {
            engine.setStripPan(strip.id, strip.pan);
            dirtyPan[channel] = false;
        }
    }
    sendFader(8, engine.masterGainDb());
    flushedPan = panSerial;
}
