#include "can_matrix.h"
#include "can_matrix_meta.h"
#include "proto_link.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
    uint8_t dg[PROTO_DATAGRAM_MAX], pl[PROTO_PAYLOAD_MAX], raw[16];
    size_t dglen = 0, pllen = 0; uint16_t id = 0;
    e2e_rx_state_t rx;
    struct can_matrix_heartbeat_t hb = { .node_state = 2, .alive_counter = 9, .cpu_load = 40, .fault_code = 0x1234 };
    struct can_matrix_heartbeat_t back;

    int n = can_matrix_heartbeat_pack(raw, &hb, sizeof raw);
    CHECK(n == 5);
    e2e_rx_reset(&rx, 0);
    CHECK(proto_encode(0x1F0, raw, 5, 7, dg, sizeof dg, &dglen) == PROTO_OK);
    CHECK(dglen == 6 + 20);
    CHECK(proto_peek_can_id(dg, dglen, &id) && id == 0x1F0);
    CHECK(proto_decode(dg, dglen, &rx, 1, &id, pl, &pllen) == PROTO_OK);
    CHECK(id == 0x1F0 && pllen == 5);
    CHECK(can_matrix_heartbeat_unpack(&back, pl, pllen) == 0);
    CHECK(back.fault_code == 0x1234 && back.alive_counter == 9);
    /* same counter again -> sequence error, state unchanged */
    CHECK(proto_decode(dg, dglen, &rx, 2, &id, pl, &pllen) == PROTO_ERR_E2E_SEQ);
    /* corrupted payload byte -> CRC */
    CHECK(proto_encode(0x1F0, raw, 5, 8, dg, sizeof dg, &dglen) == PROTO_OK);
    dg[6 + 12] ^= 0x01;
    CHECK(proto_decode(dg, dglen, &rx, 3, &id, pl, &pllen) == PROTO_ERR_E2E_CRC);
    /* errors */
    CHECK(proto_encode(0x7FF, raw, 5, 1, dg, sizeof dg, &dglen) == PROTO_ERR_UNKNOWN_ID);
    CHECK(proto_encode(0x1F0, raw, 4, 1, dg, sizeof dg, &dglen) == PROTO_ERR_PAYLOAD_LENGTH);
    CHECK(proto_encode(0x1F0, raw, 5, 1, dg, 10, &dglen) == PROTO_ERR_FRAME);
    CHECK(proto_decode(dg, 3, &rx, 3, &id, pl, &pllen) == PROTO_ERR_FRAME);
    CHECK(!proto_peek_can_id(dg, 3, &id));
    CHECK(proto_encode(0x1F0, NULL, 5, 1, dg, sizeof dg, &dglen) == PROTO_ERR_ARGUMENT);
    if (failures == 0) { printf("proto_link: all tests passed\n"); return 0; }
    return 1;
}
