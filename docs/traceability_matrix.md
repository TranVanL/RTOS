# Traceability Matrix

Yêu cầu (`requirements.md`) → thiết kế (`01_ARCHITECTURE.md`) → code → test. Cập nhật mỗi khi thêm test; CI (M6) kiểm tra mọi `REQ-SAF-*` mức M có ít nhất một test.

| Requirement | Thiết kế | Code | Test | Trạng thái |
|---|---|---|---|---|
| REQ-SAF-001 | ARCH §5.3 | _chưa có_ | F1 | Todo |
| REQ-SAF-002 | ARCH §7 | _chưa có_ | F6 | Todo |
| REQ-SAF-003 | ARCH §4 | [common/safety/safety_fsm.c](../common/safety/safety_fsm.c) | [tests/safety/test_safety_fsm.c](../tests/safety/test_safety_fsm.c); F2 (tích hợp, M4/M5) | In progress — FSM host pass + `motion_sup` kịch bản B trên QNX VM ([log](results/m3_wp3_scenarios.txt)); 3/10 miss tích hợp với node thật ở M4 |
| REQ-SAF-004 | ARCH §6.2 | [common/e2e/e2e.c](../common/e2e/e2e.c), [common/frame/can_frame.c](../common/frame/can_frame.c) | [tests/protocol/test_e2e.c](../tests/protocol/test_e2e.c), [tools/tests/test_m2_protocol.py](../tools/tests/test_m2_protocol.py) | Done (M2) — host + CI pass, 2026-11-12 |
| REQ-SAF-005 | ARCH §6.2 | [common/e2e/e2e.c](../common/e2e/e2e.c) | [tests/protocol/test_e2e.c](../tests/protocol/test_e2e.c), [tools/tests/test_m2_protocol.py](../tools/tests/test_m2_protocol.py) | Done (M2) — host + CI pass, 2026-11-12 |
| REQ-SAF-006 | ARCH §5.3 | _chưa có_ | F9 | Todo |
| REQ-SAF-007 | ARCH §5.3 | _chưa có_ | F10 | Todo |
| REQ-SAF-008 | ARCH §5.3 | _chưa có_ | F11 | Todo |
| REQ-SAF-009 | Plan F8 | _chưa có_ | F8 | Todo |
| REQ-SAF-010 | ARCH §4 | [common/safety/safety_fsm.c](../common/safety/safety_fsm.c) | [tests/safety/test_safety_fsm.c](../tests/safety/test_safety_fsm.c) | In progress — FSM host pass; `RESET_FAULT` chỉ nhận từ group `motion`, `EBUSY` ngoài SAFE_STOP, về READY ([log](results/m3_wp4_devctl.txt)); chờ CI |
| REQ-SEC-001 | ARCH §3.3 | [common/ipc/motion_ipc.c](../common/ipc/motion_ipc.c) | [tests/safety/test_motion_ipc.c](../tests/safety/test_motion_ipc.c); [results/m3_wp4_devctl.txt](results/m3_wp4_devctl.txt) (`run_m3_wp4.sh`) | Done (M3, SIL VM) — `EPERM` đúng với user ngoài group 3000, OK với group |
| REQ-SEC-002 | ARCH §3.3 | [common/ipc/motion_ipc.c](../common/ipc/motion_ipc.c) | [tests/safety/test_motion_ipc.c](../tests/safety/test_motion_ipc.c); [results/m3_wp4_devctl.txt](results/m3_wp4_devctl.txt); F7 (M5) | Done (M3, SIL VM) — `EINVAL` cho lệnh/kích thước/biên sai |
| REQ-SEC-003 | ARCH §3.1 | [common/shm/motion_shm.c](../common/shm/motion_shm.c) | [tests/safety/test_motion_shm.c](../tests/safety/test_motion_shm.c) (seqlock, API chỉ đọc const); [results/m3_wp5_shm_crash.txt](results/m3_wp5_shm_crash.txt) (`run_m3_wp5.sh`); F14 tự động hóa ở M5 | Done (M3, SIL VM) — `mgmt` ghi SHM chỉ đọc → SIGSEGV, hệ thống vẫn chạy; `O_RDWR` bởi user khác bị từ chối |
| REQ-SEC-004 | ARCH §3.1 | [qnx/motion_sup/motion_sup.c](../qnx/motion_sup/motion_sup.c) (death watcher) | [results/m3_wp5_shm_crash.txt](results/m3_wp5_shm_crash.txt); F13 ở M5 | Done (M3, SIL VM) — crash `logger`/`mgmt` (signal 11) được phát hiện, control không bị ảnh hưởng |
| REQ-COM-001 | ARCH §6.1 | [common/frame/can_frame.c](../common/frame/can_frame.c), [common/generated/can_matrix_meta.c](../common/generated/can_matrix_meta.c) | [tests/protocol/test_can_frame.c](../tests/protocol/test_can_frame.c), [tools/tests/test_m2_protocol.py](../tools/tests/test_m2_protocol.py) | Done (M2) — host + CI pass, 2026-11-12 |
| REQ-COM-002 | ARCH §6.2 | [common/e2e/e2e.c](../common/e2e/e2e.c), [common/e2e/crc32p4.c](../common/e2e/crc32p4.c) | [tests/protocol/test_e2e.c](../tests/protocol/test_e2e.c), [tools/tests/test_m2_protocol.py](../tools/tests/test_m2_protocol.py) | Done (M2) — host + CI pass, 2026-11-12 |
| REQ-COM-003 | ARCH §9 | [tools/dbc_codegen.py](../tools/dbc_codegen.py), [common/generated/can_matrix.c](../common/generated/can_matrix.c) | [tests/protocol/test_dbc_generated.c](../tests/protocol/test_dbc_generated.c), [tools/tests/test_m2_codegen.py](../tools/tests/test_m2_codegen.py), [tools/tests/test_m2_interop.py](../tools/tests/test_m2_interop.py) | Done (M2) — host + CI pass, 2026-11-12 |
| REQ-FUN-001 | ARCH §5.2 | _chưa có_ | unit pid/plant | Todo |
| REQ-FUN-002 | Plan M1 | _chưa có_ | plot_step + test | Todo |
| REQ-PERF-001 | ARCH §7 | _chưa có_ | metrics | Todo |
| REQ-PERF-002 | ARCH §7 | _chưa có_ | metrics | Todo |
| REQ-PERF-003 | Plan M3 | [qnx/tests/m3_prio_inv.c](../qnx/tests/m3_prio_inv.c) | [results/m3_wp6_prio_inheritance.txt](results/m3_wp6_prio_inheritance.txt), [trace PI](results/m3_wp6_trace_pi.txt), [trace không PI](results/m3_wp6_trace_nopi.txt) (`run_m3_wp6.sh`) | Done (M3, SIL VM) — có/không PI, 5/5 lần + kernel trace |
| REQ-ENV-001 | Plan §4 | .github/workflows/ci.yml | CI run | Todo |
