#include "RealtimeHostProcessor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>

void lightHostModernLog(const String&) {}
void setLightHostModernCrashContext(const String&) {}
bool installRealtimeAllocationAudit();

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class GainPlugin : public AudioPluginInstance
{
public:
    explicit GainPlugin(int channels)
        : AudioPluginInstance(BusesProperties().withInput("In", AudioChannelSet::discreteChannels(channels), true)
                                               .withOutput("Out", AudioChannelSet::discreteChannels(channels), true)) {}
    explicit GainPlugin(const BusesProperties& buses) : AudioPluginInstance(buses) {}
    int samples = 0;
    int largestBlock = 0;
    std::atomic<bool> hold { false }, entered { false };
    std::atomic<int> preparations { 0 };
    bool failPreparation = false;
    void fillInPluginDescription(PluginDescription&) const override {}
    const String getName() const override { return "Simulated gain"; }
    void prepareToPlay(double, int) override
    {
        preparations.fetch_add(1);
        if (failPreparation) throw std::runtime_error("Simulated prepare failure");
    }
    void releaseResources() override {}
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        entered.store(true);
        while (hold.load()) std::this_thread::yield();
        samples += buffer.getNumSamples();
        largestBlock = jmax(largestBlock, buffer.getNumSamples());
        buffer.applyGain(2.0f);
    }
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    void getStateInformation(MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

class AsymmetricPlugin final : public GainPlugin
{
public:
    AsymmetricPlugin() : GainPlugin(BusesProperties().withInput("Main", AudioChannelSet::stereo(), true)
        .withInput("Sidechain", AudioChannelSet::mono(), true).withInput("Disabled", AudioChannelSet::stereo(), false)
        .withOutput("Main", AudioChannelSet::mono(), true).withOutput("Auxiliary", AudioChannelSet::stereo(), true)) {}
    void processBlock(AudioBuffer<float>& audio, MidiBuffer&) override
    {
        require(audio.getNumChannels() == 3 && getBusCount(true) == 3 && !getBus(true, 2)->isEnabled(), "Bus layout flattened");
        require(audio.getMagnitude(2, 0, audio.getNumSamples()) == 0, "Sidechain received unrelated host channels");
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            audio.setSample(0, i, audio.getSample(0, i) * 2);
            audio.setSample(1, i, 100);
            audio.setSample(2, i, 100);
        }
    }
};

class MidiProducer final : public GainPlugin
{
public:
    MidiProducer() : GainPlugin(2), payload(60000, 0x01) { payload.front() = 0xf0; payload.back() = 0xf7; }
    void processBlock(AudioBuffer<float>&, MidiBuffer& midi) override
    {
        lightHostModern::realtimeAudit::Scope simulatedPlugin(lightHostModern::realtimeAudit::Origin::plugin);
        midi.clear();
        for (int i = 0; i < 40; ++i) midi.addEvent(payload.data(), static_cast<int>(payload.size()), i);
    }
    std::vector<uint8> payload;
};

class MidiStorageShrinker final : public GainPlugin
{
public:
    MidiStorageShrinker() : GainPlugin(2) {}
    void processBlock(AudioBuffer<float>&, MidiBuffer& midi) override
    {
        lightHostModern::realtimeAudit::Scope simulatedPlugin(lightHostModern::realtimeAudit::Origin::plugin);
        midi.clear();
        midi.data.minimiseStorageOverheads();
    }
};

int main()
{
    try
    {
        ScopedJuceInitialiser_GUI juce;
        require(installRealtimeAllocationAudit(), "Release CRT allocation interception was not installed");
        // Regression tests for the reviewed mono PR: unity sum, smooth toggle,
        // real stereo, mono plugin routing and dry paths use the same matrix.
        for (int pluginChannels : {0, 1, 2})
        {
            auto mono = std::make_unique<RealtimeHostProcessor>();
            mono->setPlayConfigDetails(2, 2, 48000, 64); mono->prepareToPlay(48000, 64);
            auto chain = std::make_shared<ChainSnapshot>();
            if (pluginChannels) chain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription{}, std::make_unique<GainPlugin>(pluginChannels)));
            mono->publishSnapshot(chain);
            AudioBuffer<float> audio(2, 64); MidiBuffer midi; mono->prepareMidiBuffer(midi);
            const auto fill = [&](float l, float r) { for (int i=0;i<64;++i) { audio.setSample(0,i,l); audio.setSample(1,i,r); } };
            for (int i=0;i<10;++i) { fill(.25f,.5f); mono->processBlock(audio,midi); }
            const auto previous = audio.getSample(0,63);
            mono->setMonoInputs(true); fill(.25f,.5f); mono->processBlock(audio,midi);
            require(std::abs(audio.getSample(0,0)-previous)<.01f,"Mono toggle dropped output abruptly");
            for (int i=0;i<10;++i) { fill(.25f,.5f); mono->processBlock(audio,midi); }
            const auto expected = pluginChannels ? 1.0f : .75f;
            require(std::abs(audio.getSample(0,63)-expected)<.0001f && std::abs(audio.getSample(1,63)-expected)<.0001f,"Mono unity sum or plugin centering failed");
            mono->setGlobalBypassed(true);
            for (int i=0;i<10;++i) { fill(.25f,.5f); mono->processBlock(audio,midi); }
            require(std::abs(audio.getSample(0,63)-.75f)<.0001f && std::abs(audio.getSample(1,63)-.75f)<.0001f,"Global dry route lost mono matrix");
            mono->setGlobalBypassed(false); mono->setMonoInputs(false);
            for (int i=0;i<10;++i) { fill(.25f,.5f); mono->processBlock(audio,midi); }
            require(std::abs(audio.getSample(0,63)-(pluginChannels ? .5f:.25f))<.0001f,"Stereo left not restored");
            if (pluginChannels != 1) require(std::abs(audio.getSample(1,63)-(pluginChannels ? 1.f:.5f))<.0001f,"True stereo not preserved");
            mono->setMonoInputs(true);
            for (int i=0;i<10;++i) { fill(.5f,-.5f); mono->processBlock(audio,midi); }
            require(audio.getMagnitude(0,64)<.0001f,"Mono phase cancellation changed");
        }
        // Physical main outputs must never be inferred from two packed channels.
        for (const auto mask : {0, 1, 2, 3, 5, 10, 12, 15})
        for (const auto rate : {48000.0, 96000.0})
        {
            auto output = std::make_unique<RealtimeHostProcessor>();
            BigInteger physical(mask);
            const int outputs = physical.countNumberOfSetBits();
            output->setPlayConfigDetails(4, outputs, rate, 64);
            output->configureOutputChannels(physical);
            output->prepareToPlay(rate, 64);
            AudioBuffer<float> audio(4, 513); MidiBuffer events;
            output->prepareMidiBuffer(events);
            const auto process = [&] {
                for (int ch = 0; ch < 4; ++ch) FloatVectorOperations::fill(audio.getWritePointer(ch), .2f * (ch + 1), 513);
                output->processBlock(audio, events);
            };
            process(); process();
            output->setMonoOutput(true);
            process();
            require(std::abs(audio.getSample(0, 0) - .2f) < .002f, "Output mono toggle caused a discontinuity");
            process();
            for (int ch = 0; ch < 4; ++ch) {
                const auto expected = (mask & 3) == 3 && ch < 2 ? .3f : .2f * (ch + 1);
                require(std::abs(audio.getSample(ch, 512) - expected) < .0001f, "Output mono changed an auxiliary/single output or failed averaging");
            }
            output->setDiagnosticsEnabled(false);
            for (int i = 0; i < 20; ++i) process(); // Expire presentation peak retention.
            const auto expectedPeak = outputs == 0 ? 0.0f : (mask & 3) == 3 && outputs == 2 ? .3f : .2f * outputs;
            require(std::abs(output->getMeterPeaks().second - expectedPeak) < .0001f, "Output meter included an unrouted input or missed final mono");
            output->setDiagnosticsEnabled(true);
            output->setMonoOutput(false); process(); process();
            require(std::abs(audio.getSample(0, 512) - .2f) < .0001f && std::abs(audio.getSample(1, 512) - .4f) < .0001f, "Output stereo was not restored");
            output->setMonoOutput(true); output->setGlobalMuted(true); process(); process();
            require(audio.getMagnitude(0, 513) == 0, "Output mono bypassed mute");
        }
        for (int pluginChannels : {0, 1, 2})
        for (bool inputMono : {false, true})
        for (bool outputMono : {false, true})
        {
            auto output = std::make_unique<RealtimeHostProcessor>();
            output->prepareToPlay(48000, 32);
            auto chain = std::make_shared<ChainSnapshot>();
            if (pluginChannels) chain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(pluginChannels)));
            output->publishSnapshot(chain);
            output->setMonoInputs(inputMono); output->setMonoOutput(outputMono);
            AudioBuffer<float> audio(2, 512); MidiBuffer events; output->prepareMidiBuffer(events);
            const auto process = [&](float left, float right) {
                FloatVectorOperations::fill(audio.getWritePointer(0), left, 512);
                FloatVectorOperations::fill(audio.getWritePointer(1), right, 512);
                output->processBlock(audio, events);
            };
            process(.2f,.6f); process(.2f,.6f);
            auto pin = [](float sample) { return std::abs(sample) <= 1.0f ? sample : std::copysign(1.0f, sample); };
            const float left = pin((inputMono ? .8f : .2f) * (pluginChannels ? 2.f : 1.f));
            const float right = pin(pluginChannels == 1 && !inputMono ? 0.f : (inputMono ? .8f : .6f) * (pluginChannels ? 2.f : 1.f));
            require(std::abs(audio.getSample(0,511) - (outputMono ? (left+right)*.5f : left)) < .0001f
                && std::abs(audio.getSample(1,511) - (outputMono ? (left+right)*.5f : right)) < .0001f, "Independent input/output mono combination failed");
            output->setGlobalBypassed(true); process(.2f,.6f); process(.2f,.6f);
            require(std::abs(audio.getSample(0,511) - (inputMono ? .8f : outputMono ? .4f : .2f)) < .0001f, "Global bypass lost output mono");
            output->setMonoInputs(false); output->setMonoOutput(true); process(.5f,-.5f); process(.5f,-.5f);
            require(audio.getMagnitude(0,512) < .0001f, "Output mono phase cancellation failed");
            process(.5f,.5f); process(.5f,.5f);
            require(std::abs(audio.getSample(0,511)-.5f)<.0001f, "Output mono doubled equal signals");
        }
        for (int pluginChannels : { 2, 32, 64, 256 })
        for (int hostChannels : { 2, 32, 64, 256 })
        {
            auto host = std::make_unique<RealtimeHostProcessor>();
            require(!host->isGlobalMuted() && !host->isGlobalBypassed(), "Global controls must start off");
            host->setPlayConfigDetails(hostChannels, hostChannels, 48000, 64);
            host->prepareToPlay(48000, 64);
            auto plugin = std::make_unique<GainPlugin>(pluginChannels);
            auto* gain = plugin.get();
            auto slot = std::make_shared<PluginSlot>(PluginDescription(), std::move(plugin));
            auto chain = std::make_shared<ChainSnapshot>();
            chain->inputChannels = chain->outputChannels = hostChannels;
            chain->slots.push_back(slot);
            host->publishSnapshot(chain);
            AudioBuffer<float> audio(hostChannels, 1001);
            MidiBuffer midi;
            midi.ensureSize(1024 * 1024);
            auto fill = [&] { for (int c = 0; c < hostChannels; ++c) FloatVectorOperations::fill(audio.getWritePointer(c), 0.25f, 1001); };
            fill(); host->processBlock(audio, midi); // Complete the resume ramp.
            fill();
            for (int offset : { 0, 63, 64, 511, 1000 }) midi.addEvent(MidiMessage::noteOn(1, 60, (uint8) 100), offset);
            host->processBlock(audio, midi);
            require(gain->samples == 2002 && gain->largestBlock == 64, "Segment dropped or oversized");
            require(midi.getNumEvents() == 5, "MIDI lost");
            int index = 0;
            const int expected[] { 0, 63, 64, 511, 1000 };
            for (const auto event : midi) require(event.samplePosition == expected[index++], "MIDI offset changed");
            for (int c = 0; c < hostChannels; ++c)
                require(std::abs(audio.getSample(c, 1000) - (c < pluginChannels ? 0.5f : 0.0f)) < 0.0001f, "Wrong channel output");
            slot->bypassed.store(true);
            fill(); host->processBlock(audio, midi);
            require(gain->samples == 3003, "Bypassed plugin stopped processing");
            require(std::abs(audio.getSample(0, 1000) - 0.25f) < 0.0001f, "Bypass dry output wrong");
            {
                RealtimeHostProcessor::ScopedSuspension suspension(*host);
                fill(); host->processBlock(audio, midi);
                require(audio.getMagnitude(0, 1001) == 0.0f && gain->samples == 3003, "Suspension allowed processing");
            }
            slot->bypassed.store(false);
            host->setGlobalBypassed(true);
            fill(); host->processBlock(audio, midi);
            for (int c = 0; c < hostChannels; ++c)
                require(std::abs(audio.getSample(c, 1000) - 0.25f) < 0.0001f, "Global bypass lost dry channel");
            host->setGlobalMuted(true);
            fill(); host->processBlock(audio, midi);
            require(audio.getMagnitude(256, 745) == 0.0f && gain->samples == 5005, "Mute must silence dry output without stopping plugins");
            host->setGlobalBypassed(false);
            fill(); host->processBlock(audio, midi);
            require(audio.getMagnitude(0, 1001) == 0.0f && gain->samples == 6006, "Mute must also silence wet output");
            host->setGlobalMuted(false);
            fill(); host->processBlock(audio, midi);
            require(std::abs(audio.getSample(0, 1000) - 0.5f) < 0.0001f && gain->samples == 7007, "Unmute must restore wet processing");
        }
        {
            auto measured = std::make_unique<RealtimeHostProcessor>();
            measured->prepareToPlay(48000, 64);
            auto measuredChain = std::make_shared<ChainSnapshot>();
            measuredChain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(2)));
            measured->publishSnapshot(measuredChain);
            AudioBuffer<float> audio(2, 14400);
            MidiBuffer events;
            const auto fill = [&] { FloatVectorOperations::fill(audio.getWritePointer(0), .75f, 14400); FloatVectorOperations::fill(audio.getWritePointer(1), .25f, 14400); };
            fill(); measured->processBlock(audio, events);
            fill(); measured->processBlock(audio, events);
            require(measured->getInputMeters().aggregate.rms == .75f && measured->getOutputMeters().aggregate.rms == 1.0f,
                "Input RMS must precede processing; output RMS must measure the pinned main pair");
            require(measured->getMeterPeaks() == std::make_pair(.75f, 1.0f),
                "Live meter peaks must reflect the pinned main pair");
            require(!measured->getInputMeters().aggregate.clipped && measured->getOutputMeters().channels[0].clipped
                && !measured->getOutputMeters().channels[1].clipped, "Clipping direction and channel isolation");
            measured->setGlobalBypassed(true);
            fill(); measured->processBlock(audio, events); fill(); measured->processBlock(audio, events);
            require(measured->getOutputMeters().aggregate.rms == .75f, "Output meter did not follow global dry selection");
            measured->setGlobalMuted(true);
            fill(); measured->processBlock(audio, events); fill(); measured->processBlock(audio, events);
            require(measured->getOutputMeters().aggregate.rms < 1e-6f && measured->getOutputMeters().aggregate.peak == 0
                && measured->getInputMeters().aggregate.rms == .75f,
                "Mute must silence only output measurements while processing continues");
            require(measured->getOutputMeters().aggregate.clipped, "Mute erased persistent clipping");
            measured->resetClipping(false, true, 0);
            require(!measured->getOutputMeters().aggregate.clipped, "Channel clipping reset requires no callback");
            const auto stats = measured->getStats();
            require(stats.processedBlocks == 6 && stats.processedSamples == 86400, "Processing counters lost large segmented blocks");
            require(measured->getMeterPeaks() == std::make_pair(.75f, 0.0f), "Live peaks must follow output mute");
            measured->releaseResources();
            require(measured->getMeterPeaks() == std::make_pair(0.0f, 0.0f), "Stopping audio must not leave stale peaks");
        }
        auto host = std::make_unique<RealtimeHostProcessor>();
        auto chain = std::make_shared<ChainSnapshot>();
        auto slot = std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(2));
        slot->outputChannels = 257;
        chain->slots.push_back(slot);
        bool rejected = false;
        try { host->publishSnapshot(chain); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && !host->getActiveSnapshot(), "Oversized layout activated");
        slot->outputChannels = 2;
        host->prepareToPlay(48000, 64);
        host->publishSnapshot(chain);
        auto* gain = static_cast<GainPlugin*>(slot->processor.get());
        gain->setLatencySamples(17);
        host->refreshLatencies();
        require(host->getStats().chainLatencySamples == 17, "Dynamic latency not applied");
        AudioBuffer<float> impulse(2, 64);
        impulse.clear(); impulse.setSample(0, 63, 1.0f);
        slot->captureDry(impulse);
        impulse.clear();
        slot->captureDry(impulse);
        slot->processBypass(impulse);
        require(impulse.getSample(0, 16) == 1.0f && impulse.getMagnitude(1, 0, 64) == 0.0f, "Dry history lost across blocks");
        MidiBuffer midi;
        host->setGlobalBypassed(true);
        for (int i = 0; i < 8; ++i) { impulse.clear(); host->processBlock(impulse, midi); }
        impulse.clear(); impulse.setSample(0, 63, 1.0f);
        host->processBlock(impulse, midi);
        impulse.clear(); host->processBlock(impulse, midi);
        require(std::abs(impulse.getSample(0, 16) - 1.0f) < 0.0001f, "Global bypass must retain chain latency across blocks");
        host->setGlobalMuted(true);
        gain->hold.store(true);
        gain->entered.store(false);
        std::thread callback([&] { host->processBlock(impulse, midi); });
        while (!gain->entered.load()) std::this_thread::yield();
        const int before = gain->preparations.load();
        std::atomic<bool> started { false };
        std::thread control([&] { started.store(true); host->prepareToPlay(96000, 128); });
        while (!started.load()) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const bool preparedDuringCallback = gain->preparations.load() != before;
        gain->hold.store(false);
        callback.join(); control.join();
        require(!preparedDuringCallback && gain->preparations.load() == before + 1, "Reconfiguration raced callback");
        require(host->isGlobalMuted() && host->isGlobalBypassed(), "Reconfiguration must preserve runtime controls");
        gain->failPreparation = true;
        host->prepareToPlay(48000, 1024);
        require(!slot->prepared && slot->processFailed.load(), "Failed preparation left stale buffers active");
        const int processedBeforeFailure = gain->samples;
        host->processBlock(impulse, midi);
        require(gain->samples == processedBeforeFailure, "Unprepared plugin was processed");
        {
            auto asymmetricHost = std::make_unique<RealtimeHostProcessor>();
            asymmetricHost->prepareToPlay(48000, 64);
            auto buses = std::make_shared<ChainSnapshot>();
            buses->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<AsymmetricPlugin>()));
            asymmetricHost->publishSnapshot(buses);
            AudioBuffer<float> audio(2, 512);
            MidiBuffer events;
            for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill(audio.getWritePointer(ch), 0.25f, 512);
            asymmetricHost->processBlock(audio, events);
            require(audio.getSample(0, 511) == 0.5f && audio.getMagnitude(1, 0, 512) == 0, "Auxiliary output escaped into main output");
        }
        {
            DryDelay history;
            history.prepare(2, 64, 17, 48000);
            AudioBuffer<float> constant(2, 64);
            for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill(constant.getWritePointer(ch), 0.25f, 64);
            for (int i = 0; i < 6; ++i) history.capture(constant);
            require(history.prepare(2, 64, 9, 48000), "Available history was not reused");
            for (int i = 0; i < 6; ++i)
            {
                history.capture(constant);
                for (int sample = 0; sample < 64; ++sample) require(std::abs(history.output().getSample(0, sample) - 0.25f) < 0.00001f, "Latency transition zeroed valid history");
            }
            require(history.allocatedSamples() < 2000, "Stereo delay reserved 256 channels");
        }
        {
            auto midiHost = std::make_unique<RealtimeHostProcessor>();
            midiHost->prepareToPlay(48000, 64);
            auto producers = std::make_shared<ChainSnapshot>();
            producers->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<MidiProducer>()));
            midiHost->publishSnapshot(producers);
            AudioBuffer<float> audio(2, 128);
            audio.clear();
            MidiBuffer events;
            midiHost->prepareMidiBuffer(events);
            midiHost->processBlock(audio, events);
            const int retained = lightHostModern::midiCapacityBytes / 60006;
            require(events.getNumEvents() == retained, "MIDI capacity did not preserve complete prefix");
            for (const auto event : events) require(event.numBytes == 60000 && event.data[0] == 0xf0 && event.data[59999] == 0xf7, "Partial MIDI event emitted");
            const auto stats = midiHost->getStats();
            require(stats.midiOverflow == 80 - static_cast<uint64>(retained), "MIDI overflow count incorrect");
            require(stats.pluginAllocations > 0, "Third-party MIDI growth was not distinguished");
        }
        require(lightHostModern::realtimeAudit::hostAllocations.load() == 0 && lightHostModern::realtimeAudit::hostFrees.load() == 0,
            "Host allocated or freed memory in a prepared callback");
        {
            auto shrinkHost = std::make_unique<RealtimeHostProcessor>();
            shrinkHost->prepareToPlay(48000, 64);
            auto shrinking = std::make_shared<ChainSnapshot>();
            shrinking->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<MidiStorageShrinker>()));
            shrinkHost->publishSnapshot(shrinking);
            AudioBuffer<float> audio(2, 512);
            audio.clear();
            MidiBuffer events;
            shrinkHost->prepareMidiBuffer(events);
            for (int offset = 0; offset < 512; offset += 64) events.addEvent(MidiMessage::noteOn(1, 60, uint8(100)), offset);
            shrinkHost->processBlock(audio, events);
            require(lightHostModern::realtimeAudit::hostAllocations.load() == 0 && lightHostModern::realtimeAudit::hostFrees.load() == 0,
                "Plugin shrinking its MIDI buffer caused a host allocation");
            shrinkHost->collectRetiredSnapshots(); // Storage repair happens on the controller.
        }
        {
            auto diagnosticsHost = std::make_unique<RealtimeHostProcessor>();
            diagnosticsHost->prepareToPlay(48000, 64);
            auto chain = std::make_shared<ChainSnapshot>();
            auto processor = std::make_unique<GainPlugin>(2);
            auto* processed = processor.get();
            chain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::move(processor)));
            diagnosticsHost->publishSnapshot(chain);
            AudioBuffer<float> audio(2, 64); MidiBuffer events;
            diagnosticsHost->prepareMidiBuffer(events);
            const auto process = [&] {
                for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill(audio.getWritePointer(ch), 0.25f, 64);
                diagnosticsHost->processBlock(audio, events);
            };
            process(); const auto before = diagnosticsHost->getStats();
            diagnosticsHost->setDiagnosticsEnabled(false);
            for (int i = 0; i < 10; ++i) process();
            const auto paused = diagnosticsHost->getStats();
            require(paused.processedBlocks == before.processedBlocks && paused.processedSamples == before.processedSamples,
                    "disabled diagnostics still collected callback counters");
            require(processed->samples == 11 * 64 && audio.getMagnitude(0, 64) == 0.5f,
                    "disabling diagnostics stopped or changed plugin processing");
            require(diagnosticsHost->getMeterPeaks() == std::pair<float,float>{0.25f,0.5f}, "dashboard peaks stopped with diagnostics");
            diagnosticsHost->setDiagnosticsEnabled(true); process();
            require(diagnosticsHost->getStats().processedBlocks == before.processedBlocks + 1, "diagnostics did not resume");
        }
        {
            auto host = std::make_unique<RealtimeHostProcessor>();
            host->setPlayConfigDetails(2, 2, 48000, 512);
            host->prepareToPlay(48000, 512);
            auto chain = std::make_shared<ChainSnapshot>();
            auto left = std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(1));
            static_cast<GainPlugin*>(left->processor.get())->setLatencySamples(10);
            StripSnapshot a, b, wide, missing;
            a.id = "a"; a.allInputs = a.allOutputs = false; a.inputMap = {0}; a.outputMap = {0}; a.busChannels = 1; a.slots = {left};
            b.id = "b"; b.allInputs = b.allOutputs = false; b.inputMap = {1}; b.outputMap = {1}; b.busChannels = 1;
            chain->slots = {left}; chain->strips = {a, b};
            host->publishSnapshot(chain);
            AudioBuffer<float> audio(2, 512); MidiBuffer midi; host->prepareMidiBuffer(midi);
            audio.clear();
            host->processBlock(audio, midi);
            audio.clear(); audio.setSample(0, 0, 0.25f); audio.setSample(1, 0, 0.5f);
            host->processBlock(audio, midi);
            require(std::abs(audio.getSample(0, 0) - 0.5f) < 0.0001f, "left strip latency or gain");
            require(std::abs(audio.getSample(1, 10) - 0.5f) < 0.0001f, "right strip was not aligned");
            require(audio.getSample(1, 0) == 0.0f, "strip leaked before its delay");
            for (int i = 0; i < 512; ++i) { audio.setSample(0, i, 0.25f); audio.setSample(1, i, 0.5f); }
            host->setStripGain("b", 0.0f);
            host->processBlock(audio, midi);
            require(audio.getMagnitude(1, 400, 112) < 0.0001f, "strip gain did not silence its output");
            require(audio.getMagnitude(0, 400, 112) > 0.2f, "other strip was silenced");
            host->setMasterGain(0.0f);
            host->processBlock(audio, midi);
            require(audio.getMagnitude(0, 400, 112) < 0.0001f, "master gain did not scale the sum");
            host->setMasterGain(1.0f); host->setStripGain("b", 1.0f);
            for (int i = 0; i < 4; ++i) host->processBlock(audio, midi);
            wide.id = "wide"; wide.allInputs = wide.allOutputs = false; wide.inputMap = {0}; wide.outputMap = {0, 1}; wide.busChannels = 2;
            missing.id = "missing"; missing.allInputs = missing.allOutputs = false; missing.inputMap = {-1}; missing.outputMap = {1}; missing.busChannels = 1;
            chain = std::make_shared<ChainSnapshot>(); chain->strips = {wide};
            host->publishSnapshot(chain);
            for (int i = 0; i < 512; ++i) { audio.setSample(0, i, 0.2f); audio.setSample(1, i, 0.8f); }
            for (int i = 0; i < 3; ++i) host->processBlock(audio, midi);
            require(std::abs(audio.getSample(0, 511) - 0.2f) < 0.0001f && std::abs(audio.getSample(1, 511) - 0.2f) < 0.0001f, "one input did not feed both outputs");
            chain = std::make_shared<ChainSnapshot>(); chain->strips = {missing};
            host->publishSnapshot(chain);
            for (int i = 0; i < 3; ++i) host->processBlock(audio, midi);
            require(audio.getMagnitude(0, 512) == 0.0f, "missing input was not silent");
            auto routed = std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(1));
            a.slots = {routed}; a.gainLinear = 1.0f;
            chain = std::make_shared<ChainSnapshot>(); chain->slots = {routed}; chain->strips = {a};
            host->publishSnapshot(chain);
            host->setGlobalBypassed(true);
            for (int i = 0; i < 512; ++i) { audio.setSample(0, i, 0.25f); audio.setSample(1, i, 0.9f); }
            const auto allocationsBefore = lightHostModern::realtimeAudit::hostAllocations.load();
            for (int i = 0; i < 8; ++i) host->processBlock(audio, midi);
            require(std::abs(audio.getSample(0, 511) - 0.25f) < 0.02f, "global bypass dropped strip routing");
            require(audio.getMagnitude(1, 400, 112) < 0.02f, "global bypass leaked into an unselected output");
            require(lightHostModern::realtimeAudit::hostAllocations.load() == allocationsBefore, "strip path allocated on the callback");
        }
        {
            auto host = std::make_unique<RealtimeHostProcessor>();
            host->setPlayConfigDetails(2, 2, 48000, 512);
            host->prepareToPlay(48000, 512);
            StripSnapshot strip;
            strip.id = "pan"; strip.allInputs = strip.allOutputs = false;
            strip.inputMap = {0}; strip.outputMap = {0, 1}; strip.busChannels = 2;
            auto chain = std::make_shared<ChainSnapshot>(); chain->strips = {strip};
            host->publishSnapshot(chain);
            AudioBuffer<float> audio(2, 512); MidiBuffer midi; host->prepareMidiBuffer(midi);
            const auto fill = [&] { for (int i = 0; i < 512; ++i) { audio.setSample(0, i, 0.5f); audio.setSample(1, i, 0.25f); } };
            fill(); host->processBlock(audio, midi); fill(); host->processBlock(audio, midi);
            require(std::abs(audio.getSample(0, 511) - 0.5f) < 0.0001f && std::abs(audio.getSample(1, 511) - 0.5f) < 0.0001f, "center pan changed the strip");
            host->setStripPan("pan", -1.0f); fill(); host->processBlock(audio, midi);
            require(std::abs(audio.getSample(0, 511) - 0.5f) < 0.0001f, "full left dropped the first output");
            require(audio.getMagnitude(1, 400, 112) < 0.0001f, "full left reached the second output");
            host->setStripPan("pan", 1.0f);
            for (int i = 0; i < 2; ++i) { fill(); host->processBlock(audio, midi); }
            require(audio.getMagnitude(0, 400, 112) < 0.0001f, "full right reached the first output");
            require(std::abs(audio.getSample(1, 511) - 0.5f) < 0.0001f, "full right dropped the second output");
            strip.outputMap = {0}; strip.busChannels = 1;
            chain = std::make_shared<ChainSnapshot>(); chain->strips = {strip};
            host->publishSnapshot(chain); host->setStripPan("pan", 1.0f);
            fill(); host->processBlock(audio, midi); fill(); host->processBlock(audio, midi);
            require(std::abs(audio.getSample(0, 511) - 0.5f) < 0.0001f, "one output ignored pan");
            strip.inputMap = {0, 1}; strip.outputMap = {0, 1}; strip.busChannels = 2; strip.pan = 0.0f;
            chain = std::make_shared<ChainSnapshot>(); chain->strips = {strip};
            host->publishSnapshot(chain);
            const auto fillFull = [&] { for (int i = 0; i < 512; ++i) { audio.setSample(0, i, 1.0f); audio.setSample(1, i, 1.0f); } };
            for (int i = 0; i < 2; ++i) { fillFull(); host->processBlock(audio, midi); }
            const float left = audio.getSample(0, 511), right = audio.getSample(1, 511);
            require(std::abs(left - 1.0f) < 0.0001f && std::abs(right - 1.0f) < 0.0001f, "two inputs were summed instead of one per side");
        }
        std::cout << "Channels, asymmetric buses, bounded MIDI, preserved delay history, lifecycle, diagnostics opt-out and Release allocation audit passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
