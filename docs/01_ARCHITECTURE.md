# Architecture — QNX–FreeRTOS Safety-Supervised Motion Control Platform

> Trạng thái: **FINAL v1.0** (2026-11-04).

## 1. Sơ đồ tổng thể

```mermaid
graph LR
    subgraph HOST["Host PC"]
        subgraph QNXVM["QNX VM (x86_64)"]
            subgraph P1["Process: motion_sup"]
                RM["resmgr /dev/motion"]
                CT["Supervisor control thread"]
                RX["Frame RX thread"]
                TX["Frame TX thread"]
                ES["E-stop thread (prio cao nhất)"]
                HB["Heartbeat monitor"]
                FSM["Safety state machine"]
            end
            subgraph P2["Process: logger"]
                LOG["Event + trace logger"]
            end
            subgraph P3["Process: mgmt"]
                MG["Management API (UDP)"]
            end
            CLI["motionctl (client)"]
        end
        subgraph QEMU["QEMU mps2-an385 (Cortex-M3 + MPU)"]
            CTRL["Control task 1 kHz: PID + plant model"]
            NET["Comm task: FreeRTOS+TCP"]
            ESN["E-stop task"]
            SUP["Supervisor task: health + watchdog kick"]
            FH["HardFault/MemManage handler"]
            OUT["Actuator output model (enable flag)"]
        end
        PY["Python SIL test framework"]
    end

    CLI -- "devctl()" --> RM
    RM --> CT
    CT --> FSM
    HB --> FSM
    RX --> HB
    CT -- "SHM state/trace (ghi)" --> LOG
    CT -- "SHM state (chỉ đọc)" --> MG
    MG -- "MsgSend (lệnh hợp lệ)" --> CT
    TX -- "UDP :5000 control frames + E2E" --> NET
    NET -- "UDP :5000 telemetry + heartbeat" --> RX
    ES -- "UDP :5001 E-stop" --> ESN
    NET --> CTRL
    ESN --> OUT
    CTRL --> OUT
    SUP --> CTRL
    FH --> OUT
    PY -- "UDP mgmt / fault inject" --> MG
    PY -- "QEMU monitor + GDB" --> QEMU
```

## 2. Môi trường chạy và đường kết nối

| Thành phần | Cấu hình |
|---|---|
| QNX | QNX SDP 8.0 (license non-commercial, cần tài khoản myQNX); VM x86_64 tạo bằng QNX Toolkit for VS Code + VMware (`mkqnximage`), network bridge/host-only với host |
| FreeRTOS | `qemu-system-arm -M mps2-an385`, NIC LAN9118 (`0x40200000`), FreeRTOS+TCP, IP tĩnh. Code nền: demo `FreeRTOS/Demo/CORTEX_MPU_M3_MPS2_QEMU_GCC` (MPU) + `FreeRTOS-Plus/Demo/FreeRTOS_Plus_TCP_Echo_Qemu_mps2` (TCP/IP) — hai demo tách rời, phải gộp ở M0 |
| Kết nối | QEMU dùng `-netdev tap` (hoặc `-netdev user` + `hostfwd` cho UDP); QNX VM và QEMU cùng subnet host-only |
| Port | `5000/udp` control + telemetry, `5001/udp` E-stop, `6000/udp` management |
| Debug | `qemu ... -s -S` + `arm-none-eabi-gdb`; QNX: `pidin`, `slog2info`, `on -p`, System Profiler (`tracelogger`) |

**M0 gate:** trước mọi milestone khác phải chứng minh được UDP echo 2 chiều giữa process QNX và task FreeRTOS trong QEMU. Đây là nền móng của toàn bộ project.

## 3. QNX Central Supervisor

### 3.1 Process và cách ly
| Process | Quyền | Vai trò |
|---|---|---|
| `motion_sup` | Ghi SHM `state`, ghi SHM `trace` | Logic chính, giao tiếp FreeRTOS, safety state machine, resmgr |
| `logger` | Đọc SHM `state`, đọc SHM `trace` | Ghi event log và trace ra file, xoay vòng file |
| `mgmt` | Chỉ đọc SHM `state` | Trả trạng thái/telemetry cho tool bên ngoài, chuyển lệnh hợp lệ vào `motion_sup` qua `MsgSend` |

- SHM tạo bằng `shm_open()` + `mmap()`; process chỉ đọc mở với `O_RDONLY` + `PROT_READ`. Ghi vào vùng chỉ đọc gây `SIGSEGV` — dùng làm một kịch bản fault injection để chứng minh memory protection giữa các process.
- Đồng bộ SHM bằng seqlock (writer duy nhất, reader không khóa) để tránh reader làm chậm control thread.
- Process crash được phát hiện bằng QNX `procmgr_event`/death pulse; `logger`/`mgmt` crash không ảnh hưởng control; `motion_sup` crash → FreeRTOS mất heartbeat → tự SAFE_STOP (xem mục 6).
- **Hiện thực (M3 WP5):** `motion_sup` tạo `logger`/`mgmt` bằng `posix_spawn`, đăng ký `procmgr_event_notify_add(PROCMGR_EVENT_PROCESS_DEATH)` nhận pulse và dùng `waitpid(WNOHANG)` để biết tín hiệu/exit code (kèm poll 50 ms phòng khi pulse đến trước khi child waitable). `/motion_state` tạo mode `0644` do `motion_sup` sở hữu; process khác uid chỉ mở được `O_RDONLY` (mở `O_RDWR` → `EACCES`), ánh xạ `PROT_READ`.

### 3.2 Thread trong `motion_sup`
| Thread | Policy / priority | Chu kỳ | Nhiệm vụ |
|---|---|---|---|
| E-stop | `SCHED_FIFO` 60 | Event | Gửi E-stop frame trên socket riêng, lặp lại 3 lần cách nhau 1 ms |
| Heartbeat monitor | `SCHED_FIFO` 55 | 5 ms (timer pulse) | Kiểm tra timeout heartbeat/telemetry, kích hoạt chuyển trạng thái |
| Supervisor control | `SCHED_FIFO` 50 | 10 ms (timer pulse) | Sinh trajectory/setpoint, gửi command, kiểm tra giới hạn |
| Frame RX | `SCHED_FIFO` 45 | Event | Nhận UDP, kiểm tra E2E, cập nhật state |
| Frame TX | `SCHED_FIFO` 45 | Event | Đóng gói frame + E2E, gửi UDP |
| resmgr pool | `SCHED_RR` 20 | Event | Xử lý `open/devctl/read` từ client |

- Timer: `timer_create(CLOCK_MONOTONIC, SIGEV_PULSE)` gửi pulse vào channel của thread.
- Mutex dùng priority inheritance (mặc định của QNX); có test cố ý tạo priority inversion để chứng minh và đo.
- CPU budget: đo bằng System Profiler, mục tiêu mỗi chu kỳ control < 20% thời gian chu kỳ trong VM.

### 3.3 Resource manager `/dev/motion`
| `devctl` | Quyền | Mô tả |
|---|---|---|
| `DCMD_MOTION_GET_STATUS` | Mọi client | Trạng thái FSM, lỗi, counters |
| `DCMD_MOTION_SET_TARGET` | Group `motion` | Đặt vị trí mục tiêu, kiểm tra biên |
| `DCMD_MOTION_ENABLE` / `DISABLE` | Group `motion` | Bật/tắt điều khiển |
| `DCMD_MOTION_ESTOP` | Mọi client | Dừng khẩn |
| `DCMD_MOTION_RESET_FAULT` | Group `motion` | Xóa lỗi, chỉ hợp lệ trong SAFE_STOP |

- Kiểm tra quyền bằng `ConnectClientInfo()` (uid/gid) — "secure IPC" theo đúng JD.
- Validate kích thước payload, giá trị biên (vị trí, tốc độ) trước khi xử lý; trả `EINVAL`/`EPERM` khi sai.

## 4. Safety State Machine

```
        +--------+  self-test OK  +---------+  enable   +-------+
        |  INIT  | -------------> |  READY  | --------> |  RUN  |
        +--------+                +---------+           +-------+
                                       ^                  |    |
                                       | reset fault      |    | timeout ngắn / lỗi E2E lặp
                                       |                  |    v
                                  +-----------+  timeout  +----------+
                                  | SAFE_STOP | <-------- | DEGRADED |
                                  +-----------+           +----------+
                                       ^   E-stop / fault nặng (từ mọi trạng thái)
```

| Transition | Điều kiện | Phản ứng |
|---|---|---|
| RUN → DEGRADED | Mất 3 heartbeat liên tiếp (30 ms) hoặc ≥ 3 lỗi E2E trong 100 ms | Giới hạn tốc độ 20%, giữ vị trí |
| DEGRADED → RUN | 10 heartbeat hợp lệ liên tiếp | Khôi phục |
| DEGRADED → SAFE_STOP | Mất 10 heartbeat (100 ms) | Gửi E-stop, disable output |
| Mọi trạng thái → SAFE_STOP | E-stop, vượt giới hạn vị trí/tốc độ, fault record từ node | Disable output, ghi fault |
| SAFE_STOP → READY | `RESET_FAULT` + nguyên nhân đã hết | Chỉ bằng lệnh có quyền |

**Hiện thực phía node (M4):** `common/safety/node_fsm.[ch]` (C thuần), gọi một lần mỗi tick điều khiển 1 ms. INIT→READY ở tick đầu; READY→RUN khi có CMD hợp lệ mode ≠ DISABLE; RUN/DEGRADED không có CMD ≥ 25 tick → DEGRADED (hạn 20 % tốc độ), ≥ **50 tick** → SAFE_STOP (output disable); E-stop từ mọi trạng thái → SAFE_STOP trong cùng tick mà control task nhận yêu cầu; SAFE_STOP → READY sau 5 CMD DISABLE liên tiếp. Chi tiết và lý do: [m4_plan.md](m4_plan.md) §2.

FreeRTOS node chạy state machine tương ứng ở phía mình: mất command quá 50 ms → tự đưa output về disable (fail-safe cục bộ, không phụ thuộc QNX).

**Hiện thực phía QNX (M3, WP1):** FSM nằm ở `common/safety/safety_fsm.[ch]` (C thuần, không đọc clock), gọi một lần mỗi tick 10 ms của supervisor; mọi thời hạn đếm bằng tick. Quy tắc đã chốt: 3 heartbeat mất *liên tiếp* → DEGRADED, 10 liên tiếp → SAFE_STOP (bộ đếm không reset khi vào DEGRADED); ≥ 3 lỗi E2E trong cửa sổ trượt 10 tick → DEGRADED; 10 heartbeat hợp lệ liên tiếp (và cửa sổ lỗi E2E dưới ngưỡng) → RUN. E-stop > vượt giới hạn > fault từ node theo thứ tự ưu tiên, từ INIT/READY/RUN/DEGRADED. `RESET_FAULT` chỉ có tác dụng trong SAFE_STOP và chỉ khi không còn E-stop/limit/fault và tick đó có heartbeat hợp lệ; ngược lại bị từ chối (`reset_rejected`). `send_estop` là xung một tick khi vào SAFE_STOP.

## 5. FreeRTOS Motion Node

### 5.1 Task
| Task | Priority | Chu kỳ | Chế độ MPU | Nhiệm vụ |
|---|---|---|---|---|
| E-stop | Cao nhất (`configMAX_PRIORITIES - 1` = 11, M4) | Event | Privileged | Nhận E-stop socket riêng, disable output ngay |
| Control | Cao (10, M4) | 1 ms (`vTaskDelayUntil`) | Restricted | PID vị trí/tốc độ, plant model, kiểm tra giới hạn |
| Comm | Trung bình (5, M4) | Poll 1 tick; TELEM + HB mỗi 10 tick | **Privileged** (M4, fallback đã ghi ở rủi ro; chỉ control task là Restricted) | Nhận command, kiểm tra E2E, gửi telemetry + heartbeat |
| Supervisor | Thấp hơn Comm | 10 ms | Privileged | Kiểm tra "alive counter" của các task, kick watchdog chỉ khi tất cả task khỏe |
| Idle hook | Thấp nhất | — | — | Đo CPU load |

- Giao tiếp giữa task: queue (command), task notification (sự kiện), double buffer (telemetry).
- **MPU:** dùng port `ARM_CM3_MPU`, control task chỉ truy cập vùng nhớ của nó + vùng shared command/state; truy cập ngoài vùng → MemManage fault.

### 5.2 Control loop & plant model
- Plant: motor DC bậc 2 (điện + cơ), tham số cấu hình được, tích phân Euler/RK2 theo chu kỳ 1 ms.
- Encoder model: lượng tử hóa (vd 4096 count/vòng) + nhiễu Gaussian + tùy chọn bị đứng (stuck) để fault injection.
- Controller: PID cascade (vị trí → tốc độ), anti-windup, saturation, rate limit, giới hạn vị trí mềm.
- Bằng chứng: đồ thị step response (overshoot, settling time, steady-state error) và jitter chu kỳ.

### 5.3 Fault handling
| Cơ chế | Hành vi |
|---|---|
| Command timeout | > 50 ms không có command hợp lệ → disable output, báo trạng thái |
| E2E error | Frame sai CRC/counter/data ID bị loại, tăng counter, báo trong telemetry |
| Task health | Supervisor task chỉ kick watchdog khi mọi task đã báo alive trong chu kỳ |
| Watchdog | CMSDK APB watchdog (`0x40008000`) trên QEMU mps2: hết hạn lần 1 → **NMI** (handler ghi fault record vào `.noinit`, tắt actuator enable); hết hạn lần 2 (RESEN=1) → QEMU thực hiện system reset (`-action watchdog=reset`, mặc định). Đối chiếu lại source `hw/arm/mps2.c` và `hw/watchdog/cmsdk-apb-watchdog.c` ở M0 và ghi vào `sil_environment.md` |
| HardFault / MemManage | Handler tối giản: ghi trực tiếp thanh ghi để disable output model, lưu fault record (PC, LR, CFSR, MMFAR) vào vùng RAM `.noinit`, rồi reset. Sau reboot, gửi fault record trong heartbeat đầu tiên |
| Stack overflow | `configCHECK_FOR_STACK_OVERFLOW = 2` → xử lý như fault nặng |

Lưu ý thiết kế: handler fault không gửi mạng và không gọi API FreeRTOS — chỉ làm việc tối thiểu, an toàn; báo lỗi sau khi reboot.

## 6. Giao thức truyền

### 6.1 Frame (tương thích CAN FD, truyền qua UDP)
| Field | Size | Mô tả |
|---|---|---|
| `magic` | 2 B | `0xCAFD`, little-endian (wire bytes `FD CA`) |
| `can_id` | 2 B | 11-bit ID, little-endian; 5 bit cao phải bằng 0 |
| `flags` | 1 B | Bit 0 = FDF (phải bằng 1), bit 1 = BRS (M2 đặt 0); bit 2–7 phải bằng 0 |
| `dlc` | 1 B | Số byte data, không phải raw CAN DLC code; legal size: 0–8, 12, 16, 20, 24, 32, 48, 64 |
| `data` | 0–64 B | `[E2E header 12 B][app payload theo DBC][padding về DLC hợp lệ]` |

Các số nhiều byte của outer header được serialize little-endian. Một UDP datagram chứa đúng một frame, độ dài datagram phải chính xác bằng `6 + dlc`; không chấp nhận byte thừa hoặc frame bị cắt. Padding giữa E2E length và `dlc` là `0x00` và bên nhận từ chối padding khác 0. `can_id` phải thuộc message có trong DBC. Data sizes được dùng hiện tại: ESTOP 16 B, CMD 20 B, TELEM 24 B, HEARTBEAT 20 B, FAULT_RECORD 32 B.

Port mapping: ESTOP (`0x010`) dùng UDP `5001`; các message khác, bao gồm FAULT_RECORD (`0x1F1`), dùng UDP `5000`. Vì UDP không có arbitration/ACK, **ưu tiên E-stop được hiện thực ở tầng ứng dụng**: socket riêng, thread/task ưu tiên cao nhất, gửi lặp.

### 6.2 E2E protection (cấu trúc theo AUTOSAR E2E Profile 4)
Header 12 byte đặt ở đầu `data` (little-endian):

| Field | Size | Mô tả |
|---|---|---|
| `length` | 2 B | Little-endian; tổng E2E data có nghĩa = 12-byte header + app payload; gồm CRC, không gồm padding |
| `counter` | 2 B | Little-endian uint16; tăng mỗi frame, **riêng cho từng Data ID** |
| `data_id` | 4 B | Little-endian uint32; zero-extend CAN ID (vd `0x010` → `0x00000010`); chống nhầm message |
| `crc32` | 4 B | Little-endian CRC-32P4; poly normal `0xF4ACFB13` (reflected implementation `0xC8DF352F`), init/xorout `0xFFFFFFFF`, RefIn/RefOut=true |

CRC input byte sequence chính xác là `length_le[2] ‖ counter_le[2] ‖ data_id_le[4] ‖ app_payload`; trường `crc32` và padding không nằm trong CRC input. Known-answer cho cấu hình CRC là ASCII `"123456789"` → `0x1697D06A`. E2E length lần lượt là 14, 19, 23, 17 và 28 byte cho ESTOP, CMD, TELEM, HEARTBEAT và FAULT_RECORD.

Receiver xử lý theo thứ tự: kiểm tra kích thước/length/Data ID, sau đó CRC, rồi counter. Frame đầu tiên hợp lệ sau `e2e_rx_reset()` thiết lập baseline và trả `OK`; delta uint16 bằng 0 trả `REPEATED`; delta 1 hoặc 2 trả `OK`; delta lớn hơn 2 trả `WRONG_SEQ`. Phép trừ modular hỗ trợ wrap `65535 → 0`. Chỉ frame `OK` cập nhật accepted counter và thời điểm nhận hợp lệ; các status lỗi hoặc `REPEATED` không cập nhật state.

`TIMEOUT` là phép kiểm tra freshness riêng theo message, không phải kết quả kiểm tra CRC của một datagram. Thư viện nhận `now` từ caller (không tự đọc clock); deadline được so sánh bằng phép trừ unsigned wrap-safe với giới hạn timeout nhỏ hơn nửa miền clock. Trước frame hợp lệ đầu tiên, caller cung cấp thời điểm bắt đầu để áp dụng startup grace; timeout không tự reset counter. Sau timeout, frame tiếp theo vẫn phải qua sequence check; resynchronization chỉ qua reset API tường minh.

Mỗi lần phát một bản sao E-stop phải dùng counter tăng riêng; receiver chấp nhận lần đầu, các bản sau có counter liên tiếp được đánh giá bình thường. Lệnh dừng ở tầng ứng dụng phải idempotent.

Phạm vi trung thực: hiện thực theo **cấu trúc** Profile 4 và wire contract của project; kiểm tra known-answer cùng vector độc lập C/Python. Chưa xác nhận bằng bộ test vector chính thức được cấp phép của AUTOSAR → gọi là "Profile 4–based", không gọi "compliant".

### 6.3 Message table (nguồn: `common/can_matrix.dbc`)
| CAN ID | Data ID | Hướng | Chu kỳ | Nội dung | Timeout bên nhận |
|---|---|---|---|---|---|
| `0x010` | `0x0010` | QNX → FreeRTOS | Event (port 5001, gửi 3 lần) | E-stop (có E2E) | — |
| `0x100` | `0x0100` | QNX → FreeRTOS | 10 ms | Mode, target position, velocity limit | 50 ms |
| `0x180` | `0x0180` | FreeRTOS → QNX | 10 ms | Position, velocity, current (model), output state, `CmdEcho` (counter E2E của CMD hợp lệ gần nhất, M4) | 30 ms |
| `0x1F0` | `0x01F0` | FreeRTOS → QNX | 10 ms | Heartbeat: node state, alive counter, CPU load, fault code | 30 ms |
| `0x1F1` | `0x01F1` | FreeRTOS → QNX | Event (UDP 5000) | Fault record sau reboot | — |

## 7. Tiêu chí timing và cách đo

Hai node chạy trên hai đồng hồ khác nhau (VMware vs QEMU, có thể `-icount`), nên **tiêu chí pass/fail dùng đại lượng tất định** (đếm tick/chu kỳ trên *cùng một* đồng hồ); số đo ms chỉ là thông tin SIL kèm nhãn.

| Hành vi | Tiêu chí pass/fail (tất định) | Số đo SIL (tham khảo, ghi nhãn) |
|---|---|---|
| Node mất command | Output disable khi không có CMD hợp lệ trong **50 tick** 1 ms của chính node (+1 tick) | — |
| QNX mất heartbeat | DEGRADED sau **3** chu kỳ 10 ms (timer pulse) bị mất; SAFE_STOP sau **10** | thời gian host ≈ 30 / 100 ms ± 10 ms |
| E-stop | Output disable trong **≤ 1 control tick** sau khi E-stop task xử lý frame hợp lệ; frame gửi 3 lần | lệnh → disable, đo bằng trace phía node, mục tiêu < 10 ms |
| QNX `motion_sup` crash | Node tự disable sau ≤ 50 tick mất command | ≤ 60 ms host |
| Jitter control task | Đo bằng DWT cycle counter, hoặc SysTick khi không có DWT (QEMU 8.2.2; đơn vị cycle, **thời gian ảo** khi `-icount`) | p99 / max, vẽ histogram |
| Round-trip command → telemetry | Đo **chỉ trên đồng hồ QNX** (`CLOCK_MONOTONIC`): thời điểm gửi CMD counter *n* → thời điểm nhận telemetry echo counter *n* | p50/p99/max, không có mục tiêu cứng |

Mọi số liệu ms ghi rõ "SIL trên VM/QEMU, không phải hard real-time".

## 8. Safety concept
Safety goals và FMEA-lite (ánh xạ mỗi failure mode → cơ chế phát hiện → phản ứng → kịch bản test) nằm trong [safety_concept.md](safety_concept.md). Đây là tài liệu *theo tinh thần* ISO 26262, không phải work product chính thức.

## 9. Bộ công cụ Python
| Công cụ | Chức năng |
|---|---|
| `dbc_codegen.py` | Sinh header C pack/unpack từ `can_matrix.dbc` |
| `frame_sniffer.py` | Bắt và giải mã frame UDP (DBC + E2E) |
| `udp_proxy.py` | Proxy giữa QNX và QEMU: drop/delay/duplicate/corrupt/reorder cho fault injection |
| `sil_runner.py` | Khởi động QEMU, kết nối QNX VM, chạy scenario |
| `fault_inject/` | pytest scenario: drop/delay/duplicate/corrupt frame, kill process, stuck encoder, MPU violation |
| `metrics.py` | Tính jitter, latency p50/p99/max, reaction time; xuất báo cáo |
| `plot_step.py` | Vẽ step response và histogram latency |
