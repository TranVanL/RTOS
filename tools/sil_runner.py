"""SIL runner: launch the FreeRTOS node in QEMU and run the M0 UDP echo acceptance test.

Pass/fail is decided by packet COUNTS (sent / received / lost), never by milliseconds.

  python tools/sil_runner.py echo --duration 600 --rate 100      # M0 rehearsal, host peer
  python tools/sil_runner.py echo --no-launch --host 192.168.x.y # node already running (QNX VM link)
"""
import argparse
import os
import socket
import struct
import subprocess
import sys
import time

DEFAULT_ELF = os.path.join(os.path.dirname(__file__), "..", "build", "freertos", "node.elf")


def qemu_cmd(elf, udp_port=5000, bind="127.0.0.1", icount=None, extra=()):
    """qemu-system-arm command line for the mps2-an385 node (user-mode networking + UDP hostfwd)."""
    cmd = ["qemu-system-arm", "-machine", "mps2-an385", "-cpu", "cortex-m3", "-kernel", elf,
           "-monitor", "null", "-semihosting", "-semihosting-config", "enable=on,target=native",
           "-serial", "stdio", "-nographic",
           "-netdev", f"user,id=n0,hostfwd=udp:{bind}:{udp_port}-:{udp_port}",
           "-net", "nic,model=lan9118,netdev=n0"]
    if icount is not None:
        cmd += ["-icount", icount]
    return cmd + list(extra)


def launch(elf, log_path, **kw):
    log = open(log_path, "wb")
    proc = subprocess.Popen(qemu_cmd(elf, **kw), stdout=log, stderr=subprocess.STDOUT)
    return proc, log


def echo_test(host, port, duration_s, rate_hz, size=32, reply_timeout_s=1.0):
    """Send numbered datagrams at rate_hz, count replies that match. Returns (sent, received, lost, bad)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(reply_timeout_s)
    sent = received = bad = 0
    period = 1.0 / rate_hz
    t_next = time.monotonic()
    t_end = t_next + duration_s
    while time.monotonic() < t_end:
        seq = sent
        payload = struct.pack("<I", seq) + bytes((seq + i) & 0xFF for i in range(size - 4))
        sock.sendto(payload, (host, port))
        sent += 1
        try:
            data, _ = sock.recvfrom(2048)
            if data == payload:
                received += 1
            else:
                bad += 1
        except socket.timeout:
            pass
        t_next += period
        delay = t_next - time.monotonic()
        if delay > 0:
            time.sleep(delay)
    return sent, received, sent - received - bad, bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("echo", help="UDP echo acceptance test")
    e.add_argument("--elf", default=DEFAULT_ELF)
    e.add_argument("--host", default="127.0.0.1")
    e.add_argument("--port", type=int, default=5000)
    e.add_argument("--duration", type=float, default=600.0, help="seconds (M0 acceptance: 600)")
    e.add_argument("--rate", type=float, default=100.0, help="datagrams per second")
    e.add_argument("--no-launch", action="store_true", help="do not start QEMU (node already running)")
    e.add_argument("--icount", default=None, help='e.g. "shift=0"')
    e.add_argument("--log", default="qemu_node.log")
    a = ap.parse_args(argv)

    proc = None
    if not a.no_launch:
        proc, log = launch(os.path.abspath(a.elf), a.log, udp_port=a.port, icount=a.icount)
        time.sleep(4)  # boot + network up (virtual time, not a pass criterion)
    try:
        sent, rx, lost, bad = echo_test(a.host, a.port, a.duration, a.rate)
    finally:
        if proc:
            proc.terminate()
            proc.wait(timeout=10)
            log.close()
    print(f"sent={sent} received={rx} lost={lost} mismatch={bad}")
    ok = lost == 0 and bad == 0 and sent > 0
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
