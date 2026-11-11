"""Plot step response from sim_closed_loop CSV and compute metrics. M1 Part 5.

Usage: py tools/plot_step.py in.csv [out.png]
Metrics are in samples of the 1 ms model tick (SIL, host simulation).
"""
import csv
import sys

COUNTS_PER_REV = 4096


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    return {k: [float(r[k]) for r in rows] for k in rows[0]}


def metrics(d, band_frac=0.02):
    ref = d["ref"][-1]
    pos = d["pos"]
    peak = max(pos)
    overshoot = max(0.0, (peak - ref) / ref * 100.0) if ref else 0.0
    band = band_frac * abs(ref)
    last_out = 0
    for i, p in enumerate(pos):
        if abs(ref - p) > band:
            last_out = i + 1
    ss_counts = abs(ref - pos[-1]) * COUNTS_PER_REV / (2 * 3.141592653589793)
    return {"overshoot_pct": overshoot, "settling_ticks": last_out, "ss_error_counts": ss_counts}


def main(argv):
    d = load(argv[1])
    m = metrics(d)
    print(m)
    if len(argv) > 2:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(3, 1, sharex=True, figsize=(8, 7))
        ax[0].plot(d["t"], d["ref"], "--", label="ref")
        ax[0].plot(d["t"], d["pos"], label="pos")
        ax[0].set_ylabel("rad"); ax[0].legend()
        ax[1].plot(d["t"], d["vel"]); ax[1].set_ylabel("rad/s")
        ax[2].plot(d["t"], d["v"]); ax[2].set_ylabel("V"); ax[2].set_xlabel("t [s] (model time)")
        ax[0].set_title("Step response (SIL host simulation) overshoot=%.1f%% settling=%d ticks" %
                        (m["overshoot_pct"], m["settling_ticks"]))
        fig.tight_layout(); fig.savefig(argv[2], dpi=120)


if __name__ == "__main__":
    main(sys.argv)
