#pragma once
#include "AudioDeviceState.h"
#include <cmath>

struct AudioDeviceSelection
{
    String backend;
    AudioDeviceManager::AudioDeviceSetup setup;
    uint64 expectedGeneration = 0;
};

namespace lightHostModern::audioSelection
{
inline var setupJson(const String& backend, const AudioDeviceManager::AudioDeviceSetup& setup)
{
    auto* object = new DynamicObject;
    object->setProperty("backend", backend);
    object->setProperty("input", setup.inputDeviceName); object->setProperty("output", setup.outputDeviceName);
    object->setProperty("inputMask", setup.inputChannels.toString(2)); object->setProperty("outputMask", setup.outputChannels.toString(2));
    object->setProperty("defaultInputChannels", setup.useDefaultInputChannels); object->setProperty("defaultOutputChannels", setup.useDefaultOutputChannels);
    object->setProperty("sampleRate", setup.sampleRate); object->setProperty("bufferSize", setup.bufferSize);
    return var(object);
}
inline bool generation(const var& value, uint64& result)
{
    if ((value.isInt() || value.isInt64()) && static_cast<int64>(value) > 0) { result = static_cast<uint64>(static_cast<int64>(value)); return true; }
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.isEmpty() || text.length() > 19 || !text.containsOnly("0123456789") || text.startsWithChar('0')) return false;
    const auto parsed = text.getLargeIntValue();
    if (parsed <= 0 || String(parsed) != text) return false;
    result = static_cast<uint64>(parsed); return true;
}
inline bool names(const var& object)
{
    if (!object.isObject()) return false;
    for (const auto* name : {"backend", "input", "output"})
        if (!object[name].isString() || object[name].toString().length() > 4096) return false;
    return true;
}
inline bool parse(const var& object, AudioDeviceSelection& result)
{
    if (!names(object) || !generation(object["expectedGeneration"], result.expectedGeneration)) return false;
    if (!object["defaultInputChannels"].isBool() || !object["defaultOutputChannels"].isBool()) return false;
    for (const auto* key : {"inputMask", "outputMask"})
        if (!object[key].isString() || object[key].toString().length() > 256 || !object[key].toString().containsOnly("01")) return false;
    const auto rate = object["sampleRate"], buffer = object["bufferSize"];
    if (!(rate.isDouble() || rate.isInt() || rate.isInt64()) || !std::isfinite(static_cast<double>(rate))
        || static_cast<double>(rate) < 0 || static_cast<double>(rate) > 768000) return false;
    if (!(buffer.isInt() || buffer.isInt64()) || static_cast<int64>(buffer) < 0 || static_cast<int64>(buffer) > 1048576) return false;
    result.backend = object["backend"].toString();
    auto& setup = result.setup;
    setup.inputDeviceName = object["input"].toString(); setup.outputDeviceName = object["output"].toString();
    setup.inputChannels.parseString(object["inputMask"].toString(), 2); setup.outputChannels.parseString(object["outputMask"].toString(), 2);
    setup.useDefaultInputChannels = static_cast<bool>(object["defaultInputChannels"]);
    setup.useDefaultOutputChannels = static_cast<bool>(object["defaultOutputChannels"]);
    setup.sampleRate = static_cast<double>(rate); setup.bufferSize = static_cast<int>(buffer);
    return true;
}
}
