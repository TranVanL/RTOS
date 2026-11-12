#include "motion_ipc.h"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
    motion_set_target_t t = { 1000, 500 };
    /* EPERM: privileged commands without group (valid payloads) */
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, false, &t, sizeof t) == MOTION_EPERM);
    CHECK(motion_validate_request(MOTION_CMD_ENABLE, false, NULL, 0) == MOTION_EPERM);
    CHECK(motion_validate_request(MOTION_CMD_DISABLE, false, NULL, 0) == MOTION_EPERM);
    CHECK(motion_validate_request(MOTION_CMD_RESET_FAULT, false, NULL, 0) == MOTION_EPERM);
    /* open to everyone */
    CHECK(motion_validate_request(MOTION_CMD_GET_STATUS, false, NULL, 0) == MOTION_OK);
    CHECK(motion_validate_request(MOTION_CMD_ESTOP, false, NULL, 0) == MOTION_OK);
    /* group member */
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, &t, sizeof t) == MOTION_OK);
    CHECK(motion_validate_request(MOTION_CMD_ENABLE, true, NULL, 0) == MOTION_OK);
    CHECK(motion_validate_request(MOTION_CMD_RESET_FAULT, true, NULL, 0) == MOTION_OK);
    /* EINVAL: bad command */
    CHECK(motion_validate_request(0, true, NULL, 0) == MOTION_EINVAL);
    CHECK(motion_validate_request(MOTION_CMD_COUNT_, true, NULL, 0) == MOTION_EINVAL);
    CHECK(motion_validate_request(0xFFFFFFFFu, false, NULL, 0) == MOTION_EINVAL);
    /* EINVAL: size */
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, &t, sizeof t - 1) == MOTION_EINVAL);
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, &t, sizeof t + 1) == MOTION_EINVAL);
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, NULL, 0) == MOTION_EINVAL);
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, NULL, sizeof t) == MOTION_EINVAL);
    CHECK(motion_validate_request(MOTION_CMD_ENABLE, true, &t, sizeof t) == MOTION_EINVAL);
    CHECK(motion_validate_request(MOTION_CMD_GET_STATUS, false, &t, 1) == MOTION_EINVAL);
    /* EINVAL: bounds (inclusive limits accepted) */
    t.target_pos = MOTION_POS_MAX;  t.vel_limit = MOTION_VEL_MAX;
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, &t, sizeof t) == MOTION_OK);
    t.target_pos = MOTION_POS_MIN;
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, &t, sizeof t) == MOTION_OK);
    t.target_pos = MOTION_POS_MAX + 1;
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, &t, sizeof t) == MOTION_EINVAL);
    t.target_pos = MOTION_POS_MIN - 1;
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, &t, sizeof t) == MOTION_EINVAL);
    t.target_pos = 0; t.vel_limit = MOTION_VEL_MAX + 1;
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, true, &t, sizeof t) == MOTION_EINVAL);
    /* permission is checked before values: no group + bad value -> EPERM */
    CHECK(motion_validate_request(MOTION_CMD_SET_TARGET, false, &t, sizeof t) == MOTION_EPERM);
    if (failures == 0) { printf("motion_ipc: all tests passed\n"); return 0; }
    printf("motion_ipc: %d failures\n", failures);
    return 1;
}
