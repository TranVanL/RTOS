"""Byte-for-byte interoperability tests between the C and Python codecs."""

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from m2_protocol import (  # noqa: E402
    MESSAGE_INFO,
    decode_datagram,
    encode_datagram,
    pack_payload,
)


SIGNAL_CASES = {
    "ESTOP": {"Command": 255, "Reason": 0},
    "CMD": {"Mode": 3, "VelLimit": 65535, "TargetPos": -2147483648},
    "TELEM": {
        "Position": 2147483647,
        "Velocity": -32768,
        "Current": 32767,
        "OutputState": 1,
    },
    "HEARTBEAT": {"NodeState": 4, "AliveCounter": 255, "CpuLoad": 100, "FaultCode": 65535},
    "FAULT_RECORD": {"PC": 0xFFFFFFFF, "LR": 0x80000000, "CFSR": 0x12345678, "MMFAR": 0},
}


def test_c_and_python_codecs_match_for_every_dbc_message(m2_protocol_build):
    cli = m2_protocol_build / "protocol_cli"
    counter = 0x1234

    for name, signals in SIGNAL_CASES.items():
        info = next(item for item in MESSAGE_INFO.values() if item["name"] == name)
        payload = pack_payload(name, signals, scaling=False)

        c_encoded = subprocess.run(
            [str(cli), "encode", "%03x" % info["can_id"], "%04x" % counter, payload.hex()],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        ).stdout.strip()
        c_datagram = bytes.fromhex(c_encoded)
        python_datagram = encode_datagram(name, payload, counter)
        assert c_datagram == python_datagram

        decoded = decode_datagram(
            c_datagram,
            source_port=info["udp_port"],
            now=100,
        )
        assert decoded["status"] == "OK"
        assert decoded["signals"] == signals

        c_decoded = subprocess.run(
            [str(cli), "decode", python_datagram.hex()],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        ).stdout.strip()
        fields = dict(part.split("=", 1) for part in c_decoded.split(";"))
        assert fields["STATUS"] == "OK"
        assert int(fields["ID"], 16) == info["can_id"]
        assert int(fields["COUNTER"], 16) == counter
        assert bytes.fromhex(fields["PAYLOAD"]) == payload


def test_c_codec_matches_and_decodes_independent_golden_datagram(m2_protocol_build):
    cli = m2_protocol_build / "protocol_cli"
    expected = bytes.fromhex(
        "FD CA 10 00 01 10 0E 00 34 12 10 00 00 00 34 83 47 2A 01 02 00 00"
    )

    c_encoded = subprocess.run(
        [str(cli), "encode", "010", "1234", "0102"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    ).stdout.strip()
    assert bytes.fromhex(c_encoded) == expected

    c_decoded = subprocess.run(
        [str(cli), "decode", expected.hex()],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    ).stdout.strip()
    assert "STATUS=OK" in c_decoded
    assert "ID=010" in c_decoded
    assert "COUNTER=1234" in c_decoded
    assert "PAYLOAD=0102" in c_decoded
