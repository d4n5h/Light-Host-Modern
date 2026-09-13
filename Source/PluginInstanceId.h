#pragma once
#include <juce_core/juce_core.h>

// Session identity is independent of the plugin's format/vendor identity. During
// legacy migration the storage key stays unchanged so duplicate states survive.
using PluginInstanceId = juce::String;

inline PluginInstanceId ensurePluginInstanceId(juce::PropertySet& properties,
                                              const juce::String& exactLegacyKey,
                                              juce::StringArray& assigned)
{
    auto id = properties.getValue(exactLegacyKey);
    const bool valid = id.length() == 32 && id.containsOnly("0123456789abcdef")
        && !juce::Uuid(id).isNull() && !assigned.contains(id);
    if (!valid)
    {
        do { id = juce::Uuid().toString(); } while (assigned.contains(id));
        properties.setValue(exactLegacyKey, id);
    }
    assigned.add(id);
    return id;
}
