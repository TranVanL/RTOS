#include "proto_link.h"

#include "can_frame.h"
#include "can_matrix_meta.h"

#include <string.h>

proto_result_t proto_encode(uint16_t can_id, const uint8_t *payload, size_t payload_len,
                            uint16_t counter, uint8_t *datagram, size_t capacity,
                            size_t *datagram_len)
{
    can_frame_t frame;
    size_t e2e_len = 0U;
    if (payload == NULL || datagram == NULL || datagram_len == NULL) {
        return PROTO_ERR_ARGUMENT;
    }
    const can_matrix_message_info_t *info = can_matrix_find_message(can_id);
    if (info == NULL) {
        return PROTO_ERR_UNKNOWN_ID;
    }
    if (payload_len != info->payload_length) {
        return PROTO_ERR_PAYLOAD_LENGTH;
    }
    memset(&frame, 0, sizeof frame);
    if (!e2e_protect(frame.data, sizeof frame.data, payload, payload_len, counter,
                     info->data_id, &e2e_len) || e2e_len != info->e2e_length) {
        return PROTO_ERR_E2E_OTHER;
    }
    frame.can_id = can_id;
    frame.flags = CAN_FRAME_FLAGS_REQUIRED;
    frame.data_length = info->wire_data_length;   /* padding already zero */
    if (can_frame_encode(&frame, datagram, capacity, datagram_len) != CAN_FRAME_STATUS_OK) {
        return PROTO_ERR_FRAME;
    }
    return PROTO_OK;
}

int proto_peek_can_id(const uint8_t *datagram, size_t len, uint16_t *can_id)
{
    if (datagram == NULL || can_id == NULL || len < CAN_FRAME_HEADER_SIZE) {
        return 0;
    }
    *can_id = (uint16_t)((uint16_t)datagram[2] | ((uint16_t)datagram[3] << 8U));
    return 1;
}

proto_result_t proto_decode(const uint8_t *datagram, size_t len, e2e_rx_state_t *rx,
                            uint32_t now, uint16_t *can_id_out,
                            uint8_t payload_out[PROTO_PAYLOAD_MAX], size_t *payload_len)
{
    can_frame_t frame;
    const uint8_t *payload = NULL;
    if (datagram == NULL || rx == NULL || can_id_out == NULL || payload_out == NULL ||
        payload_len == NULL) {
        return PROTO_ERR_ARGUMENT;
    }
    if (can_frame_decode(datagram, len, &frame) != CAN_FRAME_STATUS_OK) {
        return PROTO_ERR_FRAME;
    }
    const can_matrix_message_info_t *info = can_matrix_find_message(frame.can_id);
    if (info == NULL) {
        return PROTO_ERR_UNKNOWN_ID;
    }
    e2e_status_t st = e2e_rx_verify(rx, frame.data, info->e2e_length, info->payload_length,
                                    info->data_id, now, &payload);
    switch (st) {
    case E2E_STATUS_OK:
        break;
    case E2E_STATUS_CRC_ERR:
        return PROTO_ERR_E2E_CRC;
    case E2E_STATUS_REPEATED:
    case E2E_STATUS_WRONG_SEQ:
        return PROTO_ERR_E2E_SEQ;
    default:
        return PROTO_ERR_E2E_OTHER;
    }
    memcpy(payload_out, payload, info->payload_length);
    *payload_len = info->payload_length;
    *can_id_out = frame.can_id;
    return PROTO_OK;
}
