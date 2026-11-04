# Project Overview — QNX–FreeRTOS Real-Time Control Bridge over CAN FD

## 1. Tên project (chính thức)
**QNX–FreeRTOS Real-Time Control Bridge over CAN FD**
*(module phụ: EtherCAT Master/Slave R&D testbed)*

## 2. Mục tiêu
Xây dựng một hệ thống điều khiển real-time gồm 2 node dị thể (heterogeneous dual-OS), mô phỏng đúng mô hình "central control computer + motor/sensor control node" dùng trong thiết bị y tế/robot công nghiệp:

- **QNX** đóng vai trò **Central Supervisor** — quản lý tài nguyên, đa luồng, IPC, giám sát lỗi, giao tiếp quản trị qua TCP/UDP.
- **FreeRTOS** đóng vai trò **Deterministic I/O Node** — điều khiển motor/sensor theo chu kỳ cứng, giao tiếp CAN FD, phản ứng lỗi cục bộ (watchdog, MPU fault handler).
- Giao tiếp chính giữa 2 node: **CAN FD** (qua lớp trừu tượng `can_if`, backend mặc định là CAN bus ảo qua UDP bridge — xem [01_ARCHITECTURE.md](01_ARCHITECTURE.md)).
- Vòng điều khiển **PID** trên FreeRTOS chạy với mô hình motor (plant model) mô phỏng, có đồ thị step response.
- Bộ công cụ **Python** (cantools, pytest/Robot Framework, phân tích log) phục vụ sinh code từ DBC, test tự động và fault-injection.
- Telemetry/management dùng khung **CCSDS Space Packet** qua TCP/UDP.
- Module phụ (R&D, testbed riêng): **EtherCAT Master (SOEM) / Slave (SOES)**.

## 3. JD mục tiêu
| Công ty | Vị trí |
|---|---|
| VinSurgical | Senior Embedded Software Engineer – Basic Software |
| VinSpace | Embedded Linux Engineer (chỉ phủ phần kỹ năng chung: multi-threading, IPC, driver-level programming, fault handling — phần Linux BSP/Yocto/Device Tree đã được cover bởi các project/công việc khác, không nằm trong phạm vi project này) |

## 4. Giới hạn & lưu ý quan trọng
- **Không có board vật lý.** Toàn bộ hệ thống chạy mô phỏng bằng QEMU/Renode (cho FreeRTOS) và VM (cho QNX SDP).
- **Không làm lại phần Linux BSP/Yocto/Device Tree** — đã có project/kinh nghiệm công ty khác cover, tránh trùng lặp portfolio.
- **EtherCAT là module R&D riêng biệt**, không phải xương sống chính của hệ thống — tránh rủi ro khi bị hỏi sâu về chi tiết port EtherCAT Master lên QNX (vốn hiếm tài liệu, khó kiểm chứng).
- **CAN FD là CAN bus ảo.** QNX VM không có CAN controller thật nên frame CAN FD được mang qua UDP bridge. Thiết kế tách `can_if` (HAL) để thay backend bằng `devc-can` (QNX) / FDCAN driver (STM32) trên phần cứng thật. Phải ghi rõ điều này trong README.
- **Renode có hỗ trợ FDCAN/MCAN cho target STM32 hay không cần xác minh ở M1.** Nếu không, cả hai phía dùng backend UDP và trình bày trung thực.
- Trả lời phỏng vấn trung thực về phần cứng: "validate trên Renode + QNX VM, kiến trúc có HAL nên chuyển sang board thật là thay lớp driver".

## 5. Tech stack
| Layer | Công cụ |
|---|---|
| QNX | QNX SDP (free license), chạy trên VirtualBox/VMware |
| FreeRTOS | Renode (mô phỏng STM32) hoặc QEMU (MPS2-AN385/Cortex-M) |
| Giao thức chính | CAN FD (ma trận tín hiệu định nghĩa bằng file DBC, sinh header C bằng Python) |
| Lớp truyền | `can_if` HAL + backend UDP bridge (CAN bus ảo); backend FDCAN/`devc-can` cho phần cứng thật |
| Giao thức phụ | SPI (nội bộ node FreeRTOS), UDP/TCP (quản trị/giám sát từ QNX) |
| EtherCAT R&D | SOEM (Master) + SOES (Slave), testbed Linux/Renode riêng |
| Test | Unity/Ceedling (FreeRTOS), test harness tối giản (QNX), pytest/Robot Framework điều khiển Renode |
| Python tooling | `cantools` (DBC → code/giải mã), script phân tích log ring-buffer, đo latency, vẽ step response (matplotlib) |
| Telemetry/Management | CCSDS Space Packet qua TCP/UDP |
| CI/CD | GitHub Actions (build + test FreeRTOS trong Renode) |

## 6. Mapping kỹ năng JD → Project
| Yêu cầu JD | Thành phần project đáp ứng |
|---|---|
| QNX: multi-thread, resource manager, IPC | QNX Central Supervisor: `resmgr`, `MsgSend/MsgReceive/MsgReply`, pulse |
| FreeRTOS, motor/sensor control | FreeRTOS Deterministic I/O Node: control task chu kỳ cứng, vòng PID + plant model motor, encoder giả có nhiễu |
| CAN FD | Giao thức chính giữa 2 node |
| SPI | Task SPI trong FreeRTOS node (đọc encoder/IMU giả lập) |
| UDP/TCP | Thread quản trị/giám sát trên QNX |
| Fault handler, MMU, watchdog | State machine `INIT→RUN→DEGRADED→SAFE_STOP`, MPU fault handler, watchdog chéo 2 chiều; QNX tách process (control / logger / mgmt) với shared memory phân quyền để minh họa memory protection thật |
| EtherCAT (Master QNX/Slave FreeRTOS) | Module R&D riêng bằng SOEM/SOES, slave PDO dùng chung state machine `INIT→RUN→SAFE_STOP` với hệ thống chính |
| Unit/Integration Test, traceability | Ceedling/Unity + test fault-injection + traceability matrix |
| Git, CI/CD | GitHub Actions pipeline (build FreeRTOS + pytest/Renode test) |
| Python | Công cụ DBC codegen, test runner fault-injection, phân tích log, vẽ step response |
| VinSpace: CCSDS / telemetry | Telemetry/management dùng khung CCSDS Space Packet |

## 7. Tài liệu liên quan trong bộ hồ sơ project
- [01_ARCHITECTURE.md](01_ARCHITECTURE.md) — chi tiết kiến trúc, sơ đồ, giao thức.
- [02_PROJECT_PLAN.md](02_PROJECT_PLAN.md) — lộ trình milestone, cấu trúc repo, kế hoạch test/CI, rủi ro.
