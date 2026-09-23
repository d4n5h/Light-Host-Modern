"""Summarize scan.timing events from a verbose export or capture directory.

Durations of nested/overlapping stages must not be added as total scan time.
"""
import argparse
import json
import re
import statistics
from collections import defaultdict
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    files = sorted(args.source.glob("*.log")) if args.source.is_dir() else [args.source]
    stages = defaultdict(list)
    for file in files:
        for line in file.read_text(encoding="utf-8", errors="replace").splitlines():
            if "[scan.timing]" not in line:
                continue
            event = line.split("[scan.timing]", 1)[1]
            stage = re.match(r"\s*stage=(\w+)\s+durationMs=([\d.]+)", event)
            if not stage:
                continue
            # Timing fields are emitted after context; path text may contain spaces.
            numeric = dict((key, float(value)) for key, value in re.findall(
                r"(?:^|\s)(durationMs|launchMs|cpuMs|readBytes|writeBytes|peakWorkingSet|"
                r"walkMs|bytesRead|readMs|hashAndProgressMs|files|records|bytesWritten)=([\d.]+)(?=\s|$)", event))
            stages[stage[1]].append(numeric)
    report = {"source": str(args.source.resolve()),
              "note": "Nested/overlapping stage times are not additive. Process IO is OS transfer counts, not physical disk IO.",
              "stages": {}}
    for stage, samples in sorted(stages.items()):
        metrics = {}
        for key in sorted(set().union(*(sample.keys() for sample in samples))):
            values = [sample[key] for sample in samples if key in sample]
            metrics[key] = {"median": statistics.median(values), "sum": sum(values), "max": max(values)}
        report["stages"][stage] = {"count": len(samples), "metrics": metrics}
    output = json.dumps(report, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(output + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
