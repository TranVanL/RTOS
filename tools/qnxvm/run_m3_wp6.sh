#!/bin/bash
# M3 WP6: priority inversion with/without priority inheritance (REQ-PERF-003) + kernel trace.
# VM must be up (tools/qnxvm/run_vm.sh). Logs: docs/results/m3_wp6_prio_inheritance.txt (+ _trace_pi/_nopi.txt)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); R=$ROOT/docs/results; OUT=$R/m3_wp6_prio_inheritance.txt
KEY=""; [ -f ~/.ssh/id_qnxvm ] && KEY="-i $HOME/.ssh/id_qnxvm"
SSHO="$KEY -o StrictHostKeyChecking=no"
SSH="ssh $SSHO -p 2222 root@127.0.0.1"
source ~/qnx800/qnxsdp-env.sh >/dev/null 2>&1
TMP=$(mktemp -d)
qcc -Vgcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror -O0 "$ROOT/qnx/tests/m3_prio_inv.c" -o $TMP/m3_prio_inv || exit 2
$SSH "slay -f m3_prio_inv tracelogger 2>/dev/null; sleep 0.5" </dev/null >/dev/null 2>&1
scp -q $SSHO -P 2222 $TMP/m3_prio_inv root@127.0.0.1:/tmp/ || exit 2
{
  echo "# M3 WP6 priority inversion on QNX VM (SIL, QEMU/KVM; all threads pinned to CPU 0) — $(date -u +%F) commit $(git -C "$ROOT" rev-parse --short HEAD)"
  $SSH uname -a </dev/null
  for mode in pi nopi pi nopi pi nopi pi nopi pi nopi; do $SSH "/tmp/m3_prio_inv $mode" </dev/null; done
} > "$OUT" 2>&1
fail=0; pass() { echo "PASS $1"; }; failf() { echo "FAIL $1"; fail=1; }
{
for mode in pi nopi; do
  n=$(grep -c "mode=$mode " "$OUT")
  [ "$n" = 5 ] && pass "$mode: 5 runs" || failf "$mode: $n runs"
done
bad=$(grep "mode=pi " "$OUT" | grep -v "m_at_H_acquire=0 order=L_unlock,H_acquire,M_done" | wc -l)
[ "$bad" = 0 ] && pass "PI: in 5/5 runs H acquires with M at 0 iterations; order L_unlock,H_acquire,M_done" || failf "PI: $bad runs deviate"
bad=$(grep "mode=nopi " "$OUT" | grep -v "m_at_H_acquire=20000000 order=M_done,L_unlock,H_acquire" | wc -l)
[ "$bad" = 0 ] && pass "NO PI: in 5/5 runs H waits until M completed all N; order M_done,L_unlock,H_acquire (priority inversion)" || failf "NO PI: $bad runs deviate"
# kernel trace of one run per mode
for mode in pi nopi; do
  line=$($SSH "rm -f /tmp/t_$mode.kev; (/system/bin/tracelogger -s 3 -f /tmp/t_$mode.kev >/dev/null 2>&1 </dev/null &); sleep 1; /tmp/m3_prio_inv $mode; sleep 3" </dev/null 2>&1 | tail -1)
  pid=$(echo "$line" | sed -n 's/.*pid=\([0-9]*\).*/\1/p')
  scp -q $SSHO -P 2222 root@127.0.0.1:/tmp/t_$mode.kev $TMP/t_$mode.kev
  timeout 120 traceprinter -f $TMP/t_$mode.kev > $TMP/t_$mode.txt 2>&1
  python3 "$ROOT/tools/qnxvm/trace_timeline.py" $TMP/t_$mode.txt "$pid" > "$R/m3_wp6_trace_$mode.txt"
  echo "trace $mode (pid $pid): $(tail -1 "$R/m3_wp6_trace_$mode.txt")"
done
share() { tail -1 "$R/m3_wp6_trace_$1.txt" | sed -n "s/.* $2=\([0-9.]*\)%.*/\1/p"; }
lp=$(share pi L); mp=$(share pi M); mn=$(share nopi M)
awk -v m="$mp" -v l="$lp" 'BEGIN{exit !(m+0 < 5 && l+0 >= 90)}' && pass "trace PI: while H is blocked, L (boosted) gets ${lp}% of the CPU and M only ${mp}%" || failf "trace PI shares (L=$lp M=$mp)"
awk -v m="$mn" 'BEGIN{exit !(m+0 >= 70)}' && pass "trace NO PI: while H is blocked, M (medium) gets ${mn}% of the CPU (L starved)" || failf "trace NO PI share (M=$mn)"
} | tee -a "$OUT"
rm -rf "$TMP"; exit $fail
