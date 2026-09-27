#include "HostWindow.h"
#include "MixerView.h"
#include "RuntimeProfile.h"
#include "ProductIdentity.h"
#include <array>

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <Windows.h>

namespace
{
class PluginDragList : public juce::ListBox
{
public:
    std::function<juce::var(int)> dragForRow;
    void mouseDrag(const juce::MouseEvent& event) override
    {
        const int row = getSelectedRow();
        if (dragForRow && row >= 0 && event.getDistanceFromDragStart() > 8)
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
                container->startDragging(dragForRow(row), this);
    }
};

class InstalledPage : public juce::Component
{
public:
    explicit InstalledPage(AudioEngine& engineIn) : engine(engineIn)
    {
        for (auto* button : { &scan, &scanFolders, &addFolders, &removeFolder, &removeMissing, &clear, &remove })
            addAndMakeVisible(button);
        addAndMakeVisible(search);
        addAndMakeVisible(format);
        addAndMakeVisible(manufacturer);
        addAndMakeVisible(type);
        addAndMakeVisible(status);
        addAndMakeVisible(folders);
        addAndMakeVisible(list);
        scan.onClick = [this] { engine.scanDefaultPluginLocations(true, true); };
        scanFolders.onClick = [this] {
            const auto paths = folderModel.folders.joinIntoString(";");
            if (paths.isNotEmpty()) engine.scanPluginPath(paths, true, true);
        };
        addFolders.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Add plugin folders", juce::File{}, "*", true);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                [this](const juce::FileChooser& chosen) {
                    for (const auto& file : chosen.getResults())
                        if (file.isDirectory()) folderModel.folders.addIfNotAlreadyThere(file.getFullPathName());
                    saveFolders();
                    refresh();
                });
        };
        removeFolder.onClick = [this] {
            const int row = folders.getSelectedRow();
            if (row < 0 || row >= folderModel.folders.size()) return;
            folderModel.folders.remove(row);
            saveFolders();
            refresh();
        };
        removeMissing.onClick = [this] { engine.removeMissingKnownPlugins(); };
        clear.onClick = [this] { engine.clearKnownPlugins(); };
        remove.onClick = [this] { removeRow(list.getSelectedRow()); };
        scan.setButtonText("Scan default");
        scanFolders.setButtonText("Scan folders");
        addFolders.setButtonText("Add folders");
        removeFolder.setButtonText("Remove folder");
        removeMissing.setButtonText("Remove missing");
        clear.setButtonText("Clear database");
        remove.setButtonText("Remove");
        search.setTextToShowWhenEmpty("Search", juce::Colour(0xffb8b8b8));
        format.addItem("All formats", 1);
        format.addItem("VST3", 2);
        format.addItem("VST", 3);
        format.setSelectedId(1, juce::dontSendNotification);
        const char* types[] = { "All", "Dynamics", "EQ", "Reverb", "Delay", "Modulation", "Distortion", "Filter", "Pitch", "Spatial", "Instrument", "Analyzer", "Restoration", "Tools", "Other" };
        for (int i = 0; i < 15; ++i) type.addItem(types[i], i + 1);
        type.setSelectedId(1, juce::dontSendNotification);
        manufacturer.addItem("All manufacturers", 1);
        manufacturer.setSelectedId(1, juce::dontSendNotification);
        folders.setModel(&folderModel);
        folders.setRowHeight(22);
        list.setModel(&model);
        list.setRowHeight(28);
        list.dragForRow = [this](int row) {
            if (row < 0 || row >= (int) model.plugins.size()) return juce::var();
            return juce::var("known:" + lightHostModern::knownPluginId(model.plugins[(size_t) row]));
        };
        model.onPopup = [this](int row) { removeRow(row); };
        auto refill = [this] { applyFilter(); };
        search.onTextChange = refill;
        format.onChange = refill;
        manufacturer.onChange = refill;
        type.onChange = refill;
    }

    void refresh()
    {
        const auto scanStatus = engine.getPluginScanStatus();
        known = engine.getKnownPluginsSorted();
        folderModel.folders = juce::StringArray::fromLines(getAppProperties().getUserSettings()->getValue("pluginScanFolders"));
        folderModel.folders.removeEmptyStrings();
        folders.updateContent();
        status.setText(scanStatus.active ? "Scanning " + scanStatus.currentFile : juce::String((int) known.size()) + " plugins",
            juce::dontSendNotification);
        const auto selected = manufacturer.getText();
        manufacturer.clear(juce::dontSendNotification);
        manufacturer.addItem("All manufacturers", 1);
        juce::StringArray names;
        for (const auto& plugin : known)
            if (plugin.manufacturerName.isNotEmpty()) names.addIfNotAlreadyThere(plugin.manufacturerName);
        names.sort(true);
        for (int i = 0; i < names.size(); ++i) manufacturer.addItem(names[i], i + 2);
        manufacturer.setText(selected.isEmpty() ? "All manufacturers" : selected, juce::dontSendNotification);
        if (manufacturer.getSelectedId() == 0) manufacturer.setSelectedId(1, juce::dontSendNotification);
        applyFilter();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        auto bar = area.removeFromTop(32);
        scan.setBounds(bar.removeFromLeft(120));
        scanFolders.setBounds(bar.removeFromLeft(120));
        addFolders.setBounds(bar.removeFromLeft(110));
        removeFolder.setBounds(bar.removeFromLeft(120));
        auto bar2 = area.removeFromTop(32);
        removeMissing.setBounds(bar2.removeFromLeft(140));
        clear.setBounds(bar2.removeFromLeft(140));
        remove.setBounds(bar2.removeFromLeft(100));
        status.setBounds(area.removeFromTop(22));
        auto filters = area.removeFromTop(28);
        search.setBounds(filters.removeFromLeft(200));
        filters.removeFromLeft(8);
        format.setBounds(filters.removeFromLeft(110));
        filters.removeFromLeft(8);
        type.setBounds(filters.removeFromLeft(140));
        filters.removeFromLeft(8);
        manufacturer.setBounds(filters.removeFromLeft(200));
        area.removeFromTop(8);
        folders.setBounds(area.removeFromTop(72));
        area.removeFromTop(8);
        list.setBounds(area);
    }

private:
    struct FolderModel : juce::ListBoxModel
    {
        juce::StringArray folders;
        int getNumRows() override { return folders.size(); }
        void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
        {
            if (row < 0 || row >= folders.size()) return;
            graphics.fillAll(selected ? juce::Colour(0xff3d5a80) : juce::Colour(0xff2c2c2c));
            graphics.setColour(juce::Colour(0xfff2f2f2));
            graphics.drawText(folders[row], 8, 0, width - 8, height, juce::Justification::centredLeft, true);
        }
    };

    void saveFolders()
    {
        getAppProperties().getUserSettings()->setValue("pluginScanFolders", folderModel.folders.joinIntoString("\n"));
        getAppProperties().saveIfNeeded();
    }

    void applyFilter()
    {
        const auto query = search.getText();
        const auto chosenFormat = format.getSelectedId() <= 1 ? juce::String() : format.getText();
        const auto chosenMaker = manufacturer.getSelectedId() <= 1 ? juce::String() : manufacturer.getText();
        const auto chosenType = type.getSelectedId() <= 1 ? juce::String() : type.getText();
        model.plugins.clear();
        for (const auto& plugin : known)
            if (pluginMatches(plugin, query, chosenFormat, chosenMaker, chosenType)) model.plugins.push_back(plugin);
        list.updateContent();
        list.repaint();
    }

    void removeRow(int row)
    {
        if (row < 0 || row >= (int) model.plugins.size()) return;
        engine.removeKnownPluginByIndex(engine.findKnownPluginIndexById(lightHostModern::knownPluginId(model.plugins[(size_t) row])));
    }

    AudioEngine& engine;
    juce::TextButton scan, scanFolders, addFolders, removeFolder, removeMissing, clear, remove;
    juce::TextEditor search;
    juce::ComboBox format, manufacturer, type;
    juce::Label status;
    FolderModel folderModel;
    juce::ListBox folders;
    PluginListModel model;
    PluginDragList list;
    std::vector<juce::PluginDescription> known;
    std::unique_ptr<juce::FileChooser> chooser;
};

class AudioPage : public juce::Component
{
public:
    explicit AudioPage(AudioEngine& engineIn) : engine(engineIn)
    {
        for (auto* box : { &backend, &input, &output, &rate, &buffer, &persistence }) addAndMakeVisible(box);
        for (auto* label : { &deviceHeading, &formatHeading, &channelHeading, &driverLabel, &inputLabel, &outputLabel, &rateLabel, &bufferLabel, &recoveryLabel, &inputsHeading, &outputsHeading })
        {
            label->setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(label);
        }
        addAndMakeVisible(monoIn);
        addAndMakeVisible(monoOut);
        addAndMakeVisible(inputViewport);
        addAndMakeVisible(outputViewport);
        inputViewport.setViewedComponent(&inputList, false);
        outputViewport.setViewedComponent(&outputList, false);
        deviceHeading.setText("Device", juce::dontSendNotification);
        formatHeading.setText("Format", juce::dontSendNotification);
        channelHeading.setText("Channels", juce::dontSendNotification);
        driverLabel.setText("Driver", juce::dontSendNotification);
        inputLabel.setText("Input device", juce::dontSendNotification);
        outputLabel.setText("Output device", juce::dontSendNotification);
        rateLabel.setText("Sample rate", juce::dontSendNotification);
        bufferLabel.setText("Buffer", juce::dontSendNotification);
        recoveryLabel.setText("Device recovery", juce::dontSendNotification);
        inputsHeading.setText("Inputs", juce::dontSendNotification);
        outputsHeading.setText("Outputs", juce::dontSendNotification);
        monoIn.setButtonText("Mix inputs to mono");
        monoOut.setButtonText("Main output to mono");
        auto guard = [this](auto&& action) {
            return [this, action] {
                if (applying) return;
                action();
            };
        };
        backend.onChange = guard([this] { engine.setAudioBackendByIndex(backend.getSelectedId() - 1); });
        input.onChange = guard([this] { engine.setAudioInputDeviceByIndex(input.getSelectedId() - 1); });
        output.onChange = guard([this] { engine.setAudioOutputDeviceByIndex(output.getSelectedId() - 1); });
        rate.onChange = guard([this] {
            const auto rates = engine.getAudioDeviceConfiguration().sampleRates;
            const auto index = rate.getSelectedId() - 1;
            if (index >= 0 && index < (int) rates.size()) engine.setAudioSampleRate(rates[(size_t) index]);
        });
        buffer.onChange = guard([this] {
            const auto sizes = engine.getAudioDeviceConfiguration().bufferSizes;
            const auto index = buffer.getSelectedId() - 1;
            if (index >= 0 && index < (int) sizes.size()) engine.setAudioBufferSize(sizes[(size_t) index]);
        });
        persistence.onChange = guard([this] {
            const char* modes[] = { "disabled", "last", "custom" };
            const auto index = persistence.getSelectedId() - 1;
            if (index >= 0 && index < 3) engine.setAudioPersistenceMode(modes[index]);
        });
        monoIn.onClick = guard([this] { engine.setMonoInputs(monoIn.getToggleState()); });
        monoOut.onClick = guard([this] { engine.setMonoOutput(monoOut.getToggleState()); });
        monoIn.setClickingTogglesState(true);
        monoOut.setClickingTogglesState(true);
    }

    void refresh()
    {
        const auto config = engine.getAudioDeviceConfiguration();
        const auto recovery = engine.getAudioRecoveryConfiguration();
        applying = true;
        asioMode = config.currentBackendIndex >= 0 && config.currentBackendIndex < (int) config.backendNames.size()
            && config.backendNames[(size_t) config.currentBackendIndex].equalsIgnoreCase("ASIO");
        inputLabel.setText(asioMode ? "Device" : "Input device", juce::dontSendNotification);
        auto fill = [](juce::ComboBox& box, const std::vector<juce::String>& names, int selected) {
            box.clear(juce::dontSendNotification);
            for (int i = 0; i < (int) names.size(); ++i) box.addItem(names[(size_t) i], i + 1);
            if (selected >= 0) box.setSelectedId(selected + 1, juce::dontSendNotification);
        };
        fill(backend, config.backendNames, config.currentBackendIndex);
        fill(input, config.inputDeviceNames, config.currentInputDeviceIndex);
        fill(output, config.outputDeviceNames, config.currentOutputDeviceIndex);
        rate.clear(juce::dontSendNotification);
        buffer.clear(juce::dontSendNotification);
        const auto live = engine.getDiagnosticsSnapshot();
        for (int i = 0; i < (int) config.sampleRates.size(); ++i)
        {
            rate.addItem(juce::String(config.sampleRates[(size_t) i], 0) + " Hz", i + 1);
            if (std::abs(config.sampleRates[(size_t) i] - live.sampleRate) < 1.0) rate.setSelectedId(i + 1, juce::dontSendNotification);
        }
        for (int i = 0; i < (int) config.bufferSizes.size(); ++i)
        {
            buffer.addItem(juce::String(config.bufferSizes[(size_t) i]), i + 1);
            if (config.bufferSizes[(size_t) i] == live.bufferSize) buffer.setSelectedId(i + 1, juce::dontSendNotification);
        }
        persistence.clear(juce::dontSendNotification);
        persistence.addItem("Disabled", 1);
        persistence.addItem("Last device", 2);
        persistence.addItem("Custom", 3);
        persistence.setSelectedId(recovery.mode == "custom" ? 3 : recovery.mode == "last" ? 2 : 1, juce::dontSendNotification);
        monoIn.setToggleState(engine.isMonoInputs(), juce::dontSendNotification);
        monoOut.setToggleState(engine.isMonoOutput(), juce::dontSendNotification);
        inputList.removeAllChildren();
        outputList.removeAllChildren();
        inputToggles.clear();
        outputToggles.clear();
        const auto addChannels = [this](juce::Component& parent, juce::OwnedArray<juce::ToggleButton>& store, const std::vector<juce::String>& names, const std::vector<bool>& active, bool inputSide) {
            for (int i = 0; i < (int) names.size() && i < 192; ++i)
            {
                auto* toggle = store.add(new juce::ToggleButton(names[(size_t) i]));
                toggle->setToggleState(i < (int) active.size() && active[(size_t) i], juce::dontSendNotification);
                toggle->onClick = [this, i, inputSide, toggle] {
                    if (!applying) (inputSide ? engine.setAudioInputChannelEnabled(i, toggle->getToggleState())
                                              : engine.setAudioOutputChannelEnabled(i, toggle->getToggleState()));
                };
                parent.addAndMakeVisible(toggle);
            }
        };
        addChannels(inputList, inputToggles, config.inputChannelNames, config.activeInputChannels, true);
        addChannels(outputList, outputToggles, config.outputChannelNames, config.activeOutputChannels, false);
        applying = false;
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        auto place = [&](juce::Label& caption, juce::Component& box, bool show) {
            caption.setVisible(show);
            box.setVisible(show);
            if (!show) return;
            caption.setBounds(area.removeFromTop(18).removeFromLeft(420));
            box.setBounds(area.removeFromTop(28).removeFromLeft(420));
            area.removeFromTop(8);
        };
        deviceHeading.setBounds(area.removeFromTop(26));
        place(driverLabel, backend, true);
        place(inputLabel, input, true);
        place(outputLabel, output, !asioMode);
        area.removeFromTop(6);
        formatHeading.setBounds(area.removeFromTop(26));
        place(rateLabel, rate, true);
        place(bufferLabel, buffer, true);
        place(recoveryLabel, persistence, true);
        area.removeFromTop(6);
        channelHeading.setBounds(area.removeFromTop(26));
        auto columns = area;
        auto left = columns.removeFromLeft(columns.getWidth() / 2).reduced(0, 4);
        auto right = columns.reduced(12, 4);
        inputsHeading.setBounds(left.removeFromTop(24));
        outputsHeading.setBounds(right.removeFromTop(24));
        monoIn.setBounds(left.removeFromTop(28));
        monoOut.setBounds(right.removeFromTop(28));
        inputViewport.setBounds(left);
        outputViewport.setBounds(right);
        inputList.setSize(left.getWidth(), juce::jmax(left.getHeight(), inputToggles.size() * 28));
        outputList.setSize(right.getWidth(), juce::jmax(right.getHeight(), outputToggles.size() * 28));
        for (int i = 0; i < inputToggles.size(); ++i) inputToggles[i]->setBounds(0, i * 28, inputList.getWidth() - 8, 26);
        for (int i = 0; i < outputToggles.size(); ++i) outputToggles[i]->setBounds(0, i * 28, outputList.getWidth() - 8, 26);
    }

private:
    AudioEngine& engine;
    bool applying = false;
    bool asioMode = false;
    juce::Label deviceHeading, formatHeading, channelHeading;
    juce::Label driverLabel, inputLabel, outputLabel, rateLabel, bufferLabel, recoveryLabel, inputsHeading, outputsHeading;
    juce::ComboBox backend, input, output, rate, buffer, persistence;
    juce::ToggleButton monoIn, monoOut;
    juce::Viewport inputViewport, outputViewport;
    juce::Component inputList, outputList;
    juce::OwnedArray<juce::ToggleButton> inputToggles, outputToggles;
};

class DashboardPage : public juce::Component
{
public:
    explicit DashboardPage(AudioEngine& engineIn) : engine(engineIn)
    {
        for (auto* label : { &device, &format, &recovery, &levels, &plugins })
        {
            label->setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(label);
        }
    }
    void refresh()
    {
        const auto snapshot = engine.getDiagnosticsSnapshot();
        const auto peaks = engine.getMeterPeaks();
        device.setText(snapshot.backend + "  " + snapshot.deviceName, juce::dontSendNotification);
        format.setText(juce::String(snapshot.sampleRate, 0) + " Hz / " + juce::String(snapshot.bufferSize) + " samples", juce::dontSendNotification);
        recovery.setText(snapshot.recoveryMessage.isNotEmpty() ? snapshot.recoveryMessage : snapshot.recoveryState, juce::dontSendNotification);
        levels.setText("In " + juce::String(peaks.first, 2) + "    Out " + juce::String(peaks.second, 2), juce::dontSendNotification);
        plugins.setText(juce::String(snapshot.activePlugins) + " running plugins", juce::dontSendNotification);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        for (auto* label : { &device, &format, &recovery, &levels, &plugins })
            label->setBounds(area.removeFromTop(32));
    }
private:
    AudioEngine& engine;
    juce::Label device, format, recovery, levels, plugins;
};

class SettingsPage : public juce::Component
{
public:
    SettingsPage(AudioEngine& engineIn, std::function<void()> refreshTrayIn)
        : engine(engineIn), refreshTray(std::move(refreshTrayIn))
    {
        startup.setButtonText("Start with Windows");
        closeTray.setButtonText("Close window to tray");
        vst2.setButtonText("Enable VST2");
        startup.setClickingTogglesState(true);
        closeTray.setClickingTogglesState(true);
        vst2.setClickingTogglesState(true);
        for (auto* toggle : { &startup, &closeTray, &vst2 }) addAndMakeVisible(toggle);
        addAndMakeVisible(language);
        addAndMakeVisible(icon);
        language.addItem("English", 1);
        language.addItem("Portugues", 2);
        icon.addItem("Color", 1);
        icon.addItem("Black", 2);
        icon.addItem("White", 3);
        startup.onClick = [this] { if (!applying) setStartup(startup.getToggleState()); };
        closeTray.onClick = [this] {
            if (!applying) getAppProperties().getUserSettings()->setValue("closeBehavior", closeTray.getToggleState() ? "tray" : "quit");
        };
        vst2.onClick = [this] {
            if (applying) return;
            getAppProperties().getUserSettings()->setValue("enableVst2", vst2.getToggleState());
            getAppProperties().getUserSettings()->saveIfNeeded();
        };
        language.onChange = [this] {
            if (applying) return;
            const auto settings = juce::File(lightHostModern::RuntimeProfile::current().uiSettings().wstring().c_str());
            settings.getParentDirectory().createDirectory();
            WritePrivateProfileStringW(L"Localization", L"Language", language.getSelectedId() == 2 ? L"pt-br" : L"en-us", settings.getFullPathName().toWideCharPointer());
        };
        icon.onChange = [this] {
            if (applying) return;
            const char* modes[] = { "color", "black", "white" };
            const auto index = icon.getSelectedId() - 1;
            if (index < 0 || index > 2) return;
            getAppProperties().getUserSettings()->setValue("trayIconMode", modes[index]);
            getAppProperties().getUserSettings()->saveIfNeeded();
            if (refreshTray) refreshTray();
        };
    }

    void refresh()
    {
        applying = true;
        startup.setToggleState(startupEnabled(), juce::dontSendNotification);
        closeTray.setToggleState(getAppProperties().getUserSettings()->getValue("closeBehavior", "tray") != "quit", juce::dontSendNotification);
        vst2.setToggleState(getAppProperties().getUserSettings()->getBoolValue("enableVst2", false), juce::dontSendNotification);
        vst2.setEnabled(engine.isVst2FormatActive() || getAppProperties().getUserSettings()->getBoolValue("enableVst2", false));
        wchar_t stored[32]{};
        const auto settings = juce::File(lightHostModern::RuntimeProfile::current().uiSettings().wstring().c_str());
        GetPrivateProfileStringW(L"Localization", L"Language", L"en-us", stored, 32, settings.getFullPathName().toWideCharPointer());
        language.setSelectedId(juce::String(stored).equalsIgnoreCase("pt-br") ? 2 : 1, juce::dontSendNotification);
        const auto mode = getAppProperties().getUserSettings()->getValue("trayIconMode", "color");
        icon.setSelectedId(mode == "black" ? 2 : mode == "white" ? 3 : 1, juce::dontSendNotification);
        applying = false;
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        for (auto* child : { static_cast<juce::Component*>(&startup), static_cast<juce::Component*>(&closeTray), static_cast<juce::Component*>(&vst2) })
            child->setBounds(area.removeFromTop(32));
        language.setBounds(area.removeFromTop(32).removeFromLeft(240));
        icon.setBounds(area.removeFromTop(32).removeFromLeft(240));
    }

private:
    static juce::String startupCommand()
    {
        return "\"" + juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName() + "\" --startup";
    }
    static bool startupEnabled()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
        wchar_t value[2048]{};
        DWORD size = sizeof(value), type = 0;
        const auto result = RegQueryValueExW(key, L"LightHostModern", nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
        RegCloseKey(key);
        return result == ERROR_SUCCESS && juce::String(value).trim().equalsIgnoreCase(startupCommand());
    }
    static void setStartup(bool enabled)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
        if (enabled)
        {
            const auto command = startupCommand();
            const auto wide = command.toWideCharPointer();
            RegSetValueExW(key, L"LightHostModern", 0, REG_SZ, reinterpret_cast<const BYTE*>(wide), (DWORD) ((wcslen(wide) + 1) * sizeof(wchar_t)));
        }
        else RegDeleteValueW(key, L"LightHostModern");
        RegCloseKey(key);
    }
    AudioEngine& engine;
    std::function<void()> refreshTray;
    bool applying = false;
    juce::ToggleButton startup, closeTray, vst2;
    juce::ComboBox language, icon;
};

class DiagnosticsPage : public juce::Component
{
public:
    explicit DiagnosticsPage(AudioEngine& engineIn) : engine(engineIn)
    {
        enabled.setButtonText("Collect diagnostics");
        enabled.setClickingTogglesState(true);
        enabled.onClick = [this] { if (!applying) engine.setDiagnosticsEnabled(enabled.getToggleState()); };
        report.setMultiLine(true);
        report.setReadOnly(true);
        addAndMakeVisible(enabled);
        addAndMakeVisible(report);
    }
    void refresh()
    {
        const auto snapshot = engine.getDiagnosticsSnapshot();
        applying = true;
        enabled.setToggleState(engine.isDiagnosticsEnabled(), juce::dontSendNotification);
        applying = false;
        report.setText("Device: " + snapshot.backend + " " + snapshot.deviceName
            + "\nRecovery: " + snapshot.recoveryState + " " + snapshot.recoveryMessage
            + "\nCPU: " + juce::String(snapshot.cpuUsagePercent, 1)
            + "\nXRuns: " + juce::String(snapshot.xRunCount)
            + "\nLatency: " + juce::String(snapshot.chainLatencySamples) + " samples"
            + "\nFailures: " + juce::String(snapshot.processFailures)
            + "\nBlocks: " + juce::String(snapshot.processedBlocks), false);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        enabled.setBounds(area.removeFromTop(32));
        report.setBounds(area);
    }
private:
    AudioEngine& engine;
    bool applying = false;
    juce::ToggleButton enabled;
    juce::TextEditor report;
};

class SupportPage : public juce::Component
{
public:
    SupportPage()
    {
        about.setText("LightHostModern " + juce::String(lightHostModern::identity::name), juce::dontSendNotification);
        repo.setButtonText("Repository");
        kofi.setButtonText("Support on Ko-fi");
        repo.onClick = [] { juce::URL("https://github.com/heide-oficial/Light-Host-Modern").launchInDefaultBrowser(); };
        kofi.onClick = [] { juce::URL("https://ko-fi.com/heide_oficial").launchInDefaultBrowser(); };
        addAndMakeVisible(about);
        addAndMakeVisible(repo);
        addAndMakeVisible(kofi);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        about.setBounds(area.removeFromTop(32));
        repo.setBounds(area.removeFromTop(32).removeFromLeft(180));
        kofi.setBounds(area.removeFromTop(32).removeFromLeft(180));
    }
private:
    juce::Label about;
    juce::TextButton repo, kofi;
};

class TemplatesPage : public juce::Component, private juce::ListBoxModel
{
public:
    explicit TemplatesPage(AudioEngine& engineIn) : engine(engineIn)
    {
        for (auto* button : { &save, &saveAs, &recall, &rename, &remove }) addAndMakeVisible(button);
        addAndMakeVisible(status);
        addAndMakeVisible(list);
        save.setButtonText("Save");
        saveAs.setButtonText("Save as");
        recall.setButtonText("Recall");
        rename.setButtonText("Rename");
        remove.setButtonText("Delete");
        save.onClick = [this] { saveSelected(); };
        saveAs.onClick = [this] { askName("Save template", {}, [this](const juce::String& name) { show(engine.createTemplate(name)); }); };
        recall.onClick = [this] { recallRow(list.getSelectedRow()); };
        rename.onClick = [this] {
            const int row = list.getSelectedRow();
            if (!valid(row)) return;
            askName("Rename template", entries[(size_t) row].name, [this, id = entries[(size_t) row].id](const juce::String& name) {
                show(engine.renameTemplate(id, name));
            });
        };
        remove.onClick = [this] {
            const int row = list.getSelectedRow();
            if (!valid(row)) return;
            auto dialog = std::make_shared<juce::AlertWindow>("Delete template", "Delete " + entries[(size_t) row].name + "?", juce::AlertWindow::NoIcon);
            dialog->addButton("Delete", 1);
            dialog->addButton("Cancel", 0);
            const auto id = entries[(size_t) row].id;
            dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog, id](int choice) {
                if (choice == 1) show(engine.deleteTemplate(id));
            }), false);
        };
        list.setModel(this);
        list.setRowHeight(28);
    }
    ~TemplatesPage() override { list.setModel(nullptr); }
    void refresh()
    {
        entries = engine.listTemplates();
        active = engine.activeTemplateId();
        list.updateContent();
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].id == active) list.selectRow(i);
        list.repaint();
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        auto bar = area.removeFromTop(32);
        save.setBounds(bar.removeFromLeft(90));
        saveAs.setBounds(bar.removeFromLeft(100));
        recall.setBounds(bar.removeFromLeft(90));
        rename.setBounds(bar.removeFromLeft(90));
        remove.setBounds(bar.removeFromLeft(90));
        status.setBounds(area.removeFromTop(24));
        list.setBounds(area);
    }
    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
    {
        if (!valid(row)) return;
        const bool current = entries[(size_t) row].id == active;
        graphics.fillAll(selected || current ? juce::Colour(0xff3d5a80) : juce::Colour(0xff2c2c2c));
        graphics.setColour(juce::Colours::white);
        graphics.drawText(entries[(size_t) row].name, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { recallRow(row); }

private:
    bool valid(int row) const { return row >= 0 && row < (int) entries.size(); }
    void recallRow(int row)
    {
        if (!valid(row)) return;
        show(engine.recallTemplate(entries[(size_t) row].id));
    }
    void saveSelected()
    {
        const int row = list.getSelectedRow();
        if (valid(row)) show(engine.updateTemplate(entries[(size_t) row].id));
        else askName("Save template", {}, [this](const juce::String& name) { show(engine.createTemplate(name)); });
    }
    void show(const juce::String& error)
    {
        status.setText(message(error), juce::dontSendNotification);
        refresh();
    }
    static juce::String message(const juce::String& error)
    {
        if (error.isEmpty()) return "Recall replaces the running setup.";
        if (error == "template_name_taken") return "That name is already used.";
        if (error == "template_name_invalid") return "Enter a name.";
        if (error == "template_not_found") return "Template was not found.";
        if (error == "template_session_invalid") return "Template file is damaged.";
        if (error == "template_limit") return "Too many templates.";
        if (error == "session_read_only") return "The session is read only.";
        return error;
    }
    void askName(const juce::String& title, const juce::String& initial, std::function<void(const juce::String&)> apply)
    {
        auto dialog = std::make_shared<juce::AlertWindow>(title, "Template name", juce::AlertWindow::NoIcon);
        dialog->addTextEditor("name", initial);
        dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog->enterModalState(true, juce::ModalCallbackFunction::create([dialog, apply](int choice) {
            if (choice == 1) apply(dialog->getTextEditorContents("name"));
        }), false);
    }
    AudioEngine& engine;
    juce::TextButton save, saveAs, recall, rename, remove;
    juce::Label status;
    juce::ListBox list;
    std::vector<lightHostModern::TemplateEntry> entries;
    juce::String active;
};

class RailButton : public juce::TextButton
{
public:
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> icon;
    bool showIcon = false;
    void paintButton(juce::Graphics& graphics, bool over, bool down) override
    {
        getLookAndFeel().drawButtonBackground(graphics, *this, juce::Colours::transparentBlack, over, down);
        if (showIcon && icon)
        {
            graphics.setColour(findColour(juce::TextButton::textColourOffId));
            icon(graphics, getLocalBounds().toFloat().reduced(11.0f, 8.0f));
            return;
        }
        getLookAndFeel().drawButtonText(graphics, *this, over, down);
    }
};

void paintMixerIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    for (int i = 0; i < 3; ++i)
    {
        const float x = bounds.getX() + bounds.getWidth() * (0.18f + i * 0.32f);
        graphics.drawLine(x, bounds.getY() + 1.0f, x, bounds.getBottom() - 1.0f, 1.6f);
        const float y = bounds.getY() + bounds.getHeight() * (i == 1 ? 0.55f : 0.28f);
        graphics.fillRoundedRectangle(x - 3.5f, y, 7.0f, 4.0f, 1.0f);
    }
}

void paintPluginIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    auto body = bounds.reduced(bounds.getWidth() * 0.18f, bounds.getHeight() * 0.22f);
    graphics.drawRoundedRectangle(body, 2.0f, 1.6f);
    graphics.fillRect(body.getX() + 2.0f, bounds.getY(), 2.2f, body.getY() - bounds.getY() + 1.0f);
    graphics.fillRect(body.getRight() - 4.2f, bounds.getY(), 2.2f, body.getY() - bounds.getY() + 1.0f);
}

void paintAudioIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    juce::Path cone;
    cone.addTriangle(bounds.getX() + 2.0f, bounds.getCentreY() - 3.0f, bounds.getX() + 2.0f, bounds.getCentreY() + 3.0f, bounds.getCentreX(), bounds.getCentreY());
    graphics.fillRect(bounds.getX(), bounds.getCentreY() - 4.0f, bounds.getWidth() * 0.28f, 8.0f);
    graphics.fillPath(cone);
    graphics.drawEllipse(bounds.getCentreX(), bounds.getCentreY() - 6.0f, 8.0f, 12.0f, 1.4f);
}

void paintDashboardIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    const float gap = 2.0f;
    const float size = (juce::jmin(bounds.getWidth(), bounds.getHeight()) - gap) * 0.5f;
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 2; ++column)
            graphics.fillRoundedRectangle(bounds.getX() + column * (size + gap), bounds.getY() + row * (size + gap), size, size, 1.5f);
}

void paintSettingsIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    const auto centre = bounds.getCentre();
    graphics.drawEllipse(bounds.reduced(4.0f), 1.6f);
    for (int i = 0; i < 8; ++i)
    {
        const float angle = i * juce::MathConstants<float>::halfPi * 0.5f;
        graphics.drawLine(centre.x + std::cos(angle) * 5.0f, centre.y + std::sin(angle) * 5.0f,
            centre.x + std::cos(angle) * 8.5f, centre.y + std::sin(angle) * 8.5f, 1.6f);
    }
}

void paintDiagnosticsIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    juce::Path pulse;
    pulse.startNewSubPath(bounds.getX(), bounds.getCentreY());
    pulse.lineTo(bounds.getX() + bounds.getWidth() * 0.28f, bounds.getCentreY());
    pulse.lineTo(bounds.getX() + bounds.getWidth() * 0.42f, bounds.getY() + 1.0f);
    pulse.lineTo(bounds.getX() + bounds.getWidth() * 0.58f, bounds.getBottom() - 1.0f);
    pulse.lineTo(bounds.getX() + bounds.getWidth() * 0.72f, bounds.getCentreY());
    pulse.lineTo(bounds.getRight(), bounds.getCentreY());
    graphics.strokePath(pulse, juce::PathStrokeType(1.6f));
}

void paintSupportIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    const float width = bounds.getWidth() * 0.46f;
    graphics.fillEllipse(bounds.getX(), bounds.getY() + 1.0f, width, width);
    graphics.fillEllipse(bounds.getRight() - width, bounds.getY() + 1.0f, width, width);
    juce::Path heart;
    heart.startNewSubPath(bounds.getCentreX(), bounds.getBottom() - 1.0f);
    heart.lineTo(bounds.getX() + 1.0f, bounds.getCentreY());
    heart.lineTo(bounds.getRight() - 1.0f, bounds.getCentreY());
    heart.closeSubPath();
    graphics.fillPath(heart);
}

void paintTemplatesIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    auto back = bounds.reduced(1.0f).translated(3.0f, -2.0f);
    auto front = bounds.reduced(1.0f).translated(-2.0f, 2.0f);
    graphics.drawRoundedRectangle(back, 1.5f, 1.4f);
    graphics.setColour(juce::Colours::white);
    graphics.fillRoundedRectangle(front, 1.5f);
    graphics.setColour(juce::Colour(0xff1e1e1e));
    graphics.fillRect(front.getX() + 3.0f, front.getCentreY() - 1.0f, front.getWidth() - 6.0f, 1.4f);
}

void paintRecordIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds)
{
    graphics.fillEllipse(bounds.reduced(2.0f));
}

class RecordPage : public juce::Component, private juce::Timer
{
public:
    explicit RecordPage(AudioEngine& engineIn) : engine(engineIn)
    {
        for (auto* label : { &folderLabel, &formatLabel, &rateLabel, &iceHostLabel, &icePortLabel, &iceMountLabel, &iceUserLabel, &icePassLabel, &iceNameLabel, &deviceLabel, &midiLabel })
            addAndMakeVisible(label);
        folderLabel.setText("Folder", juce::dontSendNotification);
        formatLabel.setText("Format", juce::dontSendNotification);
        rateLabel.setText("MP3", juce::dontSendNotification);
        iceHostLabel.setText("Icecast host", juce::dontSendNotification);
        icePortLabel.setText("Port", juce::dontSendNotification);
        iceMountLabel.setText("Mount", juce::dontSendNotification);
        iceUserLabel.setText("User", juce::dontSendNotification);
        icePassLabel.setText("Password", juce::dontSendNotification);
        iceNameLabel.setText("Name", juce::dontSendNotification);
        deviceLabel.setText("Stream device", juce::dontSendNotification);
        midiLabel.setText("Mackie MIDI", juce::dontSendNotification);
        addAndMakeVisible(folder);
        addAndMakeVisible(browse);
        addAndMakeVisible(format);
        addAndMakeVisible(bitrate);
        addAndMakeVisible(mixdown);
        addAndMakeVisible(multi);
        addAndMakeVisible(interleaved);
        addAndMakeVisible(raw);
        addAndMakeVisible(record);
        addAndMakeVisible(iceHost);
        addAndMakeVisible(icePort);
        addAndMakeVisible(iceMount);
        addAndMakeVisible(iceUser);
        addAndMakeVisible(icePass);
        addAndMakeVisible(iceName);
        addAndMakeVisible(devices);
        addAndMakeVisible(stream);
        addAndMakeVisible(midi);
        addAndMakeVisible(status);
        browse.setButtonText("Browse");
        record.setButtonText("Record");
        stream.setButtonText("Stream");
        mixdown.setButtonText("Master mixdown");
        multi.setButtonText("Multitrack");
        interleaved.setButtonText("One multichannel WAV");
        raw.setButtonText("Raw (no effects)");
        format.addItem("WAV", 1);
        format.addItem("MP3", 2);
        bitrate.addItem("128 kbps", 128);
        bitrate.addItem("192 kbps", 192);
        bitrate.addItem("320 kbps", 320);
        icePass.setPasswordCharacter(0x2022);
        auto* settings = getAppProperties().getUserSettings();
        folder.setText(settings->getValue("recordFolder", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LightHostModern").getChildFile("Recordings").getFullPathName()), juce::dontSendNotification);
        format.setSelectedId(settings->getBoolValue("recordMp3", false) ? 2 : 1, juce::dontSendNotification);
        bitrate.setSelectedId(settings->getIntValue("recordBitrate", 192), juce::dontSendNotification);
        if (bitrate.getSelectedId() == 0) bitrate.setSelectedId(192, juce::dontSendNotification);
        mixdown.setToggleState(settings->getBoolValue("recordMixdown", true), juce::dontSendNotification);
        multi.setToggleState(settings->getBoolValue("recordMulti", false), juce::dontSendNotification);
        interleaved.setToggleState(settings->getBoolValue("recordInterleaved", false), juce::dontSendNotification);
        raw.setToggleState(settings->getBoolValue("recordRaw", false), juce::dontSendNotification);
        iceHost.setText(settings->getValue("icecastHost"), juce::dontSendNotification);
        icePort.setText(settings->getValue("icecastPort", "8000"), juce::dontSendNotification);
        iceMount.setText(settings->getValue("icecastMount", "/live"), juce::dontSendNotification);
        iceUser.setText(settings->getValue("icecastUser", "source"), juce::dontSendNotification);
        iceName.setText(settings->getValue("icecastName", "LightHostModern"), juce::dontSendNotification);
        browse.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Recordings", juce::File(folder.getText()), "*", true);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& box) {
                if (box.getResult() != juce::File()) folder.setText(box.getResult().getFullPathName(), juce::dontSendNotification);
            });
        };
        format.onChange = [this] { interleaved.setEnabled(format.getSelectedId() != 2 && !engine.isRecording()); };
        record.onClick = [this] { toggleRecord(); };
        stream.onClick = [this] { toggleStream(); };
        devices.onChange = [this] {
            if (applying) return;
            getAppProperties().getUserSettings()->setValue("streamDevice", devices.getSelectedId() > 1 ? devices.getText() : juce::String());
            getAppProperties().getUserSettings()->saveIfNeeded();
        };
        midi.onChange = [this] {
            if (applying) return;
            if (midi.getSelectedId() <= 1) engine.closeMackie();
            else if (const auto error = engine.openMackie(midi.getSelectedId() - 2); error.isNotEmpty()) status.setText(error, juce::dontSendNotification);
        };
        status.setText("Password is not encrypted on the network.", juce::dontSendNotification);
        startTimerHz(4);
    }

    void refresh()
    {
        applying = true;
        devices.clear(juce::dontSendNotification);
        devices.addItem("Off", 1);
        const auto names = engine.streamDeviceNames();
        for (int i = 0; i < names.size(); ++i) devices.addItem(names[i], i + 2);
        const auto savedDevice = getAppProperties().getUserSettings()->getValue("streamDevice");
        int deviceId = 1;
        for (int i = 0; i < names.size(); ++i) if (names[i] == savedDevice) deviceId = i + 2;
        devices.setSelectedId(deviceId, juce::dontSendNotification);
        midi.clear(juce::dontSendNotification);
        midi.addItem("Off", 1);
        const auto inputs = engine.midiInputNames();
        for (int i = 0; i < inputs.size(); ++i) midi.addItem(inputs[i], i + 2);
        if (midi.getSelectedId() == 0) midi.setSelectedId(1, juce::dontSendNotification);
        applying = false;
        interleaved.setEnabled(format.getSelectedId() != 2 && !engine.isRecording());
        raw.setEnabled(!engine.isRecording());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        auto row = [&](juce::Component& label, juce::Component& field) {
            auto line = area.removeFromTop(28);
            label.setBounds(line.removeFromLeft(120));
            field.setBounds(line);
            area.removeFromTop(6);
        };
        row(folderLabel, folder);
        browse.setBounds(area.removeFromTop(28).removeFromLeft(120));
        area.removeFromTop(6);
        row(formatLabel, format);
        row(rateLabel, bitrate);
        auto checks = area.removeFromTop(28);
        mixdown.setBounds(checks.removeFromLeft(150));
        multi.setBounds(checks.removeFromLeft(140));
        interleaved.setBounds(checks.removeFromLeft(190));
        raw.setBounds(checks.removeFromLeft(160));
        area.removeFromTop(6);
        record.setBounds(area.removeFromTop(32).removeFromLeft(140));
        area.removeFromTop(12);
        row(iceHostLabel, iceHost);
        row(icePortLabel, icePort);
        row(iceMountLabel, iceMount);
        row(iceUserLabel, iceUser);
        row(icePassLabel, icePass);
        row(iceNameLabel, iceName);
        row(deviceLabel, devices);
        stream.setBounds(area.removeFromTop(32).removeFromLeft(140));
        area.removeFromTop(8);
        row(midiLabel, midi);
        status.setBounds(area.removeFromTop(48));
    }

private:
    void timerCallback() override { if (isShowing()) status.setText(engine.recordingStatus().isEmpty() ? status.getText() : engine.recordingStatus(), juce::dontSendNotification); }
    void save()
    {
        auto* settings = getAppProperties().getUserSettings();
        settings->setValue("recordFolder", folder.getText());
        settings->setValue("recordMp3", format.getSelectedId() == 2);
        settings->setValue("recordBitrate", bitrate.getSelectedId());
        settings->setValue("recordMixdown", mixdown.getToggleState());
        settings->setValue("recordMulti", multi.getToggleState());
        settings->setValue("recordInterleaved", interleaved.getToggleState());
        settings->setValue("recordRaw", raw.getToggleState());
        settings->setValue("icecastHost", iceHost.getText());
        settings->setValue("icecastPort", icePort.getText());
        settings->setValue("icecastMount", iceMount.getText());
        settings->setValue("icecastUser", iceUser.getText());
        settings->setValue("icecastName", iceName.getText());
        settings->setValue("icecastPassword", icePass.getText());
        settings->saveIfNeeded();
    }
    void toggleRecord()
    {
        if (engine.isRecording()) { engine.stopRecording(); record.setButtonText("Record"); raw.setEnabled(true); return; }
        save();
        const auto error = engine.startRecording(juce::File(folder.getText()), format.getSelectedId() == 2, bitrate.getSelectedId() * 1000,
                                                  mixdown.getToggleState(), multi.getToggleState(), interleaved.getToggleState(), raw.getToggleState(),
                                                  iceHost.getText().trim(), icePort.getText().getIntValue(), iceMount.getText().trim(), iceUser.getText().trim(), icePass.getText(), iceName.getText().trim());
        if (error.isNotEmpty()) { status.setText(error, juce::dontSendNotification); return; }
        record.setButtonText("Stop");
        raw.setEnabled(false);
    }
    void toggleStream()
    {
        if (engine.isStreaming()) { engine.stopStream(); stream.setButtonText("Stream"); return; }
        const int id = devices.getSelectedId();
        if (id <= 1) { status.setText("Choose a stream device. Install a virtual cable so other apps can select it.", juce::dontSendNotification); return; }
        if (const auto error = engine.startStream(devices.getText()); error.isNotEmpty()) status.setText(error, juce::dontSendNotification);
        else { stream.setButtonText("Stop stream"); status.setText("Streaming. Other apps select the cable input.", juce::dontSendNotification); }
    }

    AudioEngine& engine;
    juce::Label folderLabel, formatLabel, rateLabel, iceHostLabel, icePortLabel, iceMountLabel, iceUserLabel, icePassLabel, iceNameLabel, deviceLabel, midiLabel, status;
    juce::TextEditor folder, iceHost, icePort, iceMount, iceUser, icePass, iceName;
    juce::ComboBox format, bitrate, devices, midi;
    juce::ToggleButton mixdown, multi, interleaved, raw;
    juce::TextButton browse, record, stream;
    std::unique_ptr<juce::FileChooser> chooser;
    bool applying = false;
};

class Shell : public juce::Component, public juce::DragAndDropContainer, private juce::Timer
{
public:
    Shell(AudioEngine& engineIn, std::function<void()> refreshTray)
        : engine(engineIn), mixer(engineIn), templates(engineIn), installed(engineIn), audio(engineIn), dashboard(engineIn),
          settings(engineIn, std::move(refreshTray)), diagnostics(engineIn), record(engineIn)
    {
        setLookAndFeel(&shellLook());
        juce::LookAndFeel::setDefaultLookAndFeel(&shellLook());
        const char* names[] = { "Mixer", "Templates", "VST Plugins", "Audio", "Dashboard", "Settings", "Diagnostics", "Support", "Record" };
        const std::function<void(juce::Graphics&, juce::Rectangle<float>)> icons[] = {
            paintMixerIcon, paintTemplatesIcon, paintPluginIcon, paintAudioIcon, paintDashboardIcon, paintSettingsIcon, paintDiagnosticsIcon, paintSupportIcon, paintRecordIcon
        };
        for (int i = 0; i < 9; ++i)
        {
            titles[i] = names[i];
            rail[i].setTooltip(titles[i]);
            rail[i].icon = icons[i];
            rail[i].setClickingTogglesState(false);
            rail[i].onClick = [this, i] { show(i); };
            addAndMakeVisible(rail[i]);
        }
        collapse.setTooltip("Collapse sidebar");
        collapse.onClick = [this] {
            collapsed = !collapsed;
            collapse.setTooltip(collapsed ? "Expand sidebar" : "Collapse sidebar");
            applyRail();
            resized();
        };
        addAndMakeVisible(collapse);
        applyRail();
        for (auto* page : pages()) { addChildComponent(page); }
        show(0);
        startTimer(400);
    }

    ~Shell() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& graphics) override { graphics.fillAll(juce::Colour(0xff1e1e1e)); }

    void resized() override
    {
        auto area = getLocalBounds();
        auto side = area.removeFromLeft(collapsed ? 52 : 168);
        collapse.setBounds(side.removeFromBottom(40).reduced(6, 4));
        for (auto& button : rail) button.setBounds(side.removeFromTop(40).reduced(6, 4));
        for (auto* page : pages()) page->setBounds(area);
    }

private:
    std::array<juce::Component*, 9> pages() { return { &mixer, &templates, &installed, &audio, &dashboard, &settings, &diagnostics, &support, &record }; }
    void applyRail()
    {
        collapse.setButtonText(collapsed ? ">" : "<");
        for (int i = 0; i < 9; ++i)
        {
            rail[i].showIcon = collapsed;
            rail[i].setButtonText(collapsed ? juce::String() : titles[i]);
        }
    }
    void show(int index)
    {
        current = index;
        auto shown = pages();
        for (int i = 0; i < 9; ++i)
        {
            shown[i]->setVisible(i == index);
            rail[i].setToggleState(i == index, juce::dontSendNotification);
        }
        refresh();
    }
    void refresh()
    {
        if (current == 0) mixer.refresh();
        else if (current == 1) templates.refresh();
        else if (current == 2) installed.refresh();
        else if (current == 3) audio.refresh();
        else if (current == 4) dashboard.refresh();
        else if (current == 5) settings.refresh();
        else if (current == 6) diagnostics.refresh();
        else if (current == 8) record.refresh();
    }
    void timerCallback() override
    {
        const auto chain = engine.getChainVersion();
        const auto audioVersion = engine.getAudioConfigVersion();
        const auto database = engine.getPluginDatabaseVersion();
        const auto profile = engine.getProfileVersion();
        if (chain != chainVersion || audioVersion != seenAudio || database != seenDatabase || profile != seenProfile || current == 4)
        {
            chainVersion = chain; seenAudio = audioVersion; seenDatabase = database; seenProfile = profile;
            refresh();
        }
    }

    AudioEngine& engine;
    MixerView mixer;
    TemplatesPage templates;
    InstalledPage installed;
    AudioPage audio;
    DashboardPage dashboard;
    SettingsPage settings;
    DiagnosticsPage diagnostics;
    SupportPage support;
    RecordPage record;
    RailButton rail[9];
    juce::TextButton collapse;
    juce::String titles[9];
    bool collapsed = false;
    juce::TooltipWindow tooltips { this, 500 };
    int current = 0;
    uint64 chainVersion = 0, seenAudio = 0, seenDatabase = 0, seenProfile = 0;
};
}

HostWindow::HostWindow(AudioEngine& engine, std::function<void()> refreshTray)
    : juce::DocumentWindow("LightHostModern", juce::Colour(0xff1e1e1e), juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new Shell(engine, std::move(refreshTray)), true);
    setResizable(true, true);
    setResizeLimits(728, 679, 8192, 8192);
    centreWithSize(1180, 760);
    setVisible(true);
}

void HostWindow::closeButtonPressed()
{
    setVisible(false);
}
