# The host must observe actual MidiBuffer storage after third-party processing.
# JUCE Array has no public capacity query in 8.0.13. Add a read-only accessor to
# the build-owned copy; no layout, allocation policy or DSP behavior changes.
set(LIGHTHOST_JUCE_BUILD_ROOT "${CMAKE_BINARY_DIR}")
cmake_path(IS_PREFIX LIGHTHOST_JUCE_BUILD_ROOT "${juce_SOURCE_DIR}" NORMALIZE LIGHTHOST_OWNS_JUCE)
if(NOT LIGHTHOST_OWNS_JUCE)
    message(FATAL_ERROR "Refusing to patch a JUCE checkout outside the build directory")
endif()

# Allow the user's diagnostics setting to stop JUCE's callback clock sampling,
# not just hide the resulting DSP-load value. The callback lock also serializes
# resets with a timer that was already admitted.
set(LIGHTHOST_DEVICE_HEADER "${juce_SOURCE_DIR}/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.h")
set(LIGHTHOST_DEVICE_SOURCE "${juce_SOURCE_DIR}/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")
file(READ "${LIGHTHOST_DEVICE_HEADER}" LIGHTHOST_DEVICE_HEADER_CONTENT)
if(NOT LIGHTHOST_DEVICE_HEADER_CONTENT MATCHES "setDiagnosticsEnabled")
    if(NOT LIGHTHOST_DEVICE_HEADER_CONTENT MATCHES "double getCpuUsage\\(\\) const;" OR
       NOT LIGHTHOST_DEVICE_HEADER_CONTENT MATCHES "AudioProcessLoadMeasurer loadMeasurer;")
        message(FATAL_ERROR "JUCE device manager changed; review diagnostics opt-out")
    endif()
    string(REPLACE "double getCpuUsage() const;"
        "double getCpuUsage() const;\n    void setDiagnosticsEnabled (bool enabled)\n    {\n        const ScopedLock lock (audioCallbackLock);\n        diagnosticsEnabled = enabled;\n        loadMeasurer.reset();\n        if (currentAudioDevice != nullptr)\n            loadMeasurer.reset (currentAudioDevice->getCurrentSampleRate(), currentAudioDevice->getCurrentBufferSizeSamples());\n    }"
        LIGHTHOST_DEVICE_HEADER_CONTENT "${LIGHTHOST_DEVICE_HEADER_CONTENT}")
    string(REPLACE "AudioProcessLoadMeasurer loadMeasurer;" "AudioProcessLoadMeasurer loadMeasurer;\n    bool diagnosticsEnabled = true;"
        LIGHTHOST_DEVICE_HEADER_CONTENT "${LIGHTHOST_DEVICE_HEADER_CONTENT}")
    file(WRITE "${LIGHTHOST_DEVICE_HEADER}" "${LIGHTHOST_DEVICE_HEADER_CONTENT}")
endif()
file(READ "${LIGHTHOST_DEVICE_SOURCE}" LIGHTHOST_DEVICE_SOURCE_CONTENT)
if(NOT LIGHTHOST_DEVICE_SOURCE_CONTENT MATCHES "if \\(diagnosticsEnabled\\) timer.emplace")
    set(LIGHTHOST_DEVICE_TIMER "AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);")
    string(FIND "${LIGHTHOST_DEVICE_SOURCE_CONTENT}" "${LIGHTHOST_DEVICE_TIMER}" LIGHTHOST_TIMER_POSITION)
    if(LIGHTHOST_TIMER_POSITION EQUAL -1)
        message(FATAL_ERROR "JUCE callback timer changed; review diagnostics opt-out")
    endif()
    string(REPLACE "${LIGHTHOST_DEVICE_TIMER}"
        "std::optional<AudioProcessLoadMeasurer::ScopedTimer> timer;\n        if (diagnosticsEnabled) timer.emplace (loadMeasurer, numSamples);"
        LIGHTHOST_DEVICE_SOURCE_CONTENT "${LIGHTHOST_DEVICE_SOURCE_CONTENT}")
    file(WRITE "${LIGHTHOST_DEVICE_SOURCE}" "${LIGHTHOST_DEVICE_SOURCE_CONTENT}")
endif()
set(LIGHTHOST_ARRAY_HEADER "${juce_SOURCE_DIR}/modules/juce_core/containers/juce_Array.h")
file(READ "${LIGHTHOST_ARRAY_HEADER}" LIGHTHOST_ARRAY_CONTENT)
if(NOT LIGHTHOST_ARRAY_CONTENT MATCHES "getAllocatedCapacity")
    set(LIGHTHOST_ARRAY_ANCHOR "    /** Increases the array's internal storage to hold a minimum number of elements.")
    string(FIND "${LIGHTHOST_ARRAY_CONTENT}" "${LIGHTHOST_ARRAY_ANCHOR}" LIGHTHOST_ARRAY_POSITION)
    if(LIGHTHOST_ARRAY_POSITION EQUAL -1)
        message(FATAL_ERROR "JUCE Array changed; review the bounded MIDI capacity adapter before building")
    endif()
    string(REPLACE "${LIGHTHOST_ARRAY_ANCHOR}"
        "    /** Light Host: read-only capacity query for allocation-free MIDI admission. */\n    int getAllocatedCapacity() const noexcept\n    {\n        const ScopedLockType lock (getLock());\n        return values.capacity();\n    }\n\n${LIGHTHOST_ARRAY_ANCHOR}"
        LIGHTHOST_ARRAY_CONTENT "${LIGHTHOST_ARRAY_CONTENT}")
    file(WRITE "${LIGHTHOST_ARRAY_HEADER}" "${LIGHTHOST_ARRAY_CONTENT}")
endif()
