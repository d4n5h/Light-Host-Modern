"""Apply test isolation and measurement only to a verified reconstructed baseline.

Every adaptation is retained as a diff and hash manifest. The frozen artifacts
and the current product sources are untouched. Build/run remains a separate step.
"""
import argparse
import difflib
import json
from pathlib import Path
from PrepareComparisonBaseline import REPO, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy', type=Path, required=True)
    args = parser.parse_args()
    copy = args.copy.resolve()
    source = copy / 'src'
    if not copy.is_relative_to((REPO / 'out').resolve()):
        raise RuntimeError('An isolated comparison copy under workspace out/ is required.')
    if (copy / 'adaptations.json').exists():
        raise RuntimeError('This copy was already adapted; refusing to apply twice.')
    reconstruction = json.loads((copy / 'reconstruction.json').read_text())
    for entry in reconstruction['sourceFiles']:
        if digest(source / entry['path']) != entry['sha256']:
            raise RuntimeError(f'The reconstruction changed: {entry["path"]}')
    original = {str(p.relative_to(source)).replace('\\', '/'): p.read_text(encoding='utf-8-sig')
                for p in source.rglob('*') if p.is_file() and p.suffix in ('.h', '.cpp', '.txt')}
    changed = dict(original)

    def replace(file, before, after):
        if changed[file].count(before) != 1:
            raise RuntimeError(f'The reviewed source pattern is not unique: {file}: {before[:70]}')
        changed[file] = changed[file].replace(before, after)

    # Complete the partial profile isolation already captured in preparation.
    replace('Source/DebugLog.cpp', '\t\tlogsDirectory.createDirectory();',
            '\t\tif (lightHostModern::RuntimeProfile::current().test)\n'
            '\t\t\tlogsDirectory = File((lightHostModern::RuntimeProfile::current().directory / L"Logs").wstring().c_str());\n'
            '\t\tlogsDirectory.createDirectory();')
    replace('Source/IconMenu.cpp', '#include "DebugLog.h"', '#include "DebugLog.h"\n#include "RuntimeProfile.h"')
    replace('Source/IconMenu.cpp', '\t\treturn FindWindowW(nullptr, L"LightHostModern");',
            '\t\t// The harness owns and closes the exact baseline UI process.\n'
            '\t\tif (lightHostModern::RuntimeProfile::current().test) return nullptr;\n'
            '\t\treturn FindWindowW(nullptr, L"LightHostModern");')
    replace('Source/IconMenu.cpp', '\tlightHostModernLog("Open New UI clicked.");',
            '\tif (lightHostModern::RuntimeProfile::current().test) return; // Launch only through the isolated harness.\n'
            '\tlightHostModernLog("Open New UI clicked.");')
    replace('Source/HostIpcServer.cpp', '#include "DebugLog.h"', '#include "DebugLog.h"\n#include "RuntimeProfile.h"')
    for function in ('isStartWithWindowsEnabled()', 'setStartWithWindows(bool enabled)'):
        replace('Source/HostIpcServer.cpp', '\tbool '+function+'\n\t{',
                '\tbool '+function+'\n\t{\n\t\tif (lightHostModern::RuntimeProfile::current().test) return false;')
    replace('Source/HostStartup.cpp', '#include "RuntimeProfile.h"', '#include "RuntimeProfile.h"\n#include "RealtimeAudit.h"')
    replace('Source/HostStartup.cpp', '        profile.createDirectories();',
            '        if (!profile.test) throw std::runtime_error("This comparison binary requires a test profile");\n'
            '        profile.createDirectories();\n        installRealtimeAllocationAudit();')
    replace('Source/AudioEngine.cpp', '#include "AudioEngine.h"', '#include "AudioEngine.h"\n#include "RuntimeProfile.h"')
    replace('Source/AudioEngine.cpp', '\tconst auto startupRecoveryConfig = getAudioRecoveryConfiguration();',
            '\tif (!lightHostModern::RuntimeProfile::current().noAudio)\n\t{\n\tconst auto startupRecoveryConfig = getAudioRecoveryConfiguration();')
    replace('Source/AudioEngine.cpp', '\tplayer.setProcessor(&hostProcessor);', '\t}\n\tplayer.setProcessor(&hostProcessor);')
    replace('Source/AudioEngine.cpp', '\tif (timerId == audioWatchdogTimerId)\n\t{',
            '\tif (timerId == audioWatchdogTimerId)\n\t{\n'
            '\t\tif (lightHostModern::RuntimeProfile::current().test && !comparisonAudioSelected) return;')

    # Same clock, histogram, audit implementation and forwarding timer as the
    # final build. Original player, chain, buses, metering and UI remain intact.
    replace('Source/AudioEngine.h', '#include "RealtimeHostProcessor.h"',
            '#include "RealtimeHostProcessor.h"\n#include "ComparisonBridge.h"')
    replace('Source/AudioEngine.h', '\tAudioProcessorPlayer player;',
            '\tComparisonAudioPlayer player;\n\tbool comparisonAudioSelected = false;')
    replace('Source/AudioEngine.h', '\tAudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }',
            '\tString configureComparison(const String&);\n\tString comparisonMeasurement();\n'
            '\tAudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }')
    replace('Source/RealtimeHostProcessor.h', '\tstatic constexpr int maxScratchChannels = 256;',
            '\tstd::atomic<uint64> comparisonBlocks {0}, comparisonSamples {0}, comparisonMidi {0};\n'
            '\tstatic constexpr int maxScratchChannels = 256;')
    replace('Source/RealtimeHostProcessor.cpp', '    lastInputLevel.store(calculatePeakLevel(buffer), std::memory_order_relaxed);',
            '    comparisonBlocks.fetch_add(1, std::memory_order_relaxed);\n'
            '    comparisonSamples.fetch_add(static_cast<uint64>(buffer.getNumSamples()), std::memory_order_relaxed);\n'
            '    comparisonMidi.fetch_add(static_cast<uint64>(midiMessages.getNumEvents()), std::memory_order_relaxed);\n'
            '    lastInputLevel.store(calculatePeakLevel(buffer), std::memory_order_relaxed);')
    replace('Source/IpcSchema.h', '        {"snapshot", A::none}',
            '        {"comparison-configure", A::text}, {"comparison-measurement", A::none},\n        {"snapshot", A::none}')
    replace('Source/HostIpcServer.cpp', '    if (command == "telemetry") return buildTelemetry();',
            '    if (command == "comparison-configure") return engine.configureComparison(args[0].toString());\n'
            '    if (command == "comparison-measurement") return engine.comparisonMeasurement();\n'
            '    if (command == "telemetry") return buildTelemetry();')
    changed['Source/AudioEngine.cpp'] += r'''

String AudioEngine::configureComparison(const String& configuration)
{
    if (!lightHostModern::RuntimeProfile::current().test || deviceManager.getCurrentAudioDevice())
        throw std::runtime_error("Arm only a stopped comparison test profile");
    const auto value = JSON::parse(configuration);
    const auto output = value["output"].toString();
    const int warmup = value["warmup"], duration = value["duration"], blockSize = value["bufferSize"];
    const double sampleRate = value["sampleRate"];
    if (output.isEmpty() || warmup < 0 || warmup > 300 || duration < 1 || duration > 1800
        || sampleRate != 48000 || blockSize < 1 || blockSize > 8192)
        throw std::runtime_error("Invalid explicit shared-WASAPI comparison setup");
    player.measurement.configure(static_cast<uint64>(Time::getHighResolutionTicksPerSecond()),
        static_cast<unsigned>(warmup), static_cast<unsigned>(duration));
    setGlobalMuted(true);
    XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "Windows Audio");
    setup.setAttribute("audioInputDeviceName", ""); setup.setAttribute("audioOutputDeviceName", output);
    setup.setAttribute("audioDeviceInChans", "0"); setup.setAttribute("audioDeviceOutChans", "11");
    setup.setAttribute("audioDeviceRate", sampleRate); setup.setAttribute("audioDeviceBufferSize", blockSize);
    const auto error = deviceManager.initialise(0, 2, &setup, false);
    auto* device = deviceManager.getCurrentAudioDevice();
    if (error.isNotEmpty() || !device || !device->isOpen() || !device->isPlaying()
        || device->getCurrentSampleRate() != sampleRate || device->getCurrentBufferSizeSamples() != blockSize
        || device->getActiveInputChannels().countNumberOfSetBits() != 0
        || device->getActiveOutputChannels().countNumberOfSetBits() != 2)
    {
        deviceManager.closeAudioDevice();
        throw std::runtime_error(("Comparison device did not open the exact configuration: " + error).toStdString());
    }
    comparisonAudioSelected = true;
    ++audioConfigVersion;
    return comparisonMeasurement();
}

String AudioEngine::comparisonMeasurement()
{
    if (!lightHostModern::RuntimeProfile::current().test) throw std::runtime_error("A comparison profile is required");
    auto result = comparisonMeasurementReply(player.measurement.snapshot());
    auto* data = result.getDynamicObject();
    data->setProperty("testProfile", String(lightHostModern::RuntimeProfile::current().name.c_str()));
    data->setProperty("processedBlocks", String(hostProcessor.comparisonBlocks.load()));
    data->setProperty("processedSamples", String(hostProcessor.comparisonSamples.load()));
    data->setProperty("midiInputEvents", String(hostProcessor.comparisonMidi.load()));
    auto* device = deviceManager.getCurrentAudioDevice();
    data->setProperty("driverAvailable", device && device->isOpen() && device->isPlaying());
    return JSON::toString(result, true);
}
'''
    replace('CMakeLists.txt', '        Source/HostStartup.cpp',
            '        Source/RealtimeAllocationAudit.cpp\n        Source/HostStartup.cpp')
    # A comparison build never stages another executable over the frozen UI.
    staging = changed['CMakeLists.txt'].index('if(MSVC)\n    set(LIGHTHOST_WINUI_SOURCE_DIR')
    changed['CMakeLists.txt'] = changed['CMakeLists.txt'][:staging] + '# The harness launches the verified frozen UI binary.\n'
    for name in ('CallbackMeasurement.h', 'RealtimeAudit.h', 'RealtimeAllocationAudit.cpp'):
        changed['Source/'+name] = (REPO / 'Source' / name).read_text(encoding='utf-8-sig')
    changed['Source/ComparisonBridge.h'] = (REPO / 'Tests/ComparisonBridge.h').read_text(encoding='utf-8-sig')
    diff, files = [], []
    for name, text in changed.items():
        if text == original.get(name):
            continue
        diff.extend(difflib.unified_diff(original.get(name, '').splitlines(keepends=True), text.splitlines(keepends=True),
                                       fromfile='a/'+name, tofile='b/'+name))
        (source / name).write_text(text, encoding='utf-8')
        files.append({'path': name, 'sha256': digest(source / name)})
    (copy / 'adaptations.patch').write_text(''.join(diff), encoding='utf-8')
    record = {'sourceDirectory': str(source), 'adaptedFiles': files, 'readyToRun': False,
              'reason': 'Build and isolated no-audio/same-device smoke verification are pending.',
              'productionVersion': '1.2.2', 'protocolVersion': 3,
              'audioImplementation': 'Frozen JUCE AudioProcessorPlayer and frozen RealtimeHostProcessor, with work counters only',
              'uiImplementation': 'Verified original frozen executable; child LOCALAPPDATA/TEMP/TMP must be isolated'}
    (copy / 'adaptations.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    print(json.dumps({'adaptedFiles': len(files), 'patch': str(copy / 'adaptations.patch'), 'readyToRun': False}))


if __name__ == '__main__':
    main()
