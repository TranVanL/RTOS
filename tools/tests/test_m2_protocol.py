"""M2 wire/E2E reference codec and frame-sniffer tests."""

import sys
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import frame_sniffer  # noqa: E402
from m2_protocol import (  # noqa: E402
    MESSAGE_BY_NAME,
    MESSAGE_INFO,
    ProtocolError,
    check_timeout,
    crc32p4,
    decode_datagram,
    encode_datagram,
    new_receiver_state,
    pack_payload,
    protect_e2e,
)


def _sample_signals(message_name):
    info = MESSAGE_BY_NAME[message_name]
    message = info["dbc_message"]
    values = {}
    for signal in message.signals:
        raw_min = int(signal.minimum if signal.minimum is not None else 0)
        raw_max = int(signal.maximum if signal.maximum is not None else 0)
        if signal.choices:
            raw_min = min(signal.choices.keys())
        values[signal.name] = raw_min if raw_min >= 0 else raw_min
        if raw_max != raw_min:
            values[signal.name] = raw_max
    return values


def test_crc32p4_known_answer():
    assert crc32p4(b"123456789") == 0x1697D06A


def test_independent_estop_e2e_and_udp_golden_vector():
    payload = bytes.fromhex("01 02")
    expected_e2e = bytes.fromhex("0E 00 34 12 10 00 00 00 34 83 47 2A 01 02")
    expected_datagram = bytes.fromhex(
        "FD CA 10 00 01 10 0E 00 34 12 10 00 00 00 34 83 47 2A 01 02 00 00"
    )

    assert protect_e2e(payload, 0x1234, 0x10) == expected_e2e
    assert encode_datagram("ESTOP", payload, 0x1234) == expected_datagram
    decoded = decode_datagram(expected_datagram, source_port=5001, now=1)
    assert decoded["status"] == "OK"
    assert decoded["payload"] == payload


def test_round_trip_all_dbc_messages_and_ports():
    for can_id, info in MESSAGE_INFO.items():
        signals = _sample_signals(info["name"])
        payload = pack_payload(info["name"], signals, scaling=False)
        datagram = encode_datagram(info["name"], payload, counter=17)
        state = {}
        decoded = decode_datagram(
            datagram,
            source_port=info["udp_port"],
            receiver_states=state,
            now=100,
        )
        assert decoded["can_id"] == can_id
        assert decoded["data_id"] == can_id
        assert decoded["name"] == info["name"]
        assert decoded["status"] == "OK"
        assert decoded["signals"] == signals
        assert len(datagram) == 6 + info["wire_length"]


def test_repeated_wrong_sequence_and_crc_error_do_not_advance_state():
    info = MESSAGE_BY_NAME["CMD"]
    payload = pack_payload("CMD", {"Mode": 1, "VelLimit": 500, "TargetPos": -1234})
    states = {}

    valid = encode_datagram("CMD", payload, 65535)
    assert decode_datagram(valid, 5000, states, 10)["status"] == "OK"
    assert decode_datagram(valid, 5000, states, 11)["status"] == "REPEATED"

    wrapped = encode_datagram("CMD", payload, 1)
    assert decode_datagram(wrapped, 5000, states, 12)["status"] == "OK"
    wrong_sequence = encode_datagram("CMD", payload, 4)
    assert decode_datagram(wrong_sequence, 5000, states, 13)["status"] == "WRONG_SEQ"

    state = states[info["can_id"]]
    assert state["last_counter"] == 1
    assert state["last_accepted_at"] == 12

    corrupted = bytearray(encode_datagram("CMD", payload, 2))
    corrupted[6 + 12] ^= 0x01
    result = decode_datagram(corrupted, 5000, states, 14)
    assert result["status"] == "CRC_ERR"
    assert state["last_counter"] == 1
    assert state["last_accepted_at"] == 12


def test_length_id_padding_port_and_outer_frame_errors():
    payload = pack_payload("ESTOP", {"Command": 1, "Reason": 2})
    datagram = bytearray(encode_datagram("ESTOP", payload, 3))

    with pytest.raises(ProtocolError) as error:
        decode_datagram(datagram, source_port=5000)
    assert error.value.status == "LEN_OR_ID_ERR"

    with pytest.raises(ProtocolError):
        decode_datagram(datagram[:-1], source_port=5001)

    datagram[6] ^= 1
    assert decode_datagram(datagram, source_port=5001)["status"] == "LEN_OR_ID_ERR"

    datagram = bytearray(encode_datagram("ESTOP", payload, 3))
    datagram[-1] = 1
    with pytest.raises(ProtocolError) as error:
        decode_datagram(datagram, source_port=5001)
    assert error.value.status == "FRAME_ERR"

    datagram = bytearray(encode_datagram("ESTOP", payload, 3))
    datagram[2:4] = b"\x00\x02"
    with pytest.raises(ProtocolError) as error:
        decode_datagram(datagram, source_port=5000)
    assert error.value.status == "LEN_OR_ID_ERR"

    datagram = bytearray(encode_datagram("ESTOP", payload, 3))
    datagram[4] = 0x03
    with pytest.raises(ProtocolError) as error:
        decode_datagram(datagram, source_port=5001)
    assert error.value.status == "FRAME_ERR"


def test_e2e_length_and_timeout_wrap_behavior():
    payload = b"\xAA\x55"
    protected = bytearray(protect_e2e(payload, 9, 0x10))
    state = new_receiver_state(0xFFFFFFF0)
    assert check_timeout(state, 0x0000000D, 30) == "OK"
    assert check_timeout(state, 0x0000000E, 30) == "TIMEOUT"

    protected[0] ^= 1
    from m2_protocol import verify_e2e

    status, decoded = verify_e2e(protected, len(payload), 0x10, state, 0x0000000E)
    assert status == "LEN_OR_ID_ERR"
    assert decoded is None
    with pytest.raises(ValueError):
        check_timeout(state, 0x0000000E, 0)


def test_offline_sniffer_hex_input(capsys):
    payload = pack_payload("HEARTBEAT", {
        "NodeState": 2,
        "AliveCounter": 9,
        "CpuLoad": 50,
        "FaultCode": 0,
    })
    datagram = encode_datagram("HEARTBEAT", payload, 4)
    result = frame_sniffer.main(["--hex", datagram.hex()])
    captured = capsys.readouterr()
    assert result == 0
    assert "HEARTBEAT" in captured.out
    assert "status=OK" in captured.out

    assert frame_sniffer.main(["--hex", "00"]) == 1
    assert "Decode failed" in capsys.readouterr().err


def test_udp_listener_accepts_one_datagram(monkeypatch, capsys):
    payload = pack_payload("CMD", {"Mode": 1, "VelLimit": 100, "TargetPos": 123})
    datagram = encode_datagram("CMD", payload, 1)

    class FakeSocket:
        def __enter__(self):
            return self

        def __exit__(self, _type, _value, _traceback):
            return False

        def bind(self, address):
            assert address == ("127.0.0.1", 5000)

        def settimeout(self, _timeout):
            pass

        def recvfrom(self, _size):
            return datagram, ("127.0.0.2", 43210)

    monkeypatch.setattr(frame_sniffer.socket, "socket", lambda *_args: FakeSocket())
    assert frame_sniffer._listen("127.0.0.1", 5000, once=True) == 0
    output = capsys.readouterr().out
    assert "CMD" in output and "status=OK" in output
