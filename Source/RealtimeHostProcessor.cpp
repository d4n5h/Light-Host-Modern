#include "RealtimeHostProcessor.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <stdexcept>

void lightHostModernLog(const String& message);
void setLightHostModernCrashContext(const String& context);

PluginSlot::PluginSlot(PluginDescription descriptionIn, std::unique_ptr<AudioPluginInstance> processorIn)
	: description(std::move(descriptionIn)),
	  processor(std::move(processorIn))
{
	if (processor != nullptr)
	{
		processor->addListener(this);
		inputChannels = processor->getTotalNumInputChannels();
        mainInputChannels = processor->getMainBusNumInputChannels();
		outputChannels = processor->getTotalNumOutputChannels();
        mainOutputChannels = processor->getMainBusNumOutputChannels();
	}
}

PluginSlot::~PluginSlot()
{
	if (processor) processor->removeListener(this);
	try { release(); }
	catch (...) { lightHostModernLog("Plugin threw during release; processor destruction continues"); }
}

void PluginSlot::prepare(double sampleRateIn, int blockSizeIn, int hostChannels)
{
	if (processor == nullptr)
		return;

	const int inputs = inputChannels;
	const int outputs = outputChannels;

	const int channels = jmax(inputs, outputs);
	if (channels > RealtimeHostProcessor::maxScratchChannels)
		throw std::invalid_argument("Plugin layout exceeds 256 channels");
	if (prepared && preparedSampleRate == sampleRateIn && preparedBlockSize == blockSizeIn && preparedChannels == channels)
    {
        dryDelay.prepare(hostChannels, blockSizeIn, latencySamples, sampleRateIn);
        return;
    }

	if (prepared)
	{
		prepared = false;
		processor->releaseResources();
	}

	// Preserve enabled, disabled and auxiliary buses exactly as negotiated by the plugin.
    processor->setRateAndBufferSizeDetails(sampleRateIn, blockSizeIn);
	processor->prepareToPlay(sampleRateIn, blockSizeIn);
	latencySamples = jmax(0, processor->getLatencySamples());
	requestedLatency.store(latencySamples);
	transitionStep = (float) (1.0 / jmax(1.0, sampleRateIn * 0.005));
	dryDelay.prepare(hostChannels, blockSizeIn, latencySamples, sampleRateIn);
	prepared = true;
	preparedSampleRate = sampleRateIn;
	preparedBlockSize = blockSizeIn;
	preparedChannels = channels;
}

void PluginSlot::release()
{
	if (processor != nullptr && prepared)
	{
		prepared = false;
		preparedSampleRate = 0.0;
		preparedBlockSize = 0;
		preparedChannels = 0;
		processor->releaseResources();
	}
}

void PluginSlot::captureDry(const AudioBuffer<float>& buffer)
{
    dryDelay.capture(buffer);
}

void PluginSlot::mixDry(AudioBuffer<float>& buffer, bool useDry)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        bypassMix = useDry ? jmin(1.0f, bypassMix + transitionStep)
                           : jmax(0.0f, bypassMix - transitionStep);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            wet[i] = wet[i] * (1.0f - bypassMix) + dryDelay.output().getSample(ch, i) * bypassMix;
        }
    }
}

void PluginSlot::processBypass(AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom(ch, 0, dryDelay.output(), ch, 0, buffer.getNumSamples());
}

bool PluginSlot::refreshLatency()
{
    if (!prepared || processor == nullptr) return true;
    const int next = requestedLatency.load();
    if (next == latencySamples) return true;
    const bool compatible = dryDelay.prepare(dryDelay.channels(), preparedBlockSize, next, preparedSampleRate);
    latencySamples = next;
    return compatible;
}

RealtimeHostProcessor::ScopedSuspension::ScopedSuspension(RealtimeHostProcessor& processor, bool fadeOnResume)
    : owner(processor), lock(processor.controlMutex),
      wasSuspended(owner.processingSuspended.exchange(true)), fade(fadeOnResume)
{
    while (owner.callbacksInFlight.load() != 0) std::this_thread::yield();
}

RealtimeHostProcessor::ScopedSuspension::~ScopedSuspension()
{
    if (!wasSuspended)
    {
        if (fade) owner.resumeFade.store(true);
        owner.processingSuspended.store(false);
    }
}

void RealtimeHostProcessor::prepareBuffers()
{
    preparedHostChannels = jlimit(1, maxScratchChannels, jmax(getTotalNumInputChannels(), getTotalNumOutputChannels()));
    preparedInputChannels = jlimit(0, maxScratchChannels, getTotalNumInputChannels());
    preparedOutputChannels = jlimit(0, maxScratchChannels, getTotalNumOutputChannels());
    monoGains.resize(static_cast<size_t>(currentBlockSize));
    scratchBuffer.setSize(maxScratchChannels, currentBlockSize, false, false, true);
    stripBus.setSize(maxScratchChannels, currentBlockSize, false, false, true);
    mixBus.setSize(maxScratchChannels, currentBlockSize, false, false, true);
    // JUCE reallocates channel-pointer storage when a view grows beyond its current
    // channel count. Keep one fixed-count view per layout, including 32+ channels.
    for (int channels = 1; channels <= maxScratchChannels; ++channels)
    {
        segmentViews[(size_t) channels].setDataToReferTo(scratchBuffer.getArrayOfWritePointers(), channels, currentBlockSize);
        expandedViews[(size_t) channels].setDataToReferTo(scratchBuffer.getArrayOfWritePointers(), channels, currentBlockSize);
        stripViews[(size_t) channels].setDataToReferTo(stripBus.getArrayOfWritePointers(), channels, currentBlockSize);
    }
    segmentMidi.ensureSize(midiCapacity);
    filteredMidi.ensureSize(midiCapacity);
    outputMidi.ensureSize(midiCapacity);
    globalControls.prepare(preparedHostChannels, currentBlockSize, getLatencySamples(), currentSampleRate);
    inputMeters.prepare(currentSampleRate, getTotalNumInputChannels());
    outputMeters.prepare(currentSampleRate, getTotalNumOutputChannels());
}

void RealtimeHostProcessor::refreshLatencies()
{
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    const auto snapshot = activeSnapshot;
    if (!snapshot) return;
    bool changed = false;
    for (const auto& slot : snapshot->slots)
        if (slot && slot->processor && slot->prepared && slot->hasPendingLatency()) changed = true;
    if (!changed) return;
    ScopedSuspension suspension(*this, false);
    bool compatible = true;
    if (snapshot->strips.empty())
    {
        snapshot->totalLatencySamples = 0;
        for (const auto& slot : snapshot->slots)
            if (slot) { compatible = slot->refreshLatency() && compatible; snapshot->totalLatencySamples += slot->getLatencySamples(); }
    }
    else
    {
        int maximum = 0;
        for (auto& strip : snapshot->strips)
        {
            strip.latencySamples = 0;
            for (const auto& slot : strip.slots)
                if (slot) { compatible = slot->refreshLatency() && compatible; strip.latencySamples += slot->getLatencySamples(); }
            maximum = jmax(maximum, strip.latencySamples);
        }
        for (auto& strip : snapshot->strips)
            if (strip.runtime) compatible = strip.runtime->align.prepare(strip.busChannels, currentBlockSize, maximum - strip.latencySamples, currentSampleRate) && compatible;
        snapshot->totalLatencySamples = maximum;
    }
    setLatencySamples(snapshot->totalLatencySamples);
    compatible = globalControls.prepare(preparedHostChannels, currentBlockSize, 0, currentSampleRate) && compatible;
    if (!compatible) resumeFade.store(true);
}

RealtimeHostProcessor::RealtimeHostProcessor()
	: AudioProcessor(BusesProperties()
		.withInput("Input", AudioChannelSet::stereo(), true)
		.withOutput("Output", AudioChannelSet::stereo(), true))
{
	prepareBuffers();
}

RealtimeHostProcessor::~RealtimeHostProcessor()
{
    ScopedSuspension suspension(*this);
    realtimeSnapshot.store(nullptr, std::memory_order_release);
    activeSnapshot.reset();
    retiredSnapshots.clear();
}

void RealtimeHostProcessor::publishSnapshot(std::shared_ptr<ChainSnapshot> snapshot)
{
	ScopedSuspension suspension(*this);
	if (snapshot) for (const auto& slot : snapshot->slots)
		if (slot && jmax(slot->inputChannels, slot->outputChannels) > maxScratchChannels)
			throw std::invalid_argument("Plugin layout exceeds 256 channels");
	if (snapshot != nullptr)
		prepareSnapshot(*snapshot);
    else { setLatencySamples(0); globalControls.prepare(preparedHostChannels, currentBlockSize, 0, currentSampleRate); }

	if (auto previous = activeSnapshot)
		retiredSnapshots.push_back(std::move(previous));

	activeSnapshot = std::move(snapshot);
    static_assert(std::atomic<ChainSnapshot*>::is_always_lock_free, "Audio snapshot publication must be lock-free");
    realtimeSnapshot.store(activeSnapshot.get(), std::memory_order_release);
	collectRetiredSnapshots();
}

std::shared_ptr<ChainSnapshot> RealtimeHostProcessor::getActiveSnapshot() const
{
	const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return activeSnapshot;
}

RealtimeHostStats RealtimeHostProcessor::getStats() const
{
	RealtimeHostStats stats;
	stats.processFailures = processFailureCount.load(std::memory_order_relaxed);
    stats.midiOverflow = midiOverflowCount.load(std::memory_order_relaxed);
    stats.processedBlocks = processedBlocks.load(std::memory_order_relaxed);
    stats.processedSamples = processedSamples.load(std::memory_order_relaxed);
    stats.inputMidiEvents = inputMidiEvents.load(std::memory_order_relaxed);
    stats.outputMidiEvents = outputMidiEvents.load(std::memory_order_relaxed);
    stats.hostAllocations = lightHostModern::realtimeAudit::hostAllocations.load();
    stats.hostFrees = lightHostModern::realtimeAudit::hostFrees.load();
    stats.pluginAllocations = lightHostModern::realtimeAudit::pluginAllocations.load();
    stats.pluginFrees = lightHostModern::realtimeAudit::pluginFrees.load();

	if (auto snapshot = getActiveSnapshot())
	{
		stats.loadedSlots = (int) snapshot->slots.size();
		stats.chainLatencySamples = snapshot->totalLatencySamples;
		stats.reusedSlots = snapshot->reusedSlots;
		stats.rebuiltSlots = snapshot->rebuiltSlots;
	}

	stats.inputLevel = lastInputLevel.load(std::memory_order_relaxed);
	stats.outputLevel = lastOutputLevel.load(std::memory_order_relaxed);
	return stats;
}

void RealtimeHostProcessor::collectRetiredSnapshots()
{
	const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (midiStorageNeedsRepair.exchange(false))
    {
        ScopedSuspension suspension(*this, false);
        segmentMidi.ensureSize(midiCapacity);
        filteredMidi.ensureSize(midiCapacity);
        outputMidi.ensureSize(midiCapacity);
    }
	retiredSnapshots.erase(std::remove_if(retiredSnapshots.begin(), retiredSnapshots.end(),
		[] (const std::shared_ptr<ChainSnapshot>& snapshot)
		{
			return snapshot == nullptr || snapshot.use_count() == 1;
		}),
		retiredSnapshots.end());
}

void RealtimeHostProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
	ScopedSuspension suspension(*this);
	lastInputLevel.store(0.0f); lastOutputLevel.store(0.0f);
    inputPresentation.reset(); outputPresentation.reset();
	currentSampleRate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
	currentBlockSize = jmax(1, maximumExpectedSamplesPerBlock);
	prepareBuffers();

	if (auto snapshot = getActiveSnapshot())
		prepareSnapshot(*snapshot);
}

void RealtimeHostProcessor::releaseResources()
{
	ScopedSuspension suspension(*this);
	lastInputLevel.store(0.0f); lastOutputLevel.store(0.0f);
    inputPresentation.reset(); outputPresentation.reset();
	if (auto snapshot = getActiveSnapshot())
		for (auto& slot : snapshot->slots)
			if (slot != nullptr)
				slot->release();
}

void RealtimeHostProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    lightHostModern::realtimeAudit::Scope audit(lightHostModern::realtimeAudit::Origin::host);
    ScopedNoDenormals noDenormals;
    // The second check closes the race with a controller suspending between the
    // first check and admission. Only the controller waits for in-flight work.
    if (processingSuspended.load()) { buffer.clear(); return; }
    callbacksInFlight.fetch_add(1);
    struct Exit { std::atomic<unsigned>& count; ~Exit() { count.fetch_sub(1); } } exit { callbacksInFlight };
    if (processingSuspended.load()) { buffer.clear(); return; }
    const int channels = buffer.getNumChannels();
    if (channels > preparedHostChannels || channels == 0) { buffer.clear(); return; }
    const bool collect = lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed);
    lastInputLevel.store(collect ? inputMeters.process(buffer.getArrayOfReadPointers(), channels, buffer.getNumSamples())
                                : buffer.getMagnitude(0, buffer.getNumSamples()), std::memory_order_relaxed);
    inputPresentation.process(lastInputLevel.load(std::memory_order_relaxed), buffer.getNumSamples(), currentSampleRate);
    if (collect) inputMidiEvents.fetch_add(static_cast<uint64>(midiMessages.getNumEvents()), std::memory_order_relaxed);
    auto* const snapshot = realtimeSnapshot.load(std::memory_order_acquire);
    if (resumeFade.exchange(false)) resumeGain = 0.0f;
    const int destinationCapacity = &midiMessages == preparedMidiDestination ? midiCapacity : jmin(midiCapacity, midiMessages.data.size());
    uint64 dropped = 0;
    outputMidi.clear();
    for (int offset = 0; offset < buffer.getNumSamples(); offset += currentBlockSize)
    {
        const int count = jmin(currentBlockSize, buffer.getNumSamples() - offset);
        auto& segment = segmentViews[(size_t) channels];
        segment.setDataToReferTo(buffer.getArrayOfWritePointers(), channels, offset, count);
        // Morph the input matrix, retaining continuity and the same dry/wet route.
        const float target = monoInputs.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        const float step = static_cast<float>(1.0 / (currentSampleRate * 0.005));
        const int inputs = jmin(preparedInputChannels, channels);
        for (int sample = 0; sample < count; ++sample)
        {
            monoMix += jlimit(-step, step, target - monoMix);
            monoGains[static_cast<size_t>(sample)] = monoMix;
            if (monoMix == 0.0f) continue;
            float sum = 0.0f;
            for (int ch = 0; ch < inputs; ++ch) sum += segment.getSample(ch, sample);
            // Main stereo route only: never duplicate the mix into auxiliary channels.
            for (int ch = 0; ch < jmin(2, channels); ++ch)
            {
                auto& value = segment.getWritePointer(ch)[sample];
                value += (sum - value) * monoMix;
            }
        }
        segmentMidi.clear();
        dropped += lightHostModern::copyBoundedMidi(segmentMidi, midiMessages, offset, count, -offset, midiCapacity);
        if (snapshot) processStrips(*snapshot, segment, segmentMidi, dropped);
        const float masterStep = static_cast<float>(1.0 / (currentSampleRate * 0.005));
        const float masterWanted = masterTarget.load(std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
        {
            masterGain += jlimit(-masterStep, masterStep, masterWanted - masterGain);
            if (masterGain == 1.0f) continue;
            for (int ch = 0; ch < channels; ++ch) segment.getWritePointer(ch)[i] *= masterGain;
        }
        {
            const float peak = segment.getMagnitude(0, count);
            const float previous = masterLevel.load(std::memory_order_relaxed);
            const float decay = std::exp(-static_cast<float>(count) / static_cast<float>(currentSampleRate) / 0.3f);
            masterLevel.store(jmax(peak, previous * decay), std::memory_order_relaxed);
        }
        globalControls.applyMute(segment);
        // Physical outputs 1/2 remain the principal pair even when JUCE packs
        // a sparse output mask. Never mix an auxiliary output into this pair.
        const bool hasPair = mainOutputLeft >= 0 && mainOutputRight >= 0
            && mainOutputLeft < preparedOutputChannels && mainOutputRight < preparedOutputChannels;
        const float outputTarget = monoOutput.load(std::memory_order_relaxed) && hasPair ? 1.0f : 0.0f;
        for (int i = 0; i < count; ++i) {
            outputMonoMix += jlimit(-step, step, outputTarget - outputMonoMix);
            if (!hasPair || outputMonoMix == 0.0f) continue;
            auto& left = segment.getWritePointer(mainOutputLeft)[i];
            auto& right = segment.getWritePointer(mainOutputRight)[i];
            const float average = 0.5f * left + 0.5f * right;
            left += (average - left) * outputMonoMix;
            right += (average - right) * outputMonoMix;
        }
        dropped += lightHostModern::copyBoundedMidi(outputMidi, segmentMidi, 0, count, offset, midiCapacity);
        for (int i = 0; i < count && resumeGain < 1.0f; ++i)
        {
            resumeGain = jmin(1.0f, resumeGain + (float) (1.0 / (currentSampleRate * 0.005)));
            for (int ch = 0; ch < channels; ++ch) segment.getWritePointer(ch)[i] *= resumeGain;
        }
    }
    midiMessages.clear();
    dropped += lightHostModern::copyBoundedMidi(midiMessages, outputMidi, 0, buffer.getNumSamples(), 0, destinationCapacity);
    float outputPeak = 0.0f;
    if (!collect)
        for (int ch = 0; ch < jmin(preparedOutputChannels, channels); ++ch)
            outputPeak = jmax(outputPeak, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
    lastOutputLevel.store(collect ? outputMeters.process(buffer.getArrayOfReadPointers(), jmin(preparedOutputChannels, channels), buffer.getNumSamples())
                                 : outputPeak, std::memory_order_relaxed);
    outputPresentation.process(lastOutputLevel.load(std::memory_order_relaxed), buffer.getNumSamples(), currentSampleRate);
    if (collect) {
        midiOverflowCount.fetch_add(dropped, std::memory_order_relaxed);
        outputMidiEvents.fetch_add(static_cast<uint64>(midiMessages.getNumEvents()), std::memory_order_relaxed);
        processedBlocks.fetch_add(1, std::memory_order_relaxed);
        processedSamples.fetch_add(static_cast<uint64>(buffer.getNumSamples()), std::memory_order_relaxed);
    }
}

void RealtimeHostProcessor::setDiagnosticsEnabled(bool enabled)
{
    ScopedSuspension suspension(*this, false);
    lightHostModern::diagnosticsCollectionEnabled.store(enabled, std::memory_order_relaxed);
    inputMeters.clearHistory(); outputMeters.clearHistory();
}

void RealtimeHostProcessor::configureOutputChannels(const BigInteger& physicalChannels)
{
    ScopedSuspension suspension(*this, false);
    mainOutputLeft = mainOutputRight = -1;
    int packed = 0;
    for (int physical = physicalChannels.findNextSetBit(0); physical >= 0; physical = physicalChannels.findNextSetBit(physical + 1), ++packed) {
        if (physical == 0) mainOutputLeft = packed;
        if (physical == 1) mainOutputRight = packed;
    }
    outputMonoMix = 0.0f;
}

void RealtimeHostProcessor::prepareMidiBuffer(MidiBuffer& buffer)
{
    ScopedSuspension suspension(*this, false);
    buffer.ensureSize(midiCapacity);
    preparedMidiDestination = &buffer;
}

std::shared_ptr<StripRuntime> RealtimeHostProcessor::runtimeFor(const juce::String& id)
{
    auto& runtime = stripRuntimes[id];
    if (!runtime) runtime = std::make_shared<StripRuntime>();
    return runtime;
}

void RealtimeHostProcessor::prepareSnapshot(ChainSnapshot& snapshot)
{
	lightHostModernLog("RealtimeHostProcessor prepareSnapshot begin slots=" + String((int) snapshot.slots.size()));
	snapshot.sampleRate = currentSampleRate;
	snapshot.blockSize = currentBlockSize;
	snapshot.maxPluginChannels = jmax(snapshot.inputChannels, snapshot.outputChannels);
	if (snapshot.strips.empty())
	{
		StripSnapshot strip;
		strip.allInputs = strip.allOutputs = true;
		strip.slots = snapshot.slots;
		snapshot.strips.push_back(std::move(strip));
	}
	const auto prepareSlot = [&](PluginSlot& slot, int busChannels) {
		snapshot.maxPluginChannels = jmax(snapshot.maxPluginChannels, jmax(slot.inputChannels, slot.outputChannels));
		try
		{
			slot.prepare(currentSampleRate, currentBlockSize, busChannels);
		}
		catch (...)
		{
			slot.processDisabled.store(true, std::memory_order_release);
			slot.processFailed.store(true, std::memory_order_release);
			if (lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1, std::memory_order_relaxed);
		}
	};
	int maximum = 0;
	for (auto& strip : snapshot.strips)
	{
		if (strip.allInputs || strip.allOutputs)
			strip.busChannels = jmax(1, preparedHostChannels);
		else if (strip.busChannels <= 0)
			strip.busChannels = jmax(1, jmax((int) strip.inputMap.size(), (int) strip.outputMap.size()));
		strip.busChannels = jlimit(1, maxScratchChannels, strip.busChannels);
		if (strip.inputMap.empty() && strip.allInputs)
			for (int channel = 0; channel < preparedHostChannels; ++channel) strip.inputMap.push_back(channel);
		if (strip.outputMap.empty() && strip.allOutputs)
			for (int channel = 0; channel < preparedHostChannels; ++channel) strip.outputMap.push_back(channel);
		strip.latencySamples = 0;
		for (const auto& slot : strip.slots)
			if (slot) { prepareSlot(*slot, strip.busChannels); strip.latencySamples += slot->getLatencySamples(); }
		maximum = jmax(maximum, strip.latencySamples);
		strip.runtime = runtimeFor(strip.id);
		strip.runtime->targetGain.store(strip.gainLinear, std::memory_order_relaxed);
		strip.runtime->targetPan.store(juce::jlimit(-1.0f, 1.0f, strip.pan), std::memory_order_relaxed);
		strip.runtime->muted.store(strip.muted, std::memory_order_relaxed);
		strip.runtime->solo.store(strip.solo, std::memory_order_relaxed);
	}
	anySolo.store(std::any_of(snapshot.strips.begin(), snapshot.strips.end(), [](const auto& strip) { return strip.solo; }), std::memory_order_relaxed);
	for (auto& strip : snapshot.strips)
		if (strip.runtime) strip.runtime->align.prepare(strip.busChannels, currentBlockSize, maximum - strip.latencySamples, currentSampleRate);
	snapshot.totalLatencySamples = maximum;
	masterTarget.store(snapshot.masterGainLinear, std::memory_order_relaxed);
	setLatencySamples(snapshot.totalLatencySamples);
    globalControls.prepare(preparedHostChannels, currentBlockSize, 0, currentSampleRate);
	lightHostModernLog("RealtimeHostProcessor prepareSnapshot completed latencySamples=" + String(snapshot.totalLatencySamples));
}

void RealtimeHostProcessor::processStrips(ChainSnapshot& snapshot, AudioBuffer<float>& segment, MidiBuffer& midi, uint64& dropped)
{
    const int count = segment.getNumSamples();
    const int channels = segment.getNumChannels();
    const bool forceDry = globalControls.isBypassed();
    const float step = static_cast<float>(1.0 / (currentSampleRate * 0.005));
    mixBus.clear(0, count);
    for (auto& strip : snapshot.strips)
    {
        const int busChannels = jlimit(1, maxScratchChannels, strip.busChannels);
        auto& bus = stripViews[static_cast<size_t>(busChannels)];
        bus.setDataToReferTo(stripBus.getArrayOfWritePointers(), busChannels, count);
        bus.clear(0, count);
        int connected = 0;
        for (int index = 0; index < (int) strip.inputMap.size(); ++index)
        {
            const int source = strip.inputMap[static_cast<size_t>(index)];
            if (source < 0 || source >= channels || index >= busChannels) continue;
            bus.copyFrom(index, 0, segment, source, 0, count);
            ++connected;
        }
        if (connected == 1 && busChannels >= 2 && strip.inputMap.size() == 1)
            bus.copyFrom(1, 0, bus, 0, 0, count);
        for (const auto& slot : strip.slots)
        {
            if (!slot || !slot->processor || !slot->prepared) continue;
            slot->captureDry(bus);
            processSlot(*slot, bus, midi);
            if (midi.data.getAllocatedCapacity() < midiCapacity) midiStorageNeedsRepair.store(true);
            filteredMidi.clear();
            dropped += lightHostModern::copyBoundedMidi(filteredMidi, midi, 0, count, 0, midiCapacity);
            midi.swapWith(filteredMidi);
            slot->mixDry(bus, forceDry || slot->bypassed.load(std::memory_order_relaxed));
        }
        if (strip.runtime)
        {
            const float target = strip.runtime->targetGain.load(std::memory_order_relaxed);
            for (int i = 0; i < count; ++i)
            {
                strip.runtime->gain += jlimit(-step, step, target - strip.runtime->gain);
                if (strip.runtime->gain == 1.0f) continue;
                for (int ch = 0; ch < busChannels; ++ch) bus.getWritePointer(ch)[i] *= strip.runtime->gain;
            }
            const float peak = bus.getMagnitude(0, count);
            const float previous = strip.runtime->level.load(std::memory_order_relaxed);
            const float decay = std::exp(-static_cast<float>(count) / static_cast<float>(currentSampleRate) / 0.3f);
            strip.runtime->level.store(jmax(peak, previous * decay), std::memory_order_relaxed);
            strip.runtime->align.capture(bus);
        }
        const auto& delayed = strip.runtime ? strip.runtime->align.output() : bus;
        const bool monoBus = busChannels == 1 && strip.outputMap.size() > 1;
        const float targetPan = strip.runtime ? strip.runtime->targetPan.load(std::memory_order_relaxed) : 0.0f;
        const bool panLive = strip.runtime && strip.outputMap.size() >= 2
            && (strip.runtime->pan != 0.0f || targetPan != 0.0f);
        const auto scatter = [&](int sampleCount, int sampleOffset, float leftGain, float rightGain) {
            for (int index = 0; index < (int) strip.outputMap.size(); ++index)
            {
                const int destination = strip.outputMap[static_cast<size_t>(index)];
                const int source = monoBus ? 0 : index;
                if (destination < 0 || destination >= channels || source >= delayed.getNumChannels()) continue;
                const float gain = index == 0 ? leftGain : index == 1 ? rightGain : 1.0f;
                mixBus.addFrom(destination, sampleOffset, delayed, source, sampleOffset, sampleCount, gain);
            }
        };
        const bool muted = strip.runtime && strip.runtime->muted.load(std::memory_order_relaxed);
        const bool soloed = strip.runtime && strip.runtime->solo.load(std::memory_order_relaxed);
        const bool silent = muted || (anySolo.load(std::memory_order_relaxed) && !soloed);
        if (silent) continue;
        if (!panLive) scatter(count, 0, 1.0f, 1.0f);
        else for (int i = 0; i < count; ++i)
        {
            strip.runtime->pan += jlimit(-step, step, targetPan - strip.runtime->pan);
            const float position = strip.runtime->pan;
            float leftGain = 1.0f, rightGain = 1.0f;
            if (position != 0.0f)
            {
                if (busChannels == 1)
                {
                    const float angle = (position + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                    leftGain = std::cos(angle);
                    rightGain = std::sin(angle);
                }
                else
                {
                    leftGain = position <= 0.0f ? 1.0f : 1.0f - position;
                    rightGain = position >= 0.0f ? 1.0f : 1.0f + position;
                }
            }
            scatter(1, i, leftGain, rightGain);
        }
    }
    for (int ch = 0; ch < channels; ++ch) segment.copyFrom(ch, 0, mixBus, ch, 0, count);
}

void RealtimeHostProcessor::processSlot(PluginSlot& slot, AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    const int samples = buffer.getNumSamples();
    if (samples <= 0) return;
    if (slot.processDisabled.load(std::memory_order_acquire)) { slot.processBypass(buffer); return; }
    const int pluginChannels = jmax(slot.inputChannels, slot.outputChannels);
    if (pluginChannels > maxScratchChannels)
    {
        slot.processDisabled.store(true);
        slot.processFailed.store(true);
        if (lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1);
        slot.processBypass(buffer);
        return;
    }
    // Auxiliary buses keep their declared layout but receive silence until the
    // host has an explicit route. Output-only channels must start at zero.
    for (int channel = 0; channel < pluginChannels; ++channel)
    {
        if (channel < jmin(buffer.getNumChannels(), slot.mainInputChannels))
            scratchBuffer.copyFrom(channel, 0, buffer, channel, 0, samples);
        else scratchBuffer.clear(channel, 0, samples);
    }
    auto& pluginBuffer = expandedViews[static_cast<size_t>(pluginChannels)];
    pluginBuffer.setDataToReferTo(scratchBuffer.getArrayOfWritePointers(), pluginChannels, samples);
    try
    {
        // Keep the JUCE format bridge in the host audit. Only executable imports
        // are intercepted; allocations inside third-party DLLs are not observed.
        slot.processor->processBlock(pluginBuffer, midiMessages);
    }
    catch (...)
    {
        slot.processDisabled.store(true);
        slot.processFailed.store(true);
        if (lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1);
        slot.processBypass(buffer);
        return;
    }
    // Main output is the serial route; asymmetric and disabled buses don't leak
    // stale auxiliary samples into subsequent processors.
    const int copied = jmin(buffer.getNumChannels(), slot.mainOutputChannels);
    for (int channel = 0; channel < copied; ++channel) buffer.copyFrom(channel, 0, scratchBuffer, channel, 0, samples);
    for (int channel = copied; channel < buffer.getNumChannels(); ++channel) buffer.clear(channel, 0, samples);
    if (slot.mainOutputChannels == 1 && buffer.getNumChannels() >= 2)
        for (int sample = 0; sample < samples; ++sample)
            buffer.setSample(1, sample, scratchBuffer.getSample(0, sample) * monoGains[static_cast<size_t>(sample)]);
}
