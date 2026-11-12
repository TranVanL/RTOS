# M3 — QNX Supervisor: resmgr, process, IPC (kế hoạch chi tiết)

> Trạng thái: **đang triển khai (bắt đầu 2026-11-12).** Xong: WP1 (safety FSM), WP2 (SHM seqlock), `common/ipc/motion_ipc` (validate `devctl`), `common/proto/proto_link` (glue frame+E2E+DBC) — đều có CTest và đã chạy lại bằng `qcc` trên QNX VM ([log](results/m3_host_tests_on_qnx.txt)). **WP3 xong (không gồm resmgr, thuộc WP4):** `qnx/motion_sup/motion_sup.c` với 5 thread SCHED_FIFO 60/55/50/45/45, timer pulse 5 ms/10 ms, mutex PI, nối FSM + SHM + codec M2; peer giả `qnx/tests/m3_peer.c`; 3 kịch bản A/B/C pass (16 kiểm tra đếm/trạng thái), log [results/m3_wp3_scenarios.txt](results/m3_wp3_scenarios.txt), chạy bằng `tools/qnxvm/run_m3_wp3.sh`. Group `motion` gid cố định **3000** (`MOTION_GROUP_GID`, tạo idempotent trong script; chưa lưu vào image VM). Giới hạn vị trí/tốc độ trong `motion_ipc.h` là **tạm thời** (±1 000 000 count, 3000 rpm). Tiếp theo: WP4 (resmgr `/dev/motion` + `motionctl`).

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
- [ ] Safety FSM host test pass, phủ mọi transition; chạy trong CI.
- [ ] SHM seqlock test đa luồng host pass.
- [ ] `devctl`: `EPERM` và `EINVAL` đúng, có log từ VM (REQ-SEC-001/002).
- [ ] `mgmt` ghi SHM chỉ đọc → `SIGSEGV`, hệ thống vẫn chạy; crash `logger`/`mgmt` được phát hiện (REQ-SEC-003/004).
- [ ] Bằng chứng priority inheritance có/không PI (REQ-PERF-003), có nhãn "SIL, VM".
- [ ] Traceability cập nhật; README/CLAUDE.md chỉ ghi M3 complete khi có evidence trong `docs/results/`.

**Tiến độ:** WP0–WP3 xong. Tiếp theo: WP4 (resmgr).
