"""Reconstruct the frozen preparation source without changing the frozen build.

The output is deliberately not executable until the separately reviewed profile
and measurement adaptations have been applied. No production preferences, audio
devices, registry keys, or application windows are touched by this script.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FROZEN = REPO / 'out/baselines/completion-20260908-release'
REVISION = '93e7dd1097ea2730bb91ad73741972354c5c1d88'
HOST_SHA256 = '4bae3d86ef71ae92e21b35b0afadc27102e5837cf1fc11ecb91af433c5c53c10'


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def run(*arguments, cwd=REPO):
    environment = os.environ.copy()
    if cwd != REPO:
        # Otherwise git finds the parent workspace and silently skips patch
        # paths outside this ignored subdirectory instead of applying them.
        environment['GIT_CEILING_DIRECTORIES'] = str(cwd.parent)
    subprocess.run(['rtk', 'proxy', *map(str, arguments)], cwd=cwd, env=environment, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=REPO / 'out/bcmp')
    args = parser.parse_args()
    destination = args.output.resolve()
    if not destination.is_relative_to((REPO / 'out').resolve()) or destination == (REPO / 'out').resolve():
        raise RuntimeError('The comparison copy must be a new directory under this workspace out/.')
    if destination.exists():
        raise RuntimeError('Refusing to overwrite an existing comparison copy.')
    if digest(FROZEN / 'host/Light Host Modern.exe') != HOST_SHA256:
        raise RuntimeError('The frozen host no longer matches its recorded baseline digest.')
    manifest = json.loads((FROZEN / 'manifest.json').read_text(encoding='utf-8-sig'))
    for entry in manifest['files']:
        source = (FROZEN / entry['path']).resolve()
        if not source.is_relative_to(FROZEN.resolve()) or digest(source) != entry['sha256'].lower():
            raise RuntimeError(f'Frozen artifact verification failed: {entry["path"]}')

    destination.mkdir(parents=True)
    source_root = destination / 'src'
    source_root.mkdir()
    archive = destination / 'original-revision.tar'
    run('git', 'archive', '--format=tar', f'--output={archive}', REVISION)
    with tarfile.open(archive) as contents:
        contents.extractall(source_root, filter='data')
    # git apply does not require a checkout; this leaves the user's tree intact.
    run('git', 'apply', '--check', FROZEN / 'working-tree.patch', cwd=source_root)
    run('git', 'apply', FROZEN / 'working-tree.patch', cwd=source_root)
    for name in ('Source', 'Tests', 'docs'):
        shutil.copytree(FROZEN / 'source' / name, source_root / name, dirs_exist_ok=True)
    if 'Source/PluginScanController.cpp' not in (source_root / 'CMakeLists.txt').read_text():
        raise RuntimeError('The frozen build-system patch was not applied.')

    source_files = []
    for path in sorted(source_root.rglob('*')):
        if path.is_file():
            source_files.append({'path': path.relative_to(source_root).as_posix(), 'sha256': digest(path)})
    record = {
        'frozenDirectory': str(FROZEN), 'frozenHostSha256': HOST_SHA256,
        'originalRevision': REVISION, 'patchSha256': digest(FROZEN / 'working-tree.patch'),
        'sourceDirectory': str(source_root), 'sourceFiles': source_files,
        'readyToRun': False,
        'reason': 'Profile isolation and identical measurement instrumentation must be applied and reviewed first.',
    }
    (destination / 'reconstruction.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    print(json.dumps({'sourceDirectory': str(source_root), 'verifiedFrozenArtifacts': len(manifest['files']),
                      'sourceFiles': len(source_files), 'readyToRun': False}))


if __name__ == '__main__':
    main()
