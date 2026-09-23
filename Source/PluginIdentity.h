#pragma once
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

namespace lightHostModern
{
// Comparison only: never replace persisted IDs or aliases with a path hash.
inline bool samePluginClass(const juce::PluginDescription& expected, const juce::PluginDescription& actual)
{
    const auto id = [](const auto& d) { return d.uniqueId != 0 ? d.uniqueId : d.deprecatedUid; };
    if (expected.pluginFormatName != actual.pluginFormatName || id(expected) == 0 || id(expected) != id(actual)) return false;
    if (expected.vst3ClassId.isNotEmpty() && expected.vst3ClassId != actual.vst3ClassId) return false;
    if (expected.fileOrIdentifier == actual.fileOrIdentifier) return true;
    if (!juce::File::isAbsolutePath(expected.fileOrIdentifier) || !juce::File::isAbsolutePath(actual.fileOrIdentifier)) return false;
    const juce::File requested(expected.fileOrIdentifier), loaded(actual.fileOrIdentifier);
    return requested == loaded || (expected.pluginFormatName == "VST3" && requested.hasFileExtension("vst3")
        && loaded.hasFileExtension("vst3") && (loaded.isAChildOf(requested)||requested.isAChildOf(loaded)));
}
}
