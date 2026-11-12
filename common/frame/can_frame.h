#ifndef M2_CAN_FRAME_H
#define M2_CAN_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_FRAME_MAGIC          0xCAFDu
#define CAN_FRAME_HEADER_SIZE    6U
#define CAN_FRAME_DATA_MAX       64U
#define CAN_FRAME_FLAG_FDF       0x01U
#define CAN_FRAME_FLAG_BRS       0x02U
#define CAN_FRAME_FLAGS_REQUIRED CAN_FRAME_FLAG_FDF

typedef struct {
    uint16_t can_id;
    uint8_t flags;
    uint8_t data_length;
    uint8_t data[CAN_FRAME_DATA_MAX];
} can_frame_t;

typedef enum {
    CAN_FRAME_STATUS_OK = 0,
    CAN_FRAME_STATUS_INVALID_ARGUMENT,
    CAN_FRAME_STATUS_INVALID_LENGTH,
    CAN_FRAME_STATUS_INVALID_MAGIC,
    CAN_FRAME_STATUS_INVALID_CAN_ID,
    CAN_FRAME_STATUS_UNKNOWN_MESSAGE_ID,
    CAN_FRAME_STATUS_INVALID_FLAGS,
    CAN_FRAME_STATUS_INVALID_DLC,
    CAN_FRAME_STATUS_INVALID_E2E_DATA,
    CAN_FRAME_STATUS_INVALID_PADDING,
    CAN_FRAME_STATUS_INSUFFICIENT_CAPACITY
} can_frame_status_t;

bool can_frame_is_legal_dlc(uint8_t data_length);

can_frame_status_t can_frame_encode(
    const can_frame_t *frame,
    uint8_t *datagram,
    size_t datagram_capacity,
    size_t *datagram_length
);

can_frame_status_t can_frame_decode(
    const uint8_t *datagram,
    size_t datagram_length,
    can_frame_t *frame_out
);

#ifdef __cplusplus
}
#endif

#endif /* M2_CAN_FRAME_H */
