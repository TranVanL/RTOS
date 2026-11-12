#include "motion_ipc.h"

#include <string.h>

bool motion_cmd_needs_group(uint32_t cmd)
{
    return cmd == MOTION_CMD_SET_TARGET || cmd == MOTION_CMD_ENABLE ||
           cmd == MOTION_CMD_DISABLE || cmd == MOTION_CMD_RESET_FAULT;
}

size_t motion_cmd_in_size(uint32_t cmd)
{
    return cmd == MOTION_CMD_SET_TARGET ? sizeof(motion_set_target_t) : 0U;
}

motion_result_t motion_validate_request(uint32_t cmd, bool caller_in_group,
                                        const void *payload, size_t nbytes)
{
    if (cmd < MOTION_CMD_GET_STATUS || cmd >= MOTION_CMD_COUNT_) {
        return MOTION_EINVAL;
    }
    if (motion_cmd_needs_group(cmd) && !caller_in_group) {
        return MOTION_EPERM;
    }
    if (nbytes != motion_cmd_in_size(cmd)) {
        return MOTION_EINVAL;
    }
    if (cmd == MOTION_CMD_SET_TARGET) {
        motion_set_target_t req;
        if (payload == NULL) {
            return MOTION_EINVAL;
        }
        memcpy(&req, payload, sizeof req);
        if (req.target_pos < MOTION_POS_MIN || req.target_pos > MOTION_POS_MAX ||
            req.vel_limit > MOTION_VEL_MAX) {
            return MOTION_EINVAL;
        }
    }
    return MOTION_OK;
}
