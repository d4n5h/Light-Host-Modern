#pragma once
#include "PluginInstances.h"
#include <stdexcept>

namespace lightHostModern
{
// Validate the instance returned by the format before passing it saved bytes.
// A replaced single-class VST2 DLL can instantiate despite a stale requested
// vendor ID. Metadata changes are harmless; a different class is not.
template<class Restore>
void restorePluginState(PluginInstanceRecord& record, const juce::PluginDescription& actual, Restore&& restore)
{
    const auto& expected = record.description;
    const auto classId = [](const juce::PluginDescription& value) { return value.uniqueId != 0 ? value.uniqueId : value.deprecatedUid; };
    bool sameModule = expected.fileOrIdentifier == actual.fileOrIdentifier;
    if (!sameModule && juce::File::isAbsolutePath(expected.fileOrIdentifier) && juce::File::isAbsolutePath(actual.fileOrIdentifier))
    {
        const juce::File requested(expected.fileOrIdentifier), loaded(actual.fileOrIdentifier);
        sameModule = requested == loaded || (expected.pluginFormatName == "VST3" && requested.hasFileExtension("vst3")
            && loaded.hasFileExtension("vst3") && loaded.isAChildOf(requested));
    }
    if (!sameModule || expected.pluginFormatName != actual.pluginFormatName || classId(expected) == 0 || classId(expected) != classId(actual))
        throw std::runtime_error("plugin_identity_mismatch");
    if (record.lastValidState.isEmpty()) return;
    juce::MemoryBlock binary;
    if (!binary.fromBase64Encoding(record.lastValidState)) throw std::runtime_error("invalid_saved_state");
    try { restore(binary.getData(), static_cast<int>(binary.getSize())); }
    catch (...)
    {
        record.stateCaptureAllowed = false;
        record.recoveryState = record.lastValidState;
        throw std::runtime_error("plugin_state_restore_failed");
    }
}

// The controller provides the appropriate plugin thread and callback exclusion.
// Assignment is last, so an exception (including encoding/allocation failure)
// cannot erase the previously captured, valid state.
template<class Capture>
bool capturePluginState(PluginInstanceRecord& record, Capture&& capture)
{
    try
    {
        juce::MemoryBlock binary;
        capture(binary);
        if (binary.getSize() > 192 * 1024 * 1024) return false;
        auto encoded = binary.toBase64Encoding();
        record.lastValidState = std::move(encoded);
        return true;
    }
    catch (...) { return false; }
}
}
