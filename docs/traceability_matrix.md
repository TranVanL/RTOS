# Traceability Matrix

Yêu cầu (`requirements.md`) → thiết kế (`01_ARCHITECTURE.md`) → code → test. Cập nhật mỗi khi thêm test; CI (M6) kiểm tra mọi `REQ-SAF-*` mức M có ít nhất một test.

| Requirement | Thiết kế | Code | Test | Trạng thái |
|---|---|---|---|---|
| REQ-SAF-001 | ARCH §5.3 | _chưa có_ | F1 | Todo |
| REQ-SAF-002 | ARCH §7 | _chưa có_ | F6 | Todo |
| REQ-SAF-003 | ARCH §4 | _chưa có_ | F2 | Todo |
| REQ-SAF-004 | ARCH §6.2 | [common/e2e/e2e.c](../common/e2e/e2e.c), [common/frame/can_frame.c](../common/frame/can_frame.c) | [tests/protocol/test_e2e.c](../tests/protocol/test_e2e.c), [tools/tests/test_m2_protocol.py](../tools/tests/test_m2_protocol.py) | In progress — host pass; CI pending |
| REQ-SAF-005 | ARCH §6.2 | [common/e2e/e2e.c](../common/e2e/e2e.c) | [tests/protocol/test_e2e.c](../tests/protocol/test_e2e.c), [tools/tests/test_m2_protocol.py](../tools/tests/test_m2_protocol.py) | In progress — host pass; CI pending |
| REQ-SAF-006 | ARCH §5.3 | _chưa có_ | F9 | Todo |
| REQ-SAF-007 | ARCH §5.3 | _chưa có_ | F10 | Todo |
| REQ-SAF-008 | ARCH §5.3 | _chưa có_ | F11 | Todo |
| REQ-SAF-009 | Plan F8 | _chưa có_ | F8 | Todo |
| REQ-SAF-010 | ARCH §4 | _chưa có_ | unit fsm | Todo |
| REQ-SEC-001 | ARCH §3.3 | _chưa có_ | qnx/tests devctl perm | Todo |
| REQ-SEC-002 | ARCH §3.3 | _chưa có_ | F7, qnx/tests | Todo |
| REQ-SEC-003 | ARCH §3.1 | _chưa có_ | F14 | Todo |
| REQ-SEC-004 | ARCH §3.1 | _chưa có_ | F13 | Todo |
| REQ-COM-001 | ARCH §6.1 | [common/frame/can_frame.c](../common/frame/can_frame.c), [common/generated/can_matrix_meta.c](../common/generated/can_matrix_meta.c) | [tests/protocol/test_can_frame.c](../tests/protocol/test_can_frame.c), [tools/tests/test_m2_protocol.py](../tools/tests/test_m2_protocol.py) | In progress — host pass; CI pending |
| REQ-COM-002 | ARCH §6.2 | [common/e2e/e2e.c](../common/e2e/e2e.c), [common/e2e/crc32p4.c](../common/e2e/crc32p4.c) | [tests/protocol/test_e2e.c](../tests/protocol/test_e2e.c), [tools/tests/test_m2_protocol.py](../tools/tests/test_m2_protocol.py) | In progress — host pass; CI pending |
| REQ-COM-003 | ARCH §9 | [tools/dbc_codegen.py](../tools/dbc_codegen.py), [common/generated/can_matrix.c](../common/generated/can_matrix.c) | [tests/protocol/test_dbc_generated.c](../tests/protocol/test_dbc_generated.c), [tools/tests/test_m2_codegen.py](../tools/tests/test_m2_codegen.py), [tools/tests/test_m2_interop.py](../tools/tests/test_m2_interop.py) | In progress — host pass; CI pending |
| REQ-FUN-001 | ARCH §5.2 | _chưa có_ | unit pid/plant | Todo |
| REQ-FUN-002 | Plan M1 | _chưa có_ | plot_step + test | Todo |
| REQ-PERF-001 | ARCH §7 | _chưa có_ | metrics | Todo |
| REQ-PERF-002 | ARCH §7 | _chưa có_ | metrics | Todo |
| REQ-PERF-003 | Plan M3 | _chưa có_ | qnx/tests prio inversion | Todo |
| REQ-ENV-001 | Plan §4 | .github/workflows/ci.yml | CI run | Todo |
