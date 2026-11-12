#ifndef M2_E2E_H
#define M2_E2E_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define E2E_HEADER_SIZE       12U
#define E2E_MAX_DATA_SIZE     64U
#define E2E_MAX_PAYLOAD_SIZE  (E2E_MAX_DATA_SIZE - E2E_HEADER_SIZE)
#define E2E_MAX_COUNTER_DELTA 2U

typedef enum {
    E2E_STATUS_OK = 0,
    E2E_STATUS_REPEATED,
    E2E_STATUS_WRONG_SEQ,
    E2E_STATUS_CRC_ERR,
    E2E_STATUS_LEN_OR_ID_ERR,
    E2E_STATUS_TIMEOUT,
    E2E_STATUS_INVALID_ARGUMENT
} e2e_status_t;

typedef struct {
    uint16_t last_counter;
    uint32_t started_at;
    uint32_t last_accepted_at;
    bool has_last_counter;
} e2e_rx_state_t;

/*
 * Builds [length:u16 LE][counter:u16 LE][data_id:u32 LE][crc32:u32 LE]
 * [application payload]. Padding is added by the outer frame layer.
 * `payload` may point inside `protected_data`; overlapping copies are supported.
 */
bool e2e_protect(
    uint8_t *protected_data,
    size_t protected_capacity,
    const uint8_t *payload,
    size_t payload_length,
    uint16_t counter,
    uint32_t data_id,
    size_t *protected_length
);

/*
 * Resets the receiver's sequence baseline and starts timeout grace at `now`.
 * The caller supplies a wrapping uint32 monotonic tick domain.
 */
void e2e_rx_reset(e2e_rx_state_t *state, uint32_t now);

/*
 * Validates one unpadded E2E data block. `expected_payload_length` and
 * `expected_data_id` come from the message definition/outer CAN ID. Only an
 * E2E_STATUS_OK frame advances the sequence and freshness timestamp.
 * `payload_out` is optional and points into `protected_data` on success.
 */
e2e_status_t e2e_rx_verify(
    e2e_rx_state_t *state,
    const uint8_t *protected_data,
    size_t protected_length,
    size_t expected_payload_length,
    uint32_t expected_data_id,
    uint32_t now,
    const uint8_t **payload_out
);

/* Returns E2E_STATUS_TIMEOUT once the interval has elapsed; timeout does not
 * reset the sequence state. `timeout_ticks` must be in (0, 2^31). */
e2e_status_t e2e_rx_check_timeout(
    const e2e_rx_state_t *state,
    uint32_t now,
    uint32_t timeout_ticks
);

#ifdef __cplusplus
}
#endif

#endif /* M2_E2E_H */
