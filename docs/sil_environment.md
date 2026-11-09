# SIL Environment

Điền trong M0. Mọi mục "Đã xác minh" phải có lệnh chạy + output/ảnh chụp làm bằng chứng.

## 1. Phiên bản công cụ
| Công cụ | Phiên bản | Ghi chú |
|---|---|---|
| QNX SDP | _TBD_ (8.0) | |
| VMware | _TBD_ | |
| QEMU | 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18) | `qemu-system-arm -M mps2-an385` |
| arm-none-eabi-gcc | 13.2.1 20231009 (15:13.2.rel1-2) | gdb-multiarch 15.1 |
| FreeRTOS kernel | V11.3.3 (`89592801dd8b73329a7583719b7e3567b50910e5`) | submodule `freertos/third_party/FreeRTOS-Kernel` |
| FreeRTOS demo (tham chiếu) | tag 202411.00 | clone nông tại `~/ref/FreeRTOS`, ngoài repo; chỉ để copy sang `freertos/port/`, `freertos/app/` |
| FreeRTOS+TCP | V4.4.2 (`b86ad086e9870099bb0ac18a75dcb7f6dd508cc7`) | submodule `freertos/third_party/FreeRTOS-Plus-TCP` |
| Python / cantools / pytest | _TBD_ | |

## 2. License
- Điều khoản QNX non-commercial: _TBD — đọc và tóm tắt; xác nhận được phép public repo/ghi vào CV hay không._
- Tài khoản myQNX: _TBD_

## 3. Mạng
| Thành phần | IP | Cách nối | Đã xác minh |
|---|---|---|---|
| QNX VM | _TBD_ | host-only/bridge | ☐ |
| QEMU node | _TBD_ | tap / user+hostfwd | ☐ |

Phương án chọn và lý do: _TBD_

## 4. Bảng xác minh peripheral QEMU `mps2-an385`
| Chức năng | Kỳ vọng | Kết quả | Fallback |
|---|---|---|---|
| MPU Cortex-M3 | Truy cập sai → MemManage | ☐ | — |
| CMSDK watchdog | Lần 1 NMI, lần 2 reset | ☐ | Đối chiếu `hw/watchdog/cmsdk-apb-watchdog.c` |
| NIC LAN9118 | UDP gửi/nhận | ☐ | `-netdev user` + hostfwd |
| DWT cycle counter | Đọc được `CYCCNT` | ☐ | SysTick |
| `-icount` | Jitter ổn định hơn | ☐ | Ghi nhãn thời gian host |

## 5. Kết quả M0 acceptance
UDP echo QNX ↔ FreeRTOS 10 phút: _TBD (gói gửi/nhận/mất)_
