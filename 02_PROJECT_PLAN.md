# Project Plan — QNX–FreeRTOS Safety-Supervised Motion Control Platform

## 1. Milestones

Mỗi milestone có **acceptance criteria đo được**. Không chuyển milestone khi chưa đạt.

### M0 — Môi trường SIL và đường kết nối (gate bắt buộc)
- Cài QNX SDP 8.0, chạy QNX VM x86_64; build và chạy "hello" bằng toolchain QNX.
- Build FreeRTOS demo cho QEMU `mps2-an385` (bản MPU và bản FreeRTOS+TCP).
- Xác minh trong QEMU: MPU hoạt động (cố ý truy cập sai → MemManage), CMSDK watchdog có reset được hay không, NIC LAN9118 nhận/gửi UDP.
- Cấu hình mạng host-only/tap giữa QNX VM và QEMU.
- **Acceptance:** UDP echo 2 chiều QNX process ↔ FreeRTOS task chạy liên tục 10 phút không mất gói; bảng xác minh peripheral (có model / fallback) được ghi vào `docs/sil_environment.md`.

### M1 — FreeRTOS Motion Node: control loop
- Task structure, priority, MPU region cho control task.
- Plant model motor DC bậc 2, encoder model, PID cascade có anti-windup/saturation/rate limit.
- Đo jitter control task bằng timer phần cứng của QEMU (DWT/SysTick).
- **Acceptance:** step response đạt overshoot < 10%, settling time theo thông số chọn; histogram jitter được xuất; unit test PID/plant pass trên host.

### M2 — Frame protocol, DBC, E2E
- Viết `can_matrix.dbc`, `dbc_codegen.py` sinh pack/unpack C dùng chung 2 phía.
- Thư viện `frame` + `e2e` (C thuần, không phụ thuộc OS) — dùng chung cho QNX và FreeRTOS.
- `frame_sniffer.py` giải mã frame trên UDP.
- **Acceptance:** unit test E2E phủ đủ các kết quả `OK/REPEATED/WRONG_SEQ/CRC_ERR/TIMEOUT`; frame do C sinh được Python giải mã đúng 100% trong test round-trip.

### M3 — QNX Supervisor: resmgr, process, IPC
- Process `motion_sup`, `logger`, `mgmt`; SHM phân quyền + seqlock.
- Thread với priority/policy theo kiến trúc, timer pulse.
- Resmgr `/dev/motion` với kiểm tra quyền client và validate input; client `motionctl`.
- Test priority inversion có/không priority inheritance.
- **Acceptance:** `devctl` sai quyền trả `EPERM`, sai tham số trả `EINVAL`; `mgmt` ghi vào SHM chỉ đọc bị `SIGSEGV` và hệ thống vẫn chạy; log System Profiler chứng minh priority inheritance.

### M4 — Tích hợp end-to-end
- Nối QNX ↔ FreeRTOS qua UDP: command 10 ms, telemetry + heartbeat 10 ms, kênh E-stop riêng.
- Safety state machine hai phía.
- `metrics.py` tính latency/jitter/reaction time từ log hai phía.
- **Acceptance:** chạy liên tục 30 phút ở RUN không có lỗi E2E; đạt timing budget trong [01_ARCHITECTURE.md](01_ARCHITECTURE.md) mục 7 (ghi rõ là số liệu SIL).

### M5 — Fault injection và safety validation
- Framework `fault_inject/` (pytest) với các kịch bản ở mục 3.2.
- Fault record sau reboot, watchdog reset, process crash QNX.
- **Acceptance:** 100% kịch bản pass, mỗi kịch bản có reaction time đo được và nằm trong budget; báo cáo tự sinh.

### M6 — CI, tài liệu, traceability, demo
- GitHub Actions: build firmware, unit test, chạy firmware trong QEMU với test SIL không cần QNX (dùng Python giả lập supervisor).
- Traceability matrix Yêu cầu → Thiết kế → Test.
- README: kiến trúc, cách chạy, kết quả đo, giới hạn SIL; video demo 3–5 phút.
- **Acceptance:** CI xanh; mỗi yêu cầu an toàn có ít nhất 1 test liên kết.

### Tùy chọn (sau M6)
- **O1 — CCSDS telemetry:** `mgmt` xuất Space Packet, Python giải mã.
- **O2 — EtherCAT R&D:** chỉ làm khi có testbed được xác minh; nếu không, viết tài liệu phân tích ánh xạ.

## 2. Cấu trúc repo

```
qnx-freertos-motion-platform/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── sil_environment.md
│   ├── requirements.md
│   ├── traceability_matrix.md
│   └── results/ (đồ thị, báo cáo timing, log)
├── common/
│   ├── can_matrix.dbc
│   ├── generated/      (pack/unpack sinh từ DBC)
│   ├── frame/          (frame CAN FD-compatible)
│   └── e2e/            (counter + CRC-32 + timeout)
├── qnx/
│   ├── motion_sup/     (resmgr, threads, safety FSM, transport)
│   ├── logger/
│   ├── mgmt/
│   ├── motionctl/      (client CLI)
│   └── tests/
├── freertos/
│   ├── app/            (control, comm, estop, supervisor tasks)
│   ├── control/        (PID, plant model, encoder model)
│   ├── safety/         (FSM, fault handler, fault record)
│   ├── port/           (QEMU mps2-an385: startup, linker, MPU, watchdog, net)
│   └── tests/          (Unity/Ceedling, chạy trên host)
├── tools/
│   ├── dbc_codegen.py
│   ├── frame_sniffer.py
│   ├── sil_runner.py
│   ├── metrics.py
│   ├── plot_step.py
│   └── fault_inject/
└── .github/workflows/ci.yml
```

## 3. Kế hoạch test

### 3.1 Các mức test
| Mức | Công cụ | Nội dung |
|---|---|---|
| Unit (host) | Unity/Ceedling | PID, plant model, E2E, frame, DBC pack/unpack, safety FSM |
| Unit (Python) | pytest | Codegen, sniffer, metrics |
| Component (QNX) | Test C + script trên QNX VM | resmgr devctl, quyền, SHM, priority inheritance |
| Component (FreeRTOS) | QEMU + pytest | Task timing, MPU fault, watchdog |
| Integration SIL | pytest + QNX VM + QEMU | End-to-end, timing budget |
| Fault injection | `fault_inject/` | Mục 3.2 |

### 3.2 Kịch bản fault injection
| # | Lỗi | Cách inject | Phản ứng mong đợi |
|---|---|---|---|
| F1 | Mất command | Proxy UDP drop gói QNX → node | Node disable output sau 50 ms |
| F2 | Mất heartbeat | Drop gói node → QNX | QNX: DEGRADED sau 30 ms, SAFE_STOP sau 100 ms |
| F3 | Sai CRC | Proxy sửa 1 bit | Frame bị loại, counter lỗi tăng, ≥ 3 lỗi → DEGRADED |
| F4 | Lặp / sai thứ tự | Proxy replay / đảo gói | `REPEATED` / `WRONG_SEQ`, không thực thi lệnh |
| F5 | Trễ gói | Proxy delay 40 ms | Phát hiện timeout đúng ngưỡng |
| F6 | E-stop | `motionctl estop` | Output disable < 10 ms |
| F7 | Vượt giới hạn vị trí/tốc độ | Lệnh target ngoài biên | `EINVAL` ở resmgr; nếu lọt → SAFE_STOP |
| F8 | Encoder stuck | Fault flag trong encoder model | Phát hiện bất thường → SAFE_STOP |
| F9 | Task treo | Lệnh debug làm control task loop vô hạn | Supervisor không kick → watchdog reset → fault record |
| F10 | MPU violation | Control task truy cập vùng cấm | MemManage → output disable → reset → fault record |
| F11 | Stack overflow | Task đệ quy cố ý | Hook phát hiện → fault nặng |
| F12 | Crash `motion_sup` | `slay motion_sup` | Node tự disable ≤ 60 ms |
| F13 | Crash `logger`/`mgmt` | `slay` | Control không bị ảnh hưởng, được phát hiện qua death pulse |
| F14 | Ghi SHM chỉ đọc | Lệnh debug trong `mgmt` | `SIGSEGV` ở `mgmt`, `motion_sup` tiếp tục |

Fault inject trên đường truyền dùng một **UDP proxy Python** đặt giữa QNX và QEMU — không phải sửa code sản phẩm.

## 4. CI/CD
- GitHub Actions (Ubuntu runner): cài `arm-none-eabi-gcc`, `qemu-system-arm`, Python.
- Job: build firmware → unit test host → chạy firmware trong QEMU với Python giả lập supervisor (F1, F3, F4, F6, F9, F10) → upload báo cáo/đồ thị.
- Phần QNX không chạy được trên CI public (license): build và test trên máy local, lưu log/kết quả vào `docs/results/`, ghi rõ trong README.

## 5. Rủi ro và cách xử lý
| Rủi ro | Mức độ | Xử lý |
|---|---|---|
| Không kết nối được mạng QNX VM ↔ QEMU | Cao | M0 là gate; thử lần lượt tap, host-only bridge, `-netdev user` + `hostfwd`; nếu vẫn không được thì chạy gateway Python trên host |
| CMSDK watchdog model không reset được | Trung bình | Fallback software watchdog + `NVIC_SystemReset()`, ghi rõ trong tài liệu |
| Timing trong QEMU dao động do host | Trung bình | Dùng `-icount` cho đo jitter firmware; báo cáo tách biệt "thời gian ảo" và "thời gian host"; không claim hard real-time |
| Bị hỏi "CAN FD có thật không" | Trung bình | Trả lời: frame tương thích CAN FD qua UDP, không mô phỏng arbitration/bus-off; safety dựa vào E2E + timeout, độc lập transport |
| Bị hỏi về board/phần cứng | Trung bình | Trả lời SIL; nêu rõ phần cần thay khi lên board (transport backend, peripheral, linker/startup); kinh nghiệm bring-up phần cứng lấy từ project/công việc khác |
| Khối lượng lớn | Trung bình | Thứ tự ưu tiên: M0 → M1 → M2 → M3 → M4 → M5; M6 làm tối thiểu nếu thiếu thời gian; module tùy chọn bỏ qua được |

## 6. Tài liệu liên quan
- [00_PROJECT_OVERVIEW.md](00_PROJECT_OVERVIEW.md) — mục tiêu, phạm vi, mapping JD.
- [01_ARCHITECTURE.md](01_ARCHITECTURE.md) — kiến trúc, giao thức, safety, timing budget.
