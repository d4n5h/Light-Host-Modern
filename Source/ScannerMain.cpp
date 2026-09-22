#include <juce_audio_utils/juce_audio_utils.h>
#include "ScanProcess.h"
#include "ScannerProtocol.h"
#include "PluginInstances.h"
#include <shellapi.h>

// This executable never constructs AudioEngine, AudioDeviceManager, application
// properties or a tray. Only the module named in this one-shot request is loaded.
int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments || count != 3) { if (arguments) LocalFree(arguments); return 2; }
    const juce::File requestFile { juce::String(arguments[1]) };
    const juce::File responseFile { juce::String(arguments[2]) };
    LocalFree(arguments);
    auto request = juce::XmlDocument::parse(requestFile);
    if (!request || !request->hasTagName("SCAN") || request->getIntAttribute("version") != lightHostModern::scan::scannerProtocolVersion) return 3;
    if (request->getStringAttribute("mode") == "enumerate") return lightHostModern::scan::enumerate(*request, responseFile);
    try
    {
        juce::ScopedJuceInitialiser_GUI initialise;
        juce::AudioPluginFormatManager formats;
       #if JUCE_PLUGINHOST_VST
        formats.addFormat(std::make_unique<juce::VSTPluginFormat>());
       #endif
       #if JUCE_PLUGINHOST_VST3
        formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
       #endif
        juce::AudioPluginFormat* selected = nullptr;
        for (auto* format : formats.getFormats())
            if (format->getName() == request->getStringAttribute("format")) selected = format;
        if (!selected) return 4;
        const auto path = request->getStringAttribute("path");
        const juce::File module(path);
        const auto fingerprint = lightHostModern::scan::fingerprint(module);
        if (fingerprint != request->getStringAttribute("fingerprint")) return 7;
        juce::OwnedArray<juce::PluginDescription> plugins;
        // JUCE uses VST3 moduleinfo when present. Every resulting class is still
        // instantiated and checked below before being accepted by the host.
        selected->findAllTypesForFile(plugins, path);
        juce::XmlElement response("SCAN");
        response.setAttribute("version", lightHostModern::scan::scannerProtocolVersion);
        response.setAttribute("mode", "probe");
        response.setAttribute("fingerprint", fingerprint);
        response.setAttribute("id", request->getStringAttribute("id"));
        response.setAttribute("path", request->getStringAttribute("path"));
        response.setAttribute("format", selected->getName());
        for (const auto* plugin : plugins)
        {
            auto* item = response.createNewChildElement("ENTRY");
            item->setAttribute("knownId", lightHostModern::knownPluginId(*plugin));
            item->addChildElement(plugin->createXml().release());
            const auto moduleInfo = module.getChildFile("Contents/Resources/moduleinfo.json");
            item->setAttribute("declaredMetadata", moduleInfo.existsAsFile() && juce::JSON::parse(moduleInfo.loadFileAsString()).isObject() ? "available" : "unavailable");
            try
            {
                juce::String error;
                auto instance = formats.createPluginInstance(*plugin, 48000, 512, error);
                if (!instance) { item->setAttribute("error", error.isEmpty() ? "validation_failed" : error); continue; }
                juce::PluginDescription actual;
                instance->fillInPluginDescription(actual);
                if (lightHostModern::knownPluginId(actual) != lightHostModern::knownPluginId(*plugin)
                    || !lightHostModern::scan::belongsToModule(actual.fileOrIdentifier, path, selected->getName()))
                { item->setAttribute("error", "identity_mismatch"); continue; }
                for (const bool input : {true, false})
                    for (int index = 0; index < instance->getBusCount(input); ++index)
                    {
                        const auto* bus = instance->getBus(input, index);
                        auto* info = item->createNewChildElement("BUS");
                        info->setAttribute("direction", input ? "input" : "output");
                        info->setAttribute("name", bus->getName());
                        info->setAttribute("channels", bus->getNumberOfChannels());
                        info->setAttribute("main", index == 0);
                        info->setAttribute("enabled", bus->isEnabled());
                        info->setAttribute("layout", bus->getDefaultLayout().getDescription());
                        info->setAttribute("defaultChannels", bus->getDefaultLayout().size());
                    }
                item->setAttribute("verifiedMetadata", "verified");
            }
            catch (...) { item->setAttribute("error", "validation_exception"); }
        }
        if (lightHostModern::scan::fingerprint(module) != fingerprint) return 7;
        return response.writeTo(responseFile) ? 0 : 5;
    }
    catch (const std::exception& error)
    {
        const juce::String reason(error.what());
        if (reason == "missing") return 8;
        if (reason == "metadata_unavailable") return 9;
        if (reason == "changed") return 7;
        return 6;
    }
    catch (...) { return 6; }
}
