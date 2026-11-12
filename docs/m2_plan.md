# M2 — Frame protocol, DBC, E2E (kế hoạch chi tiết)

> Trạng thái hiện tại: **đang triển khai, chưa qua gate hoàn tất**. Kế hoạch này cụ thể hóa M2 trong [docs/02_PROJECT_PLAN.md](02_PROJECT_PLAN.md) và theo dõi các cổng còn lại.
> Audit: 2026-11-12. Ước lượng ban đầu: **27–41 giờ**. Lịch parent plan đã được rebaseline thành tuần 5–8 (~40 giờ ở mức 10 giờ/tuần). Còn cần xác minh ARM cross-build và trạng thái CI; xem WP5/WP6 bên dưới.

## 0. Snapshot trước triển khai (2026-11-12)

Phần này là ảnh chụp lịch sử trước khi bắt đầu code, không mô tả trạng thái hiện tại.

- [common/can_matrix.dbc](../common/can_matrix.dbc) đã có 5 message và signal payload; comment quy định đây là **application payload**, chưa gồm E2E header và padding.
- Các thư mục dành cho C frame, E2E và generated code hiện chỉ có placeholder rỗng; chưa có C frame/E2E library hay generated code.
- [tools/dbc_codegen.py](../tools/dbc_codegen.py) và [tools/frame_sniffer.py](../tools/frame_sniffer.py) hiện chỉ là docstring.
- Chưa có unit test cho M2; [tools/tests/test_placeholder.py](../tools/tests/test_placeholder.py) chỉ assert `True`. CMake/CTest hiện có ở [tests/control/CMakeLists.txt](../tests/control/CMakeLists.txt), dùng được làm mẫu nhưng nên tách protocol tests để không trộn M1/M2.
- [freertos/CMakeLists.txt](../freertos/CMakeLists.txt) đã build node, FreeRTOS+TCP, app/networking, UDP echo/probe và control core, nhưng chưa compile protocol sources; [freertos/app/net.c](../freertos/app/net.c) vẫn khởi chạy echo/probe, chưa giải mã frame.
- [.github/workflows/ci.yml](../.github/workflows/ci.yml) chạy pytest; chưa có protocol CTest, DBC generation check hay firmware cross-build. TODO trong CI còn ghi host test Ceedling/M1, trong khi M1 hiện dùng CMake/CTest.

Kết luận của snapshot ban đầu: tài liệu và DBC đã định hướng tốt nhưng chưa có implementation M2. Các mục dưới đây đã được xử lý; tiến độ hiện tại nằm ở §0.1.

## 0.1 Tiến độ triển khai

- **WP0 — hoàn tất:** wire contract, status/counter/timeout rules, DLC byte-length, flag mask, padding và route 5000/5001 đã ghi trong [docs/01_ARCHITECTURE.md](01_ARCHITECTURE.md); DBC comments nêu rõ port cho cả 5 message.
- **WP1 — hoàn tất:** generator dùng cantools, kiểm inventory/ID/length/port, phát C pack/unpack và message metadata; output normalized để loại timestamp và `--check` phát hiện stale files.
- **WP2/WP3 — implementation + host tests:** CRC-32P4, E2E state/check API và C frame codec đã có; tests cover CRC known-answer, statuses, wrap/delta, timeout, malformed header, DBC ID/DLC và padding.
- **WP4 — implementation + host tests:** Python reference codec, DBC payload encode/decode và offline/UDP sniffer đã có.
- **WP5 — đang xác minh:** C/Python byte-for-byte tests hai chiều, host CTest qua pytest, DBC generation check, independent E2E/UDP golden vector và common protocol sources đã được thêm vào FreeRTOS target. Lỗi indent làm CI YAML không hợp lệ đã sửa. Lần xác minh local mới nhất: YAML parse hợp lệ, generator check pass, **19 tests passed**, `git diff --check` pass.
- **Còn mở:** ARM cross-build chưa chạy vì host không có `arm-none-eabi-gcc`; GitHub Actions remote chưa được xác nhận; so sánh tham số/layout với tài liệu AUTOSAR chính thức chưa được lưu thành evidence. Vì vậy **chưa đánh dấu M2 complete**.

## 1. Mục tiêu và ranh giới

### Mục tiêu

1. Có wire format xác định byte-for-byte, độc lập OS/compiler/endianness máy.
2. Có thư viện C thuần cho frame và E2E; không cấp phát động, không socket, không FreeRTOS/QNX API.
3. DBC là nguồn duy nhất cho application signals; code C pack/unpack được sinh lặp lại ổn định.
4. Có Python decoder/sniffer và kiểm chứng C ↔ Python bằng golden vectors độc lập; vector ESTOP cố định được lưu tại [tests/protocol/golden_vectors.md](../tests/protocol/golden_vectors.md).
5. CI chạy được host protocol tests và phát hiện generated code bị lệch DBC; firmware có thể cross-compile với thư viện chung.

### Ngoài phạm vi M2

- Chưa thay UDP echo bằng Comm task hoàn chỉnh, chưa nối QNX supervisor, FSM, timeout policy end-to-end hoặc E-stop behavior. Đây là tích hợp ở M3/M4; M2 chỉ cung cấp codec, E2E state API và công cụ kiểm tra.
- Không mô phỏng CAN arbitration, physical CAN FD, ACK, bus-off hay xác nhận timing phần cứng.
- Không tuyên bố AUTOSAR-compliant, ISO 26262-compliant hoặc CRC là cơ chế xác thực/bảo mật.

### Effort và lịch

- WP0–WP4 chiếm khoảng **20–30 giờ**; WP5–WP6 thêm **7–11 giờ**; tổng **27–41 giờ** (khoảng 3–4 tuần nếu duy trì 10 giờ/tuần).
- Lịch tuần 5–8 trong [docs/02_PROJECT_PLAN.md](02_PROJECT_PLAN.md) rebaseline thành khoảng 40 giờ để phù hợp hơn với estimate 27–41 giờ. Không được lược bỏ test interoperability/golden vectors chỉ để khớp lịch.

## 2. Thứ tự triển khai và cổng thiết kế

**Không viết production codec trước khi hoàn tất WP0.** Các đề xuất dưới đây phải được ghi thành normative contract trong [docs/01_ARCHITECTURE.md](01_ARCHITECTURE.md); nếu phát hiện khác với nguồn tiêu chuẩn/thực tế, cập nhật contract trước khi code và lưu lý do.

### 2.1 Contract v1 đề xuất cần chốt tại WP0

#### Layering và kích thước

`DBC application payload → E2E-protected data → outer frame → UDP datagram`.

- DBC chỉ pack/unpack application payload, không chứa E2E header hoặc padding.
- E2E header dài 12 byte: `length:u16`, `counter:u16`, `data_id:u32`, `crc32:u32`; mọi số nguyên serialize little-endian.
- `length` là tổng E2E data có nghĩa: 12-byte header + application payload; gồm trường CRC, **không gồm** padding ngoài.
- CRC tính trên các byte `length || counter || data_id || application_payload`; CRC field và padding không nằm trong CRC input.
- Padding tới legal CAN-FD data size là byte `0x00`; decoder từ chối padding khác 0. Không đọc application signal từ padding.

| Message | CAN ID | DBC payload | E2E length | Wire data/DLC length |
|---|---:|---:|---:|---:|
| ESTOP | `0x010` | 2 B | 14 B | 16 B |
| CMD | `0x100` | 7 B | 19 B | 20 B |
| TELEM | `0x180` | 9 B | 21 B | 24 B |
| HEARTBEAT | `0x1F0` | 5 B | 17 B | 20 B |
| FAULT_RECORD | `0x1F1` | 16 B | 28 B | 32 B |

#### Outer frame

Architecture hiện định nghĩa header 6 byte (`magic`, `can_id`, `flags`, `dlc`) nhưng chưa khóa endian, bit flags và ý nghĩa wire của DLC. Contract v1 cần chốt:

- Header 6 byte: `magic:u16 LE = 0xCAFD`, `can_id:u16 LE`, `flags:u8`, `dlc:u8`, sau đó đúng `dlc` byte data.
- `dlc` lưu **độ dài data tính bằng byte**, không phải raw 4-bit CAN DLC code; giá trị hợp lệ là 0–8, 12, 16, 20, 24, 32, 48, 64. Sửa mô tả kiến trúc để tránh nhập nhằng.
- M2 phát frame CAN-FD-compatible: FDF bit được set; BRS/reserved bits có giá trị được chốt trong spec (khuyến nghị BRS=0 cho SIL và mọi reserved bit = 0).
- Một UDP datagram chứa đúng một frame; độ dài datagram phải bằng `6 + dlc`, không chấp nhận truncated hoặc trailing bytes.
- CAN ID phải nằm trong miền 11-bit; chỉ chấp nhận message ID có trong DBC. `data_id` mặc định zero-extend CAN ID thành u32, trừ khi spec chọn mapping tường minh khác.
- Routing UDP cần chốt cùng protocol: đề xuất `0x010` ESTOP dùng port 5001; các message còn lại gồm `0x1F1` FAULT_RECORD dùng port 5000. FAULT_RECORD hiện chưa được gán port trong architecture; sửa message table trước khi coi mapping này là normative.

#### CRC và E2E counter

- Chốt chính xác CRC-32P4: polynomial normal `0xF4ACFB13` (reflected `0xC8DF352F`), init/xorout `0xFFFFFFFF`, RefIn/RefOut, thứ tự byte, thứ tự byte input và vị trí CRC field. Dùng reference AUTOSAR phù hợp để đối chiếu; luôn diễn đạt là “Profile 4–based”, không “compliant”.
- Kiểm tra thuật toán CRC với known-answer `"123456789" → 0x1697D06A` (theo CRC-32/AUTOSAR parameter set đã nêu) **và** ít nhất một golden vector của E2E header+payload được tạo độc lập. Nếu đối chiếu tiêu chuẩn cho kết quả khác, cập nhật vector/spec trước khi code.
- Counter là uint16 modular, state độc lập theo từng Data ID. Sau reset, frame đầu tiên có CRC/ID/length hợp lệ thiết lập baseline và trả `OK`; delta 0 trả `REPEATED`; delta 1 hoặc 2 trả `OK`; delta >2 trả `WRONG_SEQ`. Phép trừ phải đúng qua wrap 65535→0.
- `CRC_ERR`, `LEN_OR_ID_ERR`, `REPEATED`, `WRONG_SEQ` không được cập nhật last-accepted counter hay freshness time. Chỉ frame `OK` làm mới state.
- Sender reset/receiver resynchronization phải có API reset tường minh; timeout không tự tin cậy một counter nhảy bất kỳ nếu chưa có policy được ghi rõ.
- Timeout dùng thời gian do caller truyền vào (tick/monotonic domain), không gọi clock/OS bên trong thư viện. Chốt startup grace, wrap-safe elapsed-time arithmetic, timeout latch/recovery, và timeout riêng message theo architecture (CMD 50 ms; TELEM/HEARTBEAT 30 ms). Test dùng virtual tick, không sleep.
- E-stop được gửi nhiều lần cần semantics rõ ràng: khuyến nghị mỗi lần gửi là frame mới với counter tăng; hành động stop phía ứng dụng vẫn phải idempotent. Nếu quyết định replay nguyên datagram, các lần sau phải được kỳ vọng là `REPEATED` và không được xem là frame mới.

### 2.2 Work packages

#### WP0 — Freeze protocol contract (3–5 giờ)

**Việc làm**
- Chốt các quyết định §2.1, bao gồm routing `FAULT_RECORD`; rà soát mọi message trong DBC và message table architecture.
- Bổ sung wire byte map, flag masks, DLC encoding, length semantics, padding, Data ID mapping, CRC bytes/golden vector, counter delta, timeout, reset/restart semantics vào [docs/01_ARCHITECTURE.md](01_ARCHITECTURE.md).
- Đảm bảo thuật ngữ nhất quán: “frame tương thích CAN FD qua UDP”, “Profile 4–based”. Cập nhật [docs/requirements.md](requirements.md) nếu acceptance cần làm rõ.

**Done khi**
- Người viết C và Python có thể tự tạo cùng một datagram từ spec mà không cần suy đoán byte order/độ dài.
- Golden vectors được review độc lập; không còn quyết định protocol quan trọng ở trạng thái implicit.

#### WP1 — DBC quality và deterministic C codegen (4–6 giờ)

**Việc làm**
- Rà soát 5 message trong [common/can_matrix.dbc](../common/can_matrix.dbc): ID, direction, payload length, signedness, scaling, min/max, enum/value table, comment chu kỳ/timeout. Giữ DBC application-only.
- Hoàn thiện [tools/dbc_codegen.py](../tools/dbc_codegen.py), gọi cantools theo phiên bản được pin trong dependencies; CLI phải chạy từ bất kỳ working directory nào và hỗ trợ `--check` (fail nếu generated output lệch).
- Sinh C header/source vào generated-code directory, ghi rõ không sửa tay; xác minh generation lần 2 không tạo diff.
- Không tự viết tay pack/unpack thay thế DBC generator. Có thể thêm wrapper mỏng nếu API cantools sinh ra cần thống nhất giữa app.

**Done khi**
- Cantools load DBC thành công; generator validate đúng 5 message, output checked-in reproducible.
- Test payload cho từng message ở nominal, 0/min/max, signed negative/positive và enum boundary; encode→decode giữ đúng raw/scaled semantics.
- `--check` pass trên clean tree và fail có chủ đích khi generated file stale.

#### WP2 — CRC-32P4 + E2E C library (5–7 giờ)

**Việc làm**
- Tạo API C ổn định cho E2E, tách module CRC và E2E: protect, verify, receiver reset và timeout check.
- Sử dụng buffer/length truyền vào, kiểm tra NULL/length/capacity trước khi đọc/ghi; không packed struct, unaligned access, malloc, clock, logging hoặc OS API.
- Receiver state phân tách theo Data ID/caller context; trạng thái chỉ thay đổi theo quy tắc §2.1.
- CRC implementation byte-wise hoặc table-driven tùy lựa chọn; correctness/golden tests là bắt buộc, optimization không phải acceptance.

**Done khi**
- CTest cover known-answer CRC và E2E golden vector; statuses `OK`, `REPEATED`, `WRONG_SEQ`, `CRC_ERR`, `LEN_OR_ID_ERR`, `TIMEOUT`.
- Có test counter wrap, delta 1/2/3, first frame, explicit reset/restart, lỗi không mutate state, timeout startup/expiry/recovery và timestamp wrap.
- Invalid pointer/capacity/length được từ chối an toàn; compile strict warnings (`-Wall -Wextra -Werror`) sạch trên host.

#### WP3 — Outer frame C codec (4–6 giờ)

**Việc làm**
- Tạo C codec encode/decode cho outer frame từ trường đã chốt; không map buffer sang C struct/bitfield.
- Validate magic, CAN ID, flags, legal DLC/byte length, exact datagram length, E2E length không vượt DLC, padding, maximum 64-byte data và output capacity.
- Giữ API frame độc lập E2E nếu hợp lý; cung cấp tầng compose/decompose hoặc test chứng minh layering rõ ràng.

**Done khi**
- CTest round-trip đủ 5 message và mọi DLC đang dùng; malformed matrix cover magic/ID/flags/DLC/length/trailing bytes/truncation/nonzero padding/oversize.
- Không ghi vượt buffer khi input ngắn hoặc output capacity nhỏ; parser reject unknown CAN ID theo contract.

#### WP4 — Python reference decoder và UDP sniffer (4–6 giờ)

**Việc làm**
- Tách parser/reference implementation có thể test độc lập khỏi CLI [tools/frame_sniffer.py](../tools/frame_sniffer.py).
- Parser Python giải outer frame + E2E/status; dùng cantools decode application payload. Không import/consume output CRC từ C trong phép kiểm CRC độc lập.
- CLI hỗ trợ tối thiểu bind UDP host/port, đọc từng datagram, in CAN ID/message/status/decoded signals, đếm và báo malformed packet; thêm chế độ parse hex/file để test tái lập.
- Dùng `socket` standard library; không thêm packet-capture dependency trừ khi thực sự cần. Giới hạn log/payload output để input lỗi không làm tăng bộ nhớ vô hạn.

**Done khi**
- Pytest cover năm message, cả hai UDP port policy theo spec, mọi E2E status và malformed frame; test parser offline không cần socket/root/network.
- CLI chạy được trên datagram fixture; lỗi một datagram không làm crash/thoát listener trừ khi cấu hình yêu cầu.

#### WP5 — C ↔ Python interoperability, build và traceability (5–8 giờ)

**Việc làm**
- Thêm CMake/CTest target riêng cho protocol, link C frame/E2E/generated code; giữ [tests/control/CMakeLists.txt](../tests/control/CMakeLists.txt) độc lập cho M1.
- Thêm pytest harness gọi C test/executable để kiểm theo cả hai chiều: (1) C encode → Python decode; (2) Python/golden encode → C decode. So sánh bytes chính xác, không chỉ round-trip cùng một implementation.
- Chạy vectors cho cả 5 DBC messages, biên signal signedness/range, CRC/status/error cases. Dùng vectors Python độc lập để tránh shared-bug false pass.
- Thêm protocol CTest và generator `--check` vào workflow pytest hiện có; chưa đưa full firmware/QEMU build vào CI vì đây là phần CI rộng hơn trong M6. Kiểm tra cú pháp workflow trước khi merge. Làm một ARM cross-build compatibility check bằng preset node hiện có, với frame/E2E/generated C được compile vào target; ghi rõ đây là local/manual check nếu CI chưa cài toolchain. M2 chưa đổi đường UDP echo thành Comm production path và chưa cần boot QEMU.
- Cập nhật traceability `REQ-COM-001/002/003`, `REQ-SAF-004/005` tới test thực; xóa/thay placeholder test nếu không còn mục đích. Sửa TODO M1/Ceedling cũ trong CI khi cập nhật pipeline.

**Done khi**
- CTest + pytest + generator check pass trong CI; có log chứng minh ARM cross-build của firmware với common protocol sources pass trên toolchain (CI hoặc local theo môi trường đã chốt). Workflow YAML hợp lệ là điều kiện tối thiểu, không thay thế remote CI evidence.
- C-generated frames decode đúng Python và Python/golden frames decode đúng C cho **100% vectors đã định nghĩa**, byte-for-byte; không claim exhaustive correctness ngoài bộ vectors/tests.
- Traceability có đường REQ → architecture → code → passing test cho M2 requirements; không đánh dấu “Done” trước khi CI xanh.

#### WP6 — Review, báo cáo, chốt M2 (2–3 giờ)

**Việc làm**
- Chạy sạch host configure/build/CTest, pytest, DBC generation check và firmware cross-build; ghi toolchain/version/command/result.
- Lưu golden vectors, test summary và giới hạn SIL trong thư mục kết quả của docs; cập nhật [docs/traceability_matrix.md](traceability_matrix.md), [docs/02_PROJECT_PLAN.md](02_PROJECT_PLAN.md) và [README.md](../README.md) sau khi đạt acceptance.
- Cập nhật [CLAUDE.md](../CLAUDE.md) current state nếu nó còn báo M0; không để tài liệu trợ lý mâu thuẫn với code.

**Done khi**
- Toàn bộ Definition of Done ở §6 pass, không còn TODO/placeholder M2 gây hiểu nhầm; README chỉ tuyên bố M2 hoàn tất khi CI + cross-build/test evidence tồn tại.

## 3. Test matrix bắt buộc

| Lớp | Kiểm tra | Bằng chứng pass |
|---|---|---|
| DBC/codegen | Load, message inventory, signal boundary, repeat generation | pytest + generated `--check` |
| CRC | Known-answer + E2E golden vector độc lập | CTest và Python test vector |
| E2E TX/RX | 6 status, counter wrap/delta, Data ID, CRC, reset, timeout | CTest |
| Frame C | Valid frames, all used DLC sizes, malformed/truncated/oversized | CTest |
| Python parser/sniffer | Golden datagrams, malformed input, UDP listener behavior | pytest |
| Interop | C→Python, Python→C, exact bytes and decoded values | pytest harness + C executable |
| Build | Host strict-warning build; ARM firmware cross-build | CI logs cho host; toolchain log cho ARM |
| Traceability | COM-001/002/003 and SAF-004/005 have code/test links | Matrix review |

## 4. File map dự kiến

| Vai trò | File/thư mục |
|---|---|
| Normative protocol and requirements | [docs/01_ARCHITECTURE.md](01_ARCHITECTURE.md), [docs/requirements.md](requirements.md) |
| Existing signal source | [common/can_matrix.dbc](../common/can_matrix.dbc) |
| Generated C (không sửa tay) | [common/generated/](../common/generated/) |
| C frame/E2E | [common/frame/](../common/frame/), [common/e2e/](../common/e2e/) |
| DBC codegen / sniffer | [tools/dbc_codegen.py](../tools/dbc_codegen.py), [tools/frame_sniffer.py](../tools/frame_sniffer.py), [tools/m2_protocol.py](../tools/m2_protocol.py) |
| Tests | [tests/control/CMakeLists.txt](../tests/control/CMakeLists.txt) (M1), [tests/protocol/](../tests/protocol/), [tools/tests/](../tools/tests/) |
| Build/CI | [freertos/CMakeLists.txt](../freertos/CMakeLists.txt), [freertos/CMakePresets.json](../freertos/CMakePresets.json), [tools/requirements.txt](../tools/requirements.txt), [.github/workflows/ci.yml](../.github/workflows/ci.yml) |
| Trace/status | [docs/traceability_matrix.md](traceability_matrix.md), [README.md](../README.md), [CLAUDE.md](../CLAUDE.md) |

## 5. Rủi ro và nguyên tắc giữ scope

| Rủi ro | Cách xử lý |
|---|---|
| AUTOSAR Profile 4 layout/CRC bị hiểu nhầm là custom wire layout chuẩn | Đối chiếu parameter/spec trước; mô tả đúng mức “Profile 4–based”; lưu nguồn/vector và không copy nguyên văn tài liệu có bản quyền vào repo. |
| C và Python cùng lỗi nên round-trip vẫn pass | Có golden vectors độc lập, kiểm tra bytes ở từng boundary, hai chiều cross-language. |
| DBC payload bị nhầm là wire DLC | Duy trì ranh giới payload/E2E/padding, assert kích thước 5 message bằng test. |
| DBC generator thay output theo version | Pin cantools, generation `--check`, không sửa tay generated output. |
| Counter reset hoặc timeout tự tạo resync nguy hiểm | API reset explicit; mọi recovery semantics được định nghĩa và test trước khi tích hợp M4. |
| Tích hợp UDP sớm làm scope nở và khó debug | M2 chỉ test codec/sniffer và compile target; Comm task/runtime safety integration để M3/M4. |

## 6. Definition of Done — M2

- [ ] Protocol byte contract và independent golden vector đã được kiểm thử; cần review chốt, không còn byte-order/length/DLC/CRC ambiguity.
- [ ] DBC load và generator deterministic; application payload pack/unpack đúng boundary.
- [ ] C frame + E2E chạy độc lập OS, compile strict warning, đủ status/negative tests.
- [ ] Python sniffer/parser và C codec tương thích hai chiều bằng vectors độc lập.
- [ ] Host CTest, pytest và generation check pass trong remote CI; ARM cross-build compatibility check pass và có log lưu lại (CI hoặc local theo môi trường đã chốt).
- [ ] Traceability cập nhật cho REQ-COM-001/002/003 và REQ-SAF-004/005, trạng thái chỉ chuyển Done khi có CI evidence.
- [ ] README/project status chỉ đánh dấu M2 complete sau khi có evidence; tài liệu ghi rõ đây là SIL/Profile 4–based, không CAN vật lý hay safety certification.

**Gate sang M3/M4:** toàn bộ checklist trên xanh. Nếu chỉ có frame encode/decode hoặc chỉ có CRC unit test thì M2 chưa hoàn tất.