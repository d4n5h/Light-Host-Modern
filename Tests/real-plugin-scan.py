"""Opt-in real scanner regression, run from any directory after a Release build.

Downloads only with --download. Third-party binaries stay under ignored out/ and
are never installed, loaded into the audio host, or included in release staging.
"""
import argparse
import hashlib
import json
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

URL = "https://github.com/michaelwillis/dragonfly-reverb/releases/download/3.2.10/dragonfly-reverb-3.2.10-win64.zip"
# Reproducibility pin for the official release downloaded during validation.
# This is not a publisher signature or the application's authenticated updater.
SHA256 = "ca5f35f9dcd9a33f06490d3c92f04061a1a798cb81257d5ca5851bdda0824c66"
REPO = Path(__file__).resolve().parents[1]


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=REPO / "out/build/windows-vs2022")
    parser.add_argument("--formats", nargs="+", choices=("VST", "VST3"), default=["VST", "VST3"])
    args = parser.parse_args()
    build = args.build_dir.resolve()
    runner = build / "Release/LightHostScanControllerTests.exe"
    scanner = build / "LightHost_artefacts/Release/LightHostScanner.exe"
    if not runner.is_file() or not scanner.is_file():
        raise RuntimeError("Build scanner and controller tests in Release first")
    root = REPO / "out/real-plugin-test"
    root.mkdir(parents=True, exist_ok=True)
    archive = root / "dragonfly-reverb-3.2.10-win64.zip"
    if not archive.exists():
        if not args.download:
            raise RuntimeError("Fixture missing; pass --download to obtain the official pinned release")
        urllib.request.urlretrieve(URL, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SHA256:
        raise RuntimeError("Fixture checksum mismatch; refusing to extract or load it")
    # Exercise spaces and non-ASCII module paths, including the VST3 bundle path.
    destination = (root / "Audio \u00e1udio \u65e5\u672c").resolve()
    with zipfile.ZipFile(archive) as package:
        for member in package.infolist():
            if not (destination / member.filename).resolve().is_relative_to(destination):
                raise RuntimeError("Archive entry escapes the fixture directory")
        package.extractall(destination)
    results = []
    for format_name in args.formats:
        started = time.monotonic()
        result = subprocess.run([str(runner), "--real-scan", str(scanner), str(destination), format_name, "4"],
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=320)
        results.append({"format": format_name, "exit_code": result.returncode,
                        "seconds": round(time.monotonic() - started, 3),
                        "stdout": result.stdout, "stderr": result.stderr})
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
    report = {"source": URL, "fixture_sha256": digest,
              "scanner_sha256": hashlib.sha256(scanner.read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(runner.read_bytes()).hexdigest(),
              "fixture_directory": str(destination), "results": results}
    (root / "scan-results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if any(result["exit_code"] for result in results):
        raise RuntimeError("Real scanner validation failed; see out/real-plugin-test/scan-results.json")


if __name__ == "__main__":
    main()
