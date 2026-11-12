/* QNX devctl command numbers for /dev/motion (wrap the OS-free ids in motion_ipc.h). */
#ifndef M3_MOTION_DCMD_H
#define M3_MOTION_DCMD_H

#include "motion_ipc.h"

#include <devctl.h>
#include <stdint.h>
#include <sys/dcmd_misc.h>

#define MOTION_DEVICE_PATH "/dev/motion"

#define DCMD_MOTION_GET_STATUS  __DIOF(_DCMD_MISC, 0x60 + MOTION_CMD_GET_STATUS,  motion_status_t)
#define DCMD_MOTION_SET_TARGET  __DIOT(_DCMD_MISC, 0x60 + MOTION_CMD_SET_TARGET,  motion_set_target_t)
#define DCMD_MOTION_ENABLE      __DION(_DCMD_MISC, 0x60 + MOTION_CMD_ENABLE)
#define DCMD_MOTION_DISABLE     __DION(_DCMD_MISC, 0x60 + MOTION_CMD_DISABLE)
#define DCMD_MOTION_ESTOP       __DION(_DCMD_MISC, 0x60 + MOTION_CMD_ESTOP)
#define DCMD_MOTION_RESET_FAULT __DION(_DCMD_MISC, 0x60 + MOTION_CMD_RESET_FAULT)

#endif
