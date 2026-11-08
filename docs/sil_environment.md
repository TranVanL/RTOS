# SIL Environment

Điền trong M0. Mọi mục "Đã xác minh" phải có lệnh chạy + output/ảnh chụp làm bằng chứng.

## 1. Phiên bản công cụ
| Công cụ | Phiên bản | Ghi chú |
|---|---|---|
| QNX SDP | _TBD_ (8.0) | |
| VMware | _TBD_ | |
| QEMU | _TBD_ | `qemu-system-arm -M mps2-an385` |
| arm-none-eabi-gcc | _TBD_ | |
| FreeRTOS kernel / FreeRTOS+TCP | _TBD_ | |
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
