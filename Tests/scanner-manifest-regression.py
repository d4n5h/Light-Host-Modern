"""Opt-in VST3 manifest regression. Copies a supplied bundle; never installs it.

Only scanner children load the plugin. Use the pinned Dragonfly
Room fixture documented in docs/real-plugin-validation.md.
"""
import argparse
import copy
import json
import shutil
import subprocess
import time
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scanner", required=True, type=Path)
    parser.add_argument("--module", required=True, type=Path)
    args = parser.parse_args()
    scanner, source = args.scanner.resolve(), args.module.resolve()
    if not scanner.is_file() or not source.is_dir() or source.suffix.lower() != ".vst3":
        raise RuntimeError("Provide a built scanner and an existing VST3 bundle")
    root = Path(__file__).resolve().parents[1] / "out/manifest-regression" / uuid.uuid4().hex
    module = root / source.name
    shutil.copytree(source, module)
    metadata = module / "Contents/Resources/moduleinfo.json"
    # All edits are confined to the newly created private fixture copy.
    for relative in ("Contents/moduleinfo.json", "Contents/Resources/moduleinfo.json"):
        (module / relative).unlink(missing_ok=True)
    counter = 0

    def invoke(mode, fingerprint="", plugin=None, single=False):
        nonlocal counter
        folder = root / f"operation-{counter}"
        counter += 1
        folder.mkdir()
        request = ET.Element("SCAN", version="3", id=uuid.uuid4().hex, mode=mode,
                             format="VST3", path=str(module), fingerprint=fingerprint,
                             verifyAtEnd="1")
        if single:
            request.set("validateSingleClass", "1")
        if mode == "enumerate":
            ET.SubElement(request, "ROOT", path=str(module), index="0")
        if plugin is not None:
            request.append(copy.deepcopy(plugin))
        request_path, response_path = folder / "request.xml", folder / "response.xml"
        ET.ElementTree(request).write(request_path, encoding="utf-8", xml_declaration=True)
        started = time.monotonic()
        process = subprocess.Popen([str(scanner), str(request_path), str(response_path)],
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            code = process.wait(timeout=65)
        except subprocess.TimeoutExpired:
            # The controller's Job Object is covered by native tests. This
            # one-shot test owns and terminates its own child on timeout.
            process.kill()
            process.wait()
            raise
        if code:
            raise RuntimeError(f"{mode} failed: exit {code}")
        result = ET.parse(response_path).getroot()
        if result.get("id") != request.get("id") or result.get("version") != "3":
            raise RuntimeError("Scanner response identity mismatch")
        if mode == "enumerate":
            items = [item for batch in folder.glob("response.xml.batch-*.xml")
                     for item in ET.parse(batch).getroot() if item.tag == "CANDIDATE"]
            if len(items) != 1:
                raise RuntimeError("Expected exactly one module")
            return items[0].get("fingerprint")
        return result, time.monotonic() - started

    initial, _ = invoke("probe", invoke("enumerate"))
    descriptions = [entry.find("PLUGIN") for entry in initial]
    if not descriptions:
        raise RuntimeError("Factory returned no plugin classes")
    manifest = {"Name": module.stem, "Version": descriptions[0].get("version"),
                "Factory Info": {"Vendor": descriptions[0].get("manufacturer"), "URL": "", "E-Mail": "", "Flags": {}},
                "Classes": []}
    for plugin in descriptions:
        manifest["Classes"].append({
            "CID": uuid.UUID(bytes_le=bytes.fromhex(plugin.get("vst3ClassId"))).hex.upper(),
            "Category": "Audio Module Class", "Name": plugin.get("name"),
            "Vendor": plugin.get("manufacturer"), "Version": plugin.get("version"),
            "SDKVersion": "VST 3", "Class Flags": 0, "Cardinality": 2147483647, "Sub Categories": ["Fx"]})
    results = []
    for variant in ("missing", "valid", "invalid", "stale"):
        metadata.parent.mkdir(exist_ok=True, parents=True)
        if variant == "valid":
            metadata.write_text(json.dumps(manifest), encoding="utf-8")
        elif variant == "invalid":
            metadata.write_text("{invalid", encoding="utf-8")
        elif variant == "stale":
            stale = copy.deepcopy(manifest)
            stale["Classes"][0]["CID"] = "11111111222222223333333344444444"
            metadata.write_text(json.dumps(stale), encoding="utf-8")
        fingerprint = invoke("enumerate")
        catalog, seconds = invoke("probe", fingerprint)
        accepted = []
        for entry in catalog:
            response, elapsed = invoke("class", fingerprint, entry.find("PLUGIN"))
            verified = response.find("ENTRY")
            if verified is None or verified.get("error") or verified.get("verifiedMetadata") != "verified" or response.get("fingerprintVerified") != "1":
                raise RuntimeError(f"{variant}: class validation failed: {ET.tostring(response).decode()}")
            accepted.append(verified.find("PLUGIN").get("vst3ClassId"))
            seconds += elapsed
        if sorted(accepted) != sorted(p.get("vst3ClassId") for p in descriptions):
            raise RuntimeError("Manifest changed which real classes were accepted")
        if len(descriptions) == 1:
            combined, elapsed = invoke("probe", fingerprint, single=True)
            verified = combined.find("ENTRY")
            if (combined.get("catalog") != "0" or combined.get("fingerprintVerified") != "1"
                    or verified is None or verified.get("error")
                    or verified.get("verifiedMetadata") != "verified"
                    or verified.find("PLUGIN").get("vst3ClassId") != accepted[0]):
                raise RuntimeError(f"{variant}: combined single-class validation failed")
            seconds += elapsed
        results.append({"variant": variant, "accepted": accepted, "seconds": seconds})
    (root / "report.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("PASS: missing/valid/invalid/stale manifests and full class IDs:", root / "report.json")


if __name__ == "__main__":
    main()
