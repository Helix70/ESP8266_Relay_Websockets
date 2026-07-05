"""Result recording and JSON report writing.

Result schema mirrors scripts/tests/Invoke-BoardFunctionalHarness.ps1's
Add-Result flat shape (target, suite, test, endpoint, passed, details) so the
two suites stay comparable, extended with latency_ms/heap_before/heap_after/
heap_delta for the benchmark requirement.
"""
import json
import statistics
import time
from pathlib import Path

import coverage as coverage_module


class Report:
    def __init__(self, board_name, target):
        self.board_name = board_name
        self.target = target
        self.started_at = time.time()
        self.finished_at = None
        self.results = []
        self.specifics = {}

    def add_specific(self, name, data):
        """Record a full, non-flattened blob of what a particular test step
        actually did -- e.g. the complete relay-by-relay content of the
        purpose-built L/I/P/group template, or a new board config's full
        field set -- alongside the pass/fail results, since those don't fit
        the flat add() schema."""
        self.specifics[name] = data

    def add(self, suite, test, passed, endpoint="", details="", latency_ms=None,
            heap_before=None, heap_after=None):
        heap_delta = None
        if heap_before is not None and heap_after is not None:
            heap_delta = heap_after - heap_before
        entry = {
            "target": self.target,
            "suite": suite,
            "test": test,
            "endpoint": endpoint,
            "passed": bool(passed),
            "details": details,
            "latencyMs": round(latency_ms, 2) if latency_ms is not None else None,
            "heapBefore": heap_before,
            "heapAfter": heap_after,
            "heapDelta": heap_delta,
        }
        self.results.append(entry)
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {suite}/{test}" + (f" -- {details}" if details else ""))
        return entry

    def summary(self):
        total = len(self.results)
        failed = sum(1 for r in self.results if not r["passed"])
        return {"total": total, "passed": total - failed, "failed": failed}

    def _benchmarks(self):
        def stats(values):
            if not values:
                return None
            values = sorted(values)
            return {
                "min": round(values[0], 2),
                "median": round(statistics.median(values), 2),
                "p95": round(values[int(len(values) * 0.95) - 1] if len(values) > 1 else values[0], 2),
                "max": round(values[-1], 2),
                "count": len(values),
            }

        toggle_latencies = [r["latencyMs"] for r in self.results if r["suite"] == "relay_toggle" and r["latencyMs"] is not None]
        page_latencies = [r["latencyMs"] for r in self.results if r["suite"] == "page_nav" and r["latencyMs"] is not None]
        heap_values = [r["heapAfter"] for r in self.results if r["heapAfter"] is not None]
        heap_values += [r["heapBefore"] for r in self.results if r["heapBefore"] is not None]

        return {
            "relayToggleLatencyMs": stats(toggle_latencies),
            "pageLoadLatencyMs": stats(page_latencies),
            "heapFloorBytes": min(heap_values) if heap_values else None,
            "heapDangerZoneBytes": 1824,
            "heapApproachedDangerZone": bool(heap_values) and min(heap_values) < 3000,
        }

    def finish(self, out_path):
        self.finished_at = time.time()
        out = {
            "board": self.board_name,
            "target": self.target,
            "startedAt": self.started_at,
            "finishedAt": self.finished_at,
            "durationSec": round(self.finished_at - self.started_at, 1),
            "summary": self.summary(),
            "benchmarks": self._benchmarks(),
            "coverage": coverage_module.summary(),
            "specifics": self.specifics,
            "results": self.results,
        }
        out_path = Path(out_path)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(out, indent=2), encoding="utf-8")
        return out

    def print_summary(self):
        s = self.summary()
        b = self._benchmarks()
        print(f"\n=== {self.board_name} summary: passed={s['passed']} failed={s['failed']} total={s['total']} ===")
        if b["relayToggleLatencyMs"]:
            print(f"  relay toggle latency (ms): {b['relayToggleLatencyMs']}")
        if b["pageLoadLatencyMs"]:
            print(f"  page load latency (ms): {b['pageLoadLatencyMs']}")
        if b["heapFloorBytes"] is not None:
            flag = " <-- approached danger zone!" if b["heapApproachedDangerZone"] else ""
            print(f"  heap floor: {b['heapFloorBytes']} bytes{flag}")
