"""Unit tests for the jitter parser/statistics in tools/metrics.py."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import metrics  # noqa: E402


def _log(tmp_path, periods):
    p = tmp_path / "node.txt"
    lines = ["boot", "JITTER_BEGIN n=%d skip=500 missed_in_window=0 timer_hz=25000000 ideal_period=25000" % len(periods)]
    lines += ["J,%d,%d,60,280" % (i, v) for i, v in enumerate(periods)]
    lines += ["JITTER_END"]
    p.write_text("\n".join(lines))
    return str(p)


def test_parse_and_stats(tmp_path):
    d = metrics.parse_jitter(_log(tmp_path, [25000, 25001, 24999, 25000]))
    s = metrics.jitter_summary(d)
    assert s["n"] == 4
    assert s["jitter"]["min"] == -1 and s["jitter"]["max"] == 1
    assert s["late_cycles"] == 0
    assert s["ctrl"]["mean"] == 60


def test_late_cycle_detected(tmp_path):
    d = metrics.parse_jitter(_log(tmp_path, [25000, 50000, 25000]))
    assert metrics.jitter_summary(d)["late_cycles"] == 1


def test_percentile_nearest_rank():
    assert metrics.percentile(list(range(1, 101)), 99) == 99
    assert metrics.percentile([5], 99) == 5
