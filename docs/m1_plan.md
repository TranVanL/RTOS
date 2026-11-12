# M1 — FreeRTOS Motion Node: control loop (kế hoạch chi tiết)

Nguồn: `docs/02_PROJECT_PLAN.md` (M1), `docs/01_ARCHITECTURE.md` §5.1–5.2, §7; yêu cầu `REQ-FUN-001`, `REQ-FUN-002`, `REQ-PERF-002`. Ước lượng 3–4 tuần × ~10 h. **Acceptance M1 (từ plan):** step response overshoot < 10 % và settling time theo thông số chọn; histogram jitter xuất được; unit test PID/plant pass trên host.

## 0. Nguyên tắc xuyên suốt
1. **Viết lõi điều khiển bằng C thuần, không phụ thuộc OS** (`common/control/`: `plant`, `encoder`, `pid`, `limits`). Cùng một mã chạy (a) trên host để unit test và mô phỏng vòng kín, (b) trong task FreeRTOS. Lý do: tuning và test trên host nhanh hơn QEMU hàng chục lần, và kết quả host so được với kết quả trên node.
2. **Tiêu chí tất định**: overshoot, settling, sai số xác lập tính theo *mẫu* (tick 1 ms của mô hình), không theo giây thật. Jitter đo theo *cycle ảo* khi `-icount` (xem Phần 6).
3. **Thứ tự: host trước, node sau.** Phần 1–4 không cần QEMU; Phần 5–7 đưa lên node.
4. Mỗi phần có: kiến thức cần nắm → cách làm → kết quả cần đạt (kiểm được) → rủi ro.

| Phần | Nội dung | Ước lượng | Chạy ở đâu |
|---|---|---|---|
| 1 | Khung dự án + test host | 3 h | host |
| 2 | Plant: motor DC | 6 h | host |
| 3 | Encoder model | 4 h | host |
| 4 | PID cascade + giới hạn | 8 h | host |
| 5 | Mô phỏng vòng kín, tuning, đồ thị | 6 h | host |
| 6 | Control task trên FreeRTOS (MPU) | 8 h | QEMU |
| 7 | Đo jitter | 5 h | QEMU |
| 8 | Đối chiếu host ↔ node, tài liệu, chốt M1 | 4 h | cả hai |

---

## Phần 1 — Khung dự án và test trên host
**Kiến thức.** Tách code theo "OS-free core": hàm thuần nhận trạng thái + tham số, trả trạng thái mới, không `malloc`, không gọi FreeRTOS, không `printf`. Dễ test, dễ kiểm chứng tính tất định. Dùng `float` hay số nguyên? Cortex-M3 **không có FPU** → `float` chạy bằng thư viện mềm (vài chục–vài trăm cycle/phép tính), vẫn đủ cho 1 kHz ở 25 MHz (25 000 cycle/chu kỳ) nhưng phải *đo* chứ không đoán. Chọn `float` (đơn giản, đủ tuning), ghi lại chi phí đo được ở Phần 7; nếu vượt 30 % ngân sách chu kỳ thì xét Q15/Q31.
**Cách làm.**
- Thư mục `common/control/` + `tests/control/` (host). Build bằng CMake riêng (`gcc`, `-Wall -Wextra -Werror`), chạy qua `ctest`; thêm một test pytest gọi `ctest` để dùng chung với `py -m pytest tools -q`.
- Quy ước API: `typedef struct {...} pid_t; void pid_init(...); float pid_step(pid_t*, float ref, float meas)`.
- Đơn vị: SI (rad, rad/s, V, A) trong lõi; encoder đổi sang *count*.
**Kết quả cần đạt.** `ctest` chạy được với một test mẫu; `-Werror` sạch; thư mục lõi không `#include` header FreeRTOS (kiểm bằng `grep` trong test).
**Rủi ro.** `float` mềm làm tick 1 ms trên node lâu hơn dự kiến → Phần 7 đo, Phần 1 chỉ ghi quyết định.

## Phần 2 — Plant: motor DC bậc 2
**Kiến thức.** Motor DC nam châm vĩnh cửu: điện `L·di/dt = V − R·i − Ke·ω`, cơ `J·dω/dt = Kt·i − b·ω − τ_tải`, vị trí `dθ/dt = ω`. "Bậc 2" = hai trạng thái động (`i`, `ω`); thêm `θ` thành bậc 3 nhưng chỉ là tích phân. Tính chất kiểm được: `ω` xác lập khi đặt `V` không đổi = `Kt·V / (R·b + Kt·Ke)`; hằng số thời gian điện `L/R`, cơ `≈ J·R/(Kt·Ke)`.
**Rời rạc hóa.** Bước 1 ms. Nếu `L/R` nhỏ hơn hoặc gần 1 ms thì Euler tiến **mất ổn định** (bài toán cứng). Lựa chọn: (a) chọn tham số có `L/R ≥ 5 ms`, hoặc (b) chia mỗi tick thành N bước con (vd 10 × 100 µs), hoặc (c) RK2. Plan chọn (a) + (b) khi cần; ghi lý do trong `plant.h`.
**Cách làm.** `plant_params_t` (R, L, Ke, Kt, J, b, giới hạn điện áp ±Vmax, tải), `plant_state_t`, `plant_step(state, params, V, dt)`. Tham số mặc định chọn sao cho thời gian lên vài chục ms (để bước 1 ms đủ mịn).
**Kết quả cần đạt (unit test host).** (1) Bước điện áp → `ω` xác lập đúng công thức, sai số < 1 %. (2) Hằng số thời gian đo được lệch < 5 % so với lý thuyết. (3) Không phát nổ với `V = Vmax` trong 10 s. (4) Tất định: cùng đầu vào → cùng đầu ra bit-for-bit giữa hai lần chạy.
**Rủi ro.** Chọn tham số "đẹp" nhưng phi thực tế; chấp nhận được vì là SIL, ghi rõ "mô hình minh họa, chưa nhận dạng từ motor thật".

## Phần 3 — Encoder model
**Kiến thức.** Encoder tăng dần: vị trí đo = `floor(θ · N / 2π)` count (N = 4096 count/vòng theo ARCH §5.2) → **lượng tử hóa** (sai số ±½ count). Nhiễu Gaussian cộng trước khi lượng tử. Ước lượng tốc độ từ count: vi phân `Δcount/dt` rất nhiễu ở 1 kHz (1 count = 1,5 mrad → nhiễu tốc độ ≈ 1,5 rad/s), nên cần lọc thông thấp bậc 1 hoặc dùng cửa sổ trượt. Cờ **stuck**: count đứng yên dù `θ` đổi (phục vụ F8/`REQ-SAF-009`, M5).
**Nhiễu tất định.** Dùng PRNG tự viết (xorshift32 + Box-Muller hoặc tổng 12 số đều) với **seed cấu hình**, không dùng `rand()`: chạy lại ra cùng dãy, test được.
**Cách làm.** `encoder_params_t` (count/vòng, σ nhiễu, seed, stuck), `encoder_step` trả `int32_t count`; `speed_estimator` (lọc bậc 1, hằng số α cấu hình).
**Kết quả cần đạt.** (1) σ nhiễu=0 → count khớp `floor` chính xác. (2) Thống kê nhiễu: trung bình ≈ 0, độ lệch chuẩn đúng ±10 % trên 10⁵ mẫu. (3) Cùng seed → cùng dãy. (4) Stuck=1 → count không đổi và có API báo "stuck" để M5 dùng.

## Phần 4 — Bộ điều khiển PID cascade và các giới hạn
**Kiến thức.**
- **Cascade vị trí → tốc độ**: vòng ngoài (vị trí) cho ra tốc độ đặt; vòng trong (tốc độ) cho ra điện áp. Vòng trong phải nhanh hơn vòng ngoài khoảng 5–10 lần để coi như "tức thời" khi tuning vòng ngoài. Thường vòng ngoài P, vòng trong PI.
- **Anti-windup**: khi đầu ra bão hòa, tích phân tiếp tục tích lũy → overshoot lớn. Dùng *back-calculation* hoặc *clamping có điều kiện* (không tích phân khi bão hòa và sai số cùng dấu).
- **Dẫn xuất (D)**: lấy trên *đo lường* (không trên sai số) để tránh "derivative kick" khi đổi tham chiếu, kèm bộ lọc.
- **Saturation** (±Vmax), **rate limit** (giới hạn dV/dt), **giới hạn mềm vị trí** (kẹp tham chiếu vị trí trong [min,max]) — theo REQ-FUN-001/ARCH §5.2.
- Tham chiếu bậc thang gây đầu ra nhảy + overshoot: dùng *giới hạn tốc độ tham chiếu* hoặc *prefilter* để đạt overshoot < 10 % mà không làm chậm vòng trong.
**Cách làm.** `pid_t` (Kp, Ki, Kd, giới hạn, hệ số anti-windup, dt cố định), `cascade_step(ref_pos, pos, vel) → V`. Tách `limits.c` (clamp/rate) để test độc lập.
**Kết quả cần đạt (unit test).** (1) P thuần: đầu ra = Kp·e. (2) Tích phân xác lập: với e không đổi, đầu ra tăng tuyến tính; khi bão hòa, tích phân **không** vượt ngưỡng anti-windup (test cụ thể). (3) Rate limit: |ΔV| ≤ giới hạn mỗi tick. (4) Tham chiếu ngoài [min,max] bị kẹp. (5) Hàm thuần, không trạng thái toàn cục (hai instance độc lập).
**Rủi ro.** Tuning thủ công mất thời gian → có Phần 5 với công cụ vẽ.

## Phần 5 — Mô phỏng vòng kín, tuning và đồ thị (host)
**Kiến thức.** Các chỉ số đáp ứng bậc thang: **overshoot** = (đỉnh − giá trị cuối)/bước·100 %; **settling time** = thời điểm cuối cùng mà đáp ứng ra khỏi dải ±2 % quanh giá trị đặt; **sai số xác lập** = |ref − y| khi t→∞. Phương pháp tuning: tuning vòng trong trước (tốc độ) → kiểm tra → khóa → tuning vòng ngoài.
**Cách làm.** Chương trình C host `sim_closed_loop` chạy plant + encoder + cascade, xuất CSV `t, ref, pos, vel, v, count`. `tools/plot_step.py` đọc CSV, tính 3 chỉ số, vẽ đồ thị (matplotlib), xuất PNG vào `docs/results/`.
**Chọn thông số và ghi lại.** Bước 90° (hoặc 1 rad); mục tiêu settling ≤ 200 ms (tạm đề xuất, *chốt cuối Phần 5 và ghi vào requirements*); overshoot < 10 %; sai số xác lập < 1 count.
**Kết quả cần đạt.** Với tham số mặc định đã lưu trong repo: overshoot < 10 %, settling ≤ giá trị đã chốt, sai số xác lập ≤ 1 count — kiểm bằng **test tự động** (không chỉ nhìn đồ thị); thêm 2 kịch bản: tải ngoài bậc thang (độ cứng), và nhiễu encoder bật (overshoot vẫn < 10 %).
**Rủi ro.** Nhiễu encoder làm vòng trong rung → tăng α bộ lọc tốc độ hoặc giảm Kd.

## Phần 6 — Control task trên FreeRTOS (MPU Restricted)
**Kiến thức.**
- `vTaskDelayUntil` cho chu kỳ cố định (không trôi); tick 1 kHz đã cấu hình (`configTICK_RATE_HZ 1000`). Lưu ý: task đo/in log **không** được chặn control task.
- **`xTaskCreateRestricted`** với `MemoryRegion_t`: control task chạy không đặc quyền, chỉ truy cập (a) stack riêng, (b) vùng dữ liệu control, (c) vùng shared command/state. Vùng MPU Cortex-M3 phải **kích thước lũy thừa 2 và căn theo kích thước** (đã gặp ở M0). Truy cập ngoài → MemManage (đã chứng minh ở M0 §4.3).
- Chia sẻ trạng thái: struct `shared_state_t` đơn writer (control task ghi); người đọc dùng **seqlock** hoặc double buffer (ARCH §5.1). M1 chưa có Comm task nên chỉ cần cơ chế + test đọc từ task khác.
- Ghi log: control task đẩy mẫu vào ring buffer; một task ưu tiên thấp in CSV qua UART. Không `printf` trong control task.
**Cách làm.** `freertos/app/control_task.c`: mỗi tick đọc "encoder model" (trên node, plant chạy luôn trong task: *plant → encoder → cascade → V → plant*), cập nhật `shared_state`. Profile tham chiếu bậc thang cố định bằng cờ build `CONTROL_TEST_STEP` (vì Comm + lệnh từ QNX là M4). Priority theo ARCH §5.1 (Control cao hơn Comm/IP; **IP/RX task của M0 vẫn đặc quyền**). Giữ `udp_echo`/`udp_probe` chạy để chứng minh control không bị mạng làm hỏng.
**Kết quả cần đạt.** (1) Task Restricted chạy ≥ 60 s không MemManage; (2) cố ý ghi ra ngoài vùng → MemManage được bắt (tái dùng `vHandleMemoryFault`); (3) CSV từ UART có đúng 1 mẫu/tick (đếm tick liên tục, không nhảy), **0 lần trễ chu kỳ** (đếm số lần `vTaskDelayUntil` báo trễ = 0) khi `udp_echo` chạy 100 Hz.
**Rủi ro.** Đặt vùng MPU sai kích thước → HardFault; xử lý bằng tính toán căn lề tường minh + test vi phạm. Stack task Restricted thiếu → tăng và dùng `uxTaskGetStackHighWaterMark`.

## Phần 7 — Đo jitter control task
**Kiến thức.** **Jitter** = độ lệch của thời điểm bắt đầu chu kỳ so với lý tưởng. **DWT `CYCCNT` không có trong QEMU 8.2.2** (đã xác minh ở `sil_environment.md` §4) → dùng **SysTick** (`SysTick->VAL`, đếm xuống 25 000 → 1 count = 1 cycle ảo khi `-icount shift=0`): đọc `VAL` ở đầu chu kỳ, tính `(tick_now*RELOAD + (RELOAD−VAL))` rồi lấy hiệu hai chu kỳ liên tiếp trừ 25 000. Với `-icount` thì thời gian là *thời gian ảo* — kết quả ổn định và tái lập (đã đo ở M0: stddev ≈ 0,3 % so với tới 25 % khi không `-icount`). Đơn vị là **cycle ảo**, không phải ms thật; không tuyên bố hard real-time.
`REQ-PERF-002` và ARCH §7 đã được sửa thành "DWT hoặc SysTick fallback".
**Cách làm.** Ring buffer 10 000 mẫu `Δcycles` trong vùng dữ liệu control; sau khi đủ mẫu, task thấp in dải; `tools/metrics.py` (phần jitter) tính min/mean/p99/max và vẽ histogram PNG. Chạy ba điều kiện: (A) không tải mạng, (B) `udp_echo` 100 Hz, (C) tải mạng cao (1 kHz). Đồng thời đo chi phí một bước control (cycle) để kiểm tra giả định `float` mềm ở Phần 1.
**Kết quả cần đạt.** (1) Histogram + p99/max cho 3 điều kiện, lưu trong `docs/results/`. (2) Tiêu chí **tất định**: 0 chu kỳ trễ ≥ 1 tick trong 10 000 chu kỳ ở điều kiện B. (3) Chi phí một bước control < 30 % của 25 000 cycle (ngưỡng đề xuất; nếu vượt, ghi lại và xét Q15). (4) Số liệu ghi nhãn "SIL, QEMU, thời gian ảo".
**Rủi ro.** SysTick còn dùng cho tick RTOS nên `VAL` bị reload bất kỳ lúc → đọc cặp (tick, VAL) hai lần để tránh đọc rách.

## Phần 8 — Đối chiếu host ↔ node, tài liệu, chốt M1
**Kiến thức.** Cùng mã lõi + cùng seed + cùng tham số ⇒ đáp ứng trên node phải khớp host (sai khác chỉ do `float` mềm/biên dịch khác nhau); khớp chứng minh lõi không phụ thuộc môi trường.
**Cách làm.** So CSV step response từ node (UART) với CSV host: sai lệch lớn nhất < ngưỡng chốt (vd 1e-3 rad). Tổng hợp tài liệu: `docs/m1_walkthrough.html` (giống M0), cập nhật `sil_environment.md`/README, đổi trạng thái M1.
**Kết quả cần đạt (cổng M1).** Cả ba tiêu chí của plan: overshoot < 10 % (test tự động host *và* đo trên node); histogram jitter xuất được; unit test PID/plant (và encoder, giới hạn) pass trên host — kèm đối chiếu host ↔ node đạt.
**Traceability.** `docs/traceability_matrix.md` đang được giữ nguyên theo yêu cầu trước; cập nhật khi code vào — sẽ hỏi lại ở Phần 8 trước khi sửa.

## Phụ thuộc và ngoài phạm vi
- Ngoài phạm vi M1: nhận lệnh từ QNX, E2E, frame (M2/M4); watchdog kick theo supervisor (M5); phát hiện encoder stuck thành SAFE_STOP (M5, chỉ để sẵn API ở Phần 3).
- Công cụ: `gcc`, `cmake`, `ctest`, Python + matplotlib (cần thêm vào `tools/requirements.txt`), QEMU 8.2.2 đã có.
