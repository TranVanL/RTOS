#include "can_frame.h"
#include "e2e.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message)                                              \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (message), __FILE__, __LINE__); \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int test_valid_encode_decode(void)
{
    can_frame_t input = {
        .can_id = 0x0100U,
        .flags = CAN_FRAME_FLAGS_REQUIRED,
        .data_length = 20U,
        .data = { 0U },
    };
    uint8_t payload[7];
    for (size_t i = 0U; i < sizeof(payload); ++i) {
                payload[i] = (uint8_t)(i + 1U);
    }
    size_t e2e_length = 0U;
    CHECK(e2e_protect(input.data, sizeof(input.data), payload, sizeof(payload),
                                            1U, input.can_id, &e2e_length),
                    "fixture E2E protect failed");
    memset(&input.data[e2e_length], 0, input.data_length - e2e_length);

    uint8_t datagram[CAN_FRAME_HEADER_SIZE + CAN_FRAME_DATA_MAX];
    size_t datagram_length = 0U;
    CHECK(can_frame_encode(&input, datagram, sizeof(datagram), &datagram_length)
              == CAN_FRAME_STATUS_OK,
          "valid frame encode failed");
    CHECK(datagram_length == CAN_FRAME_HEADER_SIZE + input.data_length,
          "encoded datagram length mismatch");
    CHECK(datagram[0] == 0xFDU && datagram[1] == 0xCAU,
          "magic is not little-endian 0xCAFD");
    CHECK(datagram[2] == 0x00U && datagram[3] == 0x01U,
          "CAN ID is not little-endian");
    CHECK(datagram[4] == CAN_FRAME_FLAG_FDF && datagram[5] == 20U,
          "flags or byte-length DLC mismatch");

    can_frame_t output;
    memset(&output, 0, sizeof(output));
    CHECK(can_frame_decode(datagram, datagram_length, &output) == CAN_FRAME_STATUS_OK,
          "valid frame decode failed");
    CHECK(output.can_id == input.can_id && output.flags == input.flags
              && output.data_length == input.data_length,
          "decoded header mismatch");
    CHECK(memcmp(output.data, input.data, input.data_length) == 0,
          "decoded frame data mismatch");
    return 0;
}

static int test_legal_dlc_values(void)
{
    static const uint8_t legal_lengths[] = { 0U, 1U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U };
    for (size_t i = 0U; i < sizeof(legal_lengths) / sizeof(legal_lengths[0]); ++i) {
        CHECK(can_frame_is_legal_dlc(legal_lengths[i]), "legal byte DLC rejected");
    }
    CHECK(!can_frame_is_legal_dlc(9U), "illegal byte DLC accepted");
    CHECK(!can_frame_is_legal_dlc(63U), "63-byte DLC accepted");
    CHECK(!can_frame_is_legal_dlc(65U), "oversized DLC accepted");
    return 0;
}

static int test_malformed_frames(void)
{
    can_frame_t frame = {
        .can_id = 0x010U,
        .flags = CAN_FRAME_FLAGS_REQUIRED,
        .data_length = 16U,
        .data = { 0U },
    };
    uint8_t datagram[CAN_FRAME_HEADER_SIZE + CAN_FRAME_DATA_MAX];
    size_t datagram_length = 0U;
    can_frame_t decoded;
    const uint8_t payload[] = { 0x31U, 0x32U };
    size_t e2e_length = 0U;

    CHECK(e2e_protect(frame.data, sizeof(frame.data), payload, sizeof(payload),
                      0U, frame.can_id, &e2e_length),
          "fixture E2E protect failed");
    memset(&frame.data[e2e_length], 0, frame.data_length - e2e_length);
    CHECK(can_frame_encode(&frame, datagram, sizeof(datagram), &datagram_length)
              == CAN_FRAME_STATUS_OK,
          "fixture encode failed");
    CHECK(can_frame_decode(datagram, CAN_FRAME_HEADER_SIZE - 1U, &decoded)
              == CAN_FRAME_STATUS_INVALID_LENGTH,
          "truncated outer header accepted");

    uint8_t saved = datagram[0];
    datagram[0] ^= 0x01U;
    CHECK(can_frame_decode(datagram, datagram_length, &decoded)
              == CAN_FRAME_STATUS_INVALID_MAGIC,
          "invalid magic accepted");
    datagram[0] = saved;

    saved = datagram[3];
    datagram[3] = 0x08U;
    CHECK(can_frame_decode(datagram, datagram_length, &decoded)
              == CAN_FRAME_STATUS_INVALID_CAN_ID,
          "CAN ID outside 11-bit range accepted");
    datagram[3] = saved;

    saved = datagram[4];
    datagram[4] |= CAN_FRAME_FLAG_BRS;
    CHECK(can_frame_decode(datagram, datagram_length, &decoded)
              == CAN_FRAME_STATUS_INVALID_FLAGS,
          "unsupported BRS flag accepted");
    datagram[4] = saved;

    saved = datagram[5];
    datagram[5] = 9U;
    CHECK(can_frame_decode(datagram, datagram_length, &decoded)
              == CAN_FRAME_STATUS_INVALID_DLC,
          "illegal DLC accepted");
    datagram[5] = saved;

    CHECK(can_frame_decode(datagram, datagram_length - 1U, &decoded)
              == CAN_FRAME_STATUS_INVALID_LENGTH,
          "truncated frame data accepted");
    CHECK(can_frame_decode(datagram, datagram_length + 1U, &decoded)
              == CAN_FRAME_STATUS_INVALID_LENGTH,
          "trailing frame byte accepted");

    datagram[2] = 0x00U;
    datagram[3] = 0x02U;
    CHECK(can_frame_decode(datagram, datagram_length, &decoded)
              == CAN_FRAME_STATUS_UNKNOWN_MESSAGE_ID,
          "CAN ID absent from DBC was accepted");
    datagram[2] = 0x10U;
    datagram[3] = 0x00U;

    datagram[CAN_FRAME_HEADER_SIZE + 15U] = 1U;
    CHECK(can_frame_decode(datagram, datagram_length, &decoded)
              == CAN_FRAME_STATUS_INVALID_PADDING,
          "nonzero padding was accepted");
    datagram[CAN_FRAME_HEADER_SIZE + 15U] = 0U;

    datagram[5] = 20U;
    memset(&datagram[CAN_FRAME_HEADER_SIZE + 16U], 0, 4U);
    CHECK(can_frame_decode(datagram, CAN_FRAME_HEADER_SIZE + 20U, &decoded)
              == CAN_FRAME_STATUS_INVALID_DLC,
          "legal DLC with wrong message-specific length was accepted");
    datagram[5] = 16U;

    frame.can_id = 0x0800U;
    CHECK(can_frame_encode(&frame, datagram, sizeof(datagram), &datagram_length)
              == CAN_FRAME_STATUS_INVALID_CAN_ID,
          "encoder accepted CAN ID outside 11-bit range");
    frame.can_id = 0x010U;
    frame.flags |= CAN_FRAME_FLAG_BRS;
    CHECK(can_frame_encode(&frame, datagram, sizeof(datagram), &datagram_length)
              == CAN_FRAME_STATUS_INVALID_FLAGS,
          "encoder accepted unsupported BRS flag");
    frame.flags = CAN_FRAME_FLAGS_REQUIRED;
    CHECK(can_frame_encode(&frame, datagram, 8U, &datagram_length)
              == CAN_FRAME_STATUS_INSUFFICIENT_CAPACITY,
          "encoder accepted insufficient output capacity");
    return 0;
}

int main(void)
{
    if (test_valid_encode_decode() != 0
        || test_legal_dlc_values() != 0
        || test_malformed_frames() != 0) {
        return 1;
    }

    puts("test_can_frame: PASS");
    return 0;
}
