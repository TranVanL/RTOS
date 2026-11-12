# M4 — Tích hợp end-to-end (QNX ↔ FreeRTOS)

> Trạng thái: **M4 hoàn tất (2026-11-12, SIL).** Bằng chứng trong `docs/results/m4_*`. Dự án chốt phạm vi ở M4 (mức tối thiểu của plan); M5/M6 là hướng mở rộng, xem §3.

**Acceptance (từ plan):** chạy 30 phút ở RUN không lỗi E2E; thỏa các tiêu chí tất định trong [01_ARCHITECTURE.md](01_ARCHITECTURE.md) §7; số đo SIL ghi kèm nhãn.

## 0. Nguyên tắc
1. Pass/fail đếm **tick/frame** (tick điều khiển của node, tick 10 ms của supervisor); ms chỉ là số đo SIL "QNX VM + QEMU", không phải hard real-time. Round trip chỉ đo trên đồng hồ QNX.
2. Logic mới của node viết OS-free trong `common/safety/node_fsm.[ch]` và test trên host; phần FreeRTOS chỉ là lớp mỏng.
3. Node test được **không cần QNX**: `tools/sil_supervisor.py` (Python, đúng wire format) + QEMU chạy được trên CI. Bằng chứng QNX thật nằm trong `docs/results/m4_*` (chạy local bằng `tools/qnxvm/run_m4.sh`).
4. Không sửa hợp đồng đã chốt ngoài mức cần thiết; mọi thay đổi ghi ở §2.

## 1. Work packages
| WP | Nội dung | Chạy ở | Trạng thái |
|---|---|---|---|
| WP1 | `node_fsm` OS-free (INIT/READY/RUN/DEGRADED/SAFE_STOP, timeout 50 tick, E-stop, hồi phục) + test host | host | Xong — `tests/safety/test_node_fsm.c` (CTest `m4_node_fsm`) |
| WP2 | Node: task `comm` (CMD vào, TELEM + HB ra mỗi 10 tick), task `estop` (UDP 5001, ưu tiên cao nhất), nối vào control task Restricted qua mailbox seqlock | QEMU | Xong — build preset `node_m4_host` / `node_m4_qnx` |
| WP3 | `tools/sil_supervisor.py` + pytest `tools/tests/test_m4_node_qemu.py` (6 kịch bản) | host + QEMU | Xong |
| WP4 | `motion_sup`: gating theo trạng thái node, đo round trip (`--rtt-log`), tham số test; `tools/qnxvm/run_m4.sh` | QNX VM + QEMU | Xong — kịch bản A/B/C |
| WP5 | `metrics.py rtt|reaction` + test | host | Xong |
| WP6 | Chạy 30 phút RUN, ghi `docs/results/m4_long_30min.txt` | QNX VM + QEMU | Xong — pass (§4) |
| WP7 | Tài liệu: ARCH, traceability, README | — | Xong |

## 2. Quyết định / thay đổi hợp đồng
| Mục | Quyết định | Lý do |
|---|---|---|
| DBC `TELEM` | Thêm `CmdEcho` (uint16, bit 72) → TELEM 11 B, E2E 23 B, DLC trên dây vẫn 24 B | Cần để đo round trip "gửi CMD counter *n* → nhận telemetry echo *n*" (ARCH §7). `CmdEcho` = counter E2E của CMD hợp lệ gần nhất mà node đã nhận. Đã regenerate code + sửa test M2 |
| Ưu tiên task node | `configMAX_PRIORITIES` 10 → 12: E-stop = 11 (cùng timer task), control = 10, IP task = 9, Ethernet RX = 8, comm = 5 | ARCH §5.1 yêu cầu E-stop cao hơn control; trước đó control chiếm mức cao nhất |
| Comm task | Chạy **privileged** (socket/queue nằm trong kernel heap); chỉ control task là Restricted. Trao đổi qua mailbox/snapshot trong `g_shared` (128 B, 1 vùng MPU) | Đúng fallback đã ghi ở "Known risks" (không mở thêm vùng MPU: port CM3 chỉ có 3 vùng cấu hình) |
| Mailbox | Control task **không spin** khi writer đang ghi dở (control cao hơn writer → sẽ deadlock): seq lẻ/đổi thì dùng CMD cũ tick này, đếm `ulMailboxBusy` | Tránh priority inversion trong chính mailbox |
| Timeout node | Đếm theo tick điều khiển của node: ≥ 25 tick không CMD → DEGRADED (hạn 20 % tốc độ), ≥ 50 tick → SAFE_STOP (output disable). Chỉ "lên cò" khi đang RUN/DEGRADED, READY không timeout | ARCH §7: pass = đúng 50 tick của node |
| Hồi phục node | SAFE_STOP → READY sau 5 CMD hợp lệ liên tiếp mode DISABLE; CMD mode ≠ DISABLE reset bộ đếm | QNX gửi CMD DISABLE liên tục sau `RESET_FAULT` |
| E-stop | Frame qua length/ID/CRC nhưng counter cũ/lặp (`REPEATED`/`WRONG_SEQ`) **vẫn được thực thi** và re-baseline counter | E-stop idempotent và là hướng an toàn; tránh node "bỏ qua E-stop" sau khi QNX khởi động lại (counter về 0). CRC/length/ID sai vẫn bị loại |
| Resync CMD | ≥ 5 CMD `WRONG_SEQ` liên tiếp **và** output đang disable → reset baseline E2E của CMD; đang RUN/DEGRADED thì không resync (node tự timeout trước) | `motion_sup` khởi động lại không bị node khóa vĩnh viễn |
| QNX ↔ node | `motion_sup` coi node ở SAFE_STOP khi QNX đang RUN/DEGRADED là node fault → SAFE_STOP (nguyên nhân NODE_FAULT); `--autoenable` chỉ bật khi node báo READY | Tránh QNX "tưởng RUN" trong khi node đã tự dừng; không khóa `RESET_FAULT` |
| Heartbeat | `CpuLoad = 0` (chưa đo), `FaultCode = 0` (dành cho fault thật, M5) | Không báo số chưa kiểm chứng |

## 3. Giới hạn đã biết (ngoài phạm vi đã chốt; hướng mở rộng M5/M6)
- **Watchdog / fault record / reboot** chưa có trong firmware tích hợp (ARCH §5.3): `Supervisor task` kick watchdog, HardFault handler và `0x1F1` thuộc M5.
- Sau khi **node reboot**, counter E2E phía node về 0 → `motion_sup` hiện sẽ báo `WRONG_SEQ` mãi (chưa có resync phía QNX). Cần xử lý cùng F10 (M5).
- `hb_miss` (số tick không có heartbeat) khác 0 là bình thường: QEMU chạy theo giờ host nên chu kỳ 10 ms của node và 10 ms của QNX trượt nhau; FSM chỉ phản ứng khi mất **liên tiếp** 3 tick. Pass/fail dùng "FSM không rời RUN", không dùng `hb_miss == 0`.
- `ulMissedDeadlines` của control task > 0 trong chạy tích hợp (QEMU không `-icount`, host tải): thông tin, không phải tiêu chí; jitter nghiệm thu vẫn là phép đo `-icount` của M1.
- Node chưa có clock thực → số ms trong log UART không có nghĩa; chỉ dùng tick.

## 4. Kết quả (SIL: QNX 8.0 VM + QEMU node, không phải hard real-time)
Chạy bằng `tools/qnxvm/run_m4.sh` (motion_sup thật trên QNX VM ↔ firmware node thật trong QEMU, nối L2 bằng `-netdev socket`).

### 4.1 Nghiệm thu 30 phút ở RUN — [m4_long_30min.txt](results/m4_long_30min.txt)
| Tiêu chí (đếm, tất định) | Kết quả |
|---|---|
| 180 000 tick supervisor 10 ms (30 phút) | đạt (`ticks=180000`) |
| Lỗi E2E phía QNX | **0** (`e2e_err=0`, 359 601 frame nhận) |
| FSM QNX rời RUN | không (`final_state=RUN`, không có `FSM DEGRADED/SAFE_STOP`) |
| CMD bị node từ chối | 0 (`cmd_bad=0`; `cmd_ok=179 979` ở báo cáo cuối) |
| E-stop phát | 0 |
| Kết thúc | `motion_sup` thoát hết giờ → node tự SAFE_STOP đúng **50 tick** sau CMD cuối (nguyên nhân CMD_TIMEOUT) |

Thông tin (không phải tiêu chí): 789 tick đơn lẻ không có heartbeat (`hb_miss`, FSM không phản ứng vì chỉ tính khi mất liên tiếp); `missed=711` deadline control trên ~1,8 triệu tick của node (QEMU không `-icount`, theo giờ host).

### 4.2 Round trip CMD → TELEM echo — [m4_rtt_L_summary.txt](results/m4_rtt_L_summary.txt)
Đo trên **một đồng hồ** (QNX `CLOCK_MONOTONIC`), 179 301 mẫu, thông tin SIL: p50 ≈ 5,5 ms, p99 ≈ 10,3 ms, max ≈ 28 ms. Không có mục tiêu cứng; p50 gần nửa chu kỳ 10 ms là do hai bên chạy chu kỳ 10 ms độc lập (CMD chờ lượt gửi TELEM kế tiếp).

### 4.3 Kịch bản lỗi trên hệ thật — [m4_scenarios.txt](results/m4_scenarios.txt)
| Kịch bản | Tiêu chí (tất định) | Kết quả |
|---|---|---|
| A smoke, 3000 tick, target vuông | RUN, 0 lỗi E2E, FSM không rời RUN, ≥ 100 mẫu round trip | PASS (2982 mẫu) |
| **F6** E-stop | QNX → SAFE_STOP (nguyên nhân ESTOP), 3 bản E-stop; node disable ≤ 1 control tick sau khi E-stop task nhận | PASS (đúng 1 tick) |
| **F12** `kill -9 motion_sup` khi RUN | Node tự disable đúng 50 tick (của node) sau CMD cuối, qua DEGRADED ở tick 25 | PASS (50) |

Cùng các hành vi trên chạy trên CI-able rig (Python supervisor + QEMU) bằng `tools/tests/test_m4_node_qemu.py`: boot → READY, enable → bám target, E-stop ≤ 1 tick (**F6**), mất CMD = 50 tick (**F1**), hồi phục sau 5 CMD DISABLE, CMD sai CRC bị loại và đếm (**F3**, một frame). Cùng với **F13/F14** từ M3 → ≥ 4 kịch bản F có bằng chứng; bộ F1–F14 đầy đủ và tự động hóa là M5.
