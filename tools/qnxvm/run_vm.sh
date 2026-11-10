#!/bin/bash
# QNX VM với netdev user (không cần root); thêm -netdev socket sau để nối node
cd ~/qnxvm
exec qemu-system-x86_64 -smp 2 $( [ -w /dev/kvm ] && echo --enable-kvm --cpu host ) -m 1G \
 -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
 -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56 \
 -netdev socket,id=net1,listen=127.0.0.1:5555 -device virtio-net-pci,netdev=net1,mac=52:54:00:12:34:57 -kernel output/ifs.bin -serial file:output/qemu.out -display none \
 -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0
