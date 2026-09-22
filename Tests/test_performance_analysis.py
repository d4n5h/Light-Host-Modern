import copy
import unittest

from AnalyzeReleasePerformance import compare, weighted


def report():
    run = {
        "number": 1, "status": "measured", "productionPreferencesUnchanged": True,
        "samples": [{"phase": "measuring", "intervalSeconds": 1, "hostCpuPercent": 2,
                     "uiCpuPercent": 1, "gpuPercent": 0, "hostPrivateBytes": 10000000,
                     "uiPrivateBytes": 20000000, "hostWorkingSetBytes": 11000000,
                     "uiWorkingSetBytes": 21000000} for _ in range(300)],
        "measurement": {"frequency": "1000", "windowStartTick": "30000", "windowEndTick": "330000",
                        "callbacks": "30000", "samples": "14400000", "totalTicks": "30000",
                        "p95UpperTicks": "1", "p99UpperTicks": "2", "maximumTicks": "3",
                        "hostAllocations": "0", "hostFrees": "0"},
        "xRuns": 0, "finalDiagnostics": {"processFailures": 0},
        "transportBefore": {"telemetryRequests": 10, "retainedEventBytes": 1000},
        "transportAfter": {"telemetryRequests": 10, "retainedEventBytes": 1000},
    }
    return {"fullDurationProtocol": True, "uiState": "minimized", "runs": [dict(copy.deepcopy(run), number=n) for n in range(1, 6)]}


class PerformanceAnalysisTests(unittest.TestCase):
    def test_cpu_uses_elapsed_time_and_ignores_unavailable_samples(self):
        samples = [{"intervalSeconds": 9, "cpu": 1}, {"intervalSeconds": 1, "cpu": 11}, {"intervalSeconds": 9, "cpu": None}]
        self.assertEqual(weighted(samples, "cpu"), 2)

    def test_unchanged_complete_runs_meet_only_measured_checks(self):
        result = compare(report(), report())
        self.assertEqual(result["assessment"], "meets_measured_checks")
        self.assertFalse(result["overallApplicationValidated"])

    def test_smoke_duration_cannot_pass_by_changing_report_header(self):
        current = report()
        current["runs"][0]["measurement"]["windowEndTick"] = "32000"
        self.assertEqual(compare(report(), current)["assessment"], "incomplete")

    def test_cpu_regression_or_reduced_work_requires_investigation(self):
        current = report()
        for run in current["runs"]:
            for sample in run["samples"]:
                sample["hostCpuPercent"] = 2.12
            run["measurement"]["callbacks"] = "27000"
            run["measurement"]["samples"] = "12960000"
        result = compare(report(), current)
        self.assertEqual(result["assessment"], "requires_investigation")
        self.assertTrue({"hostCpuPercent", "callbacksPerSecond", "samplesPerSecond"}.issubset({f.get("metric") for f in result["findings"]}))

    def test_minimized_telemetry_allocation_and_growth_are_reported(self):
        current = report()
        run = current["runs"][0]
        run["transportAfter"]["telemetryRequests"] += 1
        run["measurement"]["hostAllocations"] = "1"
        for sample in run["samples"][-5:]:
            sample["hostPrivateBytes"] += 2000000
        result = compare(report(), current)
        self.assertEqual(len([f for f in result["findings"] if f["kind"] == "failed"]), 2)
        self.assertIn("hostPrivateBytesGrowth", {f.get("metric") for f in result["findings"]})

    def test_unavailable_gpu_stays_unavailable(self):
        current = report()
        for run in current["runs"]:
            for sample in run["samples"]:
                sample["gpuPercent"] = None
        self.assertIsNone(compare(report(), current)["metrics"]["gpuPercent"]["currentMedian"])

    def test_minimized_meter_pipe_is_checked_independently(self):
        current = report()
        current["runs"][0]["transportAfter"]["meterRequests"] = 1
        result = compare(report(), current)
        self.assertTrue(any(f["kind"] == "failed" and "visual telemetry" in f.get("reason", "") for f in result["findings"]))

    def test_different_window_sizes_are_not_accepted_as_equal_work(self):
        baseline, current = report(), report()
        for source, width in ((baseline, 1200), (current, 600)):
            for run in source["runs"]:
                run["window"] = {"windows": [{"title": "LightHostModern", "elements": [{"width": width, "height": 700}]}]}
        self.assertEqual(compare(baseline, current)["assessment"], "incomplete")

    def test_a_zero_baseline_does_not_hide_new_gpu_work(self):
        current = report()
        for run in current["runs"]:
            for sample in run["samples"]:
                sample["gpuPercent"] = 2
        result = compare(report(), current)
        self.assertIsNone(result["metrics"]["gpuPercent"]["changePercent"])
        self.assertEqual(result["assessment"], "requires_investigation")
        self.assertIn("gpuPercent", {f.get("metric") for f in result["findings"] if f["kind"] == "investigate"})


if __name__ == "__main__":
    unittest.main()
