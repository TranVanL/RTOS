# Project Plan — QNX–FreeRTOS Real-Time Control Bridge

## 1. Milestones

### M1 — FreeRTOS node skeleton
- Dựng môi trường Renode mô phỏng STM32.
- **Decision gate:** kiểm tra Renode có model FDCAN/MCAN cho target STM32 hay không. Nếu có → dùng driver FDCAN thật cho backend; nếu không → dùng backend `udp_bridge` ở cả hai phía và ghi rõ trong README.
- Tạo task structure: Control Task, SPI Task, Watchdog Task (chưa nối CAN).
- Cài vòng PID + plant model motor + encoder giả có nhiễu; viết `step_response.py` vẽ đồ thị.
- Deliverable: firmware chạy trên Renode, log task đúng chu kỳ, đồ thị step response (overshoot, settling time).

### M2 — CAN FD qua lớp `can_if` + DBC
- Viết `can_matrix.dbc`, `dbc_codegen.py` sinh header C (xem [01_ARCHITECTURE.md](01_ARCHITECTURE.md)).
- Thiết kế HAL `can_if` và backend `udp_bridge`; thêm CAN Task trên FreeRTOS.
- Test gửi/nhận frame giả lập nội bộ (loopback trong Renode).
- Deliverable: node tự gửi heartbeat + phản hồi lệnh giả lập, `can_decode.py` giải mã được frame.

### M3 — QNX environment & resource manager
- Cài QNX SDP trên VM, dựng toolchain build.
- Viết `resmgr` cơ bản cho `/dev/ctrlnode`, test `devctl()` qua app console đơn giản.
- Deliverable: resmgr nhận/trả lệnh qua devctl, log ra console.

### M4 — IPC, multi-process & multi-threading trên QNX
- Tách 3 process `ctrl_main`, `logger`, `mgmt`; thêm Control Thread, CAN Thread, Watchdog Thread.
- Triển khai channel + `MsgSend/MsgReceive/MsgReply`, pulse từ timer.
- Shared memory (`shm_open`/`mmap`) với quyền khác nhau cho từng process; test ghi vào vùng read-only gây fault.
- Deliverable: các process/thread phối hợp đúng, log minh chứng luồng dữ liệu và memory protection.

### M5 — Kết nối CAN FD QNX ↔ FreeRTOS (qua bridge UDP mô phỏng)
- Dựng lớp bridge UDP đóng vai "CAN bus ảo" giữa VM QNX và Renode.
- Test end-to-end: QNX gửi lệnh motor → FreeRTOS phản hồi telemetry → `mgmt` process xuất CCSDS Space Packet → `ccsds_client.py` hiển thị.
- Deliverable: demo video + log round-trip latency (phân tích bằng `log_analyzer.py`).

### M6 — Fault handling, watchdog, test, tài liệu, CI
- Hoàn thiện state machine `INIT→RUN→DEGRADED→SAFE_STOP`.
- Viết test fault-injection bằng Python (pytest/Robot Framework điều khiển Renode): ngắt CAN, sai CRC, treo task, ghi vào vùng nhớ read-only.
- Viết unit test (Ceedling/Unity cho FreeRTOS), test harness cho QNX.
- Viết traceability matrix, README (ghi rõ CAN bus ảo, phạm vi mô phỏng), dựng CI (GitHub Actions) build + test FreeRTOS trong Renode.
- Deliverable: repo hoàn chỉnh, báo cáo kiến trúc, pipeline CI chạy pass.

### (Song song, không block M1–M6) — EtherCAT R&D Testbed
- Dựng SOEM Master (Linux) + SOES Slave (Renode) trên testbed riêng.
- Demo cyclic PDO exchange cơ bản; slave dùng chung state machine `INIT→RUN→SAFE_STOP` với hệ thống chính.
- Deliverable: README riêng mô tả thử nghiệm, không gộp vào core system.

## 2. Cấu trúc repo đề xuất

```
qnx-freertos-rt-bridge/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── traceability_matrix.md
│   └── diagrams/
├── common/
│   ├── can_matrix.dbc
│   ├── can_if/ (HAL + backend udp_bridge)
│   └── generated/ (header C sinh từ DBC)
├── qnx-supervisor/
│   ├── ctrl_main/ (resmgr, control, can thread, watchdog, fsm)
│   ├── logger/
│   ├── mgmt/ (TCP/UDP + CCSDS Space Packet)
│   └── build/ (QNX toolchain scripts)
├── freertos-io-node/
│   ├── src/
│   │   ├── control_task/ (PID + plant model)
│   │   ├── can_task/
│   │   ├── spi_task/
│   │   └── watchdog_task/
│   ├── renode/ (platform .repl, .resc scripts)
│   └── tests/ (Ceedling/Unity)
├── tools/ (Python)
│   ├── dbc_codegen.py
│   ├── can_decode.py
│   ├── ccsds_client.py
│   ├── log_analyzer.py
│   ├── step_response.py
│   └── fault_inject/ (pytest/Robot Framework)
├── ethercat-rd-testbed/
│   ├── soem-master/
│   └── soes-slave/
└── .github/workflows/ci.yml
```

## 3. Kế hoạch test

| Loại test | Công cụ | Kịch bản chính |
|---|---|---|
| Unit test FreeRTOS | Ceedling/Unity | Frame encode/decode CAN FD, CRC check, state transitions |
| Unit test QNX | Test harness tối giản (C) | devctl handler, message parsing |
| Unit test Python tools | pytest | Giải mã DBC, sinh header đúng, parse/build CCSDS Space Packet |
| Control loop | `step_response.py` + Renode | PID đạt overshoot/settling time mục tiêu, giới hạn rate/saturation kích hoạt SAFE_STOP |
| Integration (fault injection) | pytest/Robot Framework điều khiển Renode + QNX VM | Ngắt CAN giữa chừng, gửi frame CRC sai, treo task, ghi vào shared memory read-only → kiểm tra chuyển DEGRADED/SAFE_STOP/fault đúng |
| Performance | Đo latency round-trip | Đo thời gian từ lệnh QNX đến phản hồi FreeRTOS qua bridge UDP |

## 4. CI/CD
- GitHub Actions: build firmware FreeRTOS, chạy trong Renode test script tự động mỗi lần push; chạy pytest cho `tools/`.
- Phần QNX: do giấy phép, build local; lưu log/screenshot kết quả build & test đính kèm vào `docs/` làm bằng chứng, có note rõ trong README.

## 5. Rủi ro & cách xử lý

| Rủi ro | Mức độ | Xử lý |
|---|---|---|
| Không có board thật, bị hỏi chi tiết phần cứng | Trung bình | Nắm vững luồng dữ liệu, log/demo mô phỏng rõ ràng, trả lời trung thực là "đã verify trên môi trường mô phỏng (Renode/QNX VM), đang chờ board thật để flash" nếu bị hỏi thẳng |
| Renode không có FDCAN/MCAN cho target STM32 | Trung bình | Decision gate ở M1; fallback backend `udp_bridge` ở cả hai phía, ghi rõ trong README |
| "CAN FD" bị hỏi là driver thật hay giả lập | Trung bình | Trả lời trung thực: CAN bus ảo qua UDP, `can_if` HAL cho phép thay backend `devc-can`/FDCAN trên phần cứng thật |
| QNX license/môi trường khó cài | Thấp | Dùng QNX SDP free, có hướng dẫn chính thức từ BlackBerry/QNX |
| EtherCAT bị hỏi sâu | Thấp (đã tách riêng) | Trả lời đúng phạm vi đã làm (testbed SOEM/SOES), không overclaim tích hợp vào QNX production |
| Thời gian làm không đủ cho cả 6 milestone | Trung bình | Ưu tiên M1–M5 (core story), M6 (test/CI) có thể làm tối giản nếu gấp; công cụ Python làm dần theo từng milestone |

## 6. Tài liệu liên quan
- [00_PROJECT_OVERVIEW.md](00_PROJECT_OVERVIEW.md) — tóm tắt mục tiêu, mapping JD.
- [01_ARCHITECTURE.md](01_ARCHITECTURE.md) — chi tiết kiến trúc, sơ đồ, giao thức.
