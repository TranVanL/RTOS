#ifndef M3_PROTO_LINK_H
#define M3_PROTO_LINK_H

/*
 * OS-free glue over the M2 codecs: DBC payload <-> E2E <-> outer frame <-> UDP
 * datagram. Used by motion_sup, the fake peer and host tests. No sockets here.
 */

#include "e2e.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_DATAGRAM_MAX 70U   /* 6 B header + 64 B data */
#define PROTO_PAYLOAD_MAX  E2E_MAX_PAYLOAD_SIZE

typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_ARGUMENT,
    PROTO_ERR_UNKNOWN_ID,
    PROTO_ERR_PAYLOAD_LENGTH,
    PROTO_ERR_FRAME,        /* outer frame rejected                        */
    PROTO_ERR_E2E_CRC,
    PROTO_ERR_E2E_SEQ,      /* REPEATED or WRONG_SEQ                       */
    PROTO_ERR_E2E_OTHER
} proto_result_t;

/* Builds one datagram for a DBC message. payload_len must equal the DBC
 * payload length of can_id. */
proto_result_t proto_encode(uint16_t can_id, const uint8_t *payload, size_t payload_len,
                            uint16_t counter, uint8_t *datagram, size_t capacity,
                            size_t *datagram_len);

/* CAN ID of a datagram without validating it (needed to pick the RX state). */
int proto_peek_can_id(const uint8_t *datagram, size_t len, uint16_t *can_id);

/* Validates frame + E2E and copies the application payload out. `rx` is the
 * receiver state of that CAN ID. Only PROTO_OK refreshes the state. */
proto_result_t proto_decode(const uint8_t *datagram, size_t len, e2e_rx_state_t *rx,
                            uint32_t now, uint16_t *can_id_out,
                            uint8_t payload_out[PROTO_PAYLOAD_MAX], size_t *payload_len);

#ifdef __cplusplus
}
#endif

#endif
