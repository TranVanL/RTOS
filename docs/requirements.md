# Requirements

ID quy ước: `REQ-<nhóm>-<số>`; nhóm: `SAF` an toàn, `FUN` chức năng, `COM` truyền thông, `SEC` cách ly/quyền, `PERF` hiệu năng/timing, `ENV` môi trường/CI.
Cột "Test" điền ở traceability matrix, không điền ở đây. Mức: `M` bắt buộc, `S` nên có.

| ID | Yêu cầu | Mức | Nguồn |
|---|---|---|---|
| REQ-SAF-001 | Node phải disable output khi không nhận CMD hợp lệ trong 50 ms (50 tick) | M | ARCH §5.3 |
| REQ-SAF-002 | E-stop phải đưa output về disable trong ≤ 1 control tick sau khi xử lý frame hợp lệ | M | ARCH §7 |
| REQ-SAF-003 | QNX chuyển DEGRADED khi mất 3 heartbeat liên tiếp và SAFE_STOP khi mất 10 | M | ARCH §4 |
| REQ-SAF-004 | Frame sai CRC/length/data ID không được thực thi | M | ARCH §6.2 |
| REQ-SAF-005 | Frame lặp hoặc sai thứ tự (delta counter > 2) không được thực thi | M | ARCH §6.2 |
| REQ-SAF-006 | Watchdog chỉ được kick khi mọi task báo alive trong chu kỳ | M | ARCH §5.3 |
| REQ-SAF-007 | HardFault/MemManage phải disable output, lưu fault record và reset; record gửi trong heartbeat đầu tiên sau boot | M | ARCH §5.3 |
| REQ-SAF-008 | Stack overflow xử lý như fault nặng | S | ARCH §5.3 |
| REQ-SAF-009 | Phát hiện encoder stuck và chuyển SAFE_STOP | S | Plan F8 |
| REQ-SAF-010 | SAFE_STOP chỉ thoát bằng `RESET_FAULT` có quyền khi nguyên nhân đã hết | M | ARCH §4 |
| REQ-SEC-001 | `devctl` kiểm tra quyền client bằng `ConnectClientInfo()`; sai quyền trả `EPERM` | M | ARCH §3.3 |
| REQ-SEC-002 | Payload/giá trị biên sai trả `EINVAL` | M | ARCH §3.3 |
| REQ-SEC-003 | `logger`/`mgmt` chỉ có quyền đọc SHM; ghi gây SIGSEGV riêng process đó | M | ARCH §3.1 |
| REQ-SEC-004 | Crash `logger`/`mgmt` không ảnh hưởng control; crash được phát hiện | M | ARCH §3.1 |
| REQ-COM-001 | Frame theo định dạng CAN FD-compatible (magic, can_id, flags, dlc, data) | M | ARCH §6.1 |
| REQ-COM-002 | Mọi message có E2E header Profile 4–based (length, counter, data_id, crc32) | M | ARCH §6.2 |
| REQ-COM-003 | Pack/unpack C sinh từ `can_matrix.dbc`, dùng chung 2 phía; C và Python giải mã khớp 100% | M | ARCH §9 |
| REQ-FUN-001 | Control task chạy 1 kHz với PID cascade, anti-windup, saturation, rate limit | M | ARCH §5.2 |
| REQ-FUN-002 | Step response overshoot < 10% | M | Plan M1 |
| REQ-PERF-001 | Round-trip CMD → telemetry đo trên một đồng hồ QNX, xuất p50/p99/max | S | ARCH §7 |
| REQ-PERF-002 | Jitter control task đo bằng DWT, xuất histogram | S | ARCH §7 |
| REQ-PERF-003 | Có test chứng minh priority inheritance (có/không) trên QNX | S | Plan M3 |
| REQ-ENV-001 | CI chạy build + unit test + QEMU scenario mỗi lần push | M | Plan M6 |
