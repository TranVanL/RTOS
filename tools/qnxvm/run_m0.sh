#!/bin/bash
# M0 acceptance: QNX process (m0_peer) <-> FreeRTOS node, 2 chiều. Dùng: tools/qnxvm/run_m0.sh [duration_s] [rate_hz]
# Cần: image QNX ở ~/qnxvm, khóa ~/.ssh/id_qnxvm, build/freertos_link/node.elf (-DNODE_IP=192,168,50,2 -DNODE_GATEWAY=192,168,50,1)
set -u
DUR=${1:-600}; RATE=${2:-100}
ROOT=$(cd "$(dirname "$0")/../.." && pwd); OUT=$ROOT/docs/results; mkdir -p "$OUT"
SSH="ssh -i $HOME/.ssh/id_qnxvm -o StrictHostKeyChecking=no -p 2222 root@127.0.0.1"
source ~/qnx800/qnxsdp-env.sh >/dev/null 2>&1
cleanup() { kill $VMPID $NODEPID 2>/dev/null; }; trap cleanup EXIT

qcc -Vgcc_ntox86_64 -Wall -Wextra -o ~/qnxvm/m0_peer "$ROOT/qnx/tests/m0_peer.c" -lsocket || exit 2
"$ROOT/tools/qnxvm/run_vm.sh" >/dev/null 2>&1 & VMPID=$!
for i in $(seq 60); do $SSH true 2>/dev/null && break; sleep 2; done
$SSH 'ifconfig vtnet1 192.168.50.1/24 up' || exit 2
scp -q -i ~/.ssh/id_qnxvm -P 2222 ~/qnxvm/m0_peer root@127.0.0.1:/tmp/m0_peer
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -kernel "$ROOT/build/freertos_link/node.elf" -monitor null \
  -semihosting -semihosting-config enable=on,target=native -serial file:"$OUT/m0_node_${DUR}s.uart.txt" -nographic \
  -netdev socket,id=n0,connect=127.0.0.1:5555 -net nic,model=lan9118,netdev=n0 >/dev/null 2>&1 & NODEPID=$!
sleep 5
$SSH "chmod +x /tmp/m0_peer; /tmp/m0_peer 192.168.50.2 $DUR $RATE" | tee "$OUT/m0_qnx_${DUR}s.result"
exit ${PIPESTATUS[0]}
