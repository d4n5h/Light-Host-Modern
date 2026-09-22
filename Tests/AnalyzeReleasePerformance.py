"""Compare recorded Release runs without confusing smoke tests with acceptance.

Uses only report files. It never starts a host, audio device, UI or installer.
CPU averages are weighted by the sampling interval; callback quantiles remain
per-run statistics, then are compared by the median of the five repetitions.
"""
from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path


def median(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


def weighted(samples, name):
    usable = [s for s in samples if s.get(name) is not None and s["intervalSeconds"] > 0]
    total = sum(s["intervalSeconds"] for s in usable)
    return sum(s[name] * s["intervalSeconds"] for s in usable) / total if total else None


def summarize_run(run):
    samples = [s for s in run["samples"] if s["phase"] == "measuring"]
    measurement = run.get("measurement", {})
    frequency = int(measurement.get("frequency", 0))
    window = (int(measurement.get("windowEndTick", 0)) - int(measurement.get("windowStartTick", 0))) / frequency if frequency else 0
    result = {"number": run["number"], "status": run["status"], "sampleCount": len(samples), "windowSeconds": window}
    for key in ("hostCpuPercent", "uiCpuPercent", "gpuPercent"):
        result[key] = weighted(samples, key)
    for process in ("host", "ui"):
        for kind in ("PrivateBytes", "WorkingSetBytes"):
            key = process + kind
            values = [s[key] for s in samples if s.get(key) is not None]
            result[key] = median(values)
            result[key + "Peak"] = max(values) if values else None
            # Endpoint medians show sustained growth without fitting startup noise.
            result[key + "Growth"] = median(values[-5:]) - median(values[:5]) if len(values) >= 10 else None
    for source, target in (("p95UpperTicks", "callbackP95Ms"), ("p99UpperTicks", "callbackP99Ms"), ("maximumTicks", "callbackMaximumMs")):
        result[target] = int(measurement[source]) * 1000 / frequency if source in measurement and frequency else None
    result["callbackMeanMs"] = int(measurement["totalTicks"]) * 1000 / frequency / int(measurement["callbacks"]) if int(measurement.get("callbacks", 0)) and frequency else None
    result["callbacksPerSecond"] = int(measurement.get("callbacks", 0)) / window if window else None
    result["samplesPerSecond"] = int(measurement.get("samples", 0)) / window if window else None
    result["hostAllocations"] = int(measurement["hostAllocations"]) if measurement.get("hostAllocations") is not None else None
    result["hostFrees"] = int(measurement["hostFrees"]) if measurement.get("hostFrees") is not None else None
    result["xRuns"] = run.get("xRuns")
    result["processFailures"] = run.get("finalDiagnostics", {}).get("processFailures")
    before, after = run.get("transportBefore"), run.get("transportAfter")
    result["telemetryRequests"] = after["telemetryRequests"] - before["telemetryRequests"] if before and after else None
    result["meterRequests"] = after.get("meterRequests", 0) - before.get("meterRequests", 0) if before and after else None
    result["retainedEventBytesGrowth"] = after["retainedEventBytes"] - before["retainedEventBytes"] if before and after else None
    result["productionPreferencesUnchanged"] = run.get("productionPreferencesUnchanged", False)
    result["processedWork"] = run.get("processedWork")
    return result


def compare(baseline, current):
    findings = []
    comparable = ("uiState", "windowPresentation", "expectedPlugins", "backend", "output", "sampleRate", "bufferSize", "inputOpened", "outputMuted", "logicalProcessors", "fixtureSha256")
    for key in comparable:
        # Earlier harness reports only supported normal presentation.
        default = "normal" if key == "windowPresentation" else None
        old, new = baseline.get(key, default), current.get(key, default)
        if old != new:
            findings.append({"kind": "incomparable", "metric": key, "baseline": old, "current": new})
    def window_sizes(report):
        return sorted({(w["elements"][0]["width"], w["elements"][0]["height"])
                       for r in report["runs"] for w in r.get("window", {}).get("windows", [])
                       if w.get("elements") and w["title"].startswith("LightHostModern")})
    if window_sizes(baseline) != window_sizes(current):
        findings.append({"kind": "incomparable", "metric": "windowSize", "baseline": window_sizes(baseline), "current": window_sizes(current)})
    runs = {label: [summarize_run(r) for r in report["runs"]] for label, report in (("baseline", baseline), ("current", current))}
    for label, report in (("baseline", baseline), ("current", current)):
        if not report["fullDurationProtocol"] or len(report["runs"]) != 5:
            findings.append({"kind": "incomplete", "variant": label, "reason": "Requires five runs with 30-second warmup and 300-second measurement."})
        for run in runs[label]:
            if run["status"] != "measured" or run["sampleCount"] < 30 or abs(run["windowSeconds"] - 300) > 0.001 or not run["productionPreferencesUnchanged"]:
                findings.append({"kind": "incomplete", "variant": label, "run": run["number"], "reason": "Run failed, has insufficient samples/duration or did not preserve production preferences."})
            if run["xRuns"] != 0 or run["processFailures"] != 0:
                findings.append({"kind": "investigate", "variant": label, "run": run["number"], "reason": "Driver xruns or processing failures", "xRuns": run["xRuns"], "processFailures": run["processFailures"]})
            if label == "current":
                if run["hostAllocations"] != 0 or run["hostFrees"] != 0:
                    findings.append({"kind": "failed", "run": run["number"], "reason": "Host callback allocation/free audit did not report zero."})
                if report["uiState"] == "minimized" and (run["telemetryRequests"] != 0 or run["meterRequests"] != 0):
                    findings.append({"kind": "failed", "run": run["number"], "reason": "Minimized UI continued requesting visual telemetry."})
                for process in ("host", "ui"):
                    growth = run[process + "PrivateBytesGrowth"]
                    retained = run[process + "PrivateBytes"]
                    if growth is not None and retained and growth > max(1024 * 1024, retained * 0.05):
                        findings.append({"kind": "investigate", "run": run["number"], "metric": process + "PrivateBytesGrowth", "bytes": growth})
                if (run["retainedEventBytesGrowth"] or 0) > 1024 * 1024:
                    findings.append({"kind": "investigate", "run": run["number"], "metric": "retainedEventBytesGrowth", "bytes": run["retainedEventBytesGrowth"]})
    metrics = {}
    for key in ("hostCpuPercent", "uiCpuPercent", "gpuPercent", "hostPrivateBytes", "uiPrivateBytes", "hostWorkingSetBytes", "uiWorkingSetBytes", "callbackMeanMs", "callbackP95Ms", "callbackP99Ms", "callbackMaximumMs", "callbacksPerSecond", "samplesPerSecond"):
        old, new = (median(r[key] for r in runs[label]) for label in ("baseline", "current"))
        change = (new / old - 1) * 100 if old is not None and old > 0 and new is not None else None
        metrics[key] = {"baselineMedian": old, "currentMedian": new, "changePercent": change}
        if key in ("callbacksPerSecond", "samplesPerSecond"):
            if change is not None and abs(change) > 5:
                findings.append({"kind": "investigate", "metric": key, "changePercent": change, "reason": "Delivered work differs."})
        elif change is not None and change > 5:
            findings.append({"kind": "investigate", "metric": key, "changePercent": change})
        elif old == 0 and new is not None and new > 0:
            findings.append({"kind": "investigate", "metric": key, "baseline": old, "current": new,
                             "reason": "A positive cost replaced a measured zero; a percentage ratio is undefined."})
        elif old is None or new is None:
            findings.append({"kind": "unavailable", "metric": key})
    state = "incomplete" if any(f["kind"] in ("incomplete", "incomparable") for f in findings) else "requires_investigation" if any(f["kind"] in ("investigate", "failed") for f in findings) else "meets_measured_checks"
    return {"assessment": state, "overallApplicationValidated": False, "scenario": current["uiState"], "metrics": metrics, "runs": runs, "findings": findings,
            "limits": ["Silent input; no incoming MIDI in this device scenario. Nonzero audio and MIDI are covered separately by offline tests.", "Third-party DLL allocations are not intercepted.", "GPU is the maximum UI engine counter, sampled every five seconds; unavailable counters remain null.", "Per-run quantiles are compared by median; these are not pooled callback quantiles."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reports = [json.loads(p.read_text(encoding="utf-8-sig")) for p in (args.baseline, args.current)]
    result = compare(*reports)
    result["sources"] = [str(p.resolve()) for p in (args.baseline, args.current)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"assessment": result["assessment"], "findings": result["findings"], "output": str(args.output)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
