#include "can_matrix.h"
#include "can_matrix_meta.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message)                                              \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (message), __FILE__, __LINE__); \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int test_estop(void)
{
    const struct can_matrix_estop_t input = { .command = 255U, .reason = 0U };
    struct can_matrix_estop_t output = { 0U, 0U };
    uint8_t payload[CAN_MATRIX_ESTOP_LENGTH];
    CHECK(can_matrix_estop_pack(payload, &input, sizeof(payload)) == (int)sizeof(payload),
          "ESTOP pack failed");
    CHECK(can_matrix_estop_unpack(&output, payload, sizeof(payload)) == 0,
          "ESTOP unpack failed");
    CHECK(output.command == input.command && output.reason == input.reason,
          "ESTOP payload mismatch");
    return 0;
}

static int test_cmd(void)
{
    const struct can_matrix_cmd_t input = {
        .mode = 3U,
        .vel_limit = 65535U,
        .target_pos = INT32_MIN,
    };
    struct can_matrix_cmd_t output = { 0U, 0U, 0 };
    uint8_t payload[CAN_MATRIX_CMD_LENGTH];
    CHECK(can_matrix_cmd_pack(payload, &input, sizeof(payload)) == (int)sizeof(payload),
          "CMD pack failed");
    CHECK(can_matrix_cmd_unpack(&output, payload, sizeof(payload)) == 0,
          "CMD unpack failed");
    CHECK(output.mode == input.mode && output.vel_limit == input.vel_limit
              && output.target_pos == input.target_pos,
          "CMD payload mismatch");
    return 0;
}

static int test_telem(void)
{
    const struct can_matrix_telem_t input = {
        .position = INT32_MAX,
        .velocity = INT16_MIN,
        .current = INT16_MAX,
        .output_state = 1U,
    };
    struct can_matrix_telem_t output = { 0, 0, 0, 0U };
    uint8_t payload[CAN_MATRIX_TELEM_LENGTH];
    CHECK(can_matrix_telem_pack(payload, &input, sizeof(payload)) == (int)sizeof(payload),
          "TELEM pack failed");
    CHECK(can_matrix_telem_unpack(&output, payload, sizeof(payload)) == 0,
          "TELEM unpack failed");
    CHECK(output.position == input.position && output.velocity == input.velocity
              && output.current == input.current && output.output_state == input.output_state,
          "TELEM payload mismatch");
    return 0;
}

static int test_heartbeat(void)
{
    const struct can_matrix_heartbeat_t input = {
        .node_state = 5U,
        .alive_counter = 255U,
        .cpu_load = 100U,
        .fault_code = 65535U,
    };
    struct can_matrix_heartbeat_t output = { 0U, 0U, 0U, 0U };
    uint8_t payload[CAN_MATRIX_HEARTBEAT_LENGTH];
    CHECK(can_matrix_heartbeat_pack(payload, &input, sizeof(payload)) == (int)sizeof(payload),
          "HEARTBEAT pack failed");
    CHECK(can_matrix_heartbeat_unpack(&output, payload, sizeof(payload)) == 0,
          "HEARTBEAT unpack failed");
    CHECK(output.node_state == input.node_state && output.alive_counter == input.alive_counter
              && output.cpu_load == input.cpu_load && output.fault_code == input.fault_code,
          "HEARTBEAT payload mismatch");
    return 0;
}

static int test_fault_record_and_metadata(void)
{
    const struct can_matrix_fault_record_t input = {
        .pc = 0xFFFFFFFFUL,
        .lr = 0x80000000UL,
        .cfsr = 0x12345678UL,
        .mmfar = 0U,
    };
    struct can_matrix_fault_record_t output = { 0U, 0U, 0U, 0U };
    uint8_t payload[CAN_MATRIX_FAULT_RECORD_LENGTH];
    CHECK(can_matrix_fault_record_pack(payload, &input, sizeof(payload)) == (int)sizeof(payload),
          "FAULT_RECORD pack failed");
    CHECK(can_matrix_fault_record_unpack(&output, payload, sizeof(payload)) == 0,
          "FAULT_RECORD unpack failed");
    CHECK(output.pc == input.pc && output.lr == input.lr && output.cfsr == input.cfsr
              && output.mmfar == input.mmfar,
          "FAULT_RECORD payload mismatch");

    const can_matrix_message_info_t *estop = can_matrix_find_message(0x010U);
    const can_matrix_message_info_t *fault = can_matrix_find_message(0x1F1U);
    CHECK(estop != NULL && estop->udp_port == 5001U && estop->timeout_ms == 0U,
          "ESTOP generated metadata mismatch");
    CHECK(fault != NULL && fault->udp_port == 5000U && fault->timeout_ms == 0U,
          "FAULT_RECORD generated metadata mismatch");
    CHECK(can_matrix_message_count() == 5U && can_matrix_message_at(5U) == NULL,
          "generated message table size/index mismatch");
    return 0;
}

int main(void)
{
    if (test_estop() != 0 || test_cmd() != 0 || test_telem() != 0
        || test_heartbeat() != 0 || test_fault_record_and_metadata() != 0) {
        return 1;
    }

    puts("test_dbc_generated: PASS");
    return 0;
}
