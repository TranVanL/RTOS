#!/bin/bash
# M4 end-to-end: REAL motion_sup on the QNX VM <-> REAL FreeRTOS node in QEMU (L2 link via -netdev socket).
# Usage: tools/qnxvm/run_m4.sh [A B C ...|long [minutes]]   (default: A B C)
#   A smoke: autoenable, square target, 3000 ticks     B E-stop at tick 1000
#   C motion_sup killed (kill -9) while RUN: node must disable itself
#   long: M4 acceptance, N minutes in RUN (default 30), E2E errors must stay 0
# Needs: VM up (tools/qnxvm/run_vm.sh), ~/.ssh/id_qnxvm, arm-none-eabi-gcc, qemu-system-arm, QNX SDP at ~/qnx800.
# Pass/fail = counts (ticks, frames). ms numbers are informational, SIL, QNX VM + QEMU. Logs: docs/results/m4_*.txt
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=$ROOT/docs/results; mkdir -p "$OUT"
KEY=""; [ -f ~/.ssh/id_qnxvm ] && KEY="-i $HOME/.ssh/id_qnxvm"
SSH="ssh $KEY -o StrictHostKeyChecking=no -p 2222 root@127.0.0.1"
source ~/qnx800/qnxsdp-env.sh >/dev/null 2>&1
TMP=$(mktemp -d); NODEPID=""
cleanup() { [ -n "$NODEPID" ] && kill $NODEPID 2>/dev/null; rm -rf "$TMP"; }; trap cleanup EXIT

INC="-I$ROOT/common/safety -I$ROOT/common/shm -I$ROOT/common/ipc -I$ROOT/common/proto -I$ROOT/common/frame -I$ROOT/common/e2e -I$ROOT/common/generated"
SRC="$ROOT/common/safety/safety_fsm.c $ROOT/common/shm/motion_shm.c $ROOT/common/ipc/motion_ipc.c $ROOT/common/proto/proto_link.c $ROOT/common/frame/can_frame.c $ROOT/common/e2e/crc32p4.c $ROOT/common/e2e/e2e.c $ROOT/common/generated/can_matrix.c $ROOT/common/generated/can_matrix_meta.c"
qcc -Vgcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror $INC "$ROOT/qnx/motion_sup/motion_sup.c" $SRC -o $TMP/motion_sup -lsocket || exit 2
(cd "$ROOT" && cmake --preset node_m4_qnx -S freertos >/dev/null && cmake --build build/freertos_m4_qnx -j >/dev/null) || exit 2
ELF=$ROOT/build/freertos_m4_qnx/node.elf

$SSH 'ifconfig vtnet1 192.168.50.1/24 up; grep -q "^motion:" /etc/group || echo "motion:x:3000:" >> /etc/group' || { echo "VM not reachable"; exit 2; }
scp -q $KEY -o StrictHostKeyChecking=no -P 2222 $TMP/motion_sup root@127.0.0.1:/tmp/motion_sup || exit 2

start_node() { # uart log path
  [ -n "$NODEPID" ] && { kill $NODEPID 2>/dev/null; wait $NODEPID 2>/dev/null; }
  qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -kernel "$ELF" -monitor null -semihosting \
    -semihosting-config enable=on,target=native -serial file:"$1" -nographic \
    -netdev socket,id=n0,connect=127.0.0.1:5555 -net nic,model=lan9118,netdev=n0 >/dev/null 2>&1 &
  NODEPID=$!
  for i in $(seq 40); do grep -q "comm: UDP" "$1" 2>/dev/null && break; sleep 0.5; done
  sleep 2   # let the node's first heartbeats (and ARP) settle
}

fail=0
chk() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; fail=1; fi; }
chkle() { if [ "$2" -le "$3" ] 2>/dev/null; then echo "PASS $1 ($2 <= $3)"; else echo "FAIL $1: got '$2' want <= $3"; fail=1; fi; }
field() { grep -oE "(^| )$2=[A-Za-z0-9_]*" | tail -1 | cut -d= -f2; }   # whole-word key (ticks= must not match monitor_ticks=)
SUPARGS="--peer-ip 192.168.50.2 --peer-port 5000 --estop-port 5001 --local-port 5000"
header() { echo "# M4 end-to-end $1 (SIL: QNX VM + QEMU node, not hard real-time) — $(date -u +%F) commit $(git -C "$ROOT" rev-parse --short HEAD)"; $SSH uname -a; }

scenario() { # name, supargs, [kill-after-seconds]
  local n=$1 args=$2 killafter=${3:-0} U=$TMP/node_$1.uart
  echo "=== $n: motion_sup $args"
  start_node $U
  $SSH "rm -f /dev/shmem/motion_state /tmp/rtt_$n.txt; slay -f motion_sup 2>/dev/null; /tmp/motion_sup $SUPARGS --rtt-log /tmp/rtt_$n.txt $args > /tmp/sup_$n.out 2>&1 &
        PID=\$!; if [ $killafter -gt 0 ]; then sleep $killafter; kill -9 \$PID; else wait \$PID; fi; sleep 1; cat /tmp/sup_$n.out"
  sleep 1
  echo "--- node UART (NODE_STATE/NODE_STATS)"; grep -E "NODE_STATE|NODE_STATS|boot|comm:|estop:" $U
  cp $U $TMP/last.uart
  $SSH "cat /tmp/rtt_$n.txt 2>/dev/null" > "$OUT/m4_rtt_$n.txt"
}

want="${*:-A B C}"
RES=$OUT/m4_scenarios.txt
long_criteria() { # file ticks
  local RES=$1 TICKS=$2
  L=$(awk '/^=== L:/{f=1;next} /^=== /{f=0} f' "$RES")
  local ss; ss=$(echo "$L" | grep 'NODE_STATE SAFE_STOP')
  { echo "--- criteria (counts only)"
    chk "L ticks == $TICKS"        "$(echo "$L" | grep SUMMARY | field x ticks)" "$TICKS"
    chk "L final_state RUN"        "$(echo "$L" | grep SUMMARY | field x final_state)" RUN
    chk "L e2e_err == 0"           "$(echo "$L" | grep SUMMARY | field x e2e_err)" 0
    chk "L QNX FSM never left RUN" "$(echo "$L" | grep -cE '^FSM (DEGRADED|SAFE_STOP)')" 0
    echo "INFO L hb_miss (isolated single-tick misses, informational): $(echo "$L" | grep SUMMARY | field x hb_miss)"
    chk "L estop_sent == 0"        "$(echo "$L" | grep SUMMARY | field x estop_sent)" 0
    chk "L node cmd_bad == 0"      "$(echo "$L" | grep NODE_STATS | tail -1 | field x cmd_bad)" 0
    # The node leaves RUN exactly once: after motion_sup exits at the end of the run (command timeout, 50 own ticks).
    chk "L node left RUN exactly once (end of run)" "$(echo "$ss" | grep -c .)" 1
    chk "L that exit is CMD_TIMEOUT (2)"            "$(echo "$ss" | field x cause)" 2
    p=$(echo "$ss" | field x ctl_tick); q=$(echo "$ss" | field x last_cmd_tick)
    chk "L timeout gap == 50 node ticks"            "$((p - q))" 50
  }
}

if [ "${1:-}" = "evallong" ]; then   # re-evaluate a saved long log: evallong FILE TICKS
  long_criteria "$2" "$3" | tee -a "$2"; exit $fail
fi
if [ "${1:-}" = "long" ]; then
  MIN=${2:-30}; TICKS=$((MIN * 60 * 100)); RES=$OUT/m4_long_${MIN}min.txt
  { header "long run ${MIN} min"
    scenario L "--autoenable --ticks $TICKS --target 2048 --vel 1000 --square 500"
  } > "$RES" 2>&1
  cat "$RES"
  long_criteria "$RES" "$TICKS" | tee -a "$RES"
  exit $fail
fi

{ header "scenarios"
  for s in $want; do
    case $s in
      A) scenario A "--autoenable --ticks 3000 --target 2048 --vel 1000 --square 400" ;;
      B) scenario B "--autoenable --ticks 2000 --target 2048 --vel 1000 --estop-at 1000" ;;
      C) scenario C "--autoenable --ticks 100000 --target 2048 --vel 1000 --square 400" 20 ;;
    esac
  done
} > "$RES" 2>&1
cat "$RES"
sect() { awk -v s="=== $1:" 'index($0,s)==1{f=1;next} /^=== /{f=0} f' "$RES"; }
{
echo "--- criteria (counts only)"
for s in $want; do
  S=$(sect $s)
  case $s in
    A) chk "A final_state RUN"           "$(echo "$S" | grep SUMMARY | field x final_state)" RUN
       chk "A ticks == 3000"             "$(echo "$S" | grep SUMMARY | field x ticks)" 3000
       chk "A e2e_err == 0"              "$(echo "$S" | grep SUMMARY | field x e2e_err)" 0
       chk "A QNX FSM never left RUN"    "$(echo "$S" | grep -cE '^FSM (DEGRADED|SAFE_STOP)')" 0
       echo "INFO A hb_miss (isolated single-tick misses, informational): $(echo "$S" | grep SUMMARY | field x hb_miss)"
       chk "A estop_sent == 0"           "$(echo "$S" | grep SUMMARY | field x estop_sent)" 0
       chk "A node cmd_bad == 0"         "$(echo "$S" | grep NODE_STATS | tail -1 | field x cmd_bad)" 0
       n=$(echo "$S" | grep RTT | field x samples); [ "${n:-0}" -gt 100 ] && echo "PASS A round-trip samples $n" || { echo "FAIL A rtt samples '$n'"; fail=1; } ;;
    B) chk "B qnx final SAFE_STOP"       "$(echo "$S" | grep SUMMARY | field x final_state)" SAFE_STOP
       chk "B qnx cause ESTOP (1)"       "$(echo "$S" | grep SUMMARY | field x cause)" 1
       chk "B estop_sent == 3"           "$(echo "$S" | grep SUMMARY | field x estop_sent)" 3
       chk "B node reached SAFE_STOP"    "$(echo "$S" | grep -c 'NODE_STATE SAFE_STOP')" 1
       chk "B node cause ESTOP (1)"      "$(echo "$S" | grep 'NODE_STATE SAFE_STOP' | field x cause)" 1
       p=$(echo "$S" | grep 'NODE_STATE SAFE_STOP' | field x ctl_tick); q=$(echo "$S" | grep 'NODE_STATE SAFE_STOP' | field x estop_post_tick)
       chkle "B node disable within 1 control tick of E-stop task" "$((p - q))" 1 ;;
    C) chk "C node reached SAFE_STOP"    "$(echo "$S" | grep -c 'NODE_STATE SAFE_STOP')" 1
       chk "C node cause CMD_TIMEOUT (2)" "$(echo "$S" | grep 'NODE_STATE SAFE_STOP' | field x cause)" 2
       p=$(echo "$S" | grep 'NODE_STATE SAFE_STOP' | field x ctl_tick); q=$(echo "$S" | grep 'NODE_STATE SAFE_STOP' | field x last_cmd_tick)
       chk "C node tick gap == 50 (own clock)" "$((p - q))" 50
       chk "C output disabled"           "$(echo "$S" | grep 'NODE_STATE SAFE_STOP' | field x output)" 0 ;;
  esac
done
} | tee -a "$RES"
exit $fail
