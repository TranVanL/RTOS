"""M2 DBC validation and deterministic code-generation tests."""

import subprocess
import sys
from pathlib import Path

import cantools


REPO_ROOT = Path(__file__).resolve().parents[2]
DBC_PATH = REPO_ROOT / "common" / "can_matrix.dbc"
GENERATOR = REPO_ROOT / "tools" / "dbc_codegen.py"
GENERATED_DIR = REPO_ROOT / "common" / "generated"
EXPECTED = {
    "ESTOP": (0x010, 2, 5001),
    "CMD": (0x100, 7, 5000),
    "TELEM": (0x180, 9, 5000),
    "HEARTBEAT": (0x1F0, 5, 5000),
    "FAULT_RECORD": (0x1F1, 16, 5000),
}


def test_dbc_inventory_and_routes() -> None:
    database = cantools.database.load_file(str(DBC_PATH), strict=True)
    assert len(database.messages) == len(EXPECTED)

    for name, (frame_id, payload_length, udp_port) in EXPECTED.items():
        message = database.get_message_by_name(name)
        assert message.frame_id == frame_id
        assert message.length == payload_length
        assert "UDP :%d" % udp_port in message.comment
        assert message.is_extended_frame is False
        assert message.length + 12 <= 64


def test_dbc_payload_boundaries_round_trip() -> None:
    database = cantools.database.load_file(str(DBC_PATH), strict=True)
    cases = {
        "ESTOP": [
            {"Command": 0, "Reason": 0},
            {"Command": 255, "Reason": 255},
        ],
        "CMD": [
            {"Mode": 0, "VelLimit": 0, "TargetPos": -2147483648},
            {"Mode": 3, "VelLimit": 65535, "TargetPos": 2147483647},
        ],
        "TELEM": [
            {"Position": -2147483648, "Velocity": -32768, "Current": -32768, "OutputState": 0},
            {"Position": 2147483647, "Velocity": 32767, "Current": 32767, "OutputState": 1},
        ],
        "HEARTBEAT": [
            {"NodeState": 0, "AliveCounter": 0, "CpuLoad": 0, "FaultCode": 0},
            {"NodeState": 5, "AliveCounter": 255, "CpuLoad": 100, "FaultCode": 65535},
        ],
        "FAULT_RECORD": [
            {"PC": 0, "LR": 0xFFFFFFFF, "CFSR": 0x80000000, "MMFAR": 0x12345678},
            {"PC": 0xFFFFFFFF, "LR": 0x80000000, "CFSR": 0x12345678, "MMFAR": 0},
        ],
    }

    for name, signal_sets in cases.items():
        message = database.get_message_by_name(name)
        for signals in signal_sets:
            encoded = message.encode(signals, scaling=False, strict=True)
            decoded = message.decode(encoded, scaling=False, decode_choices=False)
            assert decoded == signals
            assert len(encoded) == message.length


def test_codegen_is_reproducible_and_check_detects_stale_output(tmp_path: Path) -> None:
    generated_copy = tmp_path / "generated"
    command = [sys.executable, str(GENERATOR), "--output-dir", str(generated_copy)]
    subprocess.run(command, cwd="/", check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    expected_files = (
        "can_matrix.c",
        "can_matrix.h",
        "can_matrix_meta.c",
        "can_matrix_meta.h",
    )
    for filename in expected_files:
        assert (generated_copy / filename).read_bytes() == (GENERATED_DIR / filename).read_bytes()

    subprocess.run(
        [sys.executable, str(GENERATOR), "--output-dir", str(GENERATED_DIR), "--check"],
        cwd="/",
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    stale_file = generated_copy / "can_matrix_meta.h"
    stale_file.write_text(stale_file.read_text(encoding="utf-8") + "\n/* stale */\n", encoding="utf-8")
    result = subprocess.run(
        [sys.executable, str(GENERATOR), "--output-dir", str(generated_copy), "--check"],
        cwd="/",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    )
    assert result.returncode != 0
    assert "can_matrix_meta.h" in result.stderr
