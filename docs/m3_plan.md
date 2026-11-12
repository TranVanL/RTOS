# M3 — QNX Supervisor: resmgr, process, IPC (kế hoạch chi tiết)

> Trạng thái: **đang triển khai (bắt đầu 2026-11-12).** Xong: WP1 (safety FSM), WP2 (SHM seqlock), `common/ipc/motion_ipc` (validate `devctl`), `common/proto/proto_link` (glue frame+E2E+DBC) — đều có CTest và đã chạy lại bằng `qcc` trên QNX VM ([log](results/m3_host_tests_on_qnx.txt)). **WP3 xong:** `qnx/motion_sup/motion_sup.c` với 6 thread (thêm resmgr SCHED_RR 20 ở WP4) SCHED_FIFO 60/55/50/45/45, timer pulse 5 ms/10 ms, mutex PI, nối FSM + SHM + codec M2; peer giả `qnx/tests/m3_peer.c`; 3 kịch bản A/B/C pass (16 kiểm tra đếm/trạng thái), log [results/m3_wp3_scenarios.txt](results/m3_wp3_scenarios.txt), chạy bằng `tools/qnxvm/run_m3_wp3.sh`. Group `motion` gid cố định **3000** (`MOTION_GROUP_GID`, tạo idempotent trong script; chưa lưu vào image VM). Giới hạn vị trí/tốc độ trong `motion_ipc.h` là **tạm thời** (±1 000 000 count, 3000 rpm). **WP4 xong:** resmgr `/dev/motion` trong `motion_sup` (quyền theo `ConnectClientInfo()`, gid 3000; xác thực bằng `common/ipc/motion_ipc`) + client `qnx/motionctl/motionctl.c`; 18 kiểm tra pass trên VM — thiếu group → `EPERM`, lệnh/kích thước/biên sai → `EINVAL`, `enable` ngoài READY và `reset` ngoài SAFE_STOP → `EBUSY`, `estop`/`status` mở cho mọi client; chuỗi READY→RUN→SAFE_STOP→READY đúng. Log [results/m3_wp4_devctl.txt](results/m3_wp4_devctl.txt), chạy bằng `tools/qnxvm/run_m3_wp4.sh`. **WP5 xong:** `qnx/logger/logger.c` (SHM `O_RDONLY`, log xoay vòng), `qnx/mgmt/mgmt.c` (đọc SHM, chuyển lệnh qua `/dev/motion`, `poke` = lỗi cố ý F14), death watcher trong `motion_sup` (`PROCMGR_EVENT_PROCESS_DEATH` + `waitpid`). 12 kiểm tra pass trên VM: `mgmt` ghi vào SHM chỉ đọc → SIGSEGV (11) chỉ giết `mgmt`; logger crash cũng được phát hiện (signal 11); `motion_sup` chạy đủ 600 tick, vẫn RUN, không E-stop; logger khỏe tiếp tục ghi sau khi `mgmt` chết (tick 152 → 547) và xoay vòng file; user khác uid mở SHM `O_RDWR` → từ chối (`EACCES`). Log [results/m3_wp5_shm_crash.txt](results/m3_wp5_shm_crash.txt), chạy bằng `tools/qnxvm/run_m3_wp5.sh`. **WP6 xong:** `qnx/tests/m3_prio_inv.c` (L=10 giữ mutex, H=50 chờ, M=30 CPU-bound; mọi thread ghim CPU 0). Kết quả đếm/thứ tự, 5/5 lần mỗi chế độ: **có PI** H lấy được mutex khi M mới chạy 0 vòng, thứ tự `L_unlock,H_acquire,M_done`; **không PI** H phải chờ M chạy hết 20 000 000 vòng, thứ tự `M_done,L_unlock,H_acquire` (đảo ưu tiên). Trace `tracelogger` + `traceprinter` (`tools/qnxvm/trace_timeline.py`): trong khoảng H bị chặn, có PI → L (được nâng) chiếm 99.8% CPU, M 0.1%; không PI → M chiếm 89.2%, L 10.5%. Log [results/m3_wp6_prio_inheritance.txt](results/m3_wp6_prio_inheritance.txt), [trace PI](results/m3_wp6_trace_pi.txt), [trace không PI](results/m3_wp6_trace_nopi.txt), chạy bằng `tools/qnxvm/run_m3_wp6.sh`. Số cycle là dữ liệu SIL (VM), tiêu chí là thứ tự và tỉ lệ. Còn lại: WP7 (chốt M3) — cần CI xanh trên remote trước khi đánh dấu M3 complete.

**Acceptance (từ plan):** `devctl` sai quyền → `EPERM`, sai tham số → `EINVAL`; `mgmt` ghi SHM chỉ đọc → `SIGSEGV` mà hệ thống vẫn chạy; trace System Profiler chứng minh priority inheritance.

## 0. Nguyên tắc
1. **Host trước, QNX sau.** QNX không chạy được trên CI công khai, nên logic quan trọng (safety FSM, seqlock, validate `devctl`) viết bằng C thuần trong `common/` (OS-free) và test trên host bằng CTest như M1/M2. Phần QNX chỉ là lớp mỏng: thread, timer, resmgr, SHM, socket.
2. **M3 không nối FreeRTOS.** Gửi/nhận UDP thật với node là M4. M3 dùng cùng codec M2 nhưng supervisor có thể chạy với peer giả (`tools/` Python hoặc `qnx/tests/m0_peer.c`).
3. Tiêu chí tất định (đếm tick/pulse/lần gọi), số ms chỉ là dữ liệu SIL "VM", ghi nhãn rõ. Không claim hard real-time.
4. Kết quả QNX lưu local vào `docs/results/m3_*`.

## 1. Quyết định cần chốt trước khi code (WP0)
| Vấn đề | Đề xuất |
|---|---|
| Dựng FSM ở đâu | `common/safety/safety_fsm.[ch]` OS-free: `fsm_step(state, inputs, now_tick)`, không đọc clock, không I/O. Dùng lại ở M4 phía node nếu hợp lý |
| Layout SHM | `common/shm/` header chung: `struct` cố định, `uint32_t seq`, trường tường minh (không bitfield), version + magic; `state` và `trace` là hai object `shm_open` riêng |
| Seqlock | Writer: `seq++` (lẻ) → ghi → `seq++` (chẵn), có barrier; reader retry khi `seq` lẻ hoặc đổi. Viết OS-free với `stdatomic.h` để test đa luồng trên host |
| Định danh `devctl` | `DCMD_MOTION_*` trong `common/` header dùng cả `motionctl`; struct payload có kích thước cố định, kiểm `nbytes` trước khi đọc |
| Quyền "group `motion`" | gid cố định tạo trong VM; ghi lại trong `sil_environment.md`. Test với 3 user: root-less thường, thuộc group, không thuộc group |
| Giới hạn biên | Vị trí/tốc độ lấy từ `common/control/limits` (cùng nguồn với node) |

## 2. Work packages
| WP | Nội dung | Ước lượng | Chạy ở | Done khi |
|---|---|---|---|---|
| WP0 | Chốt bảng §1, cập nhật ARCH §3, tạo thư mục/CMake | 3 h | host | Quyết định ghi vào ARCH; CMake build host |
| WP1 | Safety FSM OS-free + unit test | 6 h | host | Phủ mọi transition ARCH §4 (3/10 heartbeat, ≥3 lỗi E2E/100 ms, 10 HB hợp lệ hồi phục, E-stop từ mọi trạng thái, `RESET_FAULT`), đếm theo tick, `-Werror` sạch |
| WP2 | SHM layout + seqlock + test đa luồng host | 5 h | host | Test 1 writer/N reader không đọc "rách" (checksum trường), `seq` đúng; tích hợp CTest/pytest |
| WP3 | `motion_sup`: thread/priority/policy, timer pulse, Frame RX/TX bằng codec M2, nối FSM, ghi SHM | 10 h | QNX VM | Bảng thread ARCH §3.2 khớp `pidin`; 100 pulse 10 ms đếm đủ; frame vào từ peer giả cập nhật `state` |
| WP4 | Resmgr `/dev/motion` + `motionctl` | 8 h | QNX VM | `devctl` quyền đúng chạy; sai quyền `EPERM`; payload/biên sai `EINVAL`; log lưu `docs/results/m3_devctl.txt` |
| WP5 | `logger` + `mgmt` (SHM `O_RDONLY`), death pulse khi crash | 6 h | QNX VM | `mgmt` ghi SHM → `SIGSEGV` chỉ process `mgmt`; `motion_sup` vẫn chạy; `motion_sup` phát hiện crash qua `procmgr_event` |
| WP6 | Test priority inversion có/không PI + System Profiler | 6 h | QNX VM | Kịch bản 3 thread (cao/trung/thấp + mutex) chạy với PI và không PI (`PTHREAD_PRIO_NONE`); trace đính kèm cho thấy khác biệt; đếm tick bị chặn |
| WP7 | Tài liệu, traceability, chốt M3 | 3 h | cả hai | DoD §4 xanh |

## 3. Rủi ro
| Rủi ro | Xử lý |
|---|---|
| Không dùng được `SCHED_FIFO` priority cao trong VM (quyền/`procmgr_ability`) | Chạy `motion_sup` bằng quyền đủ; ghi lại cấu hình; fallback hạ priority nhưng giữ thứ tự tương đối |
| Seqlock đúng trên host nhưng sai thứ tự bộ nhớ trên QNX/ARM | Dùng `stdatomic` acquire/release; cùng mã test trên x86 VM; ghi giới hạn |
| Khó chứng minh PI bằng số liệu | Thiết kế kịch bản tất định (đếm thứ tự chạy), trace chỉ là bằng chứng phụ; có đối chứng không-PI |
| System Profiler/`tracelogger` không hoạt động trong VM | Fallback: log thứ tự chạy có timestamp `ClockCycles`, ghi rõ giới hạn |
| Scope nở sang M4 (nối node thật) | Chỉ peer giả ở M3 |

## 4. Definition of Done — M3
- [x] Safety FSM host test pass, phủ mọi transition; chạy trong CI.
- [x] SHM seqlock test đa luồng host pass.
- [x] `devctl`: `EPERM` và `EINVAL` đúng, có log từ VM (REQ-SEC-001/002).
- [x] `mgmt` ghi SHM chỉ đọc → `SIGSEGV`, hệ thống vẫn chạy; crash `logger`/`mgmt` được phát hiện (REQ-SEC-003/004).
- [x] Bằng chứng priority inheritance có/không PI (REQ-PERF-003), có nhãn "SIL, VM".
- [ ] Traceability cập nhật; README/CLAUDE.md chỉ ghi M3 complete khi có evidence trong `docs/results/`.

**Tiến độ:** WP0–WP6 xong. Còn WP7 (chốt, chờ CI).
