"""Decode project DBC/E2E frames from hex input or a UDP socket."""

import argparse
import socket
import sys
import time
from pathlib import Path

from m2_protocol import (
	MESSAGE_INFO,
	ProtocolError,
	check_timeout,
	decode_datagram,
	new_receiver_state,
)


MAX_DATAGRAM_SIZE = 6 + 64


def _report(result, peer=None):
	prefix = ""
	if peer is not None:
		prefix = "peer=%s:%d " % (peer[0], peer[1])
	print(
		"%s%s id=0x%03X counter=%d status=%s signals=%s"
		% (
			prefix,
			result["name"],
			result["can_id"],
			result["counter"],
			result["status"],
			result["signals"],
		),
		flush=True,
	)


def _decode_offline(datagram):
	result = decode_datagram(datagram)
	_report(result)
	return 0


def _listen(bind_address, port, once=False):
	started_at = int(time.monotonic() * 1000.0) & 0xFFFFFFFF
	receiver_states = {
		can_id: new_receiver_state(started_at)
		for can_id, info in MESSAGE_INFO.items()
		if info["udp_port"] == port and info["timeout_ms"] is not None
	}
	reported_timeouts = set()

	with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
		sock.bind((bind_address, port))
		sock.settimeout(0.1)
		print("Listening on %s:%d/udp" % (bind_address, port), flush=True)
		while True:
			try:
				datagram, peer = sock.recvfrom(MAX_DATAGRAM_SIZE + 1)
			except socket.timeout:
				datagram = None
				peer = None

			now = int(time.monotonic() * 1000.0) & 0xFFFFFFFF
			if datagram is not None:
				try:
					result = decode_datagram(
						datagram,
						source_port=port,
						receiver_states=receiver_states,
						now=now,
					)
				except ProtocolError as error:
					print("REJECT %s: %s" % (error.status, error), file=sys.stderr, flush=True)
				else:
					_report(result, peer)
					if result["status"] == "OK":
						reported_timeouts.discard(result["can_id"])
				if once:
					return 0

			for can_id, state in receiver_states.items():
				timeout_ms = MESSAGE_INFO[can_id]["timeout_ms"]
				if can_id not in reported_timeouts and check_timeout(state, now, timeout_ms) == "TIMEOUT":
					print(
						"TIMEOUT %s id=0x%03X after %d ms"
						% (MESSAGE_INFO[can_id]["name"], can_id, timeout_ms),
						flush=True,
					)
					reported_timeouts.add(can_id)


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	source = parser.add_mutually_exclusive_group()
	source.add_argument("--hex", help="decode one datagram supplied as hexadecimal bytes")
	source.add_argument("--hex-file", type=Path, help="read one datagram from a text file as hexadecimal")
	parser.add_argument("--bind", default="0.0.0.0", help="UDP bind address (default: 0.0.0.0)")
	parser.add_argument("--port", type=int, choices=(5000, 5001), default=5000)
	parser.add_argument("--once", action="store_true", help="exit after the first received UDP datagram")
	args = parser.parse_args(argv)

	if args.hex is not None:
		try:
			return _decode_offline(bytes.fromhex(args.hex))
		except (ValueError, ProtocolError) as error:
			print("Decode failed: %s" % error, file=sys.stderr)
			return 1
	if args.hex_file is not None:
		try:
			datagram = bytes.fromhex(args.hex_file.read_text(encoding="utf-8"))
			return _decode_offline(datagram)
		except (OSError, ValueError, ProtocolError) as error:
			print("Decode failed: %s" % error, file=sys.stderr)
			return 1

	try:
		return _listen(args.bind, args.port, args.once)
	except OSError as error:
		print("UDP listener failed: %s" % error, file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
