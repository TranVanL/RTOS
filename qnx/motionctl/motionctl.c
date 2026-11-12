/* motionctl: client for /dev/motion. Usage:
 *   motionctl status | target <pos> <vel> | enable | disable | estop | reset
 *   motionctl rawtarget <pos> <vel> <nbytes>   (test: wrong payload size)
 *   motionctl rawcmd <dcmd-index>              (test: unknown command)
 * Prints "motionctl <cmd>: rc=<errno> (<text>)"; exit code = rc. */
#include "motion_dcmd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *state_name(uint32_t s)
{
    static const char *n[] = { "INIT", "READY", "RUN", "DEGRADED", "SAFE_STOP" };
    return s < 5 ? n[s] : "?";
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: motionctl status|target P V|enable|disable|estop|reset\n"); return 2; }
    int fd = open(MOTION_DEVICE_PATH, O_RDWR);
    if (fd < 0) { perror("open " MOTION_DEVICE_PATH); return 3; }
    const char *c = argv[1];
    int rc = EINVAL;

    if (!strcmp(c, "status")) {
        motion_status_t st; memset(&st, 0, sizeof st);
        rc = devctl(fd, DCMD_MOTION_GET_STATUS, &st, sizeof st, NULL);
        if (rc == EOK) {
            printf("STATUS state=%s cause=%u tick=%u hb_missed=%u e2e_errors=%u position=%d\n",
                   state_name(st.fsm_state), st.fsm_cause, st.tick, st.hb_missed, st.e2e_errors, st.position);
        }
    } else if (!strcmp(c, "target") && argc == 4) {
        motion_set_target_t t = { (int32_t)atol(argv[2]), (uint32_t)atol(argv[3]) };
        rc = devctl(fd, DCMD_MOTION_SET_TARGET, &t, sizeof t, NULL);
    } else if (!strcmp(c, "rawtarget") && argc == 5) {
        char buf[64] = { 0 }; motion_set_target_t t = { (int32_t)atol(argv[2]), (uint32_t)atol(argv[3]) };
        memcpy(buf, &t, sizeof t);
        rc = devctl(fd, DCMD_MOTION_SET_TARGET, buf, (size_t)atoi(argv[4]), NULL);
    } else if (!strcmp(c, "rawcmd") && argc == 3) {
        char buf[8] = { 0 };
        rc = devctl(fd, __DION(_DCMD_MISC, 0x60 + atoi(argv[2])), buf, 0, NULL);
    } else if (!strcmp(c, "enable")) {
        rc = devctl(fd, DCMD_MOTION_ENABLE, NULL, 0, NULL);
    } else if (!strcmp(c, "disable")) {
        rc = devctl(fd, DCMD_MOTION_DISABLE, NULL, 0, NULL);
    } else if (!strcmp(c, "estop")) {
        rc = devctl(fd, DCMD_MOTION_ESTOP, NULL, 0, NULL);
    } else if (!strcmp(c, "reset")) {
        rc = devctl(fd, DCMD_MOTION_RESET_FAULT, NULL, 0, NULL);
    }
    printf("motionctl %s: rc=%d (%s)\n", c, rc, strerror(rc));
    close(fd);
    return rc;
}
