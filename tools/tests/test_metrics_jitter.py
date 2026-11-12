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


def test_rtt_stats(tmp_path):
    p = tmp_path / "rtt.txt"
    p.write_text("# header\n" + "\n".join(str(v) for v in [100, 200, 300, 400, 20000]) + "\n")
    s = metrics.rtt_summary(metrics.parse_rtt(str(p)))
    assert s["n"] == 5 and s["min"] == 100 and s["max"] == 20000 and s["p50"] == 300 and s["over_10ms"] == 1


def test_reaction_ticks_from_node_log():
    estop = "NODE_STATE SAFE_STOP ctl_tick=14611 cause=1 output=0 last_cmd_tick=14606 estop_post_tick=14610\n"
    timeout = "NODE_STATE SAFE_STOP ctl_tick=24616 cause=2 output=0 last_cmd_tick=24566 estop_post_tick=0\n"
    assert metrics.reaction_ticks(metrics.parse_node_transitions(estop)) == {"cause": "ESTOP", "ticks": 1}
    assert metrics.reaction_ticks(metrics.parse_node_transitions(timeout)) == {"cause": "CMD_TIMEOUT", "ticks": 50}
    assert metrics.reaction_ticks([]) is None
