#pragma once
#include "PluginInstances.h"

namespace lightHost
{
// A catalogue alias belongs to the stable module/class identity, never its name
// or current scan position. JUCE descriptions and processor state stay original.
inline juce::String knownPluginCustomName(const juce::PropertySet& settings, const juce::PluginDescription& plugin)
{
    return settings.getValue("knownPluginName-" + knownPluginId(plugin));
}

inline bool setKnownPluginCustomName(juce::PropertySet& settings, const juce::PluginDescription& plugin, const juce::String& name)
{
    juce::String normalized;
    if (!normalizeInstanceName(name, normalized)) return false;
    const auto key = "knownPluginName-" + knownPluginId(plugin);
    if (normalized.isEmpty() || normalized == plugin.name) settings.removeValue(key);
    else settings.setValue(key, normalized);
    return true;
}

inline PluginInstanceRecord newKnownPluginInstance(const juce::PropertySet& settings, const juce::PluginDescription& plugin)
{
    PluginInstanceRecord result;
    result.id = juce::Uuid().toString();
    result.description = plugin;
    result.originalIdentity = knownPluginId(plugin);
    result.customName = knownPluginCustomName(settings, plugin);
    return result;
}
}
