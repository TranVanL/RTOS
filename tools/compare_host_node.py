"""M1 Part 8: compare node step response (UART CSV) with the host simulation.

Node line (control_task.c):  C,<tick>,<count>,<pos_urad>,<vel_mrad_s>,<volt_mV>
Host CSV (sim_closed_loop):  t,ref,pos,vel,v,count   (row k = state after control step k)

Same OS-free core, same default parameters, no noise, so the trajectories must match up to
float differences (x86 vs Cortex-M3 soft float, -Os). Compared over the first N ticks, i.e. the
first 1 rad step (the node reference is a 1 s square wave; ref toggles at tick 1000).

Usage: py tools/compare_host_node.py node_uart.txt host.csv [--ticks 1000] [--tol-rad 1e-3] [--out result.txt]
Exit code 0 = PASS (max |dpos| <= tol and node metrics inside the M1 limits).
"""
import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plot_step import metrics as step_metrics  # noqa: E402

TWO_PI = 6.283185307179586


def load_node(path):
    rows = {}
    with open(path, errors="replace") as f:
        for line in f:
            if line.startswith("C,"):
                p = line.strip().split(",")
                if len(p) == 6:
                    try:
                        rows[int(p[1])] = (int(p[2]), int(p[3]) * 1e-6, int(p[4]) * 1e-3, int(p[5]) * 1e-3)
                    except ValueError:
                        pass            # line interleaved with other UART output
    return rows


def load_host(path):
    with open(path, newline="") as f:
        return [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("node")
    ap.add_argument("host")
    ap.add_argument("--ticks", type=int, default=1000)
    ap.add_argument("--tol-rad", type=float, default=1e-3)
    ap.add_argument("--out")
    a = ap.parse_args(argv)

    node = load_node(a.node)
    host = load_host(a.host)
    missing = [k for k in range(a.ticks) if k not in node]
    if missing:
        print("FAIL: node CSV is missing %d of the first %d ticks (first missing: %d)" % (len(missing), a.ticks, missing[0]))
        return 1

    dpos = [abs(node[k][1] - host[k]["pos"]) for k in range(a.ticks)]
    dvel = [abs(node[k][2] - host[k]["vel"]) for k in range(a.ticks)]
    dv = [abs(node[k][3] - host[k]["v"]) for k in range(a.ticks)]
    dcount = [abs(node[k][0] - int(host[k]["count"])) for k in range(a.ticks)]
    worst = max(range(a.ticks), key=lambda k: dpos[k])

    nd = {"ref": [1.0] * a.ticks, "pos": [node[k][1] for k in range(a.ticks)]}
    nm = step_metrics(nd)
    hd = {"ref": [1.0] * a.ticks, "pos": [host[k]["pos"] for k in range(a.ticks)]}
    hm = step_metrics(hd)

    ok = (max(dpos) <= a.tol_rad and nm["overshoot_pct"] < 10.0 and nm["settling_ticks"] <= 200
          and nm["ss_error_counts"] <= 1.0)
    lines = [
        "M1 Part 8: host vs node step response (SIL; node = QEMU mps2-an385, virtual time, 1 tick = 1 ms model)",
        "compared ticks 0..%d" % (a.ticks - 1),
        "max |dpos| = %.3e rad at tick %d (tolerance %.1e rad)" % (max(dpos), worst, a.tol_rad),
        "max |dvel| = %.3e rad/s, max |dV| = %.3e V, max |dcount| = %d" % (max(dvel), max(dv), max(dcount)),
        "host: overshoot=%.3f%% settling=%d ticks ss_error=%.3f counts" % (hm["overshoot_pct"], hm["settling_ticks"], hm["ss_error_counts"]),
        "node: overshoot=%.3f%% settling=%d ticks ss_error=%.3f counts" % (nm["overshoot_pct"], nm["settling_ticks"], nm["ss_error_counts"]),
        "limits: overshoot < 10%%, settling <= 200 ticks, ss_error <= 1 count -> %s" % ("PASS" if ok else "FAIL"),
    ]
    text = "\n".join(lines)
    print(text)
    if a.out:
        Path(a.out).write_text(text + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
