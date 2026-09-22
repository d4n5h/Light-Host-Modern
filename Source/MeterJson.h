#pragma once
#include "AudioMeters.h"
#include <juce_core/juce_core.h>

namespace lightHostModern
{
inline juce::var measurementJson(const AudioMeasurement& measurement)
{
    auto* value = new juce::DynamicObject();
    value->setProperty("rms", measurement.rms);
    value->setProperty("peak", measurement.peak);
    value->setProperty("peakHold", measurement.peakHold);
    value->setProperty("clipped", measurement.clipped);
    return juce::var(value);
}
inline juce::var meterJson(const MeterSnapshot& snapshot, const juce::StringArray& labels,
                          const juce::BigInteger& activeChannels, bool active)
{
    auto* value = new juce::DynamicObject();
    auto measurements = snapshot;
    if (!active)
    {
        measurements.aggregate.rms = measurements.aggregate.peak = measurements.aggregate.peakHold = 0;
        for (auto& channel : measurements.channels) channel.rms = channel.peak = channel.peakHold = 0;
    }
    value->setProperty("aggregate", measurementJson(measurements.aggregate));
    value->setProperty("active", active);
    juce::Array<juce::var> channels;
    int physicalIndex = activeChannels.findNextSetBit(0);
    for (int index = 0; index < measurements.channelCount; ++index)
    {
        auto item = measurementJson(measurements.channels[static_cast<size_t>(index)]);
        auto* fields = item.getDynamicObject();
        fields->setProperty("id", index);
        fields->setProperty("deviceChannel", physicalIndex >= 0 ? juce::var(physicalIndex) : juce::var());
        fields->setProperty("label", physicalIndex >= 0 && physicalIndex < labels.size()
            ? labels[physicalIndex] : juce::String(index + 1));
        channels.add(item);
        if (physicalIndex >= 0) physicalIndex = activeChannels.findNextSetBit(physicalIndex + 1);
    }
    value->setProperty("channels", channels);
    return juce::var(value);
}
}
