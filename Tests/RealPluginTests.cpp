#include "RealtimeHostProcessor.h"
#include "PluginWindow.h"
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include <chrono>

void lightHostModernLog(const String&) {}
void setLightHostModernCrashContext(const String&) {}
bool installRealtimeAllocationAudit();
namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void pump(int milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do
    {
        MSG message;
        while (std::chrono::steady_clock::now() < end && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        { TranslateMessage(&message); DispatchMessageW(&message); }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < end);
}
struct AudioWorker
{
    explicit AudioWorker(RealtimeHostProcessor& processor) : worker([this, &processor] {
        AudioBuffer<float> audio(2, 64); MidiBuffer midi;
        while (!stop.load())
        {
            for (int c = 0; c < 2; ++c) FloatVectorOperations::fill(audio.getWritePointer(c), .05f, 64);
            processor.processBlock(audio, midi);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }) {}
    ~AudioWorker() { stop = true; worker.join(); }
    std::atomic<bool> stop{false}; std::thread worker;
};
var test(const String& formatName, const File& module, PluginWindow::WindowFormatType editorType)
{
    AudioPluginFormatManager formats; addDefaultFormatsToManager(formats);
    AudioPluginFormat* format = nullptr;
    for (auto* candidate : formats.getFormats()) if (candidate->getName() == formatName) format = candidate;
    require(format != nullptr, "Plugin format is unavailable");
    OwnedArray<PluginDescription> descriptions;
    format->findAllTypesForFile(descriptions, module.getFullPathName());
    require(descriptions.size() == 1, "Expected one effect class in this fixture module");
    const auto description = *descriptions[0];
    const auto create = [&] {
        String error;
        auto plugin = formats.createPluginInstance(description, 48000, 64, error);
        if (!plugin) throw std::runtime_error(error.toStdString());
        return plugin;
    };
    auto original = create();
    require(original->getTotalNumInputChannels() == 2 && original->getTotalNumOutputChannels() == 2, "Fixture bus layout changed");
    const bool nativeEditor = original->hasEditor();
    AudioProcessorParameter* parameter = nullptr;
    int parameterIndex = -1;
    for (auto* candidate : original->getParameters())
        if (candidate != original->getBypassParameter() && candidate->isAutomatable()
            && !candidate->getName(128).containsIgnoreCase("bypass"))
        { parameter = candidate; parameterIndex = candidate->getParameterIndex(); break; }
    require(parameter != nullptr, "Fixture has no stateful parameter");
    std::cout << "State parameter: " << parameter->getName(128) << " index=" << parameterIndex << std::endl;
    const auto before = parameter->getValue();
    original->setRateAndBufferSizeDetails(48000, 64); original->prepareToPlay(48000, 64);
    AudioBuffer<float> settling(2, 64); MidiBuffer settlingMidi; settling.clear();
    original->processBlock(settling, settlingMidi);
    MemoryBlock initial; original->getStateInformation(initial);
    require(initial.getSize() > 0, "Empty original plugin state");
    parameter->setValueNotifyingHost(before < .5f ? .8f : .2f);
    const auto changed = parameter->getValue();
    require(std::abs(before - changed) > .01f, "Fixture parameter did not change");
    // VST3 delivers host parameter changes to the component at a process
    // boundary. Capture the committed DSP state after that boundary.
    settling.clear(); original->processBlock(settling, settlingMidi);
    MemoryBlock saved; original->getStateInformation(saved);
    original->setStateInformation(initial.getData(), static_cast<int>(initial.getSize()));
    std::cout << "Restored value: " << parameter->getValue() << " expected=" << before << " changed=" << changed << std::endl;
    require(std::abs(parameter->getValue() - before) < .001f, "Original state did not restore the parameter");
    original->releaseResources();
    auto duplicate = create();
    duplicate->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(parameterIndex >= 0 && parameterIndex < duplicate->getParameters().size(), "Duplicate parameters differ");
    require(std::abs(duplicate->getParameters()[parameterIndex]->getValue() - changed) < .001f, "Duplicate state was associated with the wrong values");
    // Use the application's actual callback, buffer segmentation, slot and
    // editor paths. This runner never constructs an audio device manager.
    auto host = std::make_unique<RealtimeHostProcessor>();
    host->setPlayConfigDetails(2, 2, 48000, 64); host->prepareToPlay(48000, 64);
    auto slot = std::make_shared<PluginSlot>(description, std::move(original)); slot->instanceId = Uuid().toString();
    auto copy = std::make_shared<PluginSlot>(description, std::move(duplicate)); copy->instanceId = Uuid().toString();
    auto chain = std::make_shared<ChainSnapshot>(); chain->slots = {slot}; host->publishSnapshot(chain);
    double energy = 0; uint64 expectedSamples = 0, expectedBlocks = 0;
    for (int repeat = 0; repeat < 16; ++repeat)
        for (const int size : {17, 64, 257, 1001})
        {
            AudioBuffer<float> audio(2, size); MidiBuffer midi; host->prepareMidiBuffer(midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < size; ++i) audio.setSample(c, i, .1f * std::sin(float(expectedSamples + i) * .039269908f));
            host->processBlock(audio, midi); expectedSamples += size; ++expectedBlocks;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < size; ++i)
            { const auto value = audio.getSample(c, i); require(std::isfinite(value), "Plugin produced non-finite output"); energy += value * value; }
        }
    require(energy > .01, "All real plugin output was lost");
    const auto checked = host->getStats();
    require(checked.processedBlocks == expectedBlocks && checked.processedSamples == expectedSamples && checked.processFailures == 0, "Processing counters or failures disagree with submitted audio");
    for (const bool bypass : {false, true, false, true})
    {
        host->setGlobalBypassed(bypass); host->setGlobalMuted(true);
        AudioBuffer<float> audio(2, 1001); MidiBuffer midi;
        for (int c = 0; c < 2; ++c) FloatVectorOperations::fill(audio.getWritePointer(c), .1f, 1001);
        host->processBlock(audio, midi);
        require(audio.getMagnitude(400, 601) == 0, "Mute failed with real plugin processing");
        host->setGlobalMuted(false);
    }
    host->setGlobalBypassed(false);
    {
        AudioWorker callbacks(*host);
        auto* editor = PluginWindow::getWindowFor(*slot->processor, slot->windowProperties, editorType);
        require(editor != nullptr, "Real plugin editor did not open");
        std::cout << "Editor opened" << std::endl;
        pump(250);
        for (int iteration = 0; iteration < 4; ++iteration)
        {
            std::cout << "Capture/reconfigure " << iteration << std::endl;
            RealtimeHostProcessor::ScopedSuspension suspended(*host);
            MemoryBlock captured; slot->processor->getStateInformation(captured);
            require(captured.getSize() > 0, "Concurrent coordinated state capture failed");
            slot->processor->setStateInformation(captured.getData(), static_cast<int>(captured.getSize()));
            auto reordered = std::make_shared<ChainSnapshot>(); reordered->slots = iteration % 2 ? std::vector{slot, copy} : std::vector{copy, slot};
            host->publishSnapshot(reordered);
            host->prepareToPlay(iteration % 2 ? 48000 : 44100, iteration % 2 ? 64 : 128);
            require(PluginWindow::getWindowFor(*slot->processor, slot->windowProperties, editorType) == editor, "Reused processor lost its editor");
        }
        pump(250);
        PluginWindow::closeCurrentlyOpenWindowsFor(*slot->processor);
        std::cout << "Editor closed" << std::endl;
        require(!PluginWindow::containsActiveWindows(), "Editor did not close");
        auto* reopened = PluginWindow::getWindowFor(*slot->processor, slot->windowProperties, editorType);
        require(reopened != nullptr, "Editor could not reopen"); pump(150);
        PluginWindow::closeCurrentlyOpenWindowsFor(*slot->processor);
        RealtimeHostProcessor::ScopedSuspension suspended(*host);
        host->publishSnapshot(std::make_shared<ChainSnapshot>());
        host->collectRetiredSnapshots(); chain.reset(); slot.reset(); copy.reset();
    }
    const auto result = host->getStats();
    require(result.processFailures == 0 && result.hostAllocations == 0 && result.hostFrees == 0, "Host callback failure or allocation with real plugins");
    auto* report = new DynamicObject;
    report->setProperty("name", description.name); report->setProperty("format", formatName); report->setProperty("module", module.getFullPathName());
    report->setProperty("nativeEditor", nativeEditor); report->setProperty("stateBytes", static_cast<int64>(saved.getSize()));
    report->setProperty("editorRequested", editorType == PluginWindow::Generic ? "generic" : "native_with_generic_fallback");
    report->setProperty("signalEnergy", energy); report->setProperty("processedSamples", static_cast<int64>(result.processedSamples));
    report->setProperty("processedBlocks", static_cast<int64>(result.processedBlocks));
    report->setProperty("processFailures", static_cast<int64>(result.processFailures));
    report->setProperty("hostAllocations", static_cast<int64>(result.hostAllocations)); report->setProperty("hostFrees", static_cast<int64>(result.hostFrees));
    report->setProperty("allocationScope", "Host image C++ and CRT calls; third-party DLL internals are not intercepted");
    report->setProperty("status", "passed"); return var(report);
}
}
int main()
{
    try
    {
        ScopedJuceInitialiser_GUI initialise;
        require(installRealtimeAllocationAudit(), "Host allocation audit could not be installed");
        int count = 0; auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        require(arguments && (count == 4 || count == 5), "Usage: LightHostModernRealPluginTests <VST|VST3> <module> <result.json> [generic]");
        require(count == 4 || String(arguments[4]) == "generic", "Unknown editor option");
        const auto editorType = count == 5 ? PluginWindow::Generic : PluginWindow::Normal;
        const String format(arguments[1]); const File module{String(arguments[2])}, report{String(arguments[3])}; LocalFree(arguments);
        require(module.exists(), "Fixture module is missing");
        const auto result = test(format, module, editorType);
        require(report.replaceWithText(JSON::toString(result, true)), "Could not write result");
        std::cout << JSON::toString(result, true) << std::endl; return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << std::endl; return 1; }
}
