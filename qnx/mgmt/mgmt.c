/* mgmt (M3 WP5): management tool. Reads the `state` SHM read-only and forwards valid commands to
 * motion_sup over /dev/motion (devctl = MsgSend). It never writes the SHM.
 *   mgmt status                 print state from SHM
 *   mgmt forward estop|status   send via /dev/motion
 *   mgmt openrw                 try shm_open(O_RDWR): non-owner must fail (EACCES)
 *   mgmt poke [AFTER_MS]        INTENTIONAL fault F14: write to the read-only mapping -> SIGSEGV */
#include "motion_dcmd.h"
#include "motion_shm.h"
#include "safety_fsm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const motion_shm_t *map_ro(void)
{
    int fd = shm_open("/motion_state", O_RDONLY, 0);
    if (fd < 0) { perror("mgmt: shm_open O_RDONLY"); exit(2); }
    const motion_shm_t *p = mmap(NULL, sizeof(motion_shm_t), PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { perror("mgmt: mmap PROT_READ"); exit(2); }
    return p;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: mgmt status|forward estop|status|openrw|poke [ms]\n"); return 2; }
    if (!strcmp(argv[1], "status")) {
        const motion_shm_t *shm = map_ro(); motion_state_t s;
        if (!motion_shm_validate(shm) || !motion_shm_read(shm, &s, 100, NULL)) { puts("mgmt status: unavailable"); return 1; }
        printf("MGMT state=%s cause=%u tick=%u hb_miss=%u e2e=%u pos=%d\n",
               safety_state_name((safety_state_t)s.fsm_state), s.fsm_cause, s.tick, s.hb_missed, s.e2e_errors, s.position);
        return 0;
    }
    if (!strcmp(argv[1], "forward") && argc == 3) {
        int fd = open(MOTION_DEVICE_PATH, O_RDWR); int rc;
        if (fd < 0) { perror("mgmt: open /dev/motion"); return 3; }
        if (!strcmp(argv[2], "estop")) { rc = devctl(fd, DCMD_MOTION_ESTOP, NULL, 0, NULL); }
        else { motion_status_t st; rc = devctl(fd, DCMD_MOTION_GET_STATUS, &st, sizeof st, NULL); }
        printf("MGMT forward %s: rc=%d (%s)\n", argv[2], rc, strerror(rc));
        return rc;
    }
    if (!strcmp(argv[1], "openrw")) {
        int fd = shm_open("/motion_state", O_RDWR, 0);
        printf("MGMT openrw: %s (errno=%d)\n", fd < 0 ? "denied" : "ALLOWED", fd < 0 ? errno : 0);
        return fd < 0 ? 0 : 1;
    }
    if (!strcmp(argv[1], "poke")) {
        const motion_shm_t *shm = map_ro();
        if (argc == 3) { usleep((useconds_t)atol(argv[2]) * 1000U); }
        printf("MGMT poking read-only SHM (expect SIGSEGV)\n"); fflush(stdout);
        ((motion_shm_t *)shm)->reserved = 0xDEADBEEFu;   /* intentional: write through PROT_READ mapping */
        printf("MGMT poke: no fault (BUG)\n");
        return 1;
    }
    return 2;
}
