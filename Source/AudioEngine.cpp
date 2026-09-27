#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "MixWriter.h"
#include "StreamOutput.h"
#include "MackieSurface.h"
#include "PluginStateCapture.h"
#include "PluginWindow.h"
#include "RuntimeProfile.h"
#include "VerboseLog.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

void lightHostModernLog(const String& message);
void setLightHostModernCrashContext(const String& context);
void clearLightHostModernCrashContext();

namespace
{
	String getEnvironmentPath(const char* name)
	{
		return SystemStats::getEnvironmentVariable(name, {});
	}

	void addSearchFolder(FileSearchPath& searchPath, const String& folder)
	{
		if (folder.isEmpty())
			return;

		// Filesystem existence checks belong to the isolated enumeration worker.
		searchPath.addIfNotAlreadyThere(File(folder));
	}

	void addSearchFolderFromBase(FileSearchPath& searchPath, const String& baseFolder, const String& relativeFolder)
	{
		if (baseFolder.isNotEmpty())
			addSearchFolder(searchPath, baseFolder + relativeFolder);
	}

	FileSearchPath getWindowsDefaultPluginSearchPath(AudioPluginFormat& format, bool isVst, bool isVst3)
	{
		FileSearchPath searchPath;
		searchPath.addPath(format.getDefaultLocationsToSearch());

		const auto programFiles = getEnvironmentPath("ProgramFiles");
		const auto programFilesX86 = getEnvironmentPath("ProgramFiles(x86)");
		const auto commonProgramFiles = getEnvironmentPath("CommonProgramFiles");
		const auto commonProgramFilesX86 = getEnvironmentPath("CommonProgramFiles(x86)");
		const auto localAppData = getEnvironmentPath("LOCALAPPDATA");

		if (isVst3)
		{
			addSearchFolderFromBase(searchPath, commonProgramFiles, "\\VST3");
			addSearchFolderFromBase(searchPath, commonProgramFilesX86, "\\VST3");
			addSearchFolderFromBase(searchPath, localAppData, "\\Programs\\Common\\VST3");
		}

		if (isVst)
		{
			addSearchFolderFromBase(searchPath, programFiles, "\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFiles, "\\Steinberg\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFiles, "\\Common Files\\VST2");
			addSearchFolderFromBase(searchPath, programFiles, "\\Common Files\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFilesX86, "\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFilesX86, "\\Steinberg\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFilesX86, "\\Common Files\\VST2");
			addSearchFolderFromBase(searchPath, programFilesX86, "\\Common Files\\VSTPlugins");
		}

		return searchPath;
	}

	bool isVst2PluginHostEnabled()
	{
	#if JUCE_PLUGINHOST_VST
		return getAppProperties().getUserSettings()->getBoolValue("enableVst2", false);
	#else
		return false;
	#endif
	}

	void addEnabledPluginFormats(AudioPluginFormatManager& manager)
	{
	#if JUCE_PLUGINHOST_VST
		if (isVst2PluginHostEnabled())
			manager.addFormat(std::make_unique<VSTPluginFormat>());
	#endif

	#if JUCE_PLUGINHOST_VST3
		manager.addFormat(std::make_unique<VST3PluginFormat>());
	#endif
	}

	String getPluginStateBaseKey(String type, const PluginDescription& plugin)
	{
		return "plugin-" + type.toLowerCase() + "-" + String::toHexString(plugin.createIdentifierString().hashCode64());
	}




}

String PluginStateStore::getKey(String type, const PluginDescription& plugin)
{
	return getPluginStateBaseKey(type, plugin) + "-" + String::toHexString(plugin.deprecatedUid);
}

String PluginStateStore::getLegacyKey(String type, const PluginDescription& plugin)
{
	return "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
}

String PluginStateStore::getValue(String type, const PluginDescription& plugin, const String& defaultValue) const
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	const String key = getKey(type, plugin);
	const String value = settings->getValue(key);
	if (settings->containsKey(key))
		return value;

	const String baseValue = settings->getValue(getPluginStateBaseKey(type, plugin));
	if (baseValue.isNotEmpty())
		return baseValue;

	return settings->getValue(getLegacyKey(type, plugin), defaultValue);
}

void PluginStateStore::setValue(String type, const PluginDescription& plugin, const var& value)
{
	getAppProperties().getUserSettings()->setValue(getKey(type, plugin), value);
	dirty = true;
}

void PluginStateStore::removeValue(String type, const PluginDescription& plugin)
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	settings->removeValue(getKey(type, plugin));
	settings->removeValue(getPluginStateBaseKey(type, plugin));
	settings->removeValue(getLegacyKey(type, plugin));
	dirty = true;
}

void PluginStateStore::markDirty()
{
	dirty = true;
}

void PluginStateStore::flushIfDirty()
{
	if (!dirty)
		return;

	dirty = false;
	getAppProperties().getUserSettings()->saveIfNeeded();
}

AudioEngine::AudioEngine(bool startInSafeMode, bool shouldRestoreActivePluginsOnStartup)
	: safeMode(startInSafeMode),
	  restoreActivePluginsOnStartup(shouldRestoreActivePluginsOnStartup),
      deviceController(deviceManager, *getAppProperties().getUserSettings(),
          [this] { markSettingsDirty(); }, [this] { loadActivePlugins(); })
{
    deviceManager.allowed = [this](const String& backend, const String& input, const String& output) {
        return deviceController.isAudioDeviceCreationAllowed(backend, input, output);
    };
    addEnabledPluginFormats(formatManager);
	std::unique_ptr<XmlElement> savedPluginList(getXmlValuePreserving("pluginList"));
	if (savedPluginList != nullptr)
		knownPluginList.recreateFromXml(*savedPluginList);

	knownPluginList.addChangeListener(this);

    sessionLoadSuppressed = safeMode || !restoreActivePluginsOnStartup;
    auto* settings = getAppProperties().getUserSettings();
    setDiagnosticsEnabled(settings->getBoolValue("diagnosticsEnabled", true));
    auto storage = std::make_shared<lightHostModern::DiskSessionStorage>(settings->getFile());
    const auto recovered = lightHostModern::SessionStore::recover(*storage);
    if (recovered.document)
    {
        instances = recovered.document->instances;
        instances.ensureStrips();
        sessionMigrationId = recovered.document->migrationId;
        if (recovered.bytes.find("version=\\\"2\\\"") == std::string::npos && recovered.bytes.find("version=\\\"1\\\"") != std::string::npos)
        {
            const auto primary = settings->getFile().getSiblingFile(settings->getFile().getFileName() + ".session.json");
            const auto backup = primary.getSiblingFile(primary.getFileName() + ".v1.bak");
            if (!backup.existsAsFile() && primary.existsAsFile()) primary.copyFileTo(backup);
        }
        if (recovered.warning.isNotEmpty())
            instances.recoveryError = (instances.recoveryError.isEmpty() ? String() : instances.recoveryError + "\n") + recovered.warning;
    }
    else if (recovered.found)
    {
        instances.writable = false;
        instances.recoveryError = recovered.warning;
    }
    else
    {
        // Copy the exact previous file before device initialization or migration
        // can alter its keys. Legacy material remains untouched after activation.
        const auto backupError = lightHostModern::SessionStore::backupLegacy(*storage, sessionMigrationId);
        if (backupError.isNotEmpty())
        {
            instances.writable = false;
            instances.recoveryError = "Could not back up legacy preferences: " + backupError;
        }
        else if (settings->containsKey("pluginInstancesV1"))
        {
            const auto saved = settings->getXmlValue("pluginInstancesV1");
            if (!saved || !instances.deserialize(*saved))
            { instances.writable = false; instances.recoveryError = "Invalid instance data; original settings preserved"; }
        }
        else if (settings->containsKey("pluginListActive"))
        {
            const auto legacy = settings->getXmlValue("pluginListActive");
            if (legacy) instances.migrate(*legacy, *settings, knownPluginList.getTypes(), sessionMigrationId);
            else { instances.writable = false; instances.recoveryError = "Invalid legacy session; original settings preserved"; }
        }
    }
    sessionStore = std::make_unique<lightHostModern::SessionStore>(std::move(storage), recovered);
    chainProfiles = std::make_unique<lightHostModern::ChainProfileStore>(settings->getFile());
    templates = std::make_unique<lightHostModern::TemplateStore>(settings->getFile());
    if (instances.writable && !sessionLoadSuppressed)
    {
        String sessionHash;
        if (!recovered.bytes.empty())
        {
            const auto parsed = JSON::parse(String::fromUTF8(recovered.bytes.data(), static_cast<int>(recovered.bytes.size())));
            if (parsed.isObject()) sessionHash = parsed["contentHash"].toString();
        }
        if (const auto error = chainProfiles->ensureDefault(instances, sessionMigrationId, sessionHash); error.isNotEmpty())
            lightHostModernLog("Chain profile catalog: " + error);
    }
    deviceController.start(safeMode, lightHostModern::RuntimeProfile::current().noAudio);
    mixCapture = std::make_unique<MixCapture>();
    mixWriter = std::make_unique<MixWriter>(*mixCapture);
    streamOutput = std::make_unique<StreamOutput>();
    mackie = std::make_unique<MackieSurface>(*this);
    hostProcessor.setMixCapture(mixCapture.get());
    player.setProcessor(&hostProcessor);
    deviceManager.addAudioCallback(&player);
    deviceManager.addChangeListener(this);
    startTimer(audioWatchdogTimerId, 250);
    if (isDiagnosticsEnabled() || lightHostModern::verbose::logger().active()) startTimer(diagnosticsTimerId, 30000);
    loadActivePlugins();
    if (!sessionLoadSuppressed && instances.writable) saveActivePluginList();
}

AudioEngine::~AudioEngine()
{
	stopRecording();
	stopStream();
	closeMackie();
	cancelPluginScan();
	stopTimer(audioWatchdogTimerId);
	stopTimer(diagnosticsTimerId);
	stopTimer(persistenceTimerId);

    // State capture must precede releaseResources as well as destruction.
    if (!flushSession()) Logger::writeToLog("LightHostModern: final session remains pending: " + getSessionSaveStatus().error);
    if (sessionStore) sessionStore->shutdown();
	flushPendingSaves();

	knownPluginList.removeChangeListener(this);
	deviceManager.removeChangeListener(this);
	deviceManager.removeAudioCallback(&player);
	player.setProcessor(nullptr);

	hostProcessor.publishSnapshot(nullptr);
}

std::unique_ptr<XmlElement> AudioEngine::getXmlValuePreserving(const String& key)
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	auto xml = settings->getXmlValue(key);
	if (xml == nullptr && settings->getValue(key).isNotEmpty())
	{
		Logger::writeToLog("LightHostModern: preserved invalid XML setting '" + key + "'");
	}

	return xml;
}

int AudioEngine::findKnownPluginIndexById(const String& id) const
{
    const auto known = getKnownPluginsSorted();
    for (size_t i = 0; i < known.size(); ++i)
        if (lightHostModern::knownPluginId(known[i]) == id) return static_cast<int>(i);
    return -1;
}

int AudioEngine::findPluginIndexById(const PluginInstanceId& id) const
{
    return instances.indexOf(id);
}

std::vector<PluginDescription> AudioEngine::getActivePluginsSorted() const
{
    std::vector<PluginDescription> result;
    result.reserve(instances.records.size());
    for (const auto& record : instances.records) result.push_back(record.description);
    return result;
}

std::vector<PluginDescription> AudioEngine::getKnownPluginsSorted() const
{
	std::vector<PluginDescription> list;
	const auto types = knownPluginList.getTypes();
	for (auto& plugin : types)
		list.push_back(plugin);

	std::sort(list.begin(), list.end(), [](const PluginDescription& a, const PluginDescription& b)
	{
		const int format = a.pluginFormatName.compareNatural(b.pluginFormatName);
		if (format != 0)
			return format < 0;

		const int manufacturer = a.manufacturerName.compareNatural(b.manufacturerName);
		if (manufacturer != 0)
			return manufacturer < 0;

		return a.name.compareNatural(b.name) < 0;
	});

	return list;
}

bool AudioEngine::isVst2FormatActive() const
{
	for (int i = 0; i < formatManager.getNumFormats(); ++i)
	{
		auto* format = formatManager.getFormat(i);
		if (format == nullptr)
			continue;

		const String formatName = format->getName();
		if (formatName.containsIgnoreCase("VST") && !formatName.containsIgnoreCase("VST3"))
			return true;
	}

	return false;
}

AudioDeviceConfiguration AudioEngine::getAudioDeviceConfiguration()
{
    return deviceController.getAudioDeviceConfiguration();
}

AudioRecoveryConfiguration AudioEngine::getAudioRecoveryConfiguration() const
{
    return deviceController.getAudioRecoveryConfiguration();
}

AudioBlocklistConfiguration AudioEngine::getAudioBlocklistConfiguration() const
{
    return deviceController.getAudioBlocklistConfiguration();
}

AvailableAudioChoicesConfiguration AudioEngine::getAvailableAudioChoicesConfiguration()
{
    return deviceController.getAvailableAudioChoicesConfiguration();
}

bool AudioEngine::isAudioBackendBlocked(const String& backendName) const
{
    return deviceController.isAudioBackendBlocked(backendName);
}

bool AudioEngine::isAudioDeviceBlocked(const String& backendName, const String& role, const String& deviceName) const
{
    return deviceController.isAudioDeviceBlocked(backendName, role, deviceName);
}

bool AudioEngine::isAudioDeviceChoiceAllowed(const String& backendName,
                                             const String& inputDeviceName,
                                             const String& outputDeviceName) const
{
    return deviceController.isAudioDeviceChoiceAllowed(backendName, inputDeviceName, outputDeviceName);
}

bool AudioEngine::currentAudioDeviceMatchesPreferred(AudioRecoveryConfiguration const& recoveryConfig) const
{
    return deviceController.currentAudioDeviceMatchesPreferred(recoveryConfig);
}

void AudioEngine::closeCurrentAudioDeviceIfBlocked(const String& context)
{
    return deviceController.closeCurrentAudioDeviceIfBlocked(context);
}

void AudioEngine::rememberLastSelectedAudioDevice()
{
    return deviceController.rememberLastSelectedAudioDevice();
}

void AudioEngine::rememberManualSelectedAudioDevice()
{
    return deviceController.rememberManualSelectedAudioDevice();
}

bool AudioEngine::applyPreferredAudioDevice(AudioRecoveryConfiguration const& recoveryConfig, bool manualRetry)
{
    return deviceController.applyPreferredAudioDevice(recoveryConfig, manualRetry);
}

bool AudioEngine::setAudioBackendByIndex(int backendIndex)
{
    return deviceController.setAudioBackendByIndex(backendIndex);
}

bool AudioEngine::setAudioInputDeviceByIndex(int deviceIndex)
{
    return deviceController.setAudioInputDeviceByIndex(deviceIndex);
}

bool AudioEngine::setAudioOutputDeviceByIndex(int deviceIndex)
{
    return deviceController.setAudioOutputDeviceByIndex(deviceIndex);
}

bool AudioEngine::setAudioPersistenceMode(const String& mode)
{
    return deviceController.setAudioPersistenceMode(mode);
}

bool AudioEngine::setAudioPersistenceRetrySeconds(int seconds)
{
    return deviceController.setAudioPersistenceRetrySeconds(seconds);
}

bool AudioEngine::setAudioPersistenceRetryAttempts(int attempts)
{
    return deviceController.setAudioPersistenceRetryAttempts(attempts);
}

bool AudioEngine::setAudioPersistenceCustomBackendByIndex(int backendIndex)
{
    return deviceController.setAudioPersistenceCustomBackendByIndex(backendIndex);
}

bool AudioEngine::setAudioPersistenceCustomInputByIndex(int deviceIndex)
{
    return deviceController.setAudioPersistenceCustomInputByIndex(deviceIndex);
}

bool AudioEngine::setAudioPersistenceCustomOutputByIndex(int deviceIndex)
{
    return deviceController.setAudioPersistenceCustomOutputByIndex(deviceIndex);
}

bool AudioEngine::retryPreferredAudioDeviceNow()
{
    return deviceController.retryPreferredAudioDeviceNow();
}

bool AudioEngine::addBlockedAudioBackend(const String& backendName)
{
    return deviceController.addBlockedAudioBackend(backendName);
}

bool AudioEngine::addBlockedAudioInputDevice(const String& deviceName)
{
    return deviceController.addBlockedAudioInputDevice(deviceName);
}

bool AudioEngine::addBlockedAudioOutputDevice(const String& deviceName)
{
    return deviceController.addBlockedAudioOutputDevice(deviceName);
}

bool AudioEngine::removeBlockedAudioBackend(int index)
{
    return deviceController.removeBlockedAudioBackend(index);
}

bool AudioEngine::removeBlockedAudioDevice(int index)
{
    return deviceController.removeBlockedAudioDevice(index);
}

bool AudioEngine::setAudioBackendEnabledByIndex(int index, bool enabled)
{
    return deviceController.setAudioBackendEnabledByIndex(index, enabled);
}

bool AudioEngine::setAudioDeviceChoiceEnabledByIndex(int index, bool enabled)
{
    return deviceController.setAudioDeviceChoiceEnabledByIndex(index, enabled);
}

bool AudioEngine::setAudioSampleRate(double sampleRate)
{
    return deviceController.setAudioSampleRate(sampleRate);
}

bool AudioEngine::setAudioBufferSize(int bufferSize)
{
    return deviceController.setAudioBufferSize(bufferSize);
}

bool AudioEngine::setAudioInputChannelEnabled(int channelIndex, bool enabled)
{
    return deviceController.setAudioInputChannelEnabled(channelIndex, enabled);
}

bool AudioEngine::setAudioOutputChannelEnabled(int channelIndex, bool enabled)
{
    return deviceController.setAudioOutputChannelEnabled(channelIndex, enabled);
}

bool AudioEngine::setAllAudioInputChannelsEnabled(bool enabled)
{
    return deviceController.setAllAudioInputChannelsEnabled(enabled);
}

bool AudioEngine::setAllAudioOutputChannelsEnabled(bool enabled)
{
    return deviceController.setAllAudioOutputChannelsEnabled(enabled);
}

bool AudioEngine::setAudioInputChannelCount(int channelCount)
{
    return deviceController.setAudioInputChannelCount(channelCount);
}

bool AudioEngine::setAudioOutputChannelCount(int channelCount)
{
    return deviceController.setAudioOutputChannelCount(channelCount);
}

void AudioEngine::saveCurrentAudioChannelState()
{
    return deviceController.saveCurrentAudioChannelState();
}

void AudioEngine::applySavedAudioChannelState(AudioDeviceManager::AudioDeviceSetup& setup,
                                             const String& backendName,
                                             const String& inputDeviceName,
                                             const String& outputDeviceName)
{
    return deviceController.applySavedAudioChannelState(setup, backendName, inputDeviceName, outputDeviceName);
}

void AudioEngine::saveAudioDeviceState()
{
    return deviceController.saveAudioDeviceState();
}

void AudioEngine::scanPluginPath(const String& path, bool scanVst, bool scanVst3)
{
	const FileSearchPath searchPath(path);
	if (searchPath.getNumPaths() == 0)
		return;


	for (int i = 0; i < formatManager.getNumFormats(); ++i)
	{
		auto* format = formatManager.getFormat(i);
		if (format == nullptr)
			continue;

		const String formatName = format->getName();
		const bool isVst3 = formatName.containsIgnoreCase("VST3");
		const bool isVst = formatName.containsIgnoreCase("VST") && !isVst3;
		if ((isVst && !scanVst) || (isVst3 && !scanVst3) || (!isVst && !isVst3))
			continue;

		pluginScanner.enqueue(searchPath, formatName, knownPluginList.getTypes());
	}

}

void AudioEngine::scanDefaultPluginLocations(bool scanVst, bool scanVst3)
{
    for (auto* format : formatManager.getFormats()) {
        const auto name=format->getName();
        if((name=="VST"&&!scanVst)||(name=="VST3"&&!scanVst3))continue;
        pluginScanner.enqueue(getWindowsDefaultPluginSearchPath(*format,name=="VST",name=="VST3"),name,knownPluginList.getTypes(),false,true);
    }
}

void AudioEngine::scanPluginRoots(const var& roots)
{
    for(auto* format:formatManager.getFormats()) {
        std::map<bool,FileSearchPath> groups;
        for(const auto& root:*roots.getArray()) {
            const auto kind=root["format"].toString();
            if(kind!="all"&&kind!=format->getName())continue;
            groups[(bool)root["optional"]].add(File(root["path"].toString()));
        }
        for(auto& [optional,paths]:groups) {
            paths.removeRedundantPaths();
            pluginScanner.enqueue(paths,format->getName(),knownPluginList.getTypes(),false,optional);
        }
    }
}

void AudioEngine::collectPluginScanResults()
{
    auto results = pluginScanner.takeResults();
    if (results.empty()) return;
    for (auto plugin : results) {
        // Keep the persisted path/ID (and therefore aliases/session links) when
        // a manifest adds or removes the equivalent bundle representation.
        for(const auto& existing:knownPluginList.getTypes())
            if(lightHostModern::samePluginClass(existing,plugin)) {plugin.fileOrIdentifier=existing.fileOrIdentifier;break;}
        knownPluginList.addType(plugin);
    }
    knownPluginList.sendSynchronousChangeMessage();
    flushPendingSaves();
}

void AudioEngine::cancelPluginScan()
{
    pluginScanner.cancel();
    collectPluginScanResults();
}

bool AudioEngine::isPluginBypassed(int sortedIndex) const
{
    return isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))
        && instances.records[static_cast<size_t>(sortedIndex)].bypassed;
}

bool AudioEngine::isKnownPluginMenuId(int menuId) const
{
	return KnownPluginList::getIndexChosenByMenu(knownPluginList.getTypes(), menuId) > -1;
}

void AudioEngine::addKnownPluginsToMenu(PopupMenu& menu) const
{
	KnownPluginList::addToMenu(menu, knownPluginList.getTypes(), pluginSortMethod);
}

void AudioEngine::loadActivePlugins()
{
    const auto monoKey = deviceController.monoInputsKey();
    hostProcessor.setMonoInputs(monoKey.isNotEmpty() && getAppProperties().getUserSettings()->getBoolValue(monoKey, false));
    const auto outputKey = deviceController.monoOutputKey();
    hostProcessor.setMonoOutput(outputKey.isNotEmpty() && getAppProperties().getUserSettings()->getBoolValue(outputKey, false));
    if (isDiagnosticsEnabled()) ++chainReloadCount;
    auto snapshot = std::make_shared<ChainSnapshot>();
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        snapshot->inputChannels = jmax(1, device->getActiveInputChannels().countNumberOfSetBits());
        snapshot->outputChannels = jmax(1, device->getActiveOutputChannels().countNumberOfSetBits());
    }
    auto previous = hostProcessor.getActiveSnapshot();
    std::vector<String> slotStrips;
    for (auto& record : instances.records)
    {
        if (sessionLoadSuppressed) { record.loading = "suspended"; continue; }
        if (record.error.isNotEmpty() && record.loading != "missing") { record.loading = "failed"; continue; }
        const auto& description = record.description;
        setLightHostModernCrashContext("Loading instance " + record.id + " " + description.name);
        std::shared_ptr<PluginSlot> slot;
        if (previous)
            for (const auto& candidate : previous->slots)
                if (candidate && candidate->instanceId == record.id && !candidate->processDisabled.load())
                { slot = candidate; break; }
        if (slot) ++snapshot->reusedSlots;
        else
        {
            if (!formatManager.doesPluginStillExist(description))
            {
                record.loading = "missing";
                record.error = "Plugin file or identifier no longer exists";
                continue;
            }
            record.loading = "loading";
            record.error.clear();
            try
            {
                auto processor = formatManager.createPluginInstance(description,
                    hostProcessor.getCurrentSampleRateForPlugins(), hostProcessor.getCurrentBlockSizeForPlugins(), record.error);
                if (!processor)
                {
                    if (record.error.isEmpty()) record.error = "Could not create plugin instance";
                    record.loading = "failed";
                    continue;
                }
                if (jmax(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels()) > RealtimeHostProcessor::maxScratchChannels)
                    throw std::runtime_error("Plugin layout exceeds 256 channels");
                if (processor->getTotalNumInputChannels() == 0 && processor->getTotalNumOutputChannels() == 0 && !processor->isMidiEffect())
                    throw std::runtime_error("Plugin exposes no audio channels");
                PluginDescription actual;
                processor->fillInPluginDescription(actual);
                lightHostModern::restorePluginState(record, actual, [&](const void* data, int size) { processor->setStateInformation(data, size); });
                slot = std::make_shared<PluginSlot>(description, std::move(processor));
                slot->instanceId = record.id;
                ++snapshot->rebuiltSlots;
            }
            catch (const std::exception& error) { record.error = String::fromUTF8(error.what()); }
            catch (...) { record.error = "Plugin threw while creating instance"; }
            if (!slot) { record.loading = "failed"; continue; }
        }
        record.loading = "loaded";
        record.error.clear();
        slot->bypassed.store(record.bypassed, std::memory_order_release);
        if (!slot->windowProperties.contains("uiLastX_Normal") && record.hasEditorPosition)
        {
            slot->windowProperties.set("uiLastX_Normal", record.editorX);
            slot->windowProperties.set("uiLastY_Normal", record.editorY);
            slot->windowProperties.set("uiLastX_Generic", record.editorX);
            slot->windowProperties.set("uiLastY_Generic", record.editorY);
        }
        if (!slot->windowProperties.contains("uiLastW_Normal") && record.hasEditorSize)
        {
            slot->windowProperties.set("uiLastW_Normal", record.editorW);
            slot->windowProperties.set("uiLastH_Normal", record.editorH);
            slot->windowProperties.set("uiLastW_Generic", record.editorW);
            slot->windowProperties.set("uiLastH_Generic", record.editorH);
        }
        snapshot->maxPluginChannels = jmax(snapshot->maxPluginChannels, jmax(slot->inputChannels, slot->outputChannels));
        snapshot->slots.push_back(std::move(slot));
        slotStrips.push_back(record.stripId);
    }
    instances.ensureStrips();
    snapshot->masterGainLinear = lightHostModern::gainFromDb(instances.masterGainDb);
    BigInteger inputMask, outputMask;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        inputMask = device->getActiveInputChannels();
        outputMask = device->getActiveOutputChannels();
    }
    const auto packed = [](const BigInteger& mask, int physical) {
        if (physical < 0 || !mask[physical]) return -1;
        int index = 0;
        for (int bit = mask.findNextSetBit(0); bit >= 0 && bit < physical; bit = mask.findNextSetBit(bit + 1)) ++index;
        return index;
    };
    for (const auto& strip : instances.strips)
    {
        StripSnapshot item;
        item.id = strip.id;
        item.allInputs = strip.allInputs;
        item.allOutputs = strip.allOutputs;
        item.gainLinear = lightHostModern::gainFromDb(strip.gainDb);
        item.pan = strip.pan;
        item.muted = strip.muted;
        item.solo = strip.solo;
        item.record = strip.record;
        if (!strip.allInputs)
            for (int physical : strip.inputs) item.inputMap.push_back(packed(inputMask, physical));
        if (!strip.allOutputs)
            for (int physical : strip.outputs) item.outputMap.push_back(packed(outputMask, physical));
        if (!strip.allOutputs && item.outputMap.size() == 1 && item.outputMap[0] >= 0)
        {
            const int selected = item.outputMap[0];
            const int other = selected % 2 == 0 ? selected + 1 : selected - 1;
            if (other >= 0 && other < outputMask.countNumberOfSetBits())
            {
                item.outputMap[0] = juce::jmin(selected, other);
                item.outputMap.push_back(juce::jmax(selected, other));
            }
        }
        if (!strip.allInputs || !strip.allOutputs)
        {
            item.busChannels = jmax(1, jmax((int) item.inputMap.size(), (int) item.outputMap.size()));
            if (item.inputMap.size() <= 1 && item.outputMap.size() >= 2) item.busChannels = jmax(item.busChannels, 2);
        }
        for (size_t index = 0; index < snapshot->slots.size() && index < slotStrips.size(); ++index)
            if (slotStrips[index] == strip.id) item.slots.push_back(snapshot->slots[index]);
        snapshot->strips.push_back(std::move(item));
    }
    for (size_t index = 0; index < snapshot->slots.size() && index < slotStrips.size(); ++index)
        if (slotStrips[index] == lightHostModern::masterStripId) snapshot->masterSlots.push_back(snapshot->slots[index]);
    {
        RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
        if (previous)
            for (const auto& old : previous->slots)
                if (old && std::find(snapshot->slots.begin(), snapshot->slots.end(), old) == snapshot->slots.end())
                    PluginWindow::closeCurrentlyOpenWindowsFor(*old->processor);
        hostProcessor.publishSnapshot(std::move(snapshot));
    }
    ++chainVersion;
    clearLightHostModernCrashContext();
    markSettingsDirty();
    if (!sessionLoadSuppressed)
        for (int index = 0; index < (int) instances.records.size(); ++index)
            if (instances.records[(size_t) index].editorOpen)
                if (auto* live = findActiveSlotFor(instances.records[(size_t) index].id))
                    if (live->processor && !PluginWindow::isOpenFor(*live->processor))
                        showPluginEditor(index);
}

void AudioEngine::addPluginFromMenuId(int menuId)
{
	const auto knownTypes = knownPluginList.getTypes();
	const int knownIndex = KnownPluginList::getIndexChosenByMenu(knownTypes, menuId);
	if (knownIndex < 0)
		return;

	const auto sortedKnownTypes = getKnownPluginsSorted();
	for (int i = 0; i < (int) sortedKnownTypes.size(); ++i)
	{
		if (sortedKnownTypes[(size_t) i].isDuplicateOf(knownTypes[knownIndex]))
		{
			addKnownPluginByIndex(i);
			return;
		}
	}
}

bool AudioEngine::addKnownPluginByIndex(int sortedIndex)
{
    const auto known = getKnownPluginsSorted();
    if (!instances.writable || sessionLoadSuppressed || !isPositiveAndBelow(sortedIndex, static_cast<int>(known.size()))) return false;
    instances.ensureStrips();
    chainHistory.record(instances);
    auto record = lightHostModern::newKnownPluginInstance(*getAppProperties().getUserSettings(), known[static_cast<size_t>(sortedIndex)]);
    record.stripId = instances.strips.front().id;
    const auto id = record.id;
    instances.records.push_back(std::move(record));
    loadActivePlugins();
    saveActivePluginChain(false);
    return findActiveSlotFor(id) != nullptr;
}

void AudioEngine::duplicatePlugin(int sortedIndex)
{
    if (!instances.writable || sessionLoadSuppressed || !isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))) return;
    savePluginStates();
    chainHistory.record(instances);
    auto record = instances.records[static_cast<size_t>(sortedIndex)];
    record.id = Uuid().toString();
    instances.records.insert(instances.records.begin() + sortedIndex + 1, std::move(record));
    loadActivePlugins();
    saveActivePluginChain(false);
}

int AudioEngine::removeKnownPluginByIndex(int sortedIndex)
{
    const auto known = getKnownPluginsSorted();
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, static_cast<int>(known.size()))) return 0;
    const auto& description = known[static_cast<size_t>(sortedIndex)];
    const auto identity = lightHostModern::knownPluginId(description);
    const auto before = instances.records.size();
    instances.records.erase(std::remove_if(instances.records.begin(), instances.records.end(), [&](const auto& record) {
        return record.identityResolved && record.originalIdentity == identity;
    }), instances.records.end());
    knownPluginList.removeType(description);
    const int removed = static_cast<int>(before - instances.records.size());
    if (removed > 0) loadActivePlugins();
    saveActivePluginChain(false);
    return removed;
}

int AudioEngine::clearKnownPlugins()
{
    if (!instances.writable || sessionLoadSuppressed) return 0;
    chainHistory.clear();
    cancelPluginScan();
    const int removed = static_cast<int>(instances.records.size());
    instances.records.clear();
    knownPluginList.clear();
    loadActivePlugins();
    saveActivePluginChain(false);
    return removed;
}

void AudioEngine::openKnownPluginLocation(int sortedIndex) const
{
	const auto knownTypes = getKnownPluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) knownTypes.size())
		return;

	File location(knownTypes[(size_t) sortedIndex].fileOrIdentifier);
	if (location.existsAsFile())
		location.revealToUser();
	else if (location.getParentDirectory().exists())
		location.getParentDirectory().revealToUser();
}

void AudioEngine::removePlugin(int sortedIndex)
{
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))) return;
    savePluginStates();
    chainHistory.record(instances);
    instances.records.erase(instances.records.begin() + sortedIndex);
    loadActivePlugins();
    saveActivePluginChain(false);
}

void AudioEngine::movePluginUp(int sortedIndex)
{
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, (int) instances.records.size())) return;
    const auto strip = instances.records[(size_t) sortedIndex].stripId;
    for (int index = sortedIndex - 1; index >= 0; --index)
        if (instances.records[(size_t) index].stripId == strip)
        {
            chainHistory.record(instances);
            std::swap(instances.records[(size_t) sortedIndex], instances.records[(size_t) index]);
            loadActivePlugins();
            saveActivePluginChain(false);
            return;
        }
}

void AudioEngine::movePluginDown(int sortedIndex)
{
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, (int) instances.records.size())) return;
    const auto strip = instances.records[(size_t) sortedIndex].stripId;
    for (int index = sortedIndex + 1; index < (int) instances.records.size(); ++index)
        if (instances.records[(size_t) index].stripId == strip)
        {
            chainHistory.record(instances);
            std::swap(instances.records[(size_t) sortedIndex], instances.records[(size_t) index]);
            loadActivePlugins();
            saveActivePluginChain(false);
            return;
        }
}

void AudioEngine::movePluginToIndex(int fromSortedIndex, int toSortedIndex, bool recordHistory, bool adoptStrip)
{
    const int count = static_cast<int>(instances.records.size());
    if (!isSessionWritable() || !isPositiveAndBelow(fromSortedIndex, count) || count == 0) return;
    toSortedIndex = jlimit(0, count - 1, toSortedIndex);
    if (fromSortedIndex == toSortedIndex) return;
    const auto targetStrip = instances.records[static_cast<size_t>(toSortedIndex)].stripId;
    if (recordHistory) chainHistory.record(instances);
    auto record = std::move(instances.records[static_cast<size_t>(fromSortedIndex)]);
    if (adoptStrip) record.stripId = targetStrip;
    instances.records.erase(instances.records.begin() + fromSortedIndex);
    instances.records.insert(instances.records.begin() + toSortedIndex, std::move(record));
    loadActivePlugins();
    saveActivePluginChain(false);
}

void AudioEngine::setPluginBypassed(int sortedIndex, bool shouldBypass)
{
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))) return;
    auto& record = instances.records[static_cast<size_t>(sortedIndex)];
    if (record.bypassed == shouldBypass) return;
    chainHistory.record(instances);
    record.bypassed = shouldBypass;
    if (auto* slot = findActiveSlotFor(record.id)) slot->bypassed.store(shouldBypass, std::memory_order_release);
    if (isDiagnosticsEnabled()) ++bypassToggleCount;
    ++chainVersion;
    saveActivePluginChain(false);
}

void AudioEngine::deletePluginStates()
{
    if (!instances.writable || sessionLoadSuppressed) return;
    chainHistory.clear();
    RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
    if (auto snapshot = hostProcessor.getActiveSnapshot())
        for (const auto& slot : snapshot->slots) if (slot) PluginWindow::closeCurrentlyOpenWindowsFor(*slot->processor);
    hostProcessor.publishSnapshot(nullptr);
    for (auto& record : instances.records)
    {
        record.lastValidState.clear();
        record.recoveryState.clear();
        record.stateCaptureAllowed = true;
        record.error.clear();
        record.loading = "unloaded";
    }
    loadActivePlugins();
    saveActivePluginChain(false);
}

void AudioEngine::savePluginStates()
{
    syncEditorWindows(true);
    if (!instances.writable || sessionLoadSuppressed) return;
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    const auto snapshot = hostProcessor.getActiveSnapshot();
    if (!snapshot) return;
    bool captured = false;
    stateCaptureFailures.clear();
    for (const auto& slot : snapshot->slots)
    {
        if (!slot || !slot->processor) continue;
        const int index = instances.indexOf(slot->instanceId);
        if (index < 0) continue;
        auto& record = instances.records[static_cast<size_t>(index)];
        if (!record.stateCaptureAllowed || slot->processDisabled.load()) continue;
        slot->stateDirty.store(false, std::memory_order_relaxed);
        if (lightHostModern::capturePluginState(record, [&](MemoryBlock& binary) { slot->processor->getStateInformation(binary); }))
        {
            captured = true;
        }
        else
        {
            stateCaptureFailures.add(record.id);
            Logger::writeToLog("LightHostModern: state capture failed; previous state retained for " + record.id);
        }
    }
    stateCaptureDue = 0;
    ++chainVersion; // Includes capture diagnostics, even when the last state is retained.
    if (captured) { if (isDiagnosticsEnabled()) ++pluginStateSaveCount; saveActivePluginList(); }
}

void AudioEngine::saveActivePluginList()
{
    if (!instances.writable || sessionLoadSuppressed) return;
    if (sessionStore) sessionStore->submit(instances, instances.records.empty(), sessionMigrationId);
    if (chainProfiles)
        if (const auto error = chainProfiles->writeActive(instances, sessionMigrationId); error.isNotEmpty())
            lightHostModernLog("Chain profile save: " + error);
}

String AudioEngine::prepareChainProfileChange()
{
    if (!isSessionWritable() || !chainProfiles) return "session_read_only";
    savePluginStates();
    if (const auto error = chainProfiles->writeActive(instances, sessionMigrationId); error.isNotEmpty()) return error;
    if (sessionStore && !sessionStore->flush()) return "session_save_failed";
    return {};
}

String AudioEngine::createChainProfile(const String& name)
{
    if (const auto error = prepareChainProfileChange(); error.isNotEmpty()) return error;
    String id;
    if (const auto error = chainProfiles->create(instances, sessionMigrationId, name, id); error.isNotEmpty()) return error;
    ++profileVersion;
    return {};
}

String AudioEngine::switchChainProfile(const String& id)
{
    if (!isSessionWritable() || !chainProfiles) return "session_read_only";
    if (chainProfiles->catalog().activeId == id) return {};
    const auto previousRecords = instances.records;
    const auto previousStrips = instances.strips;
    const auto previousMaster = instances.masterGainDb;
    const auto previousRecovery = instances.recoveryError;
    const auto previousMigration = sessionMigrationId;
    if (const auto error = prepareChainProfileChange(); error.isNotEmpty()) return error;
    lightHostModern::PluginInstances loaded;
    String migrationId;
    if (const auto error = chainProfiles->switchTo(id, loaded, migrationId); error.isNotEmpty()) return error;
    instances.records = std::move(loaded.records);
    instances.strips = std::move(loaded.strips);
    instances.masterGainDb = loaded.masterGainDb;
    instances.recoveryError = loaded.recoveryError;
    sessionMigrationId = migrationId;
    loadActivePlugins();
    if (const auto error = chainProfiles->setActive(id); error.isNotEmpty())
    {
        instances.records = previousRecords;
        instances.strips = previousStrips;
        instances.masterGainDb = previousMaster;
        instances.recoveryError = previousRecovery;
        sessionMigrationId = previousMigration;
        loadActivePlugins();
        saveActivePluginList();
        return error;
    }
    saveActivePluginList();
    if (sessionStore && !sessionStore->flush()) return "session_save_failed";
    chainHistory.clear();
    ++profileVersion;
    return {};
}

String AudioEngine::renameChainProfile(const String& id, const String& name)
{
    if (!isSessionWritable() || !chainProfiles) return "session_read_only";
    if (const auto error = chainProfiles->rename(id, name); error.isNotEmpty()) return error;
    ++profileVersion;
    return {};
}

String AudioEngine::duplicateChainProfile(const String& id)
{
    if (const auto error = prepareChainProfileChange(); error.isNotEmpty()) return error;
    String copyId;
    if (const auto error = chainProfiles->duplicate(id, copyId); error.isNotEmpty()) return error;
    ++profileVersion;
    return {};
}

String AudioEngine::deleteChainProfile(const String& id)
{
    if (!isSessionWritable() || !chainProfiles) return "session_read_only";
    const auto profiles = chainProfiles->catalog().profiles;
    if (profiles.size() <= 1) return "last_profile";
    if (chainProfiles->catalog().activeId == id)
    {
        size_t index = 0;
        for (; index < profiles.size(); ++index)
            if (profiles[index].id == id) break;
        if (index >= profiles.size()) return "profile_not_found";
        const auto next = profiles[(index + 1) % profiles.size()].id;
        if (next == id) return "last_profile";
        if (const auto error = switchChainProfile(next); error.isNotEmpty()) return error;
    }
    if (const auto error = chainProfiles->remove(id); error.isNotEmpty()) return error;
    ++profileVersion;
    return {};
}

String AudioEngine::moveChainProfile(const String& id, int delta)
{
    if (!isSessionWritable() || !chainProfiles) return "session_read_only";
    if (const auto error = chainProfiles->move(id, delta); error.isNotEmpty()) return error;
    ++profileVersion;
    return {};
}

lightHostModern::TemplateSnapshot AudioEngine::captureTemplate()
{
    lightHostModern::TemplateSnapshot snapshot;
    snapshot.session = instances.serialize();
    auto captured = deviceController.captureAudioSetup();
    snapshot.device = std::move(captured.device);
    snapshot.channels = std::move(captured.channels);
    snapshot.monoInputs = isMonoInputs();
    snapshot.monoOutput = isMonoOutput();
    snapshot.muted = isGlobalMuted();
    snapshot.bypassed = isGlobalBypassed();
    snapshot.persistence = deviceController.getAudioRecoveryConfiguration().mode;
    return snapshot;
}

String AudioEngine::createTemplate(const String& name)
{
    if (!isSessionWritable() || !templates) return "session_read_only";
    flushSession();
    String id;
    if (const auto error = templates->create(name, captureTemplate(), id); error.isNotEmpty()) return error;
    getAppProperties().getUserSettings()->setValue("activeTemplate", id);
    markSettingsDirty();
    return {};
}

String AudioEngine::updateTemplate(const String& id)
{
    if (!isSessionWritable() || !templates) return "session_read_only";
    flushSession();
    if (const auto error = templates->update(id, captureTemplate()); error.isNotEmpty()) return error;
    getAppProperties().getUserSettings()->setValue("activeTemplate", id);
    markSettingsDirty();
    return {};
}

String AudioEngine::recallTemplate(const String& id)
{
    if (!isSessionWritable() || !templates) return "session_read_only";
    lightHostModern::TemplateSnapshot snapshot;
    if (const auto error = templates->read(id, snapshot); error.isNotEmpty()) return error;
    lightHostModern::PluginInstances loaded;
    if (snapshot.session == nullptr || !loaded.deserialize(*snapshot.session)) return "template_session_invalid";
    loaded.ensureStrips();
    flushSession();
    instances = std::move(loaded);
    loadActivePlugins();
    const auto audioError = deviceController.applyAudioSetup(snapshot.device.get(), snapshot.channels.get(), snapshot.persistence);
    hostProcessor.setMonoInputs(snapshot.monoInputs);
    hostProcessor.setMonoOutput(snapshot.monoOutput);
    setMonoInputs(snapshot.monoInputs);
    setMonoOutput(snapshot.monoOutput);
    setGlobalMuted(snapshot.muted);
    setGlobalBypassed(snapshot.bypassed);
    loadActivePlugins();
    saveActivePluginList();
    if (sessionStore) sessionStore->flush();
    chainHistory.clear();
    getAppProperties().getUserSettings()->setValue("activeTemplate", id);
    markSettingsDirty();
    ++chainVersion;
    return audioError;
}

String AudioEngine::renameTemplate(const String& id, const String& name)
{
    if (!isSessionWritable() || !templates) return "session_read_only";
    return templates->rename(id, name);
}

String AudioEngine::deleteTemplate(const String& id)
{
    if (!isSessionWritable() || !templates) return "session_read_only";
    if (const auto error = templates->remove(id); error.isNotEmpty()) return error;
    if (activeTemplateId() == id)
    {
        getAppProperties().getUserSettings()->removeValue("activeTemplate");
        markSettingsDirty();
    }
    return {};
}

String AudioEngine::exportTemplate(const String& id, const File& file)
{
    if (!templates) return "session_read_only";
    return templates->exportFile(id, file);
}

String AudioEngine::importTemplate(const File& file)
{
    if (!isSessionWritable() || !templates) return "session_read_only";
    String id;
    return templates->importFile(file, id);
}

bool AudioEngine::renamePlugin(int sortedIndex, const String& name)
{
    String normalized;
    if (!isSessionWritable() || sortedIndex < 0 || sortedIndex >= static_cast<int>(instances.records.size())
        || !lightHostModern::normalizeInstanceName(name, normalized)) return false;
    auto& record = instances.records[static_cast<size_t>(sortedIndex)];
    if (normalized == record.description.name) normalized.clear();
    if (record.customName == normalized) return true;
    chainHistory.record(instances);
    record.customName = normalized;
    ++chainVersion;
    saveActivePluginChain(false);
    return true;
}

bool AudioEngine::renameKnownPlugin(int sortedIndex, const String& name)
{
    const auto known = getKnownPluginsSorted();
    if (!isPositiveAndBelow(sortedIndex, static_cast<int>(known.size()))) return false;
    const auto& plugin = known[static_cast<size_t>(sortedIndex)];
    const auto previous = getKnownPluginCustomName(plugin);
    if (!lightHostModern::setKnownPluginCustomName(*getAppProperties().getUserSettings(), plugin, name)) return false;
    if (previous != getKnownPluginCustomName(plugin)) { ++pluginDatabaseVersion; markSettingsDirty(); }
    return true;
}

void AudioEngine::setMonoInputs(bool value)
{
    const auto key = deviceController.monoInputsKey();
    if (key.isEmpty()) return;
    hostProcessor.setMonoInputs(value);
    getAppProperties().getUserSettings()->setValue(key, value);
    markSettingsDirty(); ++chainVersion;
}

void AudioEngine::setMonoOutput(bool value)
{
    const auto key = deviceController.monoOutputKey();
    if (key.isEmpty()) return;
    hostProcessor.setMonoOutput(value);
    getAppProperties().getUserSettings()->setValue(key, value);
    markSettingsDirty(); ++chainVersion;
}

void AudioEngine::setDiagnosticsEnabled(bool enabled)
{
    hostProcessor.setDiagnosticsEnabled(enabled);
    {
        const ScopedLock callbackLock(deviceManager.getAudioCallbackLock());
        deviceManager.setDiagnosticsEnabled(enabled);
        if (!enabled) player.callbackMeasurement().stop();
    }
    hostCpuSampler.reset(); workerCpuSampler.reset();
    if (enabled || lightHostModern::verbose::logger().active()) startTimer(diagnosticsTimerId, 30000);
    else stopTimer(diagnosticsTimerId);
    getAppProperties().getUserSettings()->setValue("diagnosticsEnabled", enabled);
    markSettingsDirty();
}

void AudioEngine::saveActivePluginChain(bool saveProcessorStates)
{
	if (saveProcessorStates)
		savePluginStates();

	saveActivePluginList();
}

bool AudioEngine::flushSession()
{
    syncEditorWindows(true);
    savePluginStates();
    saveActivePluginList();
    if (!instances.writable) return false;
    return !sessionStore || sessionStore->flush();
}

void AudioEngine::flushPendingSaves()
{
	stopTimer(persistenceTimerId);

	if (settingsDirty || pluginStateStore.isDirty())
	{
		settingsDirty = false;
		if (isDiagnosticsEnabled()) settingsFlushCount++;
		pluginStateStore.flushIfDirty();
		getAppProperties().getUserSettings()->saveIfNeeded();
	}
}

void AudioEngine::removePluginsLackingInputOutput()
{
	std::vector<PluginDescription> removeList;
	const auto knownTypes = knownPluginList.getTypes();
	for (auto& plugin : knownTypes)
	{
		if (plugin.numInputChannels <= 0 || plugin.numOutputChannels <= 0)
			removeList.push_back(plugin);
	}

	for (auto& plugin : removeList)
		knownPluginList.removeType(plugin);
}

void AudioEngine::removeMissingKnownPlugins()
{
	std::vector<PluginDescription> removeList;
	const auto knownTypes = knownPluginList.getTypes();
	for (auto& plugin : knownTypes)
	{
		const File pluginFile(plugin.fileOrIdentifier);
		const bool looksLikePath = plugin.fileOrIdentifier.containsChar('\\')
			|| plugin.fileOrIdentifier.containsChar('/')
			|| plugin.fileOrIdentifier.containsChar(':');

		if (plugin.fileOrIdentifier.isNotEmpty()
			&& looksLikePath
			&& !pluginFile.exists())
			removeList.push_back(plugin);
	}

	for (auto& plugin : removeList)
		knownPluginList.removeType(plugin);

	if (!removeList.empty())
		flushPendingSaves();
}

String AudioEngine::addKnownPluginAt(int sortedIndex, const String& stripId, const String& beforeInstanceId)
{
    if (!isSessionWritable()) return "session_read_only";
    instances.ensureStrips();
    if (stripId != lightHostModern::masterStripId && !instances.findStrip(stripId)) return "strip_not_found";
    const auto known = getKnownPluginsSorted();
    if (!isPositiveAndBelow(sortedIndex, (int) known.size())) return "known_plugin_not_found";
    chainHistory.record(instances);
    auto record = lightHostModern::newKnownPluginInstance(*getAppProperties().getUserSettings(), known[(size_t) sortedIndex]);
    record.stripId = stripId;
    const int before = beforeInstanceId.isEmpty() ? -1 : instances.indexOf(beforeInstanceId);
    if (before >= 0) instances.records.insert(instances.records.begin() + before, std::move(record));
    else instances.records.push_back(std::move(record));
    loadActivePlugins();
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::addStrip(const String& name)
{
    if (!isSessionWritable()) return "session_read_only";
    instances.ensureStrips();
    if ((int) instances.strips.size() >= lightHostModern::maximumStrips) return "strip_limit";
    String normalized;
    if (!lightHostModern::normalizeInstanceName(name, normalized) || normalized.isEmpty()) return "profile_name_invalid";
    chainHistory.record(instances);
    lightHostModern::ChainStrip strip;
    strip.id = Uuid().toString().removeCharacters("-").toLowerCase();
    strip.name = normalized;
    strip.allInputs = false;
    strip.allOutputs = false;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const int input = device->getActiveInputChannels().findNextSetBit(0);
        if (input >= 0) strip.inputs.push_back(input);
        const auto outputs = device->getActiveOutputChannels();
        const int first = outputs.findNextSetBit(0);
        const int second = first < 0 ? -1 : outputs.findNextSetBit(first + 1);
        if (first >= 0) strip.outputs.push_back(first);
        if (second >= 0) strip.outputs.push_back(second);
    }
    if (strip.inputs.empty()) strip.allInputs = true;
    if (strip.outputs.empty()) strip.allOutputs = true;
    instances.strips.push_back(std::move(strip));
    loadActivePlugins();
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::removeStrip(const String& id)
{
    if (!isSessionWritable()) return "session_read_only";
    instances.ensureStrips();
    if (instances.strips.size() <= 1) return "last_strip";
    if (!instances.findStrip(id)) return "strip_not_found";
    savePluginStates();
    chainHistory.record(instances);
    instances.records.erase(std::remove_if(instances.records.begin(), instances.records.end(), [&](const auto& record) { return record.stripId == id; }), instances.records.end());
    instances.strips.erase(std::remove_if(instances.strips.begin(), instances.strips.end(), [&](const auto& strip) { return strip.id == id; }), instances.strips.end());
    loadActivePlugins();
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::renameStrip(const String& id, const String& name)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    String normalized;
    if (!lightHostModern::normalizeInstanceName(name, normalized) || normalized.isEmpty()) return "profile_name_invalid";
    if (strip->name == normalized) return {};
    chainHistory.record(instances);
    strip->name = normalized;
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripRouting(const String& id, bool allInputs, bool allOutputs, const std::vector<int>& inputs, const std::vector<int>& outputs)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    auto routed = outputs;
    if (!allOutputs && routed.size() == 1)
    {
        const auto active = getAudioDeviceConfiguration().activeOutputChannels;
        const int selected = routed[0];
        const int other = selected % 2 == 0 ? selected + 1 : selected - 1;
        if (other >= 0 && other < (int) active.size() && active[(size_t) other])
            routed = { jmin(selected, other), jmax(selected, other) };
    }
    if ((!allInputs && inputs.empty()) || (!allOutputs && routed.empty())) return "invalid_arguments";
    if (inputs.size() == 2 && inputs[0] == inputs[1]) return "invalid_arguments";
    if (routed.size() == 2 && routed[0] == routed[1]) return "invalid_arguments";
    if (!allInputs && inputs.size() > 2) return "invalid_arguments";
    if (!allOutputs && routed.size() > 2) return "invalid_arguments";
    if (strip->allInputs == allInputs && strip->allOutputs == allOutputs && strip->inputs == inputs && strip->outputs == routed)
        return {};
    chainHistory.record(instances);
    strip->allInputs = allInputs;
    strip->allOutputs = allOutputs;
    strip->inputs = inputs;
    strip->outputs = routed;
    strip->stereo = !allInputs && inputs.size() == 2;
    loadActivePlugins();
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripWidth(const String& id, bool stereo)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    if (strip->stereo == stereo) return {};
    const auto config = getAudioDeviceConfiguration();
    const auto partner = [](const std::vector<bool>& active, int current) {
        for (int i = current + 1; i < (int) active.size(); ++i)
            if (active[(size_t) i]) return i;
        for (int i = 0; i < current && i < (int) active.size(); ++i)
            if (active[(size_t) i]) return i;
        return -1;
    };
    chainHistory.record(instances);
    strip->stereo = stereo;
    if (!stereo)
    {
        if (!strip->inputs.empty()) strip->inputs.resize(1);
        if (!strip->outputs.empty()) strip->outputs.resize(1);
    }
    else
    {
        if (strip->allInputs)
        {
            const auto found = std::find(config.activeInputChannels.begin(), config.activeInputChannels.end(), true);
            const int first = (int) std::distance(config.activeInputChannels.begin(), found);
            if (first < (int) config.activeInputChannels.size())
            {
                strip->allInputs = false;
                strip->inputs = { first };
            }
        }
        if (!strip->allInputs && strip->inputs.size() == 1)
        {
            const int second = partner(config.activeInputChannels, strip->inputs[0]);
            if (second >= 0) strip->inputs.push_back(second);
        }
        if (strip->allOutputs)
        {
            const auto found = std::find(config.activeOutputChannels.begin(), config.activeOutputChannels.end(), true);
            const int first = (int) std::distance(config.activeOutputChannels.begin(), found);
            if (first < (int) config.activeOutputChannels.size())
            {
                strip->allOutputs = false;
                strip->outputs = { first };
            }
        }
        if (!strip->allOutputs && strip->outputs.size() == 1)
        {
            const int second = partner(config.activeOutputChannels, strip->outputs[0]);
            if (second >= 0) strip->outputs.push_back(second);
        }
    }
    loadActivePlugins();
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripGain(const String& id, float gainDb)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    gainDb = lightHostModern::clampGainDb(gainDb);
    if (strip->gainDb == gainDb) return {};
    chainHistory.record(instances, "gain:" + id);
    strip->gainDb = gainDb;
    hostProcessor.setStripGain(id, lightHostModern::gainFromDb(gainDb));
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripPan(const String& id, float pan)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    pan = lightHostModern::clampPan(pan);
    if (strip->pan == pan) return {};
    chainHistory.record(instances, "pan:" + id);
    strip->pan = pan;
    hostProcessor.setStripPan(id, pan);
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripColor(const String& id, int color)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    color = jlimit(0, 8, color);
    if (strip->color == color && strip->colour.isEmpty()) return {};
    chainHistory.record(instances);
    strip->color = color;
    strip->colour.clear();
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripColour(const String& id, const String& hex)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    auto value = hex.trim().toLowerCase().replace("#", "");
    if (value.length() != 6 || !value.containsOnly("0123456789abcdef")) return "invalid_arguments";
    if (strip->colour == value) return {};
    chainHistory.record(instances);
    strip->colour = value;
    strip->color = 0;
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripMuted(const String& id, bool muted)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    if (strip->muted == muted) return {};
    chainHistory.record(instances);
    strip->muted = muted;
    hostProcessor.setStripMuted(id, muted);
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripSolo(const String& id, bool solo)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    if (strip->solo == solo) return {};
    chainHistory.record(instances);
    strip->solo = solo;
    hostProcessor.setStripSolo(id, solo);
    hostProcessor.setAnySolo(std::any_of(instances.strips.begin(), instances.strips.end(), [](const auto& item) { return item.solo; }));
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripRecord(const String& id, bool record)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    if (strip->record == record) return {};
    chainHistory.record(instances);
    strip->record = record;
    hostProcessor.setStripRecord(id, record);
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::setStripGroup(const String& id, const String& group)
{
    if (!isSessionWritable()) return "session_read_only";
    auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id));
    if (!strip) return "strip_not_found";
    auto name = group.trim();
    if (name.length() > 64) name = name.substring(0, 64);
    if (strip->group == name) return {};
    chainHistory.record(instances);
    strip->group = name;
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::orderStrips(const std::vector<std::pair<String, String>>& order)
{
    if (!isSessionWritable()) return "session_read_only";
    if (order.size() != instances.strips.size()) return "strip_not_found";
    std::vector<lightHostModern::ChainStrip> next;
    next.reserve(order.size());
    std::set<String> seen;
    for (const auto& item : order)
    {
        if (!seen.insert(item.first).second) return "strip_not_found";
        auto found = std::find_if(instances.strips.begin(), instances.strips.end(), [&](const auto& strip) { return strip.id == item.first; });
        if (found == instances.strips.end()) return "strip_not_found";
        auto strip = *found;
        auto group = item.second.trim();
        if (group.length() > 64) group = group.substring(0, 64);
        strip.group = group;
        next.push_back(std::move(strip));
    }
    if (next.size() == instances.strips.size())
    {
        bool same = true;
        for (size_t i = 0; i < next.size(); ++i)
            same = same && next[i].id == instances.strips[i].id && next[i].group == instances.strips[i].group;
        if (same) return {};
    }
    chainHistory.record(instances);
    instances.strips = std::move(next);
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

void AudioEngine::setStripGainLive(const String& id, float gainDb)
{
    gainDb = lightHostModern::clampGainDb(gainDb);
    if (auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id))) strip->gainDb = gainDb;
    hostProcessor.setStripGain(id, lightHostModern::gainFromDb(gainDb));
    surfaceGenerationValue.fetch_add(1);
}

void AudioEngine::setStripPanLive(const String& id, float pan)
{
    pan = lightHostModern::clampPan(pan);
    if (auto* strip = const_cast<lightHostModern::ChainStrip*>(instances.findStrip(id))) strip->pan = pan;
    hostProcessor.setStripPan(id, pan);
    surfaceGenerationValue.fetch_add(1);
}

void AudioEngine::setMasterGainLive(float gainDb)
{
    instances.masterGainDb = lightHostModern::clampGainDb(gainDb);
    hostProcessor.setMasterGain(lightHostModern::gainFromDb(instances.masterGainDb));
    surfaceGenerationValue.fetch_add(1);
}

String AudioEngine::startRecording(const File& folder, bool mp3, int bitrate, bool mixdown, bool multi, bool interleaved, bool raw,
                                    const String& icecastHost, int icecastPort, const String& mount, const String& user, const String& password, const String& streamName)
{
    stopRecording();
    if (!mixdown && !multi && icecastHost.isEmpty()) return "Nothing to record";
    auto* device = deviceManager.getCurrentAudioDevice();
    const double rate = device ? device->getCurrentSampleRate() : 48000.0;
    const int block = device ? device->getCurrentBufferSizeSamples() : 512;
    {
        RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
        mixCapture->prepare(juce::jmax(block, 512), (int) instances.strips.size());
        mixCapture->arm(mixdown || icecastHost.isNotEmpty(), multi, raw);
    }
    TakeRequest request;
    request.folder = folder;
    request.mp3 = mp3;
    request.bitrate = bitrate;
    request.mixdown = mixdown;
    request.multitrack = multi;
    request.interleaved = interleaved && !mp3;
    request.raw = raw;
    request.sampleRate = rate > 0 ? rate : 48000.0;
    for (const auto& strip : instances.strips)
    {
        request.names.push_back(strip.name);
        request.armed.push_back(strip.record);
    }
    request.icecastHost = icecastHost;
    request.icecastPort = icecastPort;
    request.icecastMount = mount;
    request.icecastUser = user;
    request.icecastPassword = password;
    request.icecastName = streamName.isEmpty() ? "LightHostModern" : streamName;
    if (const auto error = mixWriter->start(request); error.isNotEmpty())
    {
        mixCapture->disarm();
        return error;
    }
    return {};
}

void AudioEngine::stopRecording()
{
    if (mixWriter) mixWriter->stop();
    if (mixCapture) mixCapture->disarm();
}

void AudioEngine::pauseRecording() { if (mixCapture) mixCapture->setRecordPaused(true); }
void AudioEngine::resumeRecording() { if (mixCapture) mixCapture->setRecordPaused(false); }
bool AudioEngine::isRecordingPaused() const { return mixCapture && mixCapture->isRecordPaused(); }

bool AudioEngine::isRecording() const { return mixWriter && mixWriter->running(); }
String AudioEngine::recordingStatus() const { return mixWriter ? mixWriter->status() : String(); }

StringArray AudioEngine::streamDeviceNames() { return streamOutput ? streamOutput->deviceNames() : StringArray(); }
String AudioEngine::liveOutputName()
{
    if (auto* device = deviceManager.getCurrentAudioDevice()) return device->getName();
    return {};
}

String AudioEngine::startStream(const String& deviceName)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    const double rate = device ? device->getCurrentSampleRate() : 48000.0;
    if (!mixCapture->armed())
    {
        RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
        mixCapture->prepare(device ? device->getCurrentBufferSizeSamples() : 512, (int) instances.strips.size());
    }
    return streamOutput->start(*mixCapture, deviceName, liveOutputName(), rate > 0 ? rate : 48000.0);
}

void AudioEngine::stopStream() { if (streamOutput) streamOutput->stop(); }
void AudioEngine::pauseStream() { if (mixCapture) mixCapture->setStreamPaused(true); }
void AudioEngine::resumeStream() { if (mixCapture) mixCapture->setStreamPaused(false); }
bool AudioEngine::isStreaming() const { return streamOutput && streamOutput->running(); }
bool AudioEngine::isStreamPaused() const { return mixCapture && mixCapture->isStreamPaused(); }
StringArray AudioEngine::midiInputNames() const { return mackie ? mackie->inputNames() : StringArray(); }
String AudioEngine::openMackie(int deviceIndex) { return mackie ? mackie->open(deviceIndex) : "MIDI unavailable"; }
void AudioEngine::closeMackie() { if (mackie) mackie->close(); }

String AudioEngine::setMasterGain(float gainDb)
{
    if (!isSessionWritable()) return "session_read_only";
    gainDb = lightHostModern::clampGainDb(gainDb);
    if (instances.masterGainDb == gainDb) return {};
    chainHistory.record(instances, "master-gain");
    instances.masterGainDb = gainDb;
    hostProcessor.setMasterGain(lightHostModern::gainFromDb(gainDb));
    ++chainVersion;
    saveActivePluginChain(false);
    return {};
}

String AudioEngine::movePluginToStrip(const String& instanceId, const String& stripId, const String& beforeInstanceId)
{
    if (!isSessionWritable()) return "session_read_only";
    if (stripId != lightHostModern::masterStripId && !instances.findStrip(stripId)) return "strip_not_found";
    const int from = instances.indexOf(instanceId);
    if (from < 0) return "instance_not_found";
    chainHistory.record(instances);
    auto record = std::move(instances.records[(size_t) from]);
    instances.records.erase(instances.records.begin() + from);
    record.stripId = stripId;
    int before = beforeInstanceId.isEmpty() ? -1 : instances.indexOf(beforeInstanceId);
    if (before >= 0) instances.records.insert(instances.records.begin() + before, std::move(record));
    else instances.records.push_back(std::move(record));
    loadActivePlugins();
    saveActivePluginChain(false);
    return {};
}

bool AudioEngine::undoChain()
{
    if (!isSessionWritable()) return false;
    auto restored = chainHistory.undo(instances);
    if (!restored) return false;
    instances = std::move(*restored);
    loadActivePlugins();
    saveActivePluginChain(false);
    return true;
}

bool AudioEngine::redoChain()
{
    if (!isSessionWritable()) return false;
    auto restored = chainHistory.redo(instances);
    if (!restored) return false;
    instances = std::move(*restored);
    loadActivePlugins();
    saveActivePluginChain(false);
    return true;
}

void AudioEngine::syncEditorWindows(bool saveNow)
{
    if (!isSessionWritable()) return;
    bool changed = false;
    for (auto& record : instances.records)
    {
        auto* slot = findActiveSlotFor(record.id);
        if (!slot || !slot->processor) continue;
        const bool open = PluginWindow::isOpenFor(*slot->processor);
        const bool positioned = slot->windowProperties.contains("uiLastX_Normal");
        const int x = positioned ? (int) slot->windowProperties["uiLastX_Normal"] : record.editorX;
        const int y = positioned ? (int) slot->windowProperties["uiLastY_Normal"] : record.editorY;
        const bool sized = slot->windowProperties.contains("uiLastW_Normal") && slot->windowProperties.contains("uiLastH_Normal");
        const int width = sized ? (int) slot->windowProperties["uiLastW_Normal"] : record.editorW;
        const int height = sized ? (int) slot->windowProperties["uiLastH_Normal"] : record.editorH;
        const bool samePosition = !positioned || (record.hasEditorPosition && record.editorX == x && record.editorY == y);
        const bool sameSize = !sized || (record.hasEditorSize && record.editorW == width && record.editorH == height);
        if (record.editorOpen == open && samePosition && sameSize) continue;
        record.editorOpen = open;
        if (positioned) { record.hasEditorPosition = true; record.editorX = x; record.editorY = y; }
        if (sized) { record.hasEditorSize = true; record.editorW = width; record.editorH = height; }
        changed = true;
    }
    const double now = Time::getMillisecondCounterHiRes();
    if (!changed) { editorStableSince = 0; return; }
    if (saveNow || (editorStableSince != 0 && now - editorStableSince >= 1000.0)) { saveActivePluginList(); editorStableSince = 0; return; }
    if (editorStableSince == 0) editorStableSince = now;
}

void AudioEngine::showPluginEditor(int sortedIndex)
{
	const auto timeSorted = getActivePluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) timeSorted.size())
		return;

	if (auto* const slot = findActiveSlotFor(instances.records[(size_t) sortedIndex].id))
		if (slot->processor != nullptr)
			if (PluginWindow* const window = PluginWindow::getWindowFor(*slot->processor, slot->windowProperties, PluginWindow::Normal))
				window->toFront(true);
}

DiagnosticsSnapshot AudioEngine::getDiagnosticsSnapshot() const
{
	DiagnosticsSnapshot snapshot = deviceController.createDiagnosticsSnapshot(const_cast<GuardedAudioDeviceManager&>(deviceManager), isDiagnosticsEnabled());
	snapshot.activePlugins = static_cast<int>(instances.records.size());
	if (!isDiagnosticsEnabled()) return snapshot;
	const RealtimeHostStats realtimeStats = hostProcessor.getStats();
#if JUCE_WINDOWS
    const auto now = GetTickCount64();
    if (now <= lightHostModern::diagnosticsVisibleUntil.load() && now - lastMemorySample >= 1000)
    { cachedMemory = lightHostModern::processMemory(); lastMemorySample = now; }
    const auto memory = now <= lightHostModern::diagnosticsVisibleUntil.load() ? cachedMemory : lightHostModern::ProcessMemory{};
    if (memory.resident) snapshot.hostResidentMiB = *memory.resident / 1048576.0;
    if (memory.committed) snapshot.hostCommittedMiB = *memory.committed / 1048576.0;
    if (lightHostModern::workerMemoryUnavailable.load() == 0) {
        snapshot.workerResidentMiB = lightHostModern::workerResidentBytes.load() / 1048576.0;
        snapshot.workerCommittedMiB = lightHostModern::workerCommittedBytes.load() / 1048576.0;
    }
    snapshot.hostCpuPercent = hostCpuSampler.sample(lightHostModern::processCpuTicks(), GetTickCount64(), lightHostModern::processorCount());
    snapshot.workerCpuPercent = workerCpuSampler.sample(lightHostModern::workerCpuTicks.load(), GetTickCount64(), lightHostModern::processorCount());
#endif
	snapshot.activePlugins = static_cast<int>(instances.records.size());
	snapshot.loadedPlugins = realtimeStats.loadedSlots;
	snapshot.chainLatencySamples = realtimeStats.chainLatencySamples;
	snapshot.chainReloads = chainReloadCount;
	snapshot.bypassToggles = bypassToggleCount;
	snapshot.pluginStateSaves = pluginStateSaveCount;
	snapshot.settingsFlushes = settingsFlushCount;
	snapshot.processFailures = realtimeStats.processFailures;
	snapshot.midiOverflow = realtimeStats.midiOverflow;
	snapshot.processedBlocks = realtimeStats.processedBlocks;
	snapshot.processedSamples = realtimeStats.processedSamples;
	snapshot.inputMidiEvents = realtimeStats.inputMidiEvents;
	snapshot.outputMidiEvents = realtimeStats.outputMidiEvents;
	snapshot.inputMeters = hostProcessor.getInputMeters();
	snapshot.outputMeters = hostProcessor.getOutputMeters();
	snapshot.reusedSlots = realtimeStats.reusedSlots;
	snapshot.rebuiltSlots = realtimeStats.rebuiltSlots;
	snapshot.inputLevel = realtimeStats.inputLevel;
	snapshot.outputLevel = realtimeStats.outputLevel;
	return snapshot;
}

bool AudioEngine::configureCallbackMeasurement(unsigned warmupSeconds, unsigned durationSeconds)
{
    if (!isDiagnosticsEnabled() || !lightHostModern::RuntimeProfile::current().test || deviceManager.getCurrentAudioDevice()) return false;
    try
    {
        player.callbackMeasurement().configure(static_cast<uint64>(Time::getHighResolutionTicksPerSecond()), warmupSeconds, durationSeconds);
        return true;
    }
    catch (const std::invalid_argument&) { return false; }
}

void AudioEngine::timerCallback(int timerId)
{
	collectPluginScanResults();
	hostProcessor.collectRetiredSnapshots();
	hostProcessor.refreshLatencies();
	recordProcessFailures();

    if (timerId == audioWatchdogTimerId)
    {
        const auto now = Time::getMillisecondCounterHiRes();
        if (const auto snapshot = hostProcessor.getActiveSnapshot(); snapshot && isSessionWritable())
            for (const auto& slot : snapshot->slots)
                if (slot && slot->stateDirty.exchange(false, std::memory_order_relaxed)) stateCaptureDue = now + 1000.0;
        if (stateCaptureDue > 0 && now >= stateCaptureDue) savePluginStates();
        if (const auto status = getSessionSaveStatus(); status.changeSerial != lastSessionStatusSerial)
        { lastSessionStatusSerial = status.changeSerial; ++chainVersion; }
        deviceController.observeCallbacks(player.callbackCount());
        deviceController.tick();
        syncEditorWindows();
        startTimer(audioWatchdogTimerId, 250);
        return;
    }

	if (timerId == persistenceTimerId)
	{
		flushPendingSaves();
		return;
	}

	if (timerId == diagnosticsTimerId)
	{
		if (isDiagnosticsEnabled() || lightHostModern::verbose::logger().active()) logDiagnosticsSnapshot();
		return;
	}
}

void AudioEngine::changeListenerCallback(ChangeBroadcaster* changed)
{
	if (changed == &knownPluginList)
	{
		pluginDatabaseVersion++;
		std::unique_ptr<XmlElement> savedPluginList(knownPluginList.createXml());
		if (savedPluginList != nullptr)
		{
			getAppProperties().getUserSettings()->setValue("pluginList", savedPluginList.get());
			markSettingsDirty();
		}
	}
	else if (changed == &deviceManager) deviceController.devicesChanged();
}

void AudioEngine::markSettingsDirty()
{
	settingsDirty = true;
	pluginStateStore.markDirty();
	startTimer(persistenceTimerId, 1000);
}

PluginSlot* AudioEngine::findActiveSlotFor(const PluginInstanceId& id) const
{
    const auto snapshot = hostProcessor.getActiveSnapshot();
    if (snapshot) for (const auto& slot : snapshot->slots) if (slot && slot->instanceId == id) return slot.get();
    return nullptr;
}

void AudioEngine::recordProcessFailures()
{
	auto snapshot = hostProcessor.getActiveSnapshot();
	if (snapshot == nullptr)
		return;

	for (auto& slot : snapshot->slots)
	{
		if (slot == nullptr || !slot->processFailed.exchange(false, std::memory_order_acq_rel))
			continue;

		const String errorMessage = "Plugin threw while processing audio";
		Logger::writeToLog("LightHostModern: failed plugin disabled in audio chain '" + slot->description.name + "': " + errorMessage);
		const auto index = instances.indexOf(slot->instanceId);
        if (index >= 0)
        {
            auto& record = instances.records[static_cast<size_t>(index)];
            record.error = errorMessage;
            record.loading = "failed";
            ++chainVersion;
            saveActivePluginList();
        }
		markSettingsDirty();
	}
}

void AudioEngine::logDiagnosticsSnapshot()
{
	const DiagnosticsSnapshot snapshot = getDiagnosticsSnapshot();
	Logger::writeToLog("LightHostModern diagnostics: backend=" + snapshot.backend
		+ ", device=" + snapshot.deviceName
		+ ", cpu=" + String(snapshot.cpuUsagePercent, 2) + "%"
		+ ", xruns=" + String(snapshot.xRunCount)
		+ ", sampleRate=" + String(snapshot.sampleRate, 0)
		+ ", buffer=" + String(snapshot.bufferSize)
		+ ", inputLatency=" + String(snapshot.inputLatency)
		+ ", outputLatency=" + String(snapshot.outputLatency)
		+ ", inputChannels=" + String(snapshot.inputChannels)
		+ ", outputChannels=" + String(snapshot.outputChannels)
		+ ", inputLevel=" + String(snapshot.inputLevel, 3)
		+ ", outputLevel=" + String(snapshot.outputLevel, 3)
		+ ", activePlugins=" + String(snapshot.activePlugins)
		+ ", loadedPlugins=" + String(snapshot.loadedPlugins)
		+ ", chainLatency=" + String(snapshot.chainLatencySamples)
		+ ", chainReloads=" + String(snapshot.chainReloads)
		+ ", bypassToggles=" + String(snapshot.bypassToggles)
		+ ", pluginStateSaves=" + String(snapshot.pluginStateSaves)
		+ ", settingsFlushes=" + String(snapshot.settingsFlushes)
		+ ", processFailures=" + String(snapshot.processFailures)
		+ ", reusedSlots=" + String(snapshot.reusedSlots)
		+ ", rebuiltSlots=" + String(snapshot.rebuiltSlots));
}
