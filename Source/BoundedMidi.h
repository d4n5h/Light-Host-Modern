#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace lightHost
{
inline constexpr int midiCapacityBytes = 1024 * 1024;

// JUCE 8 MidiBuffer stores an int32 sample position and uint16 length for each
// event. Admission uses this overhead before calling JUCE, never after growth.
// An entire event is admitted or discarded; offsets remain sample-accurate.
inline juce::uint64 copyBoundedMidi(juce::MidiBuffer& destination, const juce::MidiBuffer& source,
    int firstSample, int sampleCount, int delta, int capacityBytes) noexcept
{
    juce::uint64 dropped = 0;
    capacityBytes = juce::jmin(capacityBytes, destination.data.getAllocatedCapacity());
    bool full = false;
    for (auto cursor = source.findNextSamplePosition(firstSample); cursor != source.cend(); ++cursor)
    {
        const auto event = *cursor;
        if (sampleCount >= 0 && static_cast<juce::int64>(event.samplePosition) >= static_cast<juce::int64>(firstSample) + sampleCount) break;
        const auto required = static_cast<juce::int64>(event.numBytes) + sizeof(juce::int32) + sizeof(juce::uint16);
        if (full || required > capacityBytes - destination.data.size()) { ++dropped; full = true; continue; }
        // Both inputs are already ordered, so append without JUCE's repeated
        // linear search for each insertion (large batches remain linear).
        juce::uint8 header[sizeof(juce::int32) + sizeof(juce::uint16)];
        juce::writeUnaligned<juce::int32>(header, event.samplePosition + delta);
        juce::writeUnaligned<juce::uint16>(header + sizeof(juce::int32), static_cast<juce::uint16>(event.numBytes));
        destination.data.addArray(header, static_cast<int>(sizeof(header)));
        destination.data.addArray(event.data, event.numBytes);
    }
    return dropped;
}
}
