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
| QEMU node | 10.0.2.15 (DHCP của SLIRP), host thấy ở 10.0.2.2 | user + `hostfwd=udp::5000-:5000` | ☑ host↔node (xem §4.2); ☐ QNX VM↔node |

Phương án chọn và lý do: _TBD_

## 4. Bảng xác minh peripheral QEMU `mps2-an385`
| Chức năng | Kỳ vọng | Kết quả | Fallback |
|---|---|---|---|
| MPU Cortex-M3 | Truy cập sai → MemManage | ☑ Đạt (QEMU 8.2.2, demo `CORTEX_MPU_M3_MPS2_QEMU_GCC`, xem §4.1) | — |
| CMSDK watchdog | Lần 1 NMI, lần 2 reset | ☐ | Đối chiếu `hw/watchdog/cmsdk-apb-watchdog.c` |
| NIC LAN9118 | UDP gửi/nhận | ☑ Đạt (QEMU 8.2.2, FreeRTOS+TCP V4.4.x, xem §4.2) | `-netdev user` + hostfwd |
| DWT cycle counter | Đọc được `CYCCNT` | ☐ | SysTick |
| `-icount` | Jitter ổn định hơn | ☐ | Ghi nhãn thời gian host |

### 4.1 Bằng chứng MPU (QEMU 8.2.2, demo tag 202411.00)
Demo không in log khi MPU bắt lỗi (chỉ `printf` khi scheduler thoát/stack overflow/hết heap). Bằng chứng lấy từ thanh ghi fault qua gdb.

Lệnh (thư mục `~/ref/FreeRTOS/FreeRTOS/Demo/CORTEX_MPU_M3_MPS2_QEMU_GCC`, sau `make`):
```
qemu-system-arm -machine mps2-an385 -monitor null -semihosting --semihosting-config enable=on,target=native \
  -kernel build/RTOSDemo.axf -serial stdio -nographic -gdb tcp::1234 -S &
gdb-multiarch -q -batch build/RTOSDemo.axf -ex "set architecture arm" -ex "target remote :1234" \
  -ex "break MemMang_Handler" -ex "continue" -ex "bt 3" \
  -ex "p/x &ucSharedMemory" -ex "x/wx 0xE000ED28" -ex "x/wx 0xE000ED34"
```
Kết quả:
| Mục | Giá trị | Ý nghĩa |
|---|---|---|
| Breakpoint | dừng tại `MemMang_Handler` (vector MemManage) | MPU sinh fault |
| `bt` | `MemMang_Handler` ← `<signal handler called>` ← `prvROAccessTask` | task `ROAccess` gây fault |
| MMFSR (`0xE000ED28`) | `0x82` | bit7 MMARVALID, bit1 DACCVIOL (vi phạm truy cập dữ liệu) |
| MMFAR (`0xE000ED34`) | `0x2000b420` | bằng `&ucSharedMemory` → ghi vào vùng RO |
| `continue` lần 2 | dừng lại ở handler | fault lặp lại, handler phục hồi, hệ thống vẫn chạy |

Ghi chú: build 10 s không `-S` chạy ổn định (exit 124 do `timeout`), không crash.

### 4.2 Bằng chứng LAN9118 + UDP (QEMU 8.2.2, demo `FreeRTOS_Plus_TCP_Echo_Qemu_mps2`, tag 202411.00)
Không cần root. Node chạy UDP echo ở cổng 5000 (thay task TCP client của demo bằng `FreeRTOS_socket(DGRAM)` + `bind` + `recvfrom`/`sendto`; mã thử nghiệm nằm ngoài repo, ở `~/ref`).
```
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -kernel build/freertos_tcp_mps2_demo.axf \
  -monitor null -semihosting -semihosting-config enable=on,target=native -serial stdio -nographic \
  -netdev user,id=n0,hostfwd=udp::5000-:5000 -net nic,model=lan9118,netdev=n0 \
  -object filter-dump,id=d0,netdev=n0,file=udp.pcap
python3 send_udp.py   # 20 gói 32 B tới 127.0.0.1:5000, timeout 1 s mỗi gói
```
| Kiểm tra | Kết quả |
|---|---|
| DHCP | guest nhận 10.0.2.15, gateway 10.0.2.2, DNS 10.0.2.3 |
| TCP (demo gốc, cổng 7000, 20 s) | 85 kết nối tới echo server trên host, 0 dòng lỗi trong log guest |
| UDP host → node → host | gửi 20, echo đúng nội dung 20, mất 0, sai 0 |

Giới hạn: SLIRP chỉ cho guest đi ra host; chiều host → guest phải dùng `hostfwd` (đã dùng ở trên). Chưa thử QNX VM (VMware) → node, chưa đo mất gói dài hạn (M0 yêu cầu 10 phút). Chưa thử `-icount`, DWT, CMSDK watchdog.

## 5. Kết quả M0 acceptance
UDP echo QNX ↔ FreeRTOS 10 phút: _TBD (gói gửi/nhận/mất)_
