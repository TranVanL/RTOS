# Project Overview — QNX–FreeRTOS Safety-Supervised Motion Control Platform

## 1. Tên project (chính thức)
**QNX–FreeRTOS Safety-Supervised Motion Control Platform (Software-in-the-Loop)**

*Module tùy chọn: CCSDS telemetry, EtherCAT R&D.*

## 2. Mục tiêu
Xây dựng một hệ thống điều khiển chuyển động 2 node dị thể (QNX + FreeRTOS), chạy hoàn toàn bằng **Software-in-the-Loop (SIL)** — không cần board thật. Hệ thống mô phỏng mô hình "central control computer + motor control node" của robot phẫu thuật/robot công nghiệp, tập trung vào **kiến trúc an toàn nhiều lớp** và **kỹ thuật real-time đo được bằng số liệu**.

| Node | Môi trường | Vai trò |
|---|---|---|
| **QNX Central Supervisor** | QNX SDP 8.0 (non-commercial license) chạy trong VM x86_64 | Resource manager, 3 process cách ly, IPC message passing, safety state machine, giám sát heartbeat, management |
| **FreeRTOS Motion Node** | FreeRTOS (port MPU) trên QEMU `mps2-an385` (Cortex-M3) | Vòng PID 1 kHz + plant model motor, task isolation bằng MPU, watchdog, fault handler, safe-output |

Hai node giao tiếp qua **giao thức frame tương thích CAN FD** (CAN ID 11-bit, payload tối đa 64 byte, ma trận tín hiệu định nghĩa bằng DBC), **truyền qua UDP**, được bảo vệ bởi **lớp E2E độc lập transport** (data ID, sequence counter, CRC-32, timeout) theo tư tưởng AUTOSAR E2E.

## 3. Điểm tạo "sức nặng" của project
1. **Safety architecture nhiều lớp:** E2E protection, cross-watchdog 2 chiều, safety state machine có reaction-time budget, process isolation (QNX), MPU task isolation (FreeRTOS), safe-output khi fault.
2. **Real-time engineering có số liệu:** đo jitter chu kỳ điều khiển, latency round-trip (p50/p99/max), CPU load, worst-case reaction time của E-stop và mất heartbeat; test priority inversion với mutex priority inheritance trên QNX.
3. **Fault injection có hệ thống:** ≥ 12 kịch bản lỗi, mỗi kịch bản có phản ứng mong đợi + thời gian phản ứng đo được + test tự động.
4. **Automation & toolchain:** DBC → C codegen, test framework Python điều khiển QEMU + QNX VM, CI chạy firmware FreeRTOS trong QEMU mỗi lần push.
5. **Thiết kế sẵn sàng cho phần cứng thật:** lớp HAL tách transport/peripheral; tài liệu ghi rõ phần nào cần thay khi chuyển sang board.

## 4. JD mục tiêu
| Công ty | Vị trí | Project này phủ |
|---|---|---|
| VinSurgical | Senior Embedded Software Engineer – Basic Software | QNX (IPC, resmgr, multi-thread), FreeRTOS, motor control loop, fault handling, memory protection, test/traceability, Python, CI |
| VinSpace | Embedded Linux Engineer | Kỹ năng chung: multi-threading, IPC, POSIX timers, sockets, watchdog/health monitoring, telemetry CCSDS (tùy chọn). Phần Linux BSP/Yocto/Device Tree/kernel driver đã được cover bởi công việc và project khác |

## 5. Phạm vi và tính trung thực (không có board)
| Thành phần | Chạy thật | Mô phỏng / model |
|---|---|---|
| QNX: process, thread, IPC, resmgr, shared memory, timer | Có (QNX VM) | — |
| FreeRTOS kernel, scheduler, MPU port | Có (QEMU emulate Cortex-M3 + MPU) | — |
| Network stack FreeRTOS+TCP trên NIC LAN9118 | Có (model NIC của QEMU) | — |
| Watchdog phần cứng | CMSDK APB watchdog của QEMU `mps2-an385`: lần 1 → NMI (ghi fault record), lần 2 → system reset | Model peripheral, không phải silicon thật |
| Motor, encoder, driver công suất | — | Plant model + encoder model có nhiễu + "actuator enable" flag (thay cho PWM) |
| CAN FD bus vật lý | — | Frame tương thích CAN FD truyền qua UDP. Không có arbitration/ACK/bus-off → ưu tiên E-stop được hiện thực ở tầng ứng dụng (socket + thread riêng) |
| Timing | Đo được trong SIL | Số liệu là của môi trường VM/QEMU, **không** phải chứng nhận hard real-time trên phần cứng |

**Câu trả lời chuẩn khi phỏng vấn:** "Hệ thống được validate bằng SIL: QNX chạy trong VM, FreeRTOS chạy trên QEMU Cortex-M3 có MPU. Transport là frame tương thích CAN FD qua UDP với E2E protection; khi lên board thật chỉ thay backend transport (CAN controller + driver của BSP) và lớp peripheral, phần logic điều khiển và safety giữ nguyên."

## 6. Tech stack
| Layer | Công cụ |
|---|---|
| QNX | QNX SDP 8.0 (non-commercial, tài khoản myQNX), QNX VM x86_64 trên VMware qua QNX Toolkit for VS Code |
| FreeRTOS | FreeRTOS kernel (port `ARM_CM3_MPU`), FreeRTOS+TCP, QEMU `mps2-an385`; nền từ demo `CORTEX_MPU_M3_MPS2_QEMU_GCC` + `FreeRTOS_Plus_TCP_Echo_Qemu_mps2` |
| Frame protocol | CAN FD-compatible frame, DBC + `cantools` sinh header C |
| Safety layer | E2E: data ID + sequence counter + CRC-32 + timeout |
| Transport | UDP (port riêng cho control frames và E-stop) |
| Management (tùy chọn) | CCSDS Space Packet qua UDP |
| Test | Unity/Ceedling (unit, chạy trên host), pytest (SIL integration + fault injection) |
| Python tooling | `cantools`, `pytest`, `matplotlib`, script phân tích log/latency |
| CI/CD | GitHub Actions: build + unit test + chạy firmware trong QEMU |
| EtherCAT (tùy chọn) | SOEM/SOES — chỉ làm khi có testbed được xác minh |

## 7. Mapping kỹ năng JD → Project
| Yêu cầu JD | Thành phần project |
|---|---|
| QNX: system services, multi-thread, secure IPC | Resmgr `/dev/motion`, 3 process cách ly, `MsgSend/MsgReceive/MsgReply`, pulse, kiểm tra quyền client, shared memory phân quyền |
| FreeRTOS, motor/sensor control | PID 1 kHz + plant model, task priority design, queue/notification |
| Real-time data sync giữa FreeRTOS và QNX (CAN FD, UDP/TCP) | Frame tương thích CAN FD + E2E qua UDP, đồng bộ lệnh/telemetry chu kỳ, đo latency/jitter |
| Monitoring, fault handler, memory management/MPU | Safety state machine, cross-watchdog, MPU task isolation, HardFault/MemManage handler lưu fault record, process isolation trên QNX |
| Debug, performance analysis | GDB qua QEMU, QNX `pidin`/`slog2info`/System Profiler, đo jitter/latency/CPU load |
| Unit/Integration Test, traceability | Unity/Ceedling, pytest SIL, fault injection, traceability matrix Yêu cầu → Thiết kế → Test |
| Python | Codegen, test framework, phân tích log, vẽ đồ thị |
| Git, CI/CD, HIL model | GitHub Actions + SIL (bước trước HIL) |
| EtherCAT | Module R&D tùy chọn, trình bày đúng phạm vi |

## 8. Ngoài phạm vi
- Linux BSP, Yocto, Device Tree, kernel driver (đã có ở project/công việc khác).
- Driver CAN FD/SPI/PWM phần cứng thật (không có board).
- Chứng nhận hard real-time và tuân thủ IEC 62304.

## 9. Tài liệu liên quan
- [01_ARCHITECTURE.md](01_ARCHITECTURE.md) — kiến trúc, giao thức, safety, timing budget.
- [02_PROJECT_PLAN.md](02_PROJECT_PLAN.md) — milestone, acceptance criteria, test, CI, rủi ro.
