"""Isolated protocol-4 UI fixture. Never opens devices, plugins or user settings.

Start with --snapshot <captured read-only snapshot> --output <test directory>.
The ready.json file describes a separate test profile. Creating stop ends it.
Optional control.json can set inputPeak/outputPeak, emptyChain, bypassFirst,
errorFirst, scanActive or scanFailures. Only the isolated fixture state is affected.
"""
import argparse
import copy
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import threading
import time
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = 'redesign-' + uuid.uuid4().hex
    root = output / 'profiles'
    profile = root / name
    profile.mkdir(parents=True)
    identity = str(profile.resolve()).lower()
    key = 14695981039346656037
    for char in identity:
        key = ((key ^ ord(char)) * 1099511628211) & ((1 << 64) - 1)
    pipe = '\\\\.\\pipe\\LightHost-profile-' + str(key)
    (profile / 'ui-settings.ini').write_text(
        '[Appearance]\nThemeMode=Dark\nLayoutMode=Compact\nBackdropMode=1\n'
        '[Localization]\nLanguage=en-us\n', encoding='utf-16')
    state = json.loads(args.snapshot.read_text(encoding='utf-8-sig'))
    state.update(hostSession='ui-redesign-' + uuid.uuid4().hex, hostPid=os.getpid(),
                 hostExecutable='', chainVersion=1, pluginDbVersion=1, audioConfigVersion=1,
                 globalMuted=False, globalBypassed=False, diagnosticsEnabled=True)
    for plugin in state['knownPluginList']:
        plugin.setdefault('originalName', plugin['name'])
        plugin.setdefault('customName', '')
    state['appConfig']['closeBehavior'] = 'tray'
    original_chain = copy.deepcopy(state['activePlugins'])
    sequence = 1
    lock = threading.RLock()
    operations = {}
    snapshots = {}
    previous_control = {}
    scan_active = False
    scan_cancelled = False

    def snapshot():
        nonlocal sequence, previous_control
        control = {}
        try:
            control = json.loads((output / 'control.json').read_text(encoding='utf-8-sig'))
        except (OSError, ValueError):
            pass
        if control != previous_control:
            if control.get('emptyChain') != previous_control.get('emptyChain'):
                state['activePlugins'] = [] if control.get('emptyChain') else copy.deepcopy(original_chain)
                state['chainVersion'] += 1
            if state['activePlugins']:
                state['activePlugins'][0]['bypassed'] = bool(control.get('bypassFirst'))
                state['activePlugins'][0]['loading'] = 'failed' if control.get('errorFirst') else 'loaded'
                state['chainVersion'] += 1
            sequence += 1
            previous_control = control
        state['activePluginCount'] = len(state['activePlugins'])
        state['knownPlugins'] = len(state['knownPluginList'])
        for index, plugin in enumerate(state['activePlugins']):
            plugin['order'] = index + 1
        diagnostic = state['diagnostics']
        if state['diagnosticsEnabled']:
            diagnostic['processedBlocks'] += 1
            diagnostic['processedSamples'] += 512
        for direction, default in [('input', 0.25), ('output', 0.5)]:
            peak = control.get(direction + 'Peak', default)
            if direction == 'output' and state['globalMuted']:
                peak = 0
            diagnostic[direction + 'Level'] = peak
            meter = diagnostic['meters'][direction]
            for item in [meter['aggregate']] + meter['channels']:
                item.update(peak=peak, rms=peak * 0.707, peakHold=peak)
        return copy.deepcopy(state)

    def handle(request):
        nonlocal sequence, scan_active, scan_cancelled
        command = request['command']
        arguments = request.get('args', [])
        if command == 'events':
            time.sleep(0.4)
        if command == 'telemetry':
            time.sleep(previous_control.get('telemetryDelayMs', 0) / 1000)
        with lock:
            snap = snapshot()
            base = dict(version=4, id=request['id'], hostSession=state['hostSession'], status='ok')
            if command == 'meter-levels':
                meters = snap['diagnostics']['meters']
                return dict(base, inputPeak=meters['input']['aggregate']['peak'], outputPeak=meters['output']['aggregate']['peak'])
            if command in ('hello', 'snapshot', 'telemetry', 'snapshot-manifest'):
                base['status'] = snap['status']
            if command in ('hello', 'snapshot', 'telemetry'):
                return dict(snap, **base)
            if command == 'snapshot-manifest':
                sid = uuid.uuid4().hex
                snapshots.clear()
                snapshots[sid] = snap
                manifest = {k: v for k, v in snap.items() if k not in ('activePlugins', 'knownPluginList')}
                return dict(manifest, **base, snapshotId=sid, eventSequence=sequence,
                            collections={key: len(snap[key]) for key in ('activePlugins', 'knownPluginList')})
            if command == 'snapshot-page':
                options = arguments[0]
                stored = snapshots[options['snapshotId']]
                items = stored[options['collection']]
                offset = int(options['offset'])
                return dict(base, snapshotId=options['snapshotId'], total=len(items), offset=offset,
                            items=items[offset:offset + min(100, int(options['limit']))])
            if command == 'events':
                after = arguments[0].get('afterSequence', 0)
                return dict(base, sequence=sequence, resyncRequired=False,
                            changes={'chain': state['chainVersion']} if after < sequence else {})
            if command == 'plugin-scan-status':
                active = previous_control.get('scanActive', scan_active)
                failures = int(previous_control.get('scanFailures', 0))
                return dict(base, active=active, cancelled=previous_control.get('scanCancelled', scan_cancelled), completed=3 if active else (16 if failures else 0),
                            total=16 if active or failures else 0, cached=2 if active else 0, failureCount=failures,
                            currentFile='C:\\Test plugins\\Example.vst3' if active else '',
                            scanId='fixture-scan', revision=1, failures=[])
            if command == 'plugin-scan-failures':
                total = int(previous_control.get('scanFailures', 0))
                options = arguments[0]
                offset = int(options['offset'])
                paths = [r'C:\Program Files (x86)\Common Files\VST3',
                         r'C:\Users\Example User\AppData\Local\Programs\Common\VST3',
                         r'Z:\Studio plugins\Very long folder name for testing readable paths\Áudio\Missing.vst3']
                failures = [dict(id=f'failure-{index}', path=paths[index] if index < len(paths) else f'C:\\Test plugins\\Failed{index}.vst3',
                                 format='VST3', reason=['missing', 'access_denied', 'network_unavailable', 'crash'][index % 4], attempt=1)
                            for index in range(offset, min(total, offset + int(options['limit'])))]
                return dict(base, total=total, failures=failures, scanId='fixture-scan', revision=1)
            if command == 'enabled-audio-choices':
                choices = ['Windows Audio|input|' + value for value in [
                    'Microphone (High Definition Audio Device)',
                    'Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)',
                    'Voicemeeter Out B2 (VB-Audio Voicemeeter VAIO)',
                    'Voicemeeter Out B3 (VB-Audio Voicemeeter VAIO)']]
                choices += ['Windows Audio|output|' + value for value in [
                    'Speakers (High Definition Audio Device)',
                    'Voicemeeter AUX Input (VB-Audio Voicemeeter VAIO)',
                    'Voicemeeter Input (VB-Audio Voicemeeter VAIO)',
                    'Voicemeeter VAIO3 Input (VB-Audio Voicemeeter VAIO)']]
                choices += ['ASIO|device|' + value for value in state['audioConfig']['inputDeviceNames']]
                return dict(base, allAudioBackendNames=['Windows Audio', 'ASIO'],
                            allAudioBackendEnabled=[True, True], allAudioDeviceChoices=choices,
                            allAudioDeviceChoiceEnabled=[True] * len(choices))
            if command == 'operation-status':
                return operations[arguments[0]] | {'id': request['id']}
            if command in ('known-plugin-details', 'instance-details'):
                running = command == 'instance-details'
                plugin = next(p for p in state['activePlugins' if running else 'knownPluginList']
                              if p['instanceId' if running else 'knownId'] == arguments[0])
                return dict(plugin, **base, name=plugin['originalName'], availability='verifiedAtLastScan',
                            identity='fixture-original-identity', verifiedMetadata='verified', declaredMetadata='unavailable', buses=[])
            if command in ('set-global-mute', 'set-global-bypass'):
                state['globalMuted' if command.endswith('mute') else 'globalBypassed'] = bool(arguments[0])
            elif command == 'set-diagnostics-enabled':
                state['diagnosticsEnabled'] = bool(arguments[0])
            elif command == 'add-known-plugin':
                plugin = next(p for p in state['knownPluginList'] if p['knownId'] == arguments[0])
                state['activePlugins'].append(dict(plugin, instanceId=uuid.uuid4().hex,
                                                   loading='loaded', error='', bypassed=False))
            elif command == 'remove-plugin':
                state['activePlugins'] = [p for p in state['activePlugins'] if p['instanceId'] != arguments[0]]
            elif command in ('rename-plugin', 'rename-known-plugin'):
                running = command == 'rename-plugin'
                plugin = next(p for p in state['activePlugins' if running else 'knownPluginList']
                              if p['instanceId' if running else 'knownId'] == arguments[0])
                plugin['customName'] = arguments[1].strip() if arguments[1].strip() != plugin['originalName'] else ''
                plugin['name'] = plugin['customName'] or plugin['originalName']
                if not running:
                    state['pluginDbVersion'] += 1
            elif command == 'scan-plugin-path':
                scan_active = True
                scan_cancelled = False
            elif command == 'cancel-plugin-scan':
                scan_active = False
                scan_cancelled = True
            elif command in ('retry-plugin-scan', 'retry-plugin-scan-selection'):
                scan_active = True
                scan_cancelled = False
            elif command == 'clear-known-plugins':
                state['activePlugins'].clear()
                state['knownPluginList'].clear()
                state['pluginDbVersion'] += 1
            sequence += 1
            state['chainVersion'] += 1
            operation = dict(base, status='operation', operationId=request['id'],
                             operationState='completed', result={'status': 'ok'})
            operations[request['id']] = operation
            return operation

    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.CreateNamedPipeW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.LPVOID]
    kernel.CreateNamedPipeW.restype = w.HANDLE
    kernel.ConnectNamedPipe.argtypes = [w.HANDLE, w.LPVOID]
    kernel.ReadFile.argtypes = [w.HANDLE, w.LPVOID, w.DWORD, c.POINTER(w.DWORD), w.LPVOID]
    kernel.WriteFile.argtypes = [w.HANDLE, w.LPCVOID, w.DWORD, c.POINTER(w.DWORD), w.LPVOID]
    kernel.DisconnectNamedPipe.argtypes = [w.HANDLE]
    kernel.CloseHandle.argtypes = [w.HANDLE]

    def serve(endpoint):
        while True:
            handle_pipe = kernel.CreateNamedPipeW(endpoint, 3, 6, 2, 4 * 1024 * 1024, 4 * 1024 * 1024, 5000, None)
            if handle_pipe == c.c_void_p(-1).value:
                raise c.WinError(c.get_last_error())
            try:
                if not kernel.ConnectNamedPipe(handle_pipe, None) and c.get_last_error() != 535:
                    continue
                buffer = c.create_string_buffer(4 * 1024 * 1024)
                count = w.DWORD()
                if not kernel.ReadFile(handle_pipe, buffer, len(buffer), c.byref(count), None) or not count.value:
                    continue
                request = json.loads(buffer.raw[:count.value])
                # Command and event endpoints must not interleave append writes.
                with lock:
                    with (output / 'requests.jsonl').open('a', encoding='utf-8') as log:
                        log.write(json.dumps(dict(time=time.time(), **request)) + '\n')
                response = handle(request)
                payload = json.dumps(response, ensure_ascii=False).encode('utf-8')
                kernel.WriteFile(handle_pipe, payload, len(payload), c.byref(count), None)
                kernel.ReadFile(handle_pipe, buffer, len(buffer), c.byref(count), None)
            except Exception as error:
                with (output / 'errors.log').open('a', encoding='utf-8') as log:
                    log.write(repr(error) + '\n')
            finally:
                kernel.DisconnectNamedPipe(handle_pipe)
                kernel.CloseHandle(handle_pipe)

    for endpoint in (pipe, pipe + '-events', pipe + '-meters'):
        threading.Thread(target=serve, args=(endpoint,), daemon=True).start()
    (output / 'ready.json').write_text(json.dumps(dict(hostPid=os.getpid(), name=name,
        root=str(root), profile=str(profile), pipe=pipe), indent=2), encoding='utf-8')
    while not (output / 'stop').exists():
        time.sleep(0.2)


if __name__ == '__main__':
    main()
