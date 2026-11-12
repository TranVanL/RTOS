#!/usr/bin/env python3
"""Condense `traceprinter` output into the thread-state timeline of one process and measure,
inside the window where H is blocked on the mutex, how much CPU time L and M got.
usage: trace_timeline.py <traceprinter.txt> <pid>
Roles (creation order in qnx/tests/m3_prio_inv.c): tid1 main(60), tid2 L(10), tid3 H(50), tid4 M(30).
Window = H's THMUTEX -> H's next THREADY (mutex handed over). Slice = THRUNNING until the next
THRUNNING on the same CPU. Last line: SHARE_IN_WINDOW L=<%> M=<%> window_cyc=<n>."""
import re
import sys

ROLE = {1: "main", 2: "L", 3: "H", 4: "M"}
rx = re.compile(r"t:0x([0-9a-f]+) CPU:(\d+) THREAD +:(TH\w+) +pid:(\d+) tid:(\d+)")
pid = sys.argv[2]
mine, running = [], {}
for line in open(sys.argv[1], errors="replace"):
    m = rx.search(line)
    if not m:
        continue
    t, cpu, st, p, tid = int(m.group(1), 16), int(m.group(2)), m.group(3), m.group(4), int(m.group(5))
    if st == "THRUNNING":
        running.setdefault(cpu, []).append((t, p, tid))
    if p == pid:
        mine.append((t, cpu, st, tid))
mine.sort()
start = next((i for i, e in enumerate(mine) if e[3] >= 2), 0)
mine = mine[max(start - 1, 0):]
t0 = mine[0][0]
last = None
for t, cpu, st, tid in mine:
    if st in ("THRUNNING", "THMUTEX", "THDEAD", "THREADY") and (tid, st) != last:
        print("%12d cyc  CPU%d tid=%d %-5s %s" % (t - t0, cpu, tid, ROLE.get(tid, "?"), st))
        last = (tid, st)
blk = next((e for e in mine if e[3] == 3 and e[2] == "THMUTEX"), None)
w0 = blk[0]
w1 = next((e[0] for e in mine if e[0] > w0 and e[3] == 3 and e[2] == "THREADY"), None)
share = {2: 0, 4: 0}
for cpu, lst in running.items():
    lst.sort()
    for i, (t, p, tid) in enumerate(lst):
        end = lst[i + 1][0] if i + 1 < len(lst) else t
        if p == pid and tid in share:
            share[tid] += max(0, min(end, w1) - max(t, w0))
win = w1 - w0
print("SHARE_IN_WINDOW L=%.1f%% M=%.1f%% window_cyc=%d" % (100.0 * share[2] / win, 100.0 * share[4] / win, win))
