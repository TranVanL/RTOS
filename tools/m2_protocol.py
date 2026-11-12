"""Python reference codec and validator for the project M2 wire format."""

import re
import struct
from pathlib import Path

import cantools


REPO_ROOT = Path(__file__).resolve().parents[1]
DBC_PATH = REPO_ROOT / "common" / "can_matrix.dbc"
FRAME_MAGIC = 0xCAFD
FRAME_HEADER_SIZE = 6
FRAME_FLAG_FDF = 0x01
E2E_HEADER_SIZE = 12
MAX_FRAME_DATA = 64
MAX_E2E_PAYLOAD = MAX_FRAME_DATA - E2E_HEADER_SIZE
CRC32P4_REFLECTED_POLY = 0xC8DF352F
CRC32P4_INIT = 0xFFFFFFFF
CRC32P4_XOROUT = 0xFFFFFFFF
MAX_COUNTER_DELTA = 2
LEGAL_DATA_LENGTHS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)
UDP_PORT_PATTERN = re.compile(r"\bUDP\s*:?[\t ]*(\d{1,5})\b", re.IGNORECASE)
TIMEOUT_PATTERN = re.compile(r"\btimeout\s+(\d+)\s*ms\b", re.IGNORECASE)


class ProtocolError(ValueError):
    """Malformed frame or invalid routing data, with a stable error code."""

    def __init__(self, status, message):
        ValueError.__init__(self, message)
        self.status = status


def _load_messages():
    database = cantools.database.load_file(str(DBC_PATH), strict=True)
    messages = {}
    for message in database.messages:
        match = UDP_PORT_PATTERN.search(message.comment or "")
        if match is None:
            raise ValueError("DBC message %s is missing its UDP port comment" % message.name)
        udp_port = int(match.group(1))
        e2e_length = E2E_HEADER_SIZE + message.length
        wire_length = next(
            (length for length in LEGAL_DATA_LENGTHS if length >= e2e_length),
            None,
        )
        if wire_length is None:
            raise ValueError("DBC message %s exceeds CAN-FD data capacity" % message.name)
        timeout_match = TIMEOUT_PATTERN.search(message.comment or "")
        messages[message.frame_id] = {
            "can_id": message.frame_id,
            "data_id": message.frame_id,
            "name": message.name,
            "payload_length": message.length,
            "e2e_length": e2e_length,
            "wire_length": wire_length,
            "udp_port": udp_port,
            "timeout_ms": int(timeout_match.group(1)) if timeout_match else None,
            "dbc_message": message,
        }
    return database, messages


DATABASE, MESSAGE_INFO = _load_messages()
MESSAGE_BY_NAME = {info["name"]: info for info in MESSAGE_INFO.values()}


def crc32p4(data):
    """Return CRC-32P4 using the project profile parameters."""
    crc = CRC32P4_INIT
    for byte in bytearray(data):
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ CRC32P4_REFLECTED_POLY
            else:
                crc >>= 1
    return (crc ^ CRC32P4_XOROUT) & 0xFFFFFFFF


def pack_payload(message_name, signals, scaling=False):
    """Pack DBC application signals only (no E2E header or frame padding)."""
    try:
        message = DATABASE.get_message_by_name(message_name)
    except KeyError:
        raise ProtocolError("LEN_OR_ID_ERR", "unknown DBC message %s" % message_name)
    try:
        return message.encode(signals, scaling=scaling, strict=True)
    except (ValueError, KeyError, TypeError) as error:
        raise ProtocolError("LEN_OR_ID_ERR", "invalid %s payload: %s" % (message_name, error))


def unpack_payload(message_name, payload, scaling=False):
    """Decode one exact-size DBC application payload."""
    try:
        message = DATABASE.get_message_by_name(message_name)
    except KeyError:
        raise ProtocolError("LEN_OR_ID_ERR", "unknown DBC message %s" % message_name)
    if len(payload) != message.length:
        raise ProtocolError("LEN_OR_ID_ERR", "wrong application payload length")
    try:
        return message.decode(payload, scaling=scaling, decode_choices=False)
    except (ValueError, KeyError, TypeError) as error:
        raise ProtocolError("LEN_OR_ID_ERR", "invalid %s payload: %s" % (message_name, error))


def protect_e2e(payload, counter, data_id):
    """Build an unpadded 12-byte E2E header plus application payload."""
    if not isinstance(payload, (bytes, bytearray)):
        raise TypeError("payload must be bytes-like")
    if len(payload) > MAX_E2E_PAYLOAD:
        raise ValueError("application payload exceeds the 64-byte E2E data limit")
    if not 0 <= counter <= 0xFFFF or not 0 <= data_id <= 0xFFFFFFFF:
        raise ValueError("counter or Data ID is out of range")

    e2e_length = E2E_HEADER_SIZE + len(payload)
    prefix = struct.pack("<HHI", e2e_length, counter, data_id)
    crc = crc32p4(prefix + bytes(payload))
    return prefix + struct.pack("<I", crc) + bytes(payload)


def new_receiver_state(now):
    """Create/reset receiver state; timeout grace starts at `now`."""
    return {
        "has_last_counter": False,
        "last_counter": 0,
        "started_at": now & 0xFFFFFFFF,
        "last_accepted_at": 0,
    }


def verify_e2e(protected_data, expected_payload_length, expected_data_id, state, now):
    """Validate length, ID, CRC and sequence; update state only for OK."""
    if state is None:
        raise ValueError("receiver state is required")
    if len(protected_data) < E2E_HEADER_SIZE:
        return "LEN_OR_ID_ERR", None
    if expected_payload_length < 0 or expected_payload_length > MAX_E2E_PAYLOAD:
        return "LEN_OR_ID_ERR", None
    expected_length = E2E_HEADER_SIZE + expected_payload_length
    if len(protected_data) != expected_length:
        return "LEN_OR_ID_ERR", None

    length, counter, data_id, received_crc = struct.unpack_from("<HHII", protected_data)
    if length != expected_length or data_id != expected_data_id:
        return "LEN_OR_ID_ERR", None

    payload = bytes(protected_data[E2E_HEADER_SIZE:])
    if crc32p4(bytes(protected_data[:8]) + payload) != received_crc:
        return "CRC_ERR", None

    if state["has_last_counter"]:
        delta = (counter - state["last_counter"]) & 0xFFFF
        if delta == 0:
            return "REPEATED", None
        if delta > MAX_COUNTER_DELTA:
            return "WRONG_SEQ", None

    state["last_counter"] = counter
    state["last_accepted_at"] = now & 0xFFFFFFFF
    state["has_last_counter"] = True
    return "OK", payload


def check_timeout(state, now, timeout_ticks):
    """Return OK before the deadline, TIMEOUT at/after it, with uint32 wrap."""
    if timeout_ticks <= 0 or timeout_ticks >= 0x80000000:
        raise ValueError("timeout must be in (0, 2^31)")
    if state["has_last_counter"]:
        reference = state["last_accepted_at"]
    else:
        reference = state["started_at"]
    elapsed = ((now & 0xFFFFFFFF) - reference) & 0xFFFFFFFF
    return "TIMEOUT" if elapsed >= timeout_ticks else "OK"


def _decode_envelope(datagram, source_port=None):
    if not isinstance(datagram, (bytes, bytearray)):
        raise ProtocolError("FRAME_ERR", "datagram must be bytes-like")
    if len(datagram) < FRAME_HEADER_SIZE:
        raise ProtocolError("FRAME_ERR", "truncated outer frame header")

    magic, can_id, flags, data_length = struct.unpack_from("<HHBB", datagram)
    if magic != FRAME_MAGIC:
        raise ProtocolError("FRAME_ERR", "invalid frame magic")
    if can_id > 0x07FF:
        raise ProtocolError("FRAME_ERR", "CAN ID exceeds 11-bit range")
    if flags != FRAME_FLAG_FDF:
        raise ProtocolError("FRAME_ERR", "unsupported frame flags")
    if data_length not in LEGAL_DATA_LENGTHS:
        raise ProtocolError("FRAME_ERR", "illegal CAN-FD data length")
    if len(datagram) != FRAME_HEADER_SIZE + data_length:
        raise ProtocolError("FRAME_ERR", "datagram length does not match DLC")

    info = MESSAGE_INFO.get(can_id)
    if info is None:
        raise ProtocolError("LEN_OR_ID_ERR", "CAN ID is not defined in the DBC")
    if data_length != info["wire_length"]:
        raise ProtocolError("LEN_OR_ID_ERR", "wire length does not match DBC message")
    if source_port is not None and source_port != info["udp_port"]:
        raise ProtocolError("LEN_OR_ID_ERR", "message arrived on the wrong UDP port")

    frame_data = bytes(datagram[FRAME_HEADER_SIZE:])
    if len(frame_data) < info["e2e_length"]:
        raise ProtocolError("LEN_OR_ID_ERR", "frame is shorter than its E2E message")
    if any(frame_data[info["e2e_length"]:]):
        raise ProtocolError("FRAME_ERR", "nonzero CAN-FD padding")
    return info, frame_data[:info["e2e_length"]]


def encode_datagram(message_name, payload, counter):
    """Encode a DBC payload into one project outer-frame UDP datagram."""
    info = MESSAGE_BY_NAME.get(message_name)
    if info is None:
        raise ProtocolError("LEN_OR_ID_ERR", "unknown DBC message %s" % message_name)
    if len(payload) != info["payload_length"]:
        raise ProtocolError("LEN_OR_ID_ERR", "wrong application payload length")

    protected = protect_e2e(payload, counter, info["data_id"])
    padding = bytes(info["wire_length"] - len(protected))
    header = struct.pack(
        "<HHBB", FRAME_MAGIC, info["can_id"], FRAME_FLAG_FDF, info["wire_length"]
    )
    return header + protected + padding


def decode_datagram(datagram, source_port=None, receiver_states=None, now=0):
    """Decode one datagram into E2E status and DBC signals."""
    info, protected = _decode_envelope(datagram, source_port)
    if receiver_states is None:
        state = new_receiver_state(now)
    else:
        state = receiver_states.get(info["can_id"])
        if state is None:
            state = new_receiver_state(now)
            receiver_states[info["can_id"]] = state

    status, payload = verify_e2e(
        protected,
        info["payload_length"],
        info["data_id"],
        state,
        now,
    )
    result = {
        "can_id": info["can_id"],
        "data_id": info["data_id"],
        "name": info["name"],
        "udp_port": info["udp_port"],
        "status": status,
        "payload": payload,
        "counter": struct.unpack_from("<H", protected, 2)[0],
        "signals": None,
    }
    if status == "OK":
        result["signals"] = unpack_payload(info["name"], payload, scaling=False)
    return result
