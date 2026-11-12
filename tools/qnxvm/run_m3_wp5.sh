#!/bin/bash
# M3 WP5: logger + mgmt (read-only SHM), intentional SIGSEGV (F14), crash detection by motion_sup.
# VM must be up (tools/qnxvm/run_vm.sh). Log: docs/results/m3_wp5_shm_crash.txt. Criteria: states/signals/counts only.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=$ROOT/docs/results/m3_wp5_shm_crash.txt
KEY=""; [ -f ~/.ssh/id_qnxvm ] && KEY="-i $HOME/.ssh/id_qnxvm"
SSH="ssh $KEY -o StrictHostKeyChecking=no -p 2222 root@127.0.0.1"
source ~/qnx800/qnxsdp-env.sh >/dev/null 2>&1
INC="-I$ROOT/qnx/motion_sup -I$ROOT/common/safety -I$ROOT/common/shm -I$ROOT/common/ipc -I$ROOT/common/proto -I$ROOT/common/frame -I$ROOT/common/e2e -I$ROOT/common/generated"
SRC="$ROOT/common/safety/safety_fsm.c $ROOT/common/shm/motion_shm.c $ROOT/common/ipc/motion_ipc.c $ROOT/common/proto/proto_link.c $ROOT/common/frame/can_frame.c $ROOT/common/e2e/crc32p4.c $ROOT/common/e2e/e2e.c $ROOT/common/generated/can_matrix.c $ROOT/common/generated/can_matrix_meta.c"
TMP=$(mktemp -d); F="-Vgcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror"
qcc $F $INC "$ROOT/qnx/motion_sup/motion_sup.c" $SRC -o $TMP/motion_sup -lsocket || exit 2
qcc $F $INC "$ROOT/qnx/tests/m3_peer.c" $SRC -o $TMP/m3_peer -lsocket || exit 2
qcc $F $INC "$ROOT/qnx/logger/logger.c" $SRC -o $TMP/logger || exit 2
qcc $F $INC "$ROOT/qnx/mgmt/mgmt.c" $SRC -o $TMP/mgmt || exit 2
cat > $TMP/scenario.sh <<'EOS'
U_NO="on -u 1001:100"
chmod 755 /tmp/mgmt /tmp/logger
rm -f /dev/shmem/motion_state /tmp/motion.log* /tmp/motion_crash.log*
/tmp/motion_sup --autoenable --ticks 600 \
  --spawn "/tmp/logger --out /tmp/motion.log --max-bytes 2048 --duration-ms 5500" \
  --spawn "/tmp/logger --out /tmp/motion_crash.log --crash-after 3000" \
  --spawn "/tmp/mgmt poke 1500" > /tmp/sup.out 2>&1 &
sleep 0.5
/tmp/m3_peer --ticks 650 --linger-ms 300 > /tmp/peer.out 2>&1 &
sleep 2.5
echo "[mgmt_status_after_poke] $($U_NO /tmp/mgmt status 2>&1 | tail -1)"
echo "[mgmt_openrw_nonowner] $($U_NO /tmp/mgmt openrw 2>&1 | tail -1)"
echo "[mgmt_forward_status] $($U_NO /tmp/mgmt forward status 2>&1 | tail -1)"
ls -l /dev/shmem/motion_state
wait
echo "--- motion_sup output"; cat /tmp/sup.out
echo "--- files"; ls -l /tmp/motion.log* /tmp/motion_crash.log*
echo "--- tail motion.log"; tail -3 /tmp/motion.log
EOS
$SSH "slay -f motion_sup m3_peer logger mgmt 2>/dev/null; sleep 0.5" >/dev/null 2>&1
scp -q $KEY -o StrictHostKeyChecking=no -P 2222 $TMP/motion_sup $TMP/m3_peer $TMP/logger $TMP/mgmt $TMP/scenario.sh root@127.0.0.1:/tmp/ || exit 2
{
  echo "# M3 WP5 SHM read-only + crash detection on QNX VM (SIL, QEMU/KVM) — $(date -u +%F) commit $(git -C "$ROOT" rev-parse --short HEAD)"
  $SSH 'uname -a; sh /tmp/scenario.sh'
} > "$OUT" 2>&1
cat "$OUT"
fail=0
chk() { if grep -E -q -- "$2" "$OUT"; then echo "PASS $1"; else echo "FAIL $1 (/$2/)"; fail=1; fi; }
{
chk "mgmt write to read-only SHM -> SIGSEGV (11)"  "^DEATH mgmt pid=[0-9]+ signal=11"
chk "crashing logger detected (SIGSEGV)"           "^DEATH logger pid=[0-9]+ signal=11"
chk "healthy logger exits cleanly (exit=0)"        "^DEATH logger pid=[0-9]+ signal=0 exit=0"
chk "3 child exits observed (2 crashes + 1 clean)" "child_deaths=3 "
chk "motion_sup completed all 600 ticks"           "SUMMARY ticks=600 "
chk "motion_sup still RUN, no E-stop"              "estop_sent=0 child_deaths=3 final_state=RUN"
chk "mgmt (restarted view) reads RUN after poke"   "\[mgmt_status_after_poke\] MGMT state=RUN"
chk "non-owner O_RDWR on SHM denied"               "\[mgmt_openrw_nonowner\] MGMT openrw: denied"
chk "mgmt forward via /dev/motion works"           "\[mgmt_forward_status\] MGMT forward status: rc=0"
chk "healthy logger finished"                      "^LOGGER samples="
chk "log rotation happened"                        "LOGGER .*rotations=[1-9]"
# healthy logger kept logging after the mgmt death
mt=$(grep -E "^DEATH mgmt" "$OUT" | sed -n 's/.*at_tick=\([0-9]*\).*/\1/p')
lt=$(grep -E "^LOGGER" "$OUT" | sed -n 's/.*last_tick=\([0-9]*\).*/\1/p')
if [ -n "$mt" ] && [ -n "$lt" ] && [ "$lt" -gt "$((mt+100))" ]; then echo "PASS logger kept logging after mgmt death (tick $mt -> $lt)"; else echo "FAIL logger continuity (mgmt death tick='$mt' logger last_tick='$lt')"; fail=1; fi
} | tee -a "$OUT"
rm -rf "$TMP"; exit $fail
