#!/bin/bash
# M3 WP4: /dev/motion resmgr + motionctl on the QNX VM. Group check by ConnectClientInfo (gid 3000).
# VM must be up (tools/qnxvm/run_vm.sh). Log: docs/results/m3_wp4_devctl.txt. Pass/fail = errno values + FSM states.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=$ROOT/docs/results/m3_wp4_devctl.txt
KEY=""; [ -f ~/.ssh/id_qnxvm ] && KEY="-i $HOME/.ssh/id_qnxvm"
SSH="ssh $KEY -o StrictHostKeyChecking=no -p 2222 root@127.0.0.1"
source ~/qnx800/qnxsdp-env.sh >/dev/null 2>&1
INC="-I$ROOT/qnx/motion_sup -I$ROOT/common/safety -I$ROOT/common/shm -I$ROOT/common/ipc -I$ROOT/common/proto -I$ROOT/common/frame -I$ROOT/common/e2e -I$ROOT/common/generated"
SRC="$ROOT/common/safety/safety_fsm.c $ROOT/common/shm/motion_shm.c $ROOT/common/ipc/motion_ipc.c $ROOT/common/proto/proto_link.c $ROOT/common/frame/can_frame.c $ROOT/common/e2e/crc32p4.c $ROOT/common/e2e/e2e.c $ROOT/common/generated/can_matrix.c $ROOT/common/generated/can_matrix_meta.c"
TMP=$(mktemp -d)
F="-Vgcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror"
qcc $F $INC "$ROOT/qnx/motion_sup/motion_sup.c" $SRC -o $TMP/motion_sup -lsocket || exit 2
qcc $F $INC "$ROOT/qnx/tests/m3_peer.c" $SRC -o $TMP/m3_peer -lsocket || exit 2
qcc $F $INC "$ROOT/qnx/motionctl/motionctl.c" "$ROOT/common/ipc/motion_ipc.c" -o $TMP/motionctl || exit 2
cat > $TMP/scenario.sh <<'EOS'
U_NO="on -u 1001:100"; U_GRP="on -u 1002:3000"
m() { label=$1; shift; out=$("$@" 2>&1 | tail -2 | tr '\n' ' '); echo "[$label] $out"; }
grep -q '^motion:' /etc/group || echo 'motion:x:3000:' >> /etc/group
chmod 755 /tmp/motionctl
/tmp/motion_sup --ticks 1500 > /tmp/sup.out 2>&1 &
sleep 0.5
/tmp/m3_peer --ticks 1600 --linger-ms 500 > /tmp/peer.out 2>&1 &
sleep 1
ls -l /dev/motion
m noGrp_status    $U_NO  /tmp/motionctl status
m noGrp_target    $U_NO  /tmp/motionctl target 1000 500
m noGrp_enable    $U_NO  /tmp/motionctl enable
m noGrp_disable   $U_NO  /tmp/motionctl disable
m noGrp_reset     $U_NO  /tmp/motionctl reset
m grp_badcmd      $U_GRP /tmp/motionctl rawcmd 99
m grp_posRange    $U_GRP /tmp/motionctl target 2000000 500
m grp_velRange    $U_GRP /tmp/motionctl target 100 5000
m grp_badSize     $U_GRP /tmp/motionctl rawtarget 100 500 4
m grp_target      $U_GRP /tmp/motionctl target 1000 500
m grp_reset_ready $U_GRP /tmp/motionctl reset
m grp_enable      $U_GRP /tmp/motionctl enable
sleep 0.2
m status_run      $U_NO  /tmp/motionctl status
m noGrp_estop     $U_NO  /tmp/motionctl estop
sleep 0.2
m status_safe     $U_NO  /tmp/motionctl status
m noGrp_reset2    $U_NO  /tmp/motionctl reset
m grp_reset2      $U_GRP /tmp/motionctl reset
sleep 0.2
m status_ready    $U_NO  /tmp/motionctl status
wait
echo "[sup] $(cat /tmp/sup.out | tail -1)"
EOS
scp -q $KEY -o StrictHostKeyChecking=no -P 2222 $TMP/motion_sup $TMP/m3_peer $TMP/motionctl $TMP/scenario.sh root@127.0.0.1:/tmp/ || exit 2
{
  echo "# M3 WP4 /dev/motion on QNX VM (SIL, QEMU/KVM) — $(date -u +%F) commit $(git -C "$ROOT" rev-parse --short HEAD)"
  $SSH 'uname -a; rm -f /dev/shmem/motion_state; sh /tmp/scenario.sh'
} > "$OUT" 2>&1
cat "$OUT"
fail=0
chk() { # label pattern
  if grep -F "[$1]" "$OUT" | grep -q -E "$2"; then echo "PASS $1 ~ $2"; else echo "FAIL $1 ~ $2"; fail=1; fi; }
{
chk noGrp_status "rc=0 "
chk noGrp_target "rc=1 "; chk noGrp_enable "rc=1 "; chk noGrp_disable "rc=1 "; chk noGrp_reset "rc=1 "
chk grp_badcmd "rc=22 "; chk grp_posRange "rc=22 "; chk grp_velRange "rc=22 "; chk grp_badSize "rc=22 "
chk grp_target "rc=0 "
chk grp_reset_ready "rc=16 "
chk grp_enable "rc=0 "
chk status_run "state=RUN"
chk noGrp_estop "rc=0 "
chk status_safe "state=SAFE_STOP cause=1"
chk noGrp_reset2 "rc=1 "
chk grp_reset2 "rc=0 "
chk status_ready "state=READY"
} | tee -a "$OUT"
rm -rf "$TMP"; exit $fail
