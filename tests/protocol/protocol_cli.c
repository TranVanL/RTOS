#include "can_frame.h"
#include "can_matrix_meta.h"
#include "e2e.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEX_BUFFER_SIZE (2U * (CAN_FRAME_HEADER_SIZE + CAN_FRAME_DATA_MAX) + 1U)

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int parse_hex(const char *text, uint8_t *output, size_t capacity, size_t *length)
{
    const size_t text_length = strlen(text);
    if ((text_length % 2U) != 0U || text_length / 2U > capacity) {
        return 0;
    }

    for (size_t i = 0U; i < text_length / 2U; ++i) {
        const int high = hex_nibble(text[2U * i]);
        const int low = hex_nibble(text[2U * i + 1U]);
        if (high < 0 || low < 0) {
            return 0;
        }
        output[i] = (uint8_t)((unsigned int)high * 16U + (unsigned int)low);
    }

    *length = text_length / 2U;
    return 1;
}

static int parse_unsigned(const char *text, int base, unsigned long maximum, unsigned long *value)
{
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, base);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) {
        return 0;
    }
    *value = parsed;
    return 1;
}

static void print_hex(const uint8_t *data, size_t length)
{
    for (size_t i = 0U; i < length; ++i) {
        (void)printf("%02x", (unsigned int)data[i]);
    }
    (void)putchar('\n');
}

static int encode_command(int argc, char **argv)
{
    unsigned long can_id = 0UL;
    unsigned long counter = 0UL;
    if (argc != 5 || !parse_unsigned(argv[2], 16, 0x07FFUL, &can_id)
        || !parse_unsigned(argv[3], 16, 0xFFFFUL, &counter)) {
        return 2;
    }

    uint8_t payload[CAN_FRAME_DATA_MAX];
    size_t payload_length = 0U;
    if (!parse_hex(argv[4], payload, sizeof(payload), &payload_length)) {
        return 2;
    }

    const can_matrix_message_info_t *message = can_matrix_find_message((uint16_t)can_id);
    if (message == NULL || payload_length != message->payload_length) {
        return 2;
    }

    can_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = (uint16_t)can_id;
    frame.flags = CAN_FRAME_FLAGS_REQUIRED;
    frame.data_length = message->wire_data_length;

    size_t protected_length = 0U;
    if (!e2e_protect(frame.data, sizeof(frame.data), payload, payload_length,
                     (uint16_t)counter, message->data_id, &protected_length)) {
        return 2;
    }
    (void)protected_length;

    uint8_t datagram[CAN_FRAME_HEADER_SIZE + CAN_FRAME_DATA_MAX];
    size_t datagram_length = 0U;
    if (can_frame_encode(&frame, datagram, sizeof(datagram), &datagram_length)
        != CAN_FRAME_STATUS_OK) {
        return 2;
    }

    print_hex(datagram, datagram_length);
    return 0;
}

static int decode_command(int argc, char **argv)
{
    if (argc != 3) {
        return 2;
    }

    uint8_t datagram[CAN_FRAME_HEADER_SIZE + CAN_FRAME_DATA_MAX];
    size_t datagram_length = 0U;
    if (!parse_hex(argv[2], datagram, sizeof(datagram), &datagram_length)) {
        return 2;
    }

    can_frame_t frame;
    if (can_frame_decode(datagram, datagram_length, &frame) != CAN_FRAME_STATUS_OK) {
        return 3;
    }

    const can_matrix_message_info_t *message = can_matrix_find_message(frame.can_id);
    if (message == NULL) {
        return 3;
    }

    e2e_rx_state_t state;
    e2e_rx_reset(&state, 0U);
    const uint8_t *payload = NULL;
    const e2e_status_t status = e2e_rx_verify(
        &state,
        frame.data,
        message->e2e_length,
        message->payload_length,
        message->data_id,
        1U,
        &payload
    );
    if (status != E2E_STATUS_OK || payload == NULL) {
        (void)printf("STATUS=%d\n", (int)status);
        return 4;
    }

    (void)printf("STATUS=OK;ID=%03x;COUNTER=%04x;PAYLOAD=", frame.can_id, state.last_counter);
    for (size_t i = 0U; i < message->payload_length; ++i) {
        (void)printf("%02x", (unsigned int)payload[i]);
    }
    (void)putchar('\n');
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "encode") == 0) {
        return encode_command(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "decode") == 0) {
        return decode_command(argc, argv);
    }

    (void)fprintf(stderr,
                  "usage: protocol_cli encode <can_id_hex> <counter_hex> <payload_hex>\n"
                  "       protocol_cli decode <datagram_hex>\n");
    return 2;
}
