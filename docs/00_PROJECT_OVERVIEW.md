# Project Overview — QNX–FreeRTOS Safety-Supervised Motion Control Platform

> Trạng thái: **FINAL v1.0** (chốt 2026-11-04). Thay đổi sau này ghi vào mục "Lịch sử thay đổi" cuối file.

> **Phạm vi phát hành v1.0: M0–M4** (xem [m4_plan.md](m4_plan.md)). Các mục nói về ≥ 12 kịch bản fault injection tự động (M5) và CI chạy firmware trong QEMU đầy đủ (M6) là kế hoạch mở rộng, chưa nằm trong v1.0.

## 1. Tên project (chính thức)
**QNX–FreeRTOS Safety-Supervised Motion Control Platform (Software-in-the-Loop)**

## 2. Mục tiêu
Xây dựng hệ thống điều khiển chuyển động 2 node dị thể (QNX + FreeRTOS) chạy hoàn toàn bằng **Software-in-the-Loop (SIL)** — không cần board thật. Hệ thống mô phỏng mô hình "central supervisor + motor control node" (robot công nghiệp/phẫu thuật, ECU chuyển động ô tô), tập trung vào **cơ chế an toàn nhiều lớp theo tinh thần ISO 26262** và **kỹ thuật real-time kiểm chứng được**.

| Node | Môi trường | Vai trò |
|---|---|---|
| **QNX Central Supervisor** | QNX SDP 8.0 (non-commercial) trong VM x86_64 | Resource manager, 3 process cách ly, IPC message passing, safety state machine, giám sát heartbeat |
| **FreeRTOS Motion Node** | FreeRTOS (port `ARM_CM3_MPU`) trên QEMU `mps2-an385` (Cortex-M3) | Vòng PID 1 kHz + plant model motor, MPU task isolation, watchdog, fault handler, safe-output |

Hai node giao tiếp bằng **frame tương thích CAN FD** (CAN ID 11-bit, payload ≤ 64 B, ma trận tín hiệu trong DBC) **truyền qua UDP**, bảo vệ bởi **lớp E2E theo cấu trúc AUTOSAR E2E Profile 4** (Length, Counter 16-bit, Data ID, CRC-32), độc lập transport.

## 3. Điểm tạo giá trị (định vị cho CV automotive/embedded)
1. **Safety nhiều lớp:** E2E, cross-watchdog 2 chiều, safety FSM, process isolation (QNX), MPU isolation (FreeRTOS), safe-output khi lỗi; có `safety_concept.md` (safety goals + FMEA-lite ánh xạ vào kịch bản lỗi).
2. **Kiểm chứng có hệ thống:** ≥ 12 kịch bản fault injection, mỗi kịch bản có tiêu chí pass/fail **tất định** (đếm tick/chu kỳ) + số đo SIL tham khảo.
3. **Quy trình kiểu ASPICE:** `requirements.md` → thiết kế → test, có traceability matrix; mỗi yêu cầu an toàn ≥ 1 test.
4. **Automation:** DBC → C codegen, pytest điều khiển QEMU + QNX VM, CI chạy firmware trong QEMU mỗi lần push.
5. **Sẵn sàng lên phần cứng:** HAL tách transport/peripheral; tài liệu nêu rõ phần cần thay khi lên board.

## 4. Vai trò trong CV
| Khoảng trống CV hiện tại | Project bù |
|---|---|
| "RTOS, QEMU" trong Skills nhưng chưa có project hands-on | FreeRTOS + MPU + QEMU, QNX resmgr/IPC/priority inheritance |
| Cert ISO 26262/ASPICE chưa có sản phẩm đi kèm | Safety FSM, E2E, fault injection, FMEA-lite, traceability |
| CAN/UDS có trong Skills, chưa có bài thực hành | Frame CAN FD-compatible, DBC codegen, E2E |
| Drone đã có SIL/HIL phía Linux | Phía MCU/RTOS với MPU, watchdog, HardFault |

JD tham chiếu: automotive/embedded safety-critical (QNX, FreeRTOS, CAN, E2E, ASPICE) và VinSurgical Senior Embedded SW Engineer – Basic Software (QNX IPC/resmgr, FreeRTOS, motor control, fault handling, MPU, test/traceability).

## 5. Phạm vi và tính trung thực (không có board)
| Thành phần | Chạy thật | Mô phỏng / model |
|---|---|---|
| QNX: process, thread, IPC, resmgr, SHM, timer | Có (QNX VM) | — |
| FreeRTOS kernel, scheduler, MPU port | Có (QEMU Cortex-M3 + MPU) | — |
| FreeRTOS+TCP trên NIC LAN9118 | Có (model NIC của QEMU) | — |
| Watchdog | CMSDK APB watchdog của QEMU: lần 1 → NMI, lần 2 → reset | Model peripheral, không phải silicon |
| Motor, encoder, driver công suất | — | Plant model + encoder có nhiễu + cờ "actuator enable" |
| CAN FD bus vật lý | — | Frame CAN FD-compatible qua UDP; không có arbitration/ACK/bus-off → ưu tiên E-stop ở tầng ứng dụng |
| Timing | Đo được trong SIL | **Không** phải chứng nhận hard real-time |

**Quy ước diễn đạt (bắt buộc trong README/CV/phỏng vấn):**
- Nói "**AUTOSAR E2E Profile 4–based**" / "E2E-inspired", **không** nói "AUTOSAR-compliant" (chưa kiểm với test vector chính thức).
- Nói "safety mechanisms **theo tinh thần** ISO 26262", **không** nói "ISO 26262 compliant".
- Mọi số timing ghi rõ "SIL, VM/QEMU", tách "thời gian ảo" và "thời gian host".

**Câu trả lời chuẩn khi phỏng vấn:** "Hệ thống được validate bằng SIL: QNX trong VM, FreeRTOS trên QEMU Cortex-M3 có MPU. Transport là frame tương thích CAN FD qua UDP với E2E theo cấu trúc Profile 4; khi lên board chỉ thay backend transport và lớp peripheral, logic điều khiển và safety giữ nguyên."

## 6. Tech stack
| Layer | Công cụ |
|---|---|
| QNX | QNX SDP 8.0 (non-commercial, myQNX), VM x86_64 trên VMware qua QNX Toolkit for VS Code |
| FreeRTOS | Kernel port `ARM_CM3_MPU`, FreeRTOS+TCP, QEMU `mps2-an385`; nền từ demo `CORTEX_MPU_M3_MPS2_QEMU_GCC` + `FreeRTOS_Plus_TCP_Echo_Qemu_mps2` |
| Frame/E2E | CAN FD-compatible frame, DBC + `cantools`, E2E Profile 4–based (CRC-32 poly `0xF4ACFB13`) |
| Transport | UDP (port riêng cho control và E-stop) |
| Test | Unity/Ceedling (host unit), pytest (SIL integration + fault injection) |
| Python | `cantools`, `pytest`, `matplotlib`, script phân tích log |
| CI/CD | GitHub Actions: build + unit test + chạy firmware trong QEMU |

## 7. Mapping kỹ năng
| Kỹ năng | Thành phần |
|---|---|
| QNX system services, multi-thread, secure IPC | Resmgr `/dev/motion`, 3 process, `MsgSend/Receive/Reply`, pulse, `ConnectClientInfo()`, SHM phân quyền |
| FreeRTOS, motor control | PID 1 kHz + plant, priority design, queue/notification |
| Real-time sync FreeRTOS ↔ QNX | Frame CAN FD-compatible + E2E qua UDP, đo latency/jitter |
| Fault handling, MPU | Safety FSM, cross-watchdog, MPU isolation, HardFault/MemManage → fault record |
| Debug, performance | GDB qua QEMU, `pidin`/`slog2info`/System Profiler |
| Test, traceability (ASPICE) | Unity/Ceedling, pytest SIL, fault injection, traceability matrix |
| Python, Git, CI/CD | Codegen, test framework, GitHub Actions |

## 8. Ngoài phạm vi
- Linux BSP, Yocto, Device Tree, kernel driver (đã có ở công việc/project khác).
- Driver CAN FD/SPI/PWM phần cứng thật.
- Chứng nhận hard real-time, tuân thủ ISO 26262/IEC 62304 chính thức.
- ~~CCSDS telemetry, EtherCAT~~ — **đã loại bỏ** (không phục vụ định vị automotive, EtherCAT không có testbed).

## 9. Tài liệu liên quan
- [01_ARCHITECTURE.md](01_ARCHITECTURE.md) — kiến trúc, giao thức, safety, timing.
- [02_PROJECT_PLAN.md](02_PROJECT_PLAN.md) — milestone, lịch, test, CI, rủi ro.
- [requirements.md](requirements.md), [safety_concept.md](safety_concept.md), [traceability_matrix.md](traceability_matrix.md), [sil_environment.md](sil_environment.md).

## 10. Lịch sử thay đổi
| Ngày | Thay đổi |
|---|---|
| 2026-11-04 | Chốt v1.0: E2E theo Profile 4, metric tất định, thêm safety_concept, bỏ CCSDS/EtherCAT, định vị lại cho CV automotive, thêm lịch |
