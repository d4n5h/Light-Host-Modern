#ifndef AudioEngine_h
#define AudioEngine_h

#include "RealtimeHostProcessor.h"
#include "GuardedAudioDeviceManager.h"
#include "DeviceController.h"
#include "PluginScanController.h"
#include "PluginInstances.h"
#include "KnownPluginNames.h"
#include "HostAudioPlayer.h"
#include "ProcessMetrics.h"
#include "SessionStore.h"
#include "ChainProfileStore.h"
#include "TemplateStore.h"
#include "ChainHistory.h"
#include "MixCapture.h"

class MixWriter;
class StreamOutput;
class MackieSurface;

ApplicationProperties& getAppProperties();

class PluginStateStore
{
public:
	static String getKey(String type, const PluginDescription& plugin);
	static String getLegacyKey(String type, const PluginDescription& plugin);

	String getValue(String type, const PluginDescription& plugin, const String& defaultValue = String()) const;
	void setValue(String type, const PluginDescription& plugin, const var& value);
	void removeValue(String type, const PluginDescription& plugin);
	void markDirty();
	void flushIfDirty();
	bool isDirty() const noexcept { return dirty; }

private:
	bool dirty = false;
};

class AudioEngine : private ChangeListener, private MultiTimer
{
public:
	explicit AudioEngine(bool startInSafeMode, bool restoreActivePluginsOnStartup);
	~AudioEngine() override;

	AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
	AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
	KnownPluginList& getKnownPluginList() noexcept { return knownPluginList; }

	std::vector<PluginDescription> getActivePluginsSorted() const;
	const std::vector<lightHostModern::PluginInstanceRecord>& getPluginInstances() const { return instances.records; }
	String getSessionRecoveryError() const { return instances.recoveryError; }
	lightHostModern::SessionSaveStatus getSessionSaveStatus() const { return sessionStore ? sessionStore->status() : lightHostModern::SessionSaveStatus{}; }
	const StringArray& getStateCaptureFailures() const { return stateCaptureFailures; }
	bool isSessionWritable() const { return instances.writable && !sessionLoadSuppressed; }
	bool flushSession();
	lightHostModern::ChainProfileCatalog chainProfileCatalog() const { return chainProfiles ? chainProfiles->catalog() : lightHostModern::ChainProfileCatalog{}; }
	uint64 getProfileVersion() const noexcept { return profileVersion; }
	String createChainProfile(const String& name);
	String switchChainProfile(const String& id);
	String renameChainProfile(const String& id, const String& name);
	String duplicateChainProfile(const String& id);
	String deleteChainProfile(const String& id);
	String moveChainProfile(const String& id, int delta);
	std::vector<lightHostModern::TemplateEntry> listTemplates() const { return templates ? templates->list() : std::vector<lightHostModern::TemplateEntry>{}; }
	String activeTemplateId() const { return getAppProperties().getUserSettings()->getValue("activeTemplate"); }
	String createTemplate(const String& name);
	String updateTemplate(const String& id);
	String recallTemplate(const String& id);
	String renameTemplate(const String& id, const String& name);
	String deleteTemplate(const String& id);
	String exportTemplate(const String& id, const File& file);
	String importTemplate(const File& file);
	int findKnownPluginIndexById(const String& id) const;
	int findPluginIndexById(const PluginInstanceId& id) const;
	void setGlobalMuted(bool value) { if (hostProcessor.setGlobalMuted(value)) ++chainVersion; }
	void setGlobalBypassed(bool value) { if (hostProcessor.setGlobalBypassed(value)) ++chainVersion; }
	bool isMonoInputs() const { return hostProcessor.isMonoInputs(); }
    void setMonoInputs(bool value);
    bool isMonoOutput() const { return hostProcessor.isMonoOutput(); }
    void setMonoOutput(bool value);
    bool isGlobalMuted() const { return hostProcessor.isGlobalMuted(); }
	bool isGlobalBypassed() const { return hostProcessor.isGlobalBypassed(); }
	void resetClipping(bool input, bool output, int channel = -1)
	{ hostProcessor.resetClipping(input, output, channel); }
	std::vector<PluginDescription> getKnownPluginsSorted() const;
	AudioDeviceConfiguration getAudioDeviceConfiguration();
	AudioRecoveryConfiguration getAudioRecoveryConfiguration() const;
	AudioBlocklistConfiguration getAudioBlocklistConfiguration() const;
	AvailableAudioChoicesConfiguration getAvailableAudioChoicesConfiguration();
	bool isVst2FormatActive() const;
	bool isPluginBypassed(int sortedIndex) const;
	bool isKnownPluginMenuId(int menuId) const;
	void addKnownPluginsToMenu(PopupMenu& menu) const;

	bool setAudioBackendByIndex(int backendIndex);
    bool selectAudioDevice(const AudioDeviceSelection& selection) { return deviceController.selectConfiguration(selection); }
    bool setPreferredAudioDevice(const String& backend, const String& input, const String& output, uint64 generation)
    { return deviceController.setPreferredDevice(backend, input, output, generation); }
    var getAudioSelectionState() const { return deviceController.selectionState(); }
    var getAudioDeviceOptions(const String& backend) { return deviceController.optionsForBackend(backend); }
	bool setAudioInputDeviceByIndex(int deviceIndex);
	bool setAudioOutputDeviceByIndex(int deviceIndex);
	String getLastAudioConfigurationError() const { return deviceController.getLastAudioConfigurationError(); }
	bool setAudioSampleRate(double sampleRate);
	bool setAudioBufferSize(int bufferSize);
	bool setAudioInputChannelCount(int channelCount);
	bool setAudioOutputChannelCount(int channelCount);
	bool setAudioInputChannelEnabled(int channelIndex, bool enabled);
	bool setAudioOutputChannelEnabled(int channelIndex, bool enabled);
	bool setAllAudioInputChannelsEnabled(bool enabled);
	bool setAllAudioOutputChannelsEnabled(bool enabled);
	bool setAudioPersistenceMode(const String& mode);
	bool setAudioPersistenceRetrySeconds(int seconds);
	bool setAudioPersistenceRetryAttempts(int attempts);
	bool setAudioPersistenceCustomBackendByIndex(int backendIndex);
	bool setAudioPersistenceCustomInputByIndex(int deviceIndex);
	bool setAudioPersistenceCustomOutputByIndex(int deviceIndex);
	bool retryPreferredAudioDeviceNow();
	bool addBlockedAudioBackend(const String& backendName);
	bool addBlockedAudioInputDevice(const String& deviceName);
	bool addBlockedAudioOutputDevice(const String& deviceName);
	bool removeBlockedAudioBackend(int index);
	bool removeBlockedAudioDevice(int index);
	bool setAudioBackendEnabledByIndex(int index, bool enabled);
	bool setAudioDeviceChoiceEnabledByIndex(int index, bool enabled);

	void scanDefaultPluginLocations(bool scanVst, bool scanVst3);
	void scanPluginPath(const String& path, bool scanVst, bool scanVst3);
    void scanPluginRoots(const var& roots);
	PluginScanController::Status getPluginScanStatus() const { return pluginScanner.status(); }
	std::pair<String, uint64_t> getPluginScanVersion() const { return pluginScanner.version(); }
	void cancelPluginScan();
	bool beginPluginScan() { collectPluginScanResults(); return pluginScanner.begin(); }
	void retryPluginScanFailures() { pluginScanner.retryFailures(); }
    bool retryPluginScanFailures(const StringArray& ids) { return pluginScanner.retryFailures(ids); }
    PluginScanController::FailurePage getPluginScanFailures(const String& scanId, uint64 revision, size_t offset, size_t limit) const
    { return pluginScanner.failures(scanId, revision, offset, limit); }
    String getPluginMetadata(const String& id) const { return pluginScanner.metadata(id); }
	void collectPluginScanResults();

	void addPluginFromMenuId(int menuId);
	bool addKnownPluginByIndex(int sortedIndex);
	String addKnownPluginAt(int sortedIndex, const String& stripId, const String& beforeInstanceId);
	String addStrip(const String& name);
	String removeStrip(const String& id);
	String renameStrip(const String& id, const String& name);
	String setStripRouting(const String& id, bool allInputs, bool allOutputs, const std::vector<int>& inputs, const std::vector<int>& outputs);
	String setStripWidth(const String& id, bool stereo);
	String setStripGain(const String& id, float gainDb);
	String setStripPan(const String& id, float pan);
	String setStripColor(const String& id, int color);
	String setStripColour(const String& id, const String& hex);
	String setStripMuted(const String& id, bool muted);
	String setStripSolo(const String& id, bool solo);
	String setStripRecord(const String& id, bool record);
	String setStripGroup(const String& id, const String& group);
	String orderStrips(const std::vector<std::pair<String, String>>& order);
	float getStripLevel(const String& id) const { return hostProcessor.getStripLevel(id); }
	float getMasterLevel() const { return hostProcessor.getMasterLevel(); }
	String setMasterGain(float gainDb);
	void setStripGainLive(const String& id, float gainDb);
	void setStripPanLive(const String& id, float pan);
	void setMasterGainLive(float gainDb);
	uint64 surfaceGeneration() const noexcept { return surfaceGenerationValue.load(); }
	juce::String startRecording(const juce::File& folder, bool mp3, int bitrate, bool mixdown, bool multi, bool interleaved, bool raw,
	                            const juce::String& icecastHost, int icecastPort, const juce::String& mount, const juce::String& user, const juce::String& password, const juce::String& streamName);
	void stopRecording();
	void pauseRecording();
	void resumeRecording();
	bool isRecording() const;
	bool isRecordingPaused() const;
	juce::String recordingStatus() const;
	juce::StringArray streamDeviceNames();
	juce::String liveOutputName();
	juce::String startStream(const juce::String& deviceName);
	void stopStream();
	void pauseStream();
	void resumeStream();
	bool isStreaming() const;
	bool isStreamPaused() const;
	juce::StringArray midiInputNames() const;
	juce::String openMackie(int deviceIndex);
	void closeMackie();
	void setStreamPassword(const juce::String& value) { streamPasswordValue = value; }
	juce::String streamPassword() const { return streamPasswordValue; }
	String movePluginToStrip(const String& instanceId, const String& stripId, const String& beforeInstanceId);
	bool undoChain();
	bool redoChain();
	bool canUndoChain() const { return chainHistory.canUndo(); }
	bool canRedoChain() const { return chainHistory.canRedo(); }
	float masterGainDb() const { return instances.masterGainDb; }
	const std::vector<lightHostModern::ChainStrip>& chainStrips() const { return instances.strips; }
	String stripsJson() const
	{
		StringArray items;
		for (const auto& strip : instances.strips)
		{
			const auto list = [](const std::vector<int>& values) {
				String text = "[";
				for (size_t i = 0; i < values.size(); ++i) text += (i ? "," : "") + String(values[i]);
				return text + "]";
			};
			items.add("{\"id\":" + JSON::toString(var(strip.id), true)
				+ ",\"name\":" + JSON::toString(var(strip.name), true)
				+ ",\"allInputs\":" + String(strip.allInputs ? "true" : "false")
				+ ",\"allOutputs\":" + String(strip.allOutputs ? "true" : "false")
				+ ",\"stereo\":" + String(strip.stereo ? "true" : "false")
				+ ",\"gainDb\":" + String(strip.gainDb, 2)
				+ ",\"pan\":" + String(strip.pan, 3)
				+ ",\"color\":" + String(strip.color)
				+ ",\"colour\":" + JSON::toString(var(strip.colour), true)
				+ ",\"muted\":" + String(strip.muted ? "true" : "false")
				+ ",\"solo\":" + String(strip.solo ? "true" : "false")
				+ ",\"group\":" + JSON::toString(var(strip.group), true)
				+ ",\"inputs\":" + list(strip.inputs)
				+ ",\"outputs\":" + list(strip.outputs) + "}");
		}
		return "[" + items.joinIntoString(",") + "]";
	}
	void duplicatePlugin(int sortedIndex);
	int removeKnownPluginByIndex(int sortedIndex);
	int clearKnownPlugins();
	void openKnownPluginLocation(int sortedIndex) const;
	void removePlugin(int sortedIndex);
	void movePluginUp(int sortedIndex);
	void movePluginDown(int sortedIndex);
	void movePluginToIndex(int fromSortedIndex, int toSortedIndex, bool recordHistory = true, bool adoptStrip = true);
	void setPluginBypassed(int sortedIndex, bool shouldBypass);
	bool renamePlugin(int sortedIndex, const String& name);
	bool renameKnownPlugin(int sortedIndex, const String& name);
	String getKnownPluginCustomName(const PluginDescription& plugin) const
	{ return lightHostModern::knownPluginCustomName(*getAppProperties().getUserSettings(), plugin); }
	bool isDiagnosticsEnabled() const { return lightHostModern::diagnosticsCollectionEnabled.load(); }
	void setDiagnosticsEnabled(bool enabled);
	void deletePluginStates();
	void savePluginStates();
	void saveAudioDeviceState();
	void flushPendingSaves();
	void removePluginsLackingInputOutput();
	void removeMissingKnownPlugins();
	void loadActivePlugins();
	void showPluginEditor(int sortedIndex);

	DiagnosticsSnapshot getDiagnosticsSnapshot() const;
	std::pair<float, float> getMeterPeaks() const noexcept { return hostProcessor.getMeterPeaks(); }
    bool configureCallbackMeasurement(unsigned warmupSeconds, unsigned durationSeconds);
    lightHostModern::CallbackMeasurement::Snapshot getCallbackMeasurement() const { return player.callbackMeasurement().snapshot(); }
	uint64 getChainVersion() const noexcept { return chainVersion; }
	uint64 getPluginDatabaseVersion() const noexcept { return pluginDatabaseVersion; }
	uint64 getAudioConfigVersion() const noexcept { return deviceController.getVersion(); }

private:
	lightHostModern::TemplateSnapshot captureTemplate();
	enum TimerIds
	{
		audioWatchdogTimerId = 1,
		persistenceTimerId = 2,
		diagnosticsTimerId = 3
	};

	void dropInactiveStripInputs();
	void timerCallback(int timerId) override;
	void changeListenerCallback(ChangeBroadcaster* changed) override;
	void markSettingsDirty();
	PluginSlot* findActiveSlotFor(const PluginInstanceId& id) const;
	void recordProcessFailures();
	void logDiagnosticsSnapshot();
	std::unique_ptr<XmlElement> getXmlValuePreserving(const String& key);
	void saveActivePluginList();
	void syncEditorWindows(bool saveNow = false);
	String prepareChainProfileChange();
	void saveActivePluginChain(bool saveProcessorStates);
	void saveCurrentAudioChannelState();
	void applySavedAudioChannelState(AudioDeviceManager::AudioDeviceSetup& setup,
	                                 const String& backendName,
	                                 const String& inputDeviceName,
	                                 const String& outputDeviceName);
	void rememberLastSelectedAudioDevice();
	void rememberManualSelectedAudioDevice();
	bool applyPreferredAudioDevice(AudioRecoveryConfiguration const& recoveryConfig, bool manualRetry);
	bool isAudioBackendBlocked(const String& backendName) const;
	bool isAudioDeviceBlocked(const String& backendName, const String& role, const String& deviceName) const;
	bool isAudioDeviceChoiceAllowed(const String& backendName,
	                                const String& inputDeviceName,
	                                const String& outputDeviceName) const;
	bool currentAudioDeviceMatchesPreferred(AudioRecoveryConfiguration const& recoveryConfig) const;
	void closeCurrentAudioDeviceIfBlocked(const String& context);

	bool safeMode = false;
	bool restoreActivePluginsOnStartup = false;
	bool settingsDirty = false;
	uint64 chainReloadCount = 0;
	uint64 bypassToggleCount = 0;
	uint64 settingsFlushCount = 0;
	uint64 pluginStateSaveCount = 0;
	uint64 chainVersion = 0;
	std::atomic<uint64> surfaceGenerationValue { 0 };
	uint64 profileVersion = 0;
	uint64 pluginDatabaseVersion = 0;

	PluginStateStore pluginStateStore;
	PluginScanController pluginScanner;
	GuardedAudioDeviceManager deviceManager;
	DeviceController deviceController;
	AudioPluginFormatManager formatManager;
	KnownPluginList knownPluginList;
	lightHostModern::PluginInstances instances;
	lightHostModern::ChainHistory chainHistory;
	double editorStableSince = 0;
	std::unique_ptr<lightHostModern::SessionStore> sessionStore;
	std::unique_ptr<lightHostModern::ChainProfileStore> chainProfiles;
	std::unique_ptr<lightHostModern::TemplateStore> templates;
	String sessionMigrationId;
	StringArray stateCaptureFailures;
	uint64 lastSessionStatusSerial = 0;
	double stateCaptureDue = 0;
	bool sessionLoadSuppressed = false;
	KnownPluginList::SortMethod pluginSortMethod = KnownPluginList::sortByManufacturer;
	juce::String streamPasswordValue;
	std::unique_ptr<MixCapture> mixCapture;
	std::unique_ptr<MixWriter> mixWriter;
	std::unique_ptr<StreamOutput> streamOutput;
	std::unique_ptr<MackieSurface> mackie;
	RealtimeHostProcessor hostProcessor;
	HostAudioPlayer player;
	mutable lightHostModern::ProcessMemory cachedMemory;
    mutable uint64_t lastMemorySample = 0;
    mutable lightHostModern::CpuUsageSampler hostCpuSampler, workerCpuSampler;
};

#endif /* AudioEngine_h */
