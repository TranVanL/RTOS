#!/bin/bash
# M3 WP3: build motion_sup + fake peer with qcc, run scenarios in the QNX VM (VM must be up:
# tools/qnxvm/run_vm.sh). Pass/fail uses counts only. Log: docs/results/m3_wp3_scenarios.txt
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=$ROOT/docs/results/m3_wp3_scenarios.txt
KEY=""; [ -f ~/.ssh/id_qnxvm ] && KEY="-i $HOME/.ssh/id_qnxvm"
SSH="ssh $KEY -o StrictHostKeyChecking=no -p 2222 root@127.0.0.1"
source ~/qnx800/qnxsdp-env.sh >/dev/null 2>&1
INC="-I$ROOT/common/safety -I$ROOT/common/shm -I$ROOT/common/ipc -I$ROOT/common/proto -I$ROOT/common/frame -I$ROOT/common/e2e -I$ROOT/common/generated"
SRC="$ROOT/common/safety/safety_fsm.c $ROOT/common/shm/motion_shm.c $ROOT/common/ipc/motion_ipc.c $ROOT/common/proto/proto_link.c $ROOT/common/frame/can_frame.c $ROOT/common/e2e/crc32p4.c $ROOT/common/e2e/e2e.c $ROOT/common/generated/can_matrix.c $ROOT/common/generated/can_matrix_meta.c"
TMP=$(mktemp -d)
qcc -Vgcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror $INC "$ROOT/qnx/motion_sup/motion_sup.c" $SRC -o $TMP/motion_sup -lsocket || exit 2
qcc -Vgcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror $INC "$ROOT/qnx/tests/m3_peer.c" $SRC -o $TMP/m3_peer -lsocket || exit 2
$SSH "slay -f motion_sup m3_peer logger mgmt 2>/dev/null; sleep 0.5" >/dev/null 2>&1
scp -q $KEY -o StrictHostKeyChecking=no -P 2222 $TMP/motion_sup $TMP/m3_peer root@127.0.0.1:/tmp/ || exit 2
# fixed group for resmgr privilege checks (gid 3000), idempotent
$SSH 'grep -q "^motion:" /etc/group || echo "motion:x:3000:" >> /etc/group; grep "^motion:" /etc/group'

run() { # name, sup args, peer args
  echo "=== $1"; shift
  $SSH "rm -f /dev/shmem/motion_state; /tmp/motion_sup $1 > /tmp/sup.out & sleep 0.5; /tmp/m3_peer $2 > /tmp/peer.out & sleep 0.5; pidin -p motion_sup 2>/dev/null | head -12 > /tmp/pidin.out; wait; cat /tmp/sup.out /tmp/peer.out; echo '--- pidin (mid-run)'; cat /tmp/pidin.out"
}
RES=$(mktemp)
{
  echo "# M3 WP3 scenarios on QNX VM (SIL, QEMU/KVM) — $(date -u +%F) commit $(git -C "$ROOT" rev-parse --short HEAD)"
  $SSH uname -a
  run A "--autoenable --ticks 250" "--ticks 300"
  run B "--autoenable --ticks 300" "--ticks 350 --stop-after 100"
  run C "--autoenable --ticks 250 --estop-at 150" "--ticks 300"
} > "$OUT" 2>&1
cat "$OUT"
# deterministic criteria (counts/states only)
sect() { awk -v s="=== $1" '$0==s{f=1;next} /^===/{f=0} f' "$OUT"; }
field() { grep -o "$2=[A-Za-z0-9_]*" | head -1 | cut -d= -f2; }
fail=0
chk() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fail=1; fi; }
{
A=$(sect A); B=$(sect B); C=$(sect C)
chk "A final_state RUN"         "$(echo "$A" | field x final_state)" RUN
chk "A ticks == 250"            "$(echo "$A" | field x ticks)" 250
chk "A estop_sent == 0"         "$(echo "$A" | field x estop_sent)" 0
chk "A peer cmd_bad == 0"       "$(echo "$A" | grep PEER | field x cmd_bad)" 0
chk "A e2e_err == 0"            "$(echo "$A" | field x e2e_err)" 0
chk "B final_state SAFE_STOP"   "$(echo "$B" | field x final_state)" SAFE_STOP
chk "B cause HB_TIMEOUT (4)"    "$(echo "$B" | field x cause)" 4
chk "B peer estop_ok == 3"      "$(echo "$B" | grep PEER | field x estop_ok)" 3
chk "C final_state SAFE_STOP"   "$(echo "$C" | field x final_state)" SAFE_STOP
chk "C cause ESTOP (1)"         "$(echo "$C" | field x cause)" 1
chk "C peer estop_ok == 3"      "$(echo "$C" | grep PEER | field x estop_ok)" 3
chk "C peer estop_bad == 0"     "$(echo "$C" | grep PEER | field x estop_bad)" 0
for n in 60f 55f 50f 45f; do grep -q " $n " "$OUT" && echo "PASS pidin shows thread priority $n" || { echo "FAIL pidin priority $n"; fail=1; }; done
} | tee -a "$OUT"
rm -rf "$TMP" "$RES"
exit $fail
