#include "can_frame.h"

#include "can_matrix_meta.h"
#include "e2e.h"

#include <string.h>

static void write_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)(value >> 8U);
}

static uint16_t read_u16_le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0]
        | ((uint32_t)src[1] << 8U)
        | ((uint32_t)src[2] << 16U)
        | ((uint32_t)src[3] << 24U);
}

static can_frame_status_t validate_message_data(
    uint16_t can_id,
    uint8_t data_length,
    const uint8_t *data
)
{
    const can_matrix_message_info_t *message = can_matrix_find_message(can_id);
    if (message == NULL) {
        return CAN_FRAME_STATUS_UNKNOWN_MESSAGE_ID;
    }
    if (data_length != message->wire_data_length) {
        return CAN_FRAME_STATUS_INVALID_DLC;
    }
    if (read_u16_le(&data[0]) != message->e2e_length
        || read_u32_le(&data[4]) != message->data_id) {
        return CAN_FRAME_STATUS_INVALID_E2E_DATA;
    }
    for (size_t i = message->e2e_length; i < message->wire_data_length; ++i) {
        if (data[i] != 0U) {
            return CAN_FRAME_STATUS_INVALID_PADDING;
        }
    }
    return CAN_FRAME_STATUS_OK;
}

bool can_frame_is_legal_dlc(uint8_t data_length)
{
    return data_length <= 8U
        || data_length == 12U
        || data_length == 16U
        || data_length == 20U
        || data_length == 24U
        || data_length == 32U
        || data_length == 48U
        || data_length == 64U;
}

can_frame_status_t can_frame_encode(
    const can_frame_t *frame,
    uint8_t *datagram,
    size_t datagram_capacity,
    size_t *datagram_length
)
{
    if (frame == NULL || datagram == NULL || datagram_length == NULL) {
        return CAN_FRAME_STATUS_INVALID_ARGUMENT;
    }
    if (frame->can_id > 0x07FFU) {
        return CAN_FRAME_STATUS_INVALID_CAN_ID;
    }
    if (frame->flags != CAN_FRAME_FLAGS_REQUIRED) {
        return CAN_FRAME_STATUS_INVALID_FLAGS;
    }
    if (!can_frame_is_legal_dlc(frame->data_length)) {
        return CAN_FRAME_STATUS_INVALID_DLC;
    }
    const can_frame_status_t message_status =
        validate_message_data(frame->can_id, frame->data_length, frame->data);
    if (message_status != CAN_FRAME_STATUS_OK) {
        return message_status;
    }

    const size_t required_length = CAN_FRAME_HEADER_SIZE + frame->data_length;
    if (datagram_capacity < required_length) {
        return CAN_FRAME_STATUS_INSUFFICIENT_CAPACITY;
    }

    write_u16_le(&datagram[0], CAN_FRAME_MAGIC);
    write_u16_le(&datagram[2], frame->can_id);
    datagram[4] = frame->flags;
    datagram[5] = frame->data_length;
    if (frame->data_length != 0U) {
        memcpy(&datagram[CAN_FRAME_HEADER_SIZE], frame->data, frame->data_length);
    }

    *datagram_length = required_length;
    return CAN_FRAME_STATUS_OK;
}

can_frame_status_t can_frame_decode(
    const uint8_t *datagram,
    size_t datagram_length,
    can_frame_t *frame_out
)
{
    if (datagram == NULL || frame_out == NULL) {
        return CAN_FRAME_STATUS_INVALID_ARGUMENT;
    }
    if (datagram_length < CAN_FRAME_HEADER_SIZE) {
        return CAN_FRAME_STATUS_INVALID_LENGTH;
    }
    if (read_u16_le(&datagram[0]) != CAN_FRAME_MAGIC) {
        return CAN_FRAME_STATUS_INVALID_MAGIC;
    }

    const uint16_t can_id = read_u16_le(&datagram[2]);
    if (can_id > 0x07FFU) {
        return CAN_FRAME_STATUS_INVALID_CAN_ID;
    }
    if (datagram[4] != CAN_FRAME_FLAGS_REQUIRED) {
        return CAN_FRAME_STATUS_INVALID_FLAGS;
    }

    const uint8_t data_length = datagram[5];
    if (!can_frame_is_legal_dlc(data_length)) {
        return CAN_FRAME_STATUS_INVALID_DLC;
    }
    if (datagram_length != CAN_FRAME_HEADER_SIZE + (size_t)data_length) {
        return CAN_FRAME_STATUS_INVALID_LENGTH;
    }

    const can_frame_status_t message_status = validate_message_data(
        can_id,
        data_length,
        &datagram[CAN_FRAME_HEADER_SIZE]
    );
    if (message_status != CAN_FRAME_STATUS_OK) {
        return message_status;
    }

    frame_out->can_id = can_id;
    frame_out->flags = datagram[4];
    frame_out->data_length = data_length;
    if (data_length != 0U) {
        memcpy(frame_out->data, &datagram[CAN_FRAME_HEADER_SIZE], data_length);
    }
    return CAN_FRAME_STATUS_OK;
}
