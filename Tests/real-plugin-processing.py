"""Opt-in processing/state/editor tests; fixtures stay inside ignored out/.

Each module runs in a separate process with a 60-second deadline. No devices or
system plugin locations are opened. --download only permits official sources.
"""
import argparse
import hashlib
import json
import os
import subprocess
import time
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FIXTURES = [
    ('dragonfly-reverb-3.2.10-win64.zip',
     'https://github.com/michaelwillis/dragonfly-reverb/releases/download/3.2.10/dragonfly-reverb-3.2.10-win64.zip',
     'ca5f35f9dcd9a33f06490d3c92f04061a1a798cb81257d5ca5851bdda0824c66'),
    ('PurestGain.zip', 'https://www.airwindows.com/wp-content/uploads/2016/11/PurestGain.zip',
     '0a79f7b3c2d35fe7e3819edb64f680d6a141495bc6e7bb4bbca77e494f8fcc6e')]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--match', default='')
    parser.add_argument('--exclude', action='append', default=[], help='Skip an exact module filename; excluded fixtures are recorded in the report')
    parser.add_argument('--generic-editor', action='store_true', help='Exercise the host generic editor; does not validate the native editor')
    parser.add_argument('--vmware-no-llvmpipe', action='store_true',
                        help='Set SVGA_ALLOW_LLVMPIPE=0 only in each test child; record the override separately')
    args = parser.parse_args()
    root = REPO / 'out/real-plugin-test'
    root.mkdir(parents=True, exist_ok=True)
    destination = root / 'Processing áudio 日本'
    destination.mkdir(exist_ok=True)
    for filename, source, digest in FIXTURES:
        archive = root / filename
        if not archive.exists():
            if not args.download:
                raise RuntimeError(f'Missing {filename}; use --download to fetch the official fixture')
            request = urllib.request.Request(source, headers={'User-Agent': 'LightHost-plugin-validation/1.2.2'})
            with urllib.request.urlopen(request, timeout=30) as response:
                archive.write_bytes(response.read())
        if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Fixture digest changed: {filename}')
        with zipfile.ZipFile(archive) as package:
            for entry in package.infolist():
                if not (destination / entry.filename).resolve().is_relative_to(destination.resolve()):
                    raise RuntimeError('Unsafe archive member')
            if filename == 'PurestGain.zip':
                package.extract('PurestGain64.dll', destination)
            else:
                package.extractall(destination)
    modules = list((destination / 'dragonfly-reverb-3.2.10').glob('Dragonfly*-vst.dll'))
    modules += list((destination / 'dragonfly-reverb-3.2.10').glob('Dragonfly*.vst3'))
    modules += [destination / 'PurestGain64.dll']
    if len(modules) != 9:
        raise RuntimeError('Expected eight Dragonfly modules and one Airwindows module')
    excluded = [str(module) for module in modules if module.name in args.exclude]
    modules = [module for module in modules if args.match in module.name and module.name not in args.exclude]
    if not modules:
        raise RuntimeError('No fixture matched')
    executable = REPO / 'out/build/windows-vs2022/Release/LightHostRealPluginTests.exe'
    environment = os.environ.copy()
    overrides = {}
    if args.vmware_no_llvmpipe:
        overrides['SVGA_ALLOW_LLVMPIPE'] = '0'
        environment.update(overrides)
    suffix = '-vmware-no-llvmpipe' if overrides else ''
    if args.generic_editor:
        suffix += '-generic-editor'
    if args.match:
        suffix += '-selected'
    if args.exclude:
        suffix += '-filtered'
    results = []
    for i, module in enumerate(modules):
        output = root / f'processing{suffix}-{module.name}.json'
        if output.exists():
            output.unlink()
        started = time.monotonic()
        try:
            command = [str(executable), 'VST3' if module.suffix == '.vst3' else 'VST', str(module), str(output)]
            if args.generic_editor:
                command.append('generic')
            run = subprocess.run(command,
                                 capture_output=True, encoding='utf-8', errors='replace', timeout=60, env=environment)
            row = json.loads(output.read_text(encoding='utf-8-sig')) if output.exists() else {'module': str(module), 'status': 'failed'}
            row.update(exitCode=run.returncode, seconds=round(time.monotonic() - started, 3), stdout=run.stdout, stderr=run.stderr)
            if run.returncode != 0:
                row['status'] = 'failed'
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value if isinstance(value, str) else (value or b'').decode('utf-8', errors='replace')
            row = {'module': str(module), 'status': 'failed', 'error': '60-second process timeout',
                   'seconds': round(time.monotonic() - started, 3),
                   'stdout': decoded(error.stdout), 'stderr': decoded(error.stderr)}
        results.append(row)
        print(json.dumps(row, ensure_ascii=True), flush=True)
    report = {'fixtures': [{'file': f, 'source': u, 'sha256': s} for f, u, s in FIXTURES],
              'runnerSha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'childEnvironmentOverrides': overrides, 'excludedModules': excluded, 'results': results}
    report_path = root / f'processing{suffix}-results.json'
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    if any(row['status'] != 'passed' for row in results):
        raise RuntimeError(f'Real plugin tests failed; see {report_path}')

if __name__ == '__main__':
    main()
