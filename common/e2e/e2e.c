#include "e2e.h"

#include "crc32p4.h"

#include <string.h>

#define E2E_LENGTH_OFFSET  0U
#define E2E_COUNTER_OFFSET 2U
#define E2E_DATA_ID_OFFSET 4U
#define E2E_CRC_OFFSET     8U
#define E2E_PAYLOAD_OFFSET E2E_HEADER_SIZE
#define E2E_CRC_PREFIX_LEN E2E_CRC_OFFSET
#define E2E_CRC_INPUT_MAX  (E2E_CRC_PREFIX_LEN + E2E_MAX_PAYLOAD_SIZE)

static void write_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)(value >> 8U);
}

static uint16_t read_u16_le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8U));
}

static void write_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
    dst[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0]
        | ((uint32_t)src[1] << 8U)
        | ((uint32_t)src[2] << 16U)
        | ((uint32_t)src[3] << 24U);
}

static bool calculate_e2e_crc(
    const uint8_t *protected_data,
    size_t payload_length,
    uint32_t *crc_out
)
{
    uint8_t crc_input[E2E_CRC_INPUT_MAX];
    const size_t crc_input_length = E2E_CRC_PREFIX_LEN + payload_length;

    memcpy(crc_input, protected_data, E2E_CRC_PREFIX_LEN);
    if (payload_length != 0U) {
        memcpy(
            &crc_input[E2E_CRC_PREFIX_LEN],
            &protected_data[E2E_PAYLOAD_OFFSET],
            payload_length
        );
    }

    return crc32p4_compute(crc_input, crc_input_length, crc_out);
}

bool e2e_protect(
    uint8_t *protected_data,
    size_t protected_capacity,
    const uint8_t *payload,
    size_t payload_length,
    uint16_t counter,
    uint32_t data_id,
    size_t *protected_length
)
{
    if (protected_data == NULL || protected_length == NULL
        || (payload == NULL && payload_length != 0U)
        || payload_length > E2E_MAX_PAYLOAD_SIZE) {
        return false;
    }

    const size_t total_length = E2E_HEADER_SIZE + payload_length;
    if (protected_capacity < total_length) {
        return false;
    }

    /* Copy first so a payload that overlaps the output buffer remains valid. */
    if (payload_length != 0U) {
        memmove(&protected_data[E2E_PAYLOAD_OFFSET], payload, payload_length);
    }

    write_u16_le(&protected_data[E2E_LENGTH_OFFSET], (uint16_t)total_length);
    write_u16_le(&protected_data[E2E_COUNTER_OFFSET], counter);
    write_u32_le(&protected_data[E2E_DATA_ID_OFFSET], data_id);
    write_u32_le(&protected_data[E2E_CRC_OFFSET], 0U);

    uint32_t crc = 0U;
    if (!calculate_e2e_crc(protected_data, payload_length, &crc)) {
        return false;
    }

    write_u32_le(&protected_data[E2E_CRC_OFFSET], crc);
    *protected_length = total_length;
    return true;
}

void e2e_rx_reset(e2e_rx_state_t *state, uint32_t now)
{
    if (state == NULL) {
        return;
    }

    state->last_counter = 0U;
    state->started_at = now;
    state->last_accepted_at = 0U;
    state->has_last_counter = false;
}

e2e_status_t e2e_rx_verify(
    e2e_rx_state_t *state,
    const uint8_t *protected_data,
    size_t protected_length,
    size_t expected_payload_length,
    uint32_t expected_data_id,
    uint32_t now,
    const uint8_t **payload_out
)
{
    if (payload_out != NULL) {
        *payload_out = NULL;
    }

    if (state == NULL || protected_data == NULL
        || expected_payload_length > E2E_MAX_PAYLOAD_SIZE) {
        return E2E_STATUS_INVALID_ARGUMENT;
    }

    const size_t expected_length = E2E_HEADER_SIZE + expected_payload_length;
    if (protected_length != expected_length
        || protected_length > E2E_MAX_DATA_SIZE
        || read_u16_le(&protected_data[E2E_LENGTH_OFFSET]) != protected_length
        || read_u32_le(&protected_data[E2E_DATA_ID_OFFSET]) != expected_data_id) {
        return E2E_STATUS_LEN_OR_ID_ERR;
    }

    uint32_t calculated_crc = 0U;
    if (!calculate_e2e_crc(protected_data, expected_payload_length, &calculated_crc)
        || calculated_crc != read_u32_le(&protected_data[E2E_CRC_OFFSET])) {
        return E2E_STATUS_CRC_ERR;
    }

    const uint16_t counter = read_u16_le(&protected_data[E2E_COUNTER_OFFSET]);
    if (state->has_last_counter) {
        const uint16_t delta = (uint16_t)(counter - state->last_counter);
        if (delta == 0U) {
            return E2E_STATUS_REPEATED;
        }
        if (delta > E2E_MAX_COUNTER_DELTA) {
            return E2E_STATUS_WRONG_SEQ;
        }
    }

    state->last_counter = counter;
    state->last_accepted_at = now;
    state->has_last_counter = true;
    if (payload_out != NULL) {
        *payload_out = &protected_data[E2E_PAYLOAD_OFFSET];
    }
    return E2E_STATUS_OK;
}

e2e_status_t e2e_rx_check_timeout(
    const e2e_rx_state_t *state,
    uint32_t now,
    uint32_t timeout_ticks
)
{
    if (state == NULL || timeout_ticks == 0U || timeout_ticks >= 0x80000000UL) {
        return E2E_STATUS_INVALID_ARGUMENT;
    }

    const uint32_t reference_time = state->has_last_counter
        ? state->last_accepted_at
        : state->started_at;
    const uint32_t elapsed = now - reference_time;
    return (elapsed >= timeout_ticks) ? E2E_STATUS_TIMEOUT : E2E_STATUS_OK;
}
