"""Python stand-in for the QNX supervisor (M4 host rehearsal, CI-capable).

Talks to the FreeRTOS node in QEMU over UDP with the real wire format (tools/m2_protocol.py):
CMD every 10 ms on 5000, E-stop on 5001, receives TELEM + HEARTBEAT on a local port.
It is NOT the product supervisor (that is qnx/motion_sup); it exists so the node can be tested on a
public CI runner without QNX. Pass/fail criteria use frame/tick COUNTS read from the node UART log;
host-clock numbers (round trip) are informational, SIL, host Python + QEMU.

Port plan with QEMU user networking (see qemu_cmd(): host ports are offset so the node's
telemetry (to host:5000) does not collide with the hostfwd listener):
    host 127.0.0.1:15000 -> node :5000 (CMD)      node -> host:5000 (TELEM/HB, via 10.0.2.2)
    host 127.0.0.1:15001 -> node :5001 (E-stop)
"""
import os
import socket
import struct
import subprocess
import threading
import time

import m2_protocol as proto

DEFAULT_ELF = os.path.join(os.path.dirname(__file__), "..", "build", "freertos_m4_host", "node.elf")
MODE_DISABLE, MODE_POSITION, MODE_HOLD = 0, 1, 2
NODE_STATES = ["INIT", "READY", "RUN", "DEGRADED", "SAFE_STOP"]


def qemu_cmd(elf, cmd_port=15000, estop_port=15001, icount=None):
    cmd = ["qemu-system-arm", "-machine", "mps2-an385", "-cpu", "cortex-m3", "-kernel", elf,
           "-monitor", "null", "-semihosting", "-semihosting-config", "enable=on,target=native",
           "-serial", "stdio", "-nographic",
           "-netdev", "user,id=n0,hostfwd=udp:127.0.0.1:%d-:5000,hostfwd=udp:127.0.0.1:%d-:5001" % (cmd_port, estop_port),
           "-net", "nic,model=lan9118,netdev=n0"]
    if icount:
        cmd += ["-icount", icount]
    return cmd


class Node:
    """QEMU process with its UART captured to a log file."""

    def __init__(self, elf=DEFAULT_ELF, log_path="node_m4.log", **kw):
        self.log_path = log_path
        self._log = open(log_path, "wb")
        self.proc = subprocess.Popen(qemu_cmd(os.path.abspath(elf), **kw), stdout=self._log, stderr=subprocess.STDOUT)

    def log_text(self):
        self._log.flush()
        with open(self.log_path, errors="replace") as f:
            return f.read()

    def wait_log(self, needle, timeout=15.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if needle in self.log_text():
                return True
            time.sleep(0.1)
        return False

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self._log.close()


class Supervisor:
    def __init__(self, node_host="127.0.0.1", cmd_port=15000, estop_port=15001, listen_port=5000):
        self.node = (node_host, cmd_port)
        self.estop_addr = (node_host, estop_port)
        self.rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx.bind(("0.0.0.0", listen_port))
        self.rx.settimeout(0.05)
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.mode, self.target, self.vel = MODE_DISABLE, 0, 1000
        self.send_cmds = True
        self.cmd_counter = 0
        self.estop_counter = 0
        self.sent_at = {}            # cmd counter -> host monotonic time
        self.telem, self.hb = [], []   # (host time, signals)
        self.rtt = []                # seconds, host clock (informational)
        self.e2e_bad = 0
        self.cmd_sent = 0
        self._stop = threading.Event()
        self._states = {}
        self._threads = [threading.Thread(target=self._rx_loop, daemon=True),
                         threading.Thread(target=self._tx_loop, daemon=True)]

    def start(self):
        for t in self._threads:
            t.start()

    def stop(self):
        self._stop.set()
        for t in self._threads:
            t.join(timeout=2)
        self.rx.close()
        self.tx.close()

    # ---- control surface ----
    def command(self, mode, target=None, vel=None):
        if target is not None:
            self.target = target
        if vel is not None:
            self.vel = vel
        self.mode = mode

    def send_estop(self, copies=3, spacing_s=0.001, reason=1):
        for _ in range(copies):
            payload = proto.pack_payload("ESTOP", {"Command": 1, "Reason": reason})
            self.tx.sendto(proto.encode_datagram("ESTOP", payload, self.estop_counter & 0xFFFF), self.estop_addr)
            self.estop_counter += 1
            time.sleep(spacing_s)

    def send_raw_estop(self, datagram):
        self.tx.sendto(datagram, self.estop_addr)

    # ---- threads ----
    def _tx_loop(self):
        t_next = time.monotonic()
        while not self._stop.is_set():
            if self.send_cmds:
                payload = proto.pack_payload("CMD", {"Mode": self.mode, "VelLimit": self.vel, "TargetPos": self.target})
                c = self.cmd_counter & 0xFFFF
                self.sent_at[c] = time.monotonic()
                self.tx.sendto(proto.encode_datagram("CMD", payload, c), self.node)
                self.cmd_counter += 1
                self.cmd_sent += 1
            t_next += 0.010
            delay = t_next - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                t_next = time.monotonic()

    def _rx_loop(self):
        while not self._stop.is_set():
            try:
                data, _ = self.rx.recvfrom(256)
            except socket.timeout:
                continue
            except OSError:
                return
            now = time.monotonic()
            try:
                d = proto.decode_datagram(data, receiver_states=self._states, now=int(now * 1000))
            except proto.ProtocolError:
                self.e2e_bad += 1
                continue
            if d["status"] != "OK":
                self.e2e_bad += 1
                continue
            if d["name"] == "TELEM":
                self.telem.append((now, d["signals"]))
                t0 = self.sent_at.pop(d["signals"]["CmdEcho"], None)
                if t0 is not None:
                    self.rtt.append(now - t0)
            elif d["name"] == "HEARTBEAT":
                self.hb.append((now, d["signals"]))

    # ---- queries ----
    def last_node_state(self):
        return NODE_STATES[self.hb[-1][1]["NodeState"]] if self.hb else None

    def wait_state(self, state, timeout=10.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if self.last_node_state() == state:
                return True
            time.sleep(0.02)
        return False
