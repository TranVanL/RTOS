# Safety Concept (FMEA-lite)

Tài liệu *theo tinh thần* ISO 26262 để cấu trúc hóa tư duy an toàn. **Không** phải work product chính thức, **không** có ASIL decomposition hay chứng nhận. Điền chi tiết ở M5; bản dưới là khung xuất phát.

## 1. Safety goals
| ID | Safety goal | Safe state |
|---|---|---|
| SG1 | Không được điều khiển motor khi lệnh không hợp lệ/không tin cậy | Output disable |
| SG2 | Mất liên lạc giữa hai node phải dẫn tới trạng thái an toàn trong thời hạn xác định | Output disable (node) / SAFE_STOP (QNX) |
| SG3 | E-stop luôn có hiệu lực | Output disable |
| SG4 | Lỗi phần mềm trên node (treo, vi phạm bộ nhớ) không được để output bật | Output disable + reset + fault record |
| SG5 | Lỗi của process không an toàn-quan-trọng không ảnh hưởng control | Control tiếp tục |

## 2. FMEA-lite
`Detect` = cơ chế phát hiện; `React` = phản ứng; `Test` = kịch bản trong Plan §3.2. Điền cột "Ghi chú" khi review.

| # | Failure mode | Hậu quả | SG | Detect | React | Test |
|---|---|---|---|---|---|---|
| FM1 | Mất/trễ command | Node giữ lệnh cũ | SG2 | Timeout 50 tick | Disable output | F1, F5 |
| FM2 | Mất heartbeat/telemetry | QNX điều khiển mù | SG2 | Timeout 3/10 chu kỳ | DEGRADED → SAFE_STOP | F2 |
| FM3 | Bit lỗi trong frame | Lệnh sai | SG1 | CRC-32 | Loại frame, đếm lỗi | F3 |
| FM4 | Frame lặp/đảo thứ tự | Lệnh cũ thực thi lại | SG1 | Counter delta | Loại frame | F4 |
| FM5 | E-stop bị mất | Không dừng được | SG3 | Gửi lặp 3 lần, socket riêng | Disable ngay | F6 |
| FM6 | Target ngoài biên | Vượt hành trình | SG1 | Validate ở resmgr + giới hạn mềm ở node | EINVAL / SAFE_STOP | F7 |
| FM7 | Encoder stuck | Vòng điều khiển sai | SG1 | So sánh model/đo bất thường | SAFE_STOP | F8 |
| FM8 | Control task treo | Output không cập nhật | SG4 | Alive counter + watchdog | Reset + fault record | F9 |
| FM9 | Truy cập bộ nhớ trái phép | Hỏng dữ liệu | SG4 | MPU → MemManage | Disable → reset | F10 |
| FM10 | Stack overflow | Hỏng bộ nhớ | SG4 | Hook stack check | Fault nặng | F11 |
| FM11 | `motion_sup` crash | Mất điều khiển | SG2 | Node mất command | Node tự disable | F12 |
| FM12 | `logger`/`mgmt` crash | Mất log/management | SG5 | Death pulse | Control tiếp tục, ghi sự kiện | F13 |
| FM13 | Process ghi sai SHM | Hỏng state | SG5 | Quyền `PROT_READ` | SIGSEGV riêng process | F14 |
| FM14 | Priority inversion | Trễ thread an toàn | SG2 | Priority inheritance | — | M3 test |

## 3. Giới hạn
- Không có phân tích common-cause, FTA, diagnostic coverage định lượng.
- Fault injection chỉ ở mức phần mềm/SIL.
