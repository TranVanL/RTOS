#include "crc32p4.h"
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

static int test_crc32p4_check_value(void)
{
    static const uint8_t input[] = "123456789";
    uint32_t crc = 0U;

    CHECK(crc32p4_compute(input, sizeof(input) - 1U, &crc), "CRC calculation failed");
    CHECK(crc == 0x1697D06AU, "CRC-32P4 check value mismatch");
    CHECK(!crc32p4_compute(NULL, 1U, &crc), "NULL CRC input was accepted");
    CHECK(!crc32p4_compute(input, sizeof(input) - 1U, NULL), "NULL CRC output was accepted");
    return 0;
}

static int test_e2e_golden_vector(void)
{
      static const uint8_t payload[] = { 0x01U, 0x02U };
      static const uint8_t expected[] = {
            0x0EU, 0x00U, 0x34U, 0x12U, 0x10U, 0x00U, 0x00U, 0x00U,
            0x34U, 0x83U, 0x47U, 0x2AU, 0x01U, 0x02U
      };
      uint8_t protected_data[E2E_MAX_DATA_SIZE];
      size_t protected_length = 0U;

      CHECK(e2e_protect(protected_data, sizeof(protected_data), payload, sizeof(payload),
                                0x1234U, 0x10U, &protected_length), "golden-vector protect failed");
      CHECK(protected_length == sizeof(expected), "golden-vector length mismatch");
      CHECK(memcmp(protected_data, expected, sizeof(expected)) == 0,
              "E2E bytes differ from independent golden vector");
      return 0;
}

static int test_protect_verify_sequence_and_errors(void)
{
    const uint8_t payload[] = { 0xA5U, 0x00U, 0x7FU, 0xFEU };
    uint8_t protected_data[E2E_MAX_DATA_SIZE];
    size_t protected_length = 0U;
    const uint32_t data_id = 0x00000010U;
    e2e_rx_state_t state;
    const uint8_t *decoded = NULL;

    e2e_rx_reset(&state, 100U);
    CHECK(e2e_protect(protected_data, sizeof(protected_data), payload, sizeof(payload),
                      65535U, data_id, &protected_length), "protect failed");
    CHECK(protected_length == E2E_HEADER_SIZE + sizeof(payload), "protected length mismatch");
    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        data_id, 101U, &decoded) == E2E_STATUS_OK,
          "first valid frame was not accepted");
    CHECK(decoded != NULL && memcmp(decoded, payload, sizeof(payload)) == 0,
          "decoded payload mismatch");
    CHECK(state.last_counter == 65535U && state.last_accepted_at == 101U,
          "first accepted frame did not update receiver state");

    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        data_id, 102U, NULL) == E2E_STATUS_REPEATED,
          "duplicate counter was not rejected as repeated");
    CHECK(state.last_counter == 65535U && state.last_accepted_at == 101U,
          "repeated frame changed receiver state");

    CHECK(e2e_protect(protected_data, sizeof(protected_data), payload, sizeof(payload),
                      1U, data_id, &protected_length), "wrap protect failed");
    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        data_id, 103U, NULL) == E2E_STATUS_OK,
          "counter delta 2 across wrap was not accepted");
    CHECK(state.last_counter == 1U && state.last_accepted_at == 103U,
          "accepted wrapped counter did not update state");

    CHECK(e2e_protect(protected_data, sizeof(protected_data), payload, sizeof(payload),
                      2U, data_id, &protected_length), "delta-one protect failed");
    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        data_id, 104U, NULL) == E2E_STATUS_OK,
          "counter delta 1 was not accepted");

    CHECK(e2e_protect(protected_data, sizeof(protected_data), payload, sizeof(payload),
                      5U, data_id, &protected_length), "wrong-sequence protect failed");
    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        data_id, 105U, NULL) == E2E_STATUS_WRONG_SEQ,
          "counter delta 3 was not rejected");
    CHECK(state.last_counter == 2U && state.last_accepted_at == 104U,
          "wrong-sequence frame changed receiver state");

    CHECK(e2e_protect(protected_data, sizeof(protected_data), payload, sizeof(payload),
                      3U, data_id, &protected_length), "CRC error protect failed");
    protected_data[E2E_HEADER_SIZE] ^= 0x01U;
    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        data_id, 106U, NULL) == E2E_STATUS_CRC_ERR,
          "corrupt payload was not detected");
    CHECK(state.last_counter == 2U && state.last_accepted_at == 104U,
          "CRC-error frame changed receiver state");

    CHECK(e2e_protect(protected_data, sizeof(protected_data), payload, sizeof(payload),
                      3U, data_id, &protected_length), "ID error protect failed");
    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        data_id + 1U, 107U, NULL) == E2E_STATUS_LEN_OR_ID_ERR,
          "wrong expected data ID was not rejected");
    protected_data[0] ^= 1U;
    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        data_id, 107U, NULL) == E2E_STATUS_LEN_OR_ID_ERR,
          "wrong declared length was not rejected");
    CHECK(state.last_counter == 2U && state.last_accepted_at == 104U,
          "length/ID error changed receiver state");

    CHECK(!e2e_protect(protected_data, E2E_HEADER_SIZE, payload, sizeof(payload),
                       0U, data_id, &protected_length), "insufficient output capacity accepted");
    CHECK(!e2e_protect(protected_data, sizeof(protected_data), NULL, 1U,
                       0U, data_id, &protected_length), "NULL payload accepted");
    CHECK(e2e_rx_verify(NULL, protected_data, protected_length, sizeof(payload),
                        data_id, 0U, NULL) == E2E_STATUS_INVALID_ARGUMENT,
          "NULL receiver state was accepted");
    return 0;
}

static int test_timeout_and_reset(void)
{
    e2e_rx_state_t state;
    uint8_t protected_data[E2E_MAX_DATA_SIZE];
    size_t protected_length = 0U;
    const uint8_t payload[] = { 0x42U };

    e2e_rx_reset(&state, 0xFFFFFFF0U);
    CHECK(e2e_rx_check_timeout(&state, 0x0000000DU, 30U) == E2E_STATUS_OK,
          "startup grace expired one tick early across wrap");
    CHECK(e2e_rx_check_timeout(&state, 0x0000000EU, 30U) == E2E_STATUS_TIMEOUT,
          "startup timeout did not expire at threshold across wrap");
    CHECK(e2e_rx_check_timeout(&state, 0U, 0U) == E2E_STATUS_INVALID_ARGUMENT,
          "zero timeout was accepted");
    CHECK(e2e_rx_check_timeout(&state, 0U, 0x80000000U) == E2E_STATUS_INVALID_ARGUMENT,
          "ambiguous half-range timeout was accepted");

    CHECK(e2e_protect(protected_data, sizeof(protected_data), payload, sizeof(payload),
                      10U, 0x100U, &protected_length), "timeout recovery protect failed");
    CHECK(e2e_rx_verify(&state, protected_data, protected_length, sizeof(payload),
                        0x100U, 0x0000000EU, NULL) == E2E_STATUS_OK,
          "valid frame after timeout was not accepted as first frame after reset");
    CHECK(e2e_rx_check_timeout(&state, 0x0000002BU, 30U) == E2E_STATUS_OK,
          "valid frame did not refresh timeout reference");
    CHECK(e2e_rx_check_timeout(&state, 0x0000002CU, 30U) == E2E_STATUS_TIMEOUT,
          "timeout after accepted frame did not expire at threshold");
    return 0;
}

int main(void)
{
    if (test_crc32p4_check_value() != 0
            || test_e2e_golden_vector() != 0
        || test_protect_verify_sequence_and_errors() != 0
        || test_timeout_and_reset() != 0) {
        return 1;
    }

    puts("test_e2e: PASS");
    return 0;
}
