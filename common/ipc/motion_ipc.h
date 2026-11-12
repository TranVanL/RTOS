#ifndef M3_MOTION_IPC_H
#define M3_MOTION_IPC_H

/*
 * /dev/motion devctl contract (ARCH 3.3), OS-free. The QNX resmgr wraps the
 * command numbers with __DIOT/__DIOF and maps motion_result_t to
 * EOK/EPERM/EINVAL. Validation lives here so it is host-testable.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOTION_CMD_GET_STATUS = 1,
    MOTION_CMD_SET_TARGET,
    MOTION_CMD_ENABLE,
    MOTION_CMD_DISABLE,
    MOTION_CMD_ESTOP,
    MOTION_CMD_RESET_FAULT,
    MOTION_CMD_COUNT_
} motion_cmd_t;

typedef enum {
    MOTION_OK = 0,
    MOTION_EPERM,     /* caller lacks required privilege  */
    MOTION_EINVAL,    /* bad command, size or value       */
    MOTION_ESTATE     /* valid but not allowed in state   */
} motion_result_t;

/* Provisional limits (counts / rpm); same units as DBC CMD signals. */
#define MOTION_POS_MIN (-1000000)
#define MOTION_POS_MAX ( 1000000)
#define MOTION_VEL_MAX 3000U

typedef struct {
    int32_t  target_pos;
    uint32_t vel_limit;
} motion_set_target_t;

typedef struct {
    uint32_t fsm_state;
    uint32_t fsm_cause;
    uint32_t tick;
    uint32_t hb_missed;
    uint32_t e2e_errors;
    int32_t  position;
} motion_status_t;

/* true if the command needs membership of group `motion`. */
bool motion_cmd_needs_group(uint32_t cmd);

/*
 * Checks command id, caller privilege, payload size and values, in that order
 * (unknown command -> EINVAL first; then EPERM; then size/values -> EINVAL).
 * `payload` may be NULL when `nbytes` is 0.
 */
motion_result_t motion_validate_request(uint32_t cmd, bool caller_in_group,
                                        const void *payload, size_t nbytes);

/* Expected input payload size for a command (0 for none). */
size_t motion_cmd_in_size(uint32_t cmd);

#ifdef __cplusplus
}
#endif

#endif
