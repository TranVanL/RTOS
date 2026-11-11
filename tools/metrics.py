"""Compute jitter, latency p50/p99/max, reaction ticks from logs.

M1 Part 7 (implemented): control-task jitter and per-step cost from the node UART log.
M4 (not yet): round-trip latency and reaction ticks.

Node log format (control_task.c, one block per run):
    JITTER_BEGIN n=10000 skip=500 missed_in_window=0 timer_hz=25000000 ideal_period=25000
    J,<i>,<period_counts>,<cost_ctrl_counts>,<cost_plant_counts>      (n lines)
    JITTER_END
The counter is CMSDK TIMER0 (25 MHz, 1 count = 40 ns of *virtual* time under QEMU -icount).
All numbers are SIL, QEMU, virtual time - not wall-clock and not hardware.

Usage:
    py tools/metrics.py jitter A=logA.txt B=logB.txt C=logC.txt --out docs/results/m1_jitter
"""
import argparse
import re
import sys


def parse_jitter(path):
    """Return dict(n, skip, missed, ideal, period[], ctrl[], plant[]) from a node UART log."""
    meta = None
    period, ctrl, plant = [], [], []
    with open(path, errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("JITTER_BEGIN"):
                meta = dict((k, int(v)) for k, v in re.findall(r"(\w+)=(\d+)", line))
            elif line.startswith("J,"):
                parts = line.split(",")
                if len(parts) == 5:
                    period.append(int(parts[2]))
                    ctrl.append(int(parts[3]))
                    plant.append(int(parts[4]))
    if meta is None:
        raise ValueError("no JITTER_BEGIN block in %s" % path)
    return {"n": meta["n"], "skip": meta["skip"], "missed": meta["missed_in_window"],
            "ideal": meta["ideal_period"], "period": period, "ctrl": ctrl, "plant": plant}


def percentile(sorted_vals, q):
    """Nearest-rank percentile, q in [0,100]."""
    if not sorted_vals:
        raise ValueError("empty")
    k = max(0, min(len(sorted_vals) - 1, int(-(-q / 100.0 * len(sorted_vals) // 1)) - 1))
    return sorted_vals[k]


def stats(vals):
    s = sorted(vals)
    n = len(s)
    mean = sum(s) / n
    var = sum((x - mean) ** 2 for x in s) / n
    return {"n": n, "min": s[0], "mean": mean, "p50": percentile(s, 50), "p99": percentile(s, 99),
            "max": s[-1], "std": var ** 0.5}


def jitter_summary(d):
    """Jitter = period - ideal, in virtual timer counts (1 count = 40 ns virtual)."""
    jit = [p - d["ideal"] for p in d["period"]]
    late = sum(1 for p in d["period"] if p >= 2 * d["ideal"] - d["ideal"] // 2)  # >= ~1.5 ticks: a tick was lost
    return {"jitter": stats(jit), "ctrl": stats(d["ctrl"]), "plant": stats(d["plant"]),
            "missed_in_window": d["missed"], "late_cycles": late, "n": len(jit)}


def format_summary(label, s):
    j, c, p = s["jitter"], s["ctrl"], s["plant"]
    return (
        "%s: n=%d missed_in_window=%d late_cycles(>=1.5 tick)=%d\n"
        "  jitter  [counts] min=%d mean=%.2f p50=%d p99=%d max=%d std=%.2f\n"
        "  ctrl    [counts] min=%d mean=%.2f p99=%d max=%d   (encoder+speed+cascade)\n"
        "  plant   [counts] min=%d mean=%.2f p99=%d max=%d   (test-fixture plant model)\n"
        % (label, s["n"], s["missed_in_window"], s["late_cycles"],
           j["min"], j["mean"], j["p50"], j["p99"], j["max"], j["std"],
           c["min"], c["mean"], c["p99"], c["max"], p["min"], p["mean"], p["p99"], p["max"]))


def write_csv(path, d):
    with open(path, "w") as f:
        f.write("i,period,ctrl,plant\n")
        for i, (a, b, c) in enumerate(zip(d["period"], d["ctrl"], d["plant"])):
            f.write("%d,%d,%d,%d\n" % (i, a, b, c))


def plot_hist(path, results):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(len(results), 1, figsize=(8, 2.6 * len(results)), squeeze=False)
    for ax, (label, d) in zip(axes[:, 0], results):
        jit = [p - d["ideal"] for p in d["period"]]
        lo, hi = min(jit), max(jit)
        bins = range(lo, hi + 2)
        ax.hist(jit, bins=bins, align="left", rwidth=0.9)
        ax.set_yscale("log")
        ax.set_ylabel("cycles")
        s = stats(jit)
        ax.set_title("%s  n=%d  min=%d p99=%d max=%d  (counts, 25 MHz virtual)" % (label, s["n"], s["min"], s["p99"], s["max"]), fontsize=9)
    axes[-1, 0].set_xlabel("jitter = period - 25000 [virtual timer counts]; SIL, QEMU -icount shift=0")
    fig.tight_layout()
    fig.savefig(path, dpi=120)


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    j = sub.add_parser("jitter")
    j.add_argument("inputs", nargs="+", help="LABEL=logfile ...")
    j.add_argument("--out", help="output prefix (writes _summary.txt, _hist.png, _<label>.csv)")
    a = ap.parse_args(argv)
    results = []
    text = ""
    for item in a.inputs:
        label, _, path = item.partition("=")
        d = parse_jitter(path)
        results.append((label, d))
        text += format_summary(label, jitter_summary(d))
    print(text)
    if a.out:
        with open(a.out + "_summary.txt", "w") as f:
            f.write("M1 Part 7 jitter (SIL, QEMU -icount shift=0, virtual time; 1 count = 40 ns virtual)\n" + text)
        for label, d in results:
            write_csv("%s_%s.csv" % (a.out, label), d)
        plot_hist(a.out + "_hist.png", results)
    return 0


if __name__ == "__main__":
    sys.exit(main())
