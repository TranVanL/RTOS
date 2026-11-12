"""M4 node behaviour in QEMU against the Python supervisor stand-in (tools/sil_supervisor.py).

Skipped when arm-none-eabi-gcc / qemu-system-arm / cmake are missing. Pass/fail uses FreeRTOS control-tick
counts printed by the node ("NODE_STATE ... ctl_tick=...") and frame counts - never host milliseconds.
"""
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))
import m2_protocol as proto  # noqa: E402
import sil_supervisor as sil  # noqa: E402

pytestmark = pytest.mark.skipif(
    not all(shutil.which(t) for t in ("arm-none-eabi-gcc", "qemu-system-arm", "cmake")),
    reason="ARM toolchain, QEMU and CMake are required")


@pytest.fixture(scope="session")
def node_elf(tmp_path_factory):
    build = tmp_path_factory.mktemp("m4-node-build")
    cfg = ["cmake", "-S", str(REPO_ROOT / "freertos"), "-B", str(build), "-G", "Unix Makefiles",
           "-DCMAKE_TOOLCHAIN_FILE=%s" % (REPO_ROOT / "freertos" / "cmake" / "arm-none-eabi.cmake"),
           "-DNODE_M4=ON", "-DCONTROL_TEST_STEP=OFF", "-DCONTROL_LOG_CSV=OFF", "-DNODE_PROBE_ENABLE=OFF"]
    for cmd in (cfg, ["cmake", "--build", str(build), "--parallel"]):
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        assert r.returncode == 0, r.stdout[-3000:]
    return build / "node.elf"


class Rig:
    def __init__(self, elf, tmp):
        self.node = sil.Node(elf=str(elf), log_path=str(tmp / "node.log"))
        self.sup = sil.Supervisor()
        self.sup.start()

    def close(self):
        self.sup.stop()
        self.node.stop()

    def log(self):
        return self.node.log_text()

    def transitions(self):
        """[(state, ctl_tick, cause, last_cmd_tick, estop_post_tick)] from the node UART log."""
        out = []
        for m in re.finditer(r"NODE_STATE (\w+) ctl_tick=(\d+) cause=(\d+) output=(\d) last_cmd_tick=(\d+) estop_post_tick=(\d+)",
                             self.log()):
            out.append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))))
        return out


@pytest.fixture
def rig(node_elf, tmp_path):
    r = Rig(node_elf, tmp_path)
    yield r
    r.close()


def _boot_to_ready(rig):
    assert rig.sup.wait_state("READY", 15), rig.log()[-1500:]


def _enable(rig, target=2048):
    rig.sup.command(sil.MODE_POSITION, target, 1000)
    assert rig.sup.wait_state("RUN", 5), rig.log()[-1500:]


def test_boot_heartbeat_and_enable_reaches_target(rig):
    _boot_to_ready(rig)
    assert rig.sup.telem[-1][1]["OutputState"] == 0
    _enable(rig, 2048)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and abs(rig.sup.telem[-1][1]["Position"] - 2048) > 40:
        time.sleep(0.1)
    last = rig.sup.telem[-1][1]
    assert abs(last["Position"] - 2048) <= 40, last          # closed loop follows the QNX-side command
    assert last["OutputState"] == 1
    assert rig.sup.e2e_bad == 0                              # every TELEM/HB frame passed E2E
    assert rig.node.wait_log("NODE_STATS", 10)               # periodic node counters (every 5000 ticks)
    stats = re.findall(r"NODE_STATS.*", rig.log())[-1]
    assert "cmd_bad=0" in stats, stats                     # missed= is informational (host-paced QEMU, SIL)


def test_telemetry_echo_gives_round_trips(rig):
    _boot_to_ready(rig)
    _enable(rig)
    time.sleep(2)
    assert len(rig.sup.rtt) > 100                            # CmdEcho matches CMD counters
    assert all(r >= 0 for r in rig.sup.rtt)


def test_estop_disables_within_one_control_tick(rig):
    _boot_to_ready(rig)
    _enable(rig)
    time.sleep(0.5)
    rig.sup.send_estop()
    assert rig.sup.wait_state("SAFE_STOP", 5), rig.log()[-1500:]
    t = [x for x in rig.transitions() if x[0] == "SAFE_STOP"][0]
    state, tick, cause, out, _, post_tick = t
    assert cause == 1 and out == 0                           # NODE_CAUSE_ESTOP, output disabled
    assert 0 <= tick - post_tick <= 1, t                     # ARCH 7: <= 1 control tick after the E-stop task accepted it
    assert rig.sup.telem[-1][1]["OutputState"] == 0


def test_command_loss_disables_after_50_ticks(rig):
    _boot_to_ready(rig)
    _enable(rig)
    time.sleep(0.5)
    rig.sup.send_cmds = False                                # supervisor "crash": CMDs just stop
    assert rig.sup.wait_state("SAFE_STOP", 5), rig.log()[-1500:]
    t = [x for x in rig.transitions() if x[0] == "SAFE_STOP"][0]
    _, tick, cause, out, last_cmd_tick, _ = t
    assert cause == 2 and out == 0                           # NODE_CAUSE_CMD_TIMEOUT
    assert tick - last_cmd_tick == 50, t                     # ARCH 7: 50 node ticks (own clock), exactly
    states = [x[0] for x in rig.transitions()]
    assert "DEGRADED" in states                              # passed through DEGRADED first


def test_node_recovers_after_disable_cmds(rig):
    _boot_to_ready(rig)
    _enable(rig)
    rig.sup.send_cmds = False
    assert rig.sup.wait_state("SAFE_STOP", 5)
    rig.sup.command(sil.MODE_DISABLE)
    rig.sup.send_cmds = True
    assert rig.sup.wait_state("READY", 5), rig.log()[-1500:]
    _enable(rig, 1024)                                       # and can run again


def test_corrupted_cmd_is_rejected(rig):
    _boot_to_ready(rig)
    payload = proto.pack_payload("CMD", {"Mode": 1, "VelLimit": 1000, "TargetPos": 5000})
    dg = bytearray(proto.encode_datagram("CMD", payload, 40000))
    dg[10] ^= 0xFF                                           # break the E2E CRC
    rig.sup.tx.sendto(bytes(dg), rig.sup.node)
    time.sleep(0.5)
    assert rig.sup.last_node_state() == "READY"              # never accepted as an enable
    rig.sup.send_cmds = False
    time.sleep(5.5)
    assert re.search(r"cmd_bad=[1-9]", rig.log())            # counted by the node
