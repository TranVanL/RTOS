# SIL Environment

Điền trong M0. Mọi mục "Đã xác minh" phải có lệnh chạy + output/ảnh chụp làm bằng chứng.

## 1. Phiên bản công cụ
| Công cụ | Phiên bản | Ghi chú |
|---|---|---|
| QNX SDP | 8.0.0 (`~/qnx800`, build 2026/06/05) | image bằng `mkqnximage`, thêm `sshd-auth` qua snippet (`tools/qnxvm/`) |
| VMware | _TBD_ | |
| QEMU | 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18) | `qemu-system-arm -M mps2-an385` |
| arm-none-eabi-gcc | 13.2.1 20231009 (15:13.2.rel1-2) | gdb-multiarch 15.1 |
| FreeRTOS kernel | V11.3.3 (`89592801dd8b73329a7583719b7e3567b50910e5`) | submodule `freertos/third_party/FreeRTOS-Kernel` |
| FreeRTOS demo (tham chiếu) | tag 202411.00 | clone nông tại `~/ref/FreeRTOS`, ngoài repo; chỉ để copy sang `freertos/port/`, `freertos/app/` |
| FreeRTOS+TCP | V4.4.2 (`b86ad086e9870099bb0ac18a75dcb7f6dd508cc7`) | submodule `freertos/third_party/FreeRTOS-Plus-TCP` |
| Python / cantools / pytest | _TBD_ | |

## 2. License
- Điều khoản QNX non-commercial: chủ dự án đã đọc điều khoản (2026-11-10) và xác nhận **được phép public repo** cho project này. Không chép lại nguyên văn điều khoản ở đây; nội dung chính thức là bản trên myQNX. Repo chỉ chứa mã nguồn của dự án, không chứa binary/thư viện QNX hay image VM.
- Tài khoản myQNX: có (chủ dự án); không ghi thông tin tài khoản vào repo.

## 3. Mạng
| Thành phần | IP | Cách nối | Đã xác minh |
|---|---|---|---|
| QNX VM | 192.168.50.1/24 (`vtnet1`); `vtnet0` = SLIRP 10.0.2.15 chỉ để ssh `127.0.0.1:2222` | QEMU x86_64 + KVM (`mkqnximage --type=qemu`, chạy bằng `tools/qnxvm/run_vm.sh`) | ☑ ping QNX→node 3/3 |
| QEMU node | 192.168.50.2/24 (build `-DNODE_IP=192,168,50,2 -DNODE_GATEWAY=192,168,50,1`) | `-netdev socket,connect=127.0.0.1:5555` tới VM (L2, không cần root) | ☑ ping; ☑ UDP 2 chiều (thử 30 s, 0 mất; nghiệm thu 10 phút: xem §5) |
| QEMU node | 10.0.2.15 (DHCP của SLIRP), host thấy ở 10.0.2.2 | user + `hostfwd=udp::5000-:5000` | ☑ host↔node (xem §4.2); ☐ QNX VM↔node |

Phương án chọn cho M0 (phía FreeRTOS, đã xác minh): **`-netdev user` + `hostfwd=udp:<bind>:5000-:5000`, IP tĩnh 10.0.2.15/24, gateway 10.0.2.2.**
- Lý do: không cần root; SLIRP cho node thấy host ở 10.0.2.2; IP tĩnh (không DHCP) cho khởi động tất định, không chờ 250 tick DHCP. Đổi bằng `cmake -DNODE_IP=a,b,c,d ...`.
- Giới hạn: SLIRP chỉ cho node đi ra host; chiều host → node cần `hostfwd`. Node chủ động gửi tới 10.0.2.2 sẽ đến **host**, không tới QNX VM → cần relay Python trên host hoặc tap. `-netdev tap` cần root (chưa thử trên máy này, không có sudo).
- Với QNX VM (VMware host-only): bind `hostfwd` vào IP host-only (ví dụ `hostfwd=udp:<IP host-only>:5000-:5000`) thay vì 127.0.0.1. Chưa xác minh (chưa có QNX VM).
- Đo trên QEMU: SYSCLK = 25 MHz; `configCPU_CLOCK_HZ` của demo gốc (20 MHz) làm 1 tick FreeRTOS = 0,8 ms thời gian ảo. Firmware `freertos/` đặt 25 MHz.

## 4. Bảng xác minh peripheral QEMU `mps2-an385`
| Chức năng | Kỳ vọng | Kết quả | Fallback |
|---|---|---|---|
| MPU Cortex-M3 | Truy cập sai → MemManage | ☑ Đạt, trên demo gốc (§4.1) **và** trên firmware ghép `freertos/` (§4.3) | — |
| CMSDK watchdog | Lần 1 NMI, lần 2 reset | ☑ Đạt (§4.3): không kick → NMI ở ~0,5 s, reset ở ~1,0 s; kick đều → không NMI, không reset | Đối chiếu `hw/watchdog/cmsdk-apb-watchdog.c` (chưa cần) |
| `.noinit` qua watchdog reset | Giá trị còn nguyên sau reset | ☑ Đạt (§4.3): `boot #N`, `nmi_count` cộng dồn qua 8 lần reset | — (M1, M5 phụ thuộc mục này) |
| NIC LAN9118 | UDP gửi/nhận | ☑ Đạt (QEMU 8.2.2, FreeRTOS+TCP V4.4.x, xem §4.2) | `-netdev user` + hostfwd |
| DWT cycle counter | Đọc được `CYCCNT` | ☒ **Không có model** trong QEMU 8.2.2: `DEMCR`, `DWT_CTRL`, `CYCCNT` đọc 0, ghi không có tác dụng (§4.3) | **Dùng SysTick** (M1 đo jitter bằng SysTick, không dùng DWT) |
| `-icount` | Jitter ổn định hơn | ☑ Đạt (§4.3): stddev công việc/tick 54–185 (≈0,3 %) so với 1242–3613 (tới ≈25 %); không đổi khi host bị tải. Đổi lại chậm ~15× thời gian thật khi host tải | Chỉ dùng cho phép đo jitter (M1); chạy tích hợp dùng không `-icount` và ghi nhãn thời gian host |

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

### 4.3 Bằng chứng trên firmware ghép `freertos/` (ARM_CM3_MPU + FreeRTOS+TCP, QEMU 8.2.2)
Build: `cmake --preset node && cmake --build --preset node` → `build/freertos/node.elf`. Bài kiểm tra ngoại vi là bản build riêng: `cmake -S freertos -B build/freertos_<t> -DCMAKE_TOOLCHAIN_FILE=freertos/cmake/arm-none-eabi.cmake -DPERIPH_TEST=<t>` với `<t>` ∈ `wdt|wdt_kick|dwt|icount`. Lệnh chạy chung:
```
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -kernel build/freertos_<t>/node.elf \
  -monitor null -semihosting -semihosting-config enable=on,target=native -serial stdio -nographic [-icount shift=0]
```
| Kiểm tra | Kết quả (trích log) |
|---|---|
| MPU (firmware ghép) | Task không đặc quyền ghi vào vùng chỉ đọc mỗi 1 s: `mpu_test faults=20 CFSR=0x00000082 MMFAR=0x20042440 PC=0x0001292c (protected@0x20042440)`; MMFAR = địa chỉ vùng bảo vệ; handler xoá cờ, nhảy qua lệnh, hệ thống vẫn chạy cùng UDP echo |
| Watchdog, không kick (`wdt`, LOAD=12 500 000, CTRL=3) | `t=502 ms VALUE=20060 RIS=0 nmi=0` → `t=1002 ms RIS=1 nmi=1` → reset → `boot #2, nmi_count carried over = 1` → lặp lại |
| Watchdog, kick mỗi 100 ms (`wdt_kick`, 6 s) | `RIS=0 nmi=0` ở mọi dòng, chỉ 1 dòng `boot #` (không reset) |
| `.noinit` | `boot #1 … boot #8`, `nmi_count carried over` tăng 0→7; chỉ lần đầu in `cold start` |
| DWT (`dwt`) | `DEMCR=0x00000000 DWT_CTRL=0x00000000`, `before=0 after100k=0 after300k=0` → không có model |
| `-icount` (`icount`, 2000 tick, host rảnh) | không `-icount`: `min=0 max=19354 mean=15870 stddev=1242` (lần sau stddev 3551, 3613); `-icount shift=0`: `mean=55525 stddev=185, 54, 75` |
| `-icount` (host tải 8 tiến trình) | không `-icount`: `mean=6130 stddev=1862` (min=0); `-icount shift=0`: `mean=55525 stddev=185, 54` — như lúc rảnh |

Ghi chú: chỉ số "công việc idle mỗi tick" là số vòng idle hook giữa hai tick 1 ms; đây là đại lượng tất định để so sánh, không phải ms. Handler NMI của bài kiểm tra cố ý **không** xoá ngắt để lần hết hạn thứ hai sinh reset.

## 5. Kết quả M0 acceptance
UDP echo 2 chiều process QNX ↔ task FreeRTOS, 600 s, 100 Hz, nối bằng `-netdev socket` (xem `docs/m0_ethernet_qnx_freertos.html`):
```
tools/qnxvm/run_m0.sh 600 100
dir1 (QNX->node->QNX): sent=60000 received=60000 lost=0 mismatch=0
dir2 (node->QNX):      expected=60033 received=60033 lost=0
PASS
```
Log: `docs/results/m0_qnx_600s.result`, `docs/results/m0_node_600s.uart.txt`. SIL (QEMU/KVM trên một host); tiêu chí là đếm gói, không phải ms.

Diễn tập trước đó (peer Python thay QNX, `-netdev user` + `hostfwd`): `sent=60000 received=60000 lost=0 mismatch=0 PASS`.

M0 đã chốt (2026-11-10): license đã xác nhận (§2). Không dùng `-netdev tap` (cần root); thay bằng `-netdev socket`.

## 6. Kết quả M1 (control loop trên node)
Tất cả là **SIL, QEMU 8.2.2, thời gian ảo (`-icount shift=0`)**; không phải phần cứng, không tuyên bố hard real-time. Chi tiết và code: `docs/m1_part4_pid_cascade_guide.html` … `m1_part8_host_vs_node_guide.html`.

| Hạng mục | Kết quả |
|---|---|
| Step response (host, bước 1 rad, tick 1 ms) | overshoot 0,21 %, settling 139 tick (chốt ≤ 200), sai số xác lập 0,19 count; có tải ngoài và nhiễu encoder: đạt (`control_step_response`) |
| Control task Restricted (MPU) | 70 040 tick, 0 khoảng trống tick, `missed=0`, `dropped=0`, 0 torn read (seqlock); ghi cố ý ngoài vùng → MemManage bị bắt, task chạy tiếp |
| Nguồn thời gian | DWT không có (§4); SysTick **không đọc được từ task không đặc quyền** (vùng PPB) → dùng CMSDK **TIMER0** (`0x40000000`, 25 MHz, đếm xuống 32 bit, cấp bằng vùng MPU chỉ-đọc 32 B) |
| Jitter (10 000 chu kỳ, count 40 ns ảo) | A không tải: ±3 (σ 0,15); B echo ≈ 88 Hz ảo + probe 100 Hz: −8…+8 (p99 2); C echo ≈ 1 013 Hz ảo: −10…+10 (p99 4); 0 chu kỳ trễ ≥ 1 tick |
| Chi phí một bước | controller ≈ 2,6 nghìn lệnh (65,6 count @shift=0; 2 087 count @shift=5, cùng số lệnh); plant trên node ≈ 11,4 nghìn lệnh; controller 10,5 % của 25 000 nếu CPI = 1, cả hai 56 % (> 30 %) |
| Host ↔ node | max \|Δpos\| = 1·10⁻⁶ rad (làm tròn 1 µrad của log), count lệch 0; overshoot/settling/sai số xác lập giống hệt (0,209 % / 139 / 0,187) |

Ghi chú và giới hạn: (a) không có `-icount`, control task bị trễ chu kỳ 8–16 lần/5 s do vCPU bị chặn khi ghi UART (suy ra, chưa kiểm chứng trực tiếp); (b) `sil_runner echo` mất 4 gói mỗi lượt (nghi lúc khởi động, chưa kiểm chứng); (c) tải "100 Hz / 1 kHz" được hiệu chuẩn theo bộ đếm `udp_echo rx` của node trên thời gian ảo (thực đo 88 Hz và 1 013 Hz); (d) CPI thật của Cortex-M3 không đo được bằng QEMU. Dữ liệu thô: `docs/results/m1_*`.
