# Project Plan — QNX–FreeRTOS Safety-Supervised Motion Control Platform

> Nền kiến trúc: **FINAL v1.0** (2026-11-04). Lịch tiến độ được rebaseline ngày 2026-11-12 sau khi chia nhỏ M2.

## 1. Milestones và lịch

Mỗi milestone có **acceptance criteria đo được**; không chuyển milestone khi chưa đạt. Lịch là **ước lượng ~10 giờ/tuần** (làm song song công việc chính) — chỉnh lại sau M0 khi biết tốc độ thật.

| MS | Tuần (ước lượng) | Nội dung | Ghi CV được chưa |
|---|---|---|---|
| M0 | 1–2 | Môi trường SIL + kết nối | — |
| M1 | 3–4 | FreeRTOS control loop | — |
| M2 | 5–8 | Frame, DBC, E2E — **xong 2026-11-12** | — |
| M3 | 9–11 | QNX supervisor — **xong 2026-11-12** | — |
| M4 | 12–13 | Tích hợp end-to-end | **Mức tối thiểu để ghi CV** (kèm ≥ 4 kịch bản F) |
| M5 | 14–15 | Fault injection đầy đủ + safety_concept | Bản đầy đủ |
| M6 | 16–17 | CI, traceability, README, video | Hoàn thiện |

### M0 — Môi trường SIL và đường kết nối (gate bắt buộc)
- Đăng ký myQNX, **kiểm tra điều khoản non-commercial** (ghi vào `sil_environment.md` mục "License"), cài QNX SDP 8.0 + Toolkit for VS Code, tạo VM x86_64; build và chạy "hello".
- Build 2 demo nền cho `mps2-an385`: `CORTEX_MPU_M3_MPS2_QEMU_GCC` và `FreeRTOS_Plus_TCP_Echo_Qemu_mps2`, gộp thành 1 firmware (MPU port + FreeRTOS+TCP).
- Xác minh trong QEMU: MPU hoạt động (truy cập sai → MemManage), CMSDK watchdog NMI → reset, NIC LAN9118 gửi/nhận UDP.
- Cấu hình mạng tap/host-only giữa QNX VM và QEMU.
- **Acceptance:** UDP echo 2 chiều QNX process ↔ FreeRTOS task liên tục 10 phút không mất gói; bảng xác minh peripheral (có model / fallback) trong `docs/sil_environment.md`.

### M1 — FreeRTOS Motion Node: control loop
- Task structure, priority, MPU region cho control task.
- Plant motor DC bậc 2, encoder model, PID cascade (anti-windup, saturation, rate limit).
- Đo jitter bằng DWT/SysTick.
- **Acceptance:** step response overshoot < 10%, settling time theo thông số chọn; histogram jitter xuất được; unit test PID/plant pass trên host.

### M2 — Frame protocol, DBC, E2E
- Kế hoạch chi tiết và contract cần freeze: [m2_plan.md](m2_plan.md).
- Hoàn thiện `common/can_matrix.dbc`; `tools/dbc_codegen.py` sinh pack/unpack C dùng chung 2 phía.
- Thư viện `frame` + `e2e` (C thuần, không phụ thuộc OS) theo Profile 4–based; đối chiếu tham số CRC/layout với spec AUTOSAR E2E.
- `frame_sniffer.py` giải mã frame trên UDP.
- **Acceptance:** unit test E2E phủ `OK/REPEATED/WRONG_SEQ/CRC_ERR/LEN_OR_ID_ERR/TIMEOUT`; CRC khớp vector tính độc lập bằng Python; frame do C sinh được Python giải mã đúng 100% (round-trip).

### M3 — QNX Supervisor: resmgr, process, IPC
- Kế hoạch chi tiết: [m3_plan.md](m3_plan.md). **Hoàn tất 2026-11-12** (SIL): xem [m3_complete_guide.html](m3_complete_guide.html).
- Process `motion_sup`, `logger`, `mgmt`; SHM phân quyền + seqlock.
- Thread theo priority/policy kiến trúc, timer pulse.
- Resmgr `/dev/motion` kiểm tra quyền + validate input; client `motionctl`.
- Test priority inversion có/không priority inheritance.
- **Acceptance:** `devctl` sai quyền → `EPERM`, sai tham số → `EINVAL`; `mgmt` ghi SHM chỉ đọc → `SIGSEGV` mà hệ thống vẫn chạy; trace System Profiler chứng minh priority inheritance.

### M4 — Tích hợp end-to-end
- Nối QNX ↔ FreeRTOS qua UDP: command 10 ms, telemetry + heartbeat 10 ms, kênh E-stop riêng; safety FSM hai phía.
- `metrics.py` tính latency/jitter/reaction từ log hai phía.
- **Acceptance:** chạy 30 phút ở RUN không lỗi E2E; thỏa các tiêu chí tất định trong [01_ARCHITECTURE.md](01_ARCHITECTURE.md) mục 7; số đo SIL được ghi kèm nhãn.

### M5 — Fault injection và safety validation
- `tools/fault_inject/` (pytest) + `tools/udp_proxy.py` cho F1–F14.
- Fault record sau reboot, watchdog reset, process crash QNX.
- Hoàn thiện `safety_concept.md` (FMEA-lite ↔ F1–F14).
- **Acceptance:** 100% kịch bản pass theo tiêu chí tất định; báo cáo tự sinh.

### M6 — CI, tài liệu, traceability, demo
- GitHub Actions: build firmware, unit test, chạy firmware trong QEMU với Python giả lập supervisor.
- Hoàn thiện `traceability_matrix.md`.
- README (kiến trúc, cách chạy, kết quả, giới hạn SIL, badge CI) + video demo 3–5 phút.
- **Acceptance:** CI xanh; mỗi yêu cầu an toàn có ≥ 1 test liên kết.

## 2. Cấu trúc repo

```
.
├── README.md
├── CLAUDE.md
├── docs/
│   ├── 00_PROJECT_OVERVIEW.md  01_ARCHITECTURE.md  02_PROJECT_PLAN.md
│   ├── requirements.md         safety_concept.md
│   ├── traceability_matrix.md  sil_environment.md
│   └── results/                (đồ thị, báo cáo timing, log)
├── common/
│   ├── can_matrix.dbc
│   ├── generated/              (pack/unpack sinh từ DBC — không sửa tay)
│   ├── frame/                  (frame CAN FD-compatible)
│   └── e2e/                    (Profile 4–based)
├── qnx/        motion_sup/ logger/ mgmt/ motionctl/ tests/
├── freertos/   app/ control/ safety/ port/ tests/
├── tools/
│   ├── dbc_codegen.py  frame_sniffer.py  udp_proxy.py
│   ├── sil_runner.py   metrics.py        plot_step.py
│   ├── fault_inject/   tests/            requirements.txt
└── .github/workflows/ci.yml
```

## 3. Kế hoạch test

### 3.1 Các mức test
| Mức | Công cụ | Nội dung |
|---|---|---|
| Unit (host) | Unity/Ceedling | PID, plant, E2E, frame, DBC pack/unpack, safety FSM |
| Unit (Python) | pytest | Codegen, sniffer, metrics |
| Component (QNX) | Test C + script trên VM | resmgr devctl, quyền, SHM, priority inheritance |
| Component (FreeRTOS) | QEMU + pytest | Task timing, MPU fault, watchdog |
| Integration SIL | pytest + QNX VM + QEMU | End-to-end, tiêu chí tất định |
| Fault injection | `tools/fault_inject/` | Mục 3.2 |

### 3.2 Kịch bản fault injection
Cột "Pass" là tiêu chí tất định; thời gian ms chỉ ghi nhận.

| # | Lỗi | Cách inject | Phản ứng / Pass |
|---|---|---|---|
| F1 | Mất command | Proxy drop QNX → node | Node disable output sau 50 tick |
| F2 | Mất heartbeat | Drop node → QNX | DEGRADED sau 3 chu kỳ, SAFE_STOP sau 10 |
| F3 | Sai CRC | Proxy lật 1 bit | Frame bị loại, counter lỗi tăng; ≥ 3 lỗi/100 ms → DEGRADED |
| F4 | Lặp / sai thứ tự | Proxy replay / đảo gói | `REPEATED` / `WRONG_SEQ`, lệnh không được thực thi |
| F5 | Trễ gói | Proxy delay 40 ms | Phát hiện timeout đúng ngưỡng |
| F6 | E-stop | `motionctl estop` | Output disable ≤ 1 control tick sau khi xử lý frame |
| F7 | Vượt giới hạn vị trí/tốc độ | Target ngoài biên | `EINVAL` ở resmgr; nếu lọt → SAFE_STOP |
| F8 | Encoder stuck | Cờ lỗi trong encoder model | Phát hiện → SAFE_STOP |
| F9 | Task treo | Lệnh debug làm control task loop vô hạn | Không kick → watchdog reset → fault record |
| F10 | MPU violation | Control task truy cập vùng cấm | MemManage → disable → reset → fault record |
| F11 | Stack overflow | Task đệ quy cố ý | Hook phát hiện → fault nặng |
| F12 | Crash `motion_sup` | `slay motion_sup` | Node tự disable ≤ 50 tick mất command |
| F13 | Crash `logger`/`mgmt` | `slay` | Control không ảnh hưởng, phát hiện qua death pulse |
| F14 | Ghi SHM chỉ đọc | Lệnh debug trong `mgmt` | `SIGSEGV` ở `mgmt`, `motion_sup` tiếp tục |

Fault inject trên đường truyền dùng `tools/udp_proxy.py` đặt giữa QNX và QEMU — không sửa code sản phẩm.

## 4. CI/CD
- GitHub Actions (Ubuntu): cài `arm-none-eabi-gcc`, `qemu-system-arm`, Python.
- Job M6: build firmware → unit test host → chạy firmware trong QEMU với Python giả lập supervisor (F1, F3, F4, F6, F9, F10) → upload báo cáo/đồ thị. Workflow được cấu hình chạy M1/M2 host tests, CTest wrappers và DBC generation check; remote run xanh (2026-11-12). ARM/QEMU CI vẫn còn mở.
- QNX không chạy được trên CI public (license): build/test local, lưu log vào `docs/results/`, ghi rõ trong README.
- [.github/workflows/ci.yml](../.github/workflows/ci.yml) được cấu hình chạy DBC generation check và pytest cho `tools/` (bao gồm M1/M2 host CTest wrappers); remote run xanh (2026-11-12). Thêm ARM firmware/QEMU job ở M6.

## 5. Rủi ro và cách xử lý
| Rủi ro | Mức | Xử lý |
|---|---|---|
| Không kết nối được mạng QNX VM ↔ QEMU | Cao | M0 là gate; thử tap, host-only bridge, `-netdev user`+`hostfwd`; cuối cùng gateway Python trên host |
| License/availability QNX non-commercial | Trung bình | Kiểm tra ở M0 trước khi đầu tư tiếp; nếu không dùng được, giữ phần FreeRTOS + E2E + Python và thay QNX bằng supervisor POSIX/Linux (ghi rõ trong README) |
| Gộp MPU demo + FreeRTOS+TCP lỗi MPU/privilege | Trung bình | IP task chạy privileged, chỉ isolate control/comm task; ghi rõ |
| Timing QEMU/VM dao động | Trung bình | Pass/fail bằng đại lượng tất định; `-icount` cho jitter; tách thời gian ảo/host |
| Bị hỏi "CAN FD có thật không" | Trung bình | Frame tương thích CAN FD qua UDP, không mô phỏng arbitration/bus-off; safety dựa vào E2E + timeout |
| Bị hỏi "E2E có chuẩn AUTOSAR không" | Trung bình | Profile 4–based, kiểm vector tự tính; chưa đối chiếu vector chính thức |
| Bị hỏi về board/phần cứng | Trung bình | Trả lời SIL; nêu phần cần thay khi lên board; bring-up thật lấy từ drone/công việc |
| Khối lượng lớn, làm bán thời gian | Cao | Thứ tự M0 → M5; **M0–M4 là mức tối thiểu ghi CV**; M6 làm gọn nhưng giữ traceability + CI |

## 6. Tài liệu liên quan
- [00_PROJECT_OVERVIEW.md](00_PROJECT_OVERVIEW.md) · [01_ARCHITECTURE.md](01_ARCHITECTURE.md)
