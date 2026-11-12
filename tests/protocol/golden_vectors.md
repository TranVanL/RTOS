# M2 Golden Vector — ESTOP

This fixed vector checks the E2E header, CRC input/serialization, outer frame,
and CAN-FD zero padding without generating the expected bytes from either
project implementation.

## Inputs

| Field | Value |
|---|---|
| Message | ESTOP |
| CAN ID / Data ID | `0x010` / `0x00000010` |
| Counter | `0x1234` |
| DBC payload | `01 02` (Command = 1, Reason = 2) |
| E2E length | 14 bytes (`12-byte header + 2-byte payload`) |
| Outer data length | 16 bytes, padded with two zero bytes |

CRC input, in wire order, excludes the CRC field:

`0E 00 34 12 10 00 00 00 01 02`

The independent calculation uses the normal CRC-32P4 polynomial `0xF4ACFB13`,
initial value and final XOR `0xFFFFFFFF`, reflects each input byte, processes
bits MSB-first, then reflects the final 32-bit result. It first reproduces the
check value `123456789 -> 0x1697D06A`. For this E2E input the CRC value is
`0x2A478334`, serialized little-endian as `34 83 47 2A`.

## Expected bytes

E2E block (14 bytes):

`0E 00 34 12 10 00 00 00 34 83 47 2A 01 02`

Complete UDP datagram (22 bytes):

`FD CA 10 00 01 10 0E 00 34 12 10 00 00 00 34 83 47 2A 01 02 00 00`

The first six bytes are the outer header: magic `0xCAFD` little-endian, CAN ID
`0x010` little-endian, FDF-only flags `0x01`, and data length `0x10` (16 bytes).
The final `00 00` bytes are CAN-FD padding and are not part of the E2E length or
CRC input.

This vector is asserted by both the C E2E test and Python tests, and is also
encoded and decoded by the C/Python interoperability harness. It is an
independent project vector, not an official AUTOSAR conformance vector.