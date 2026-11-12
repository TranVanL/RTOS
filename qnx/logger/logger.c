/* logger (M3 WP5): read-only consumer of the `state` SHM. Opens /motion_state O_RDONLY and maps it
 * PROT_READ, so it cannot corrupt control state. Writes a text event log with size-based rotation.
 *   logger --out PATH [--max-bytes N] [--duration-ms D] [--period-ms P] [--crash-after MS]
 * --crash-after is a test hook (null-pointer write) used to show crash detection. */
#include "motion_shm.h"
#include "safety_fsm.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static unsigned long ms_now(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long)(t.tv_sec * 1000UL + (unsigned long)t.tv_nsec / 1000000UL);
}

int main(int argc, char **argv)
{
    const char *out = "/tmp/motion.log"; unsigned long max_bytes = 65536, duration = 0, period = 20, crash_after = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--out")) out = argv[i + 1];
        else if (!strcmp(argv[i], "--max-bytes")) max_bytes = strtoul(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--duration-ms")) duration = strtoul(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--period-ms")) period = strtoul(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--crash-after")) crash_after = strtoul(argv[i + 1], NULL, 10);
        else { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
    }
    int fd = shm_open("/motion_state", O_RDONLY, 0);
    if (fd < 0) { perror("logger: shm_open O_RDONLY"); return 2; }
    const motion_shm_t *shm = mmap(NULL, sizeof(motion_shm_t), PROT_READ, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("logger: mmap"); return 2; }
    if (!motion_shm_validate(shm)) { fprintf(stderr, "logger: bad SHM magic/version\n"); return 2; }

    char rotated[256]; snprintf(rotated, sizeof rotated, "%s.1", out);
    FILE *f = fopen(out, "w"); if (!f) { perror("logger: fopen"); return 2; }
    unsigned long start = ms_now(), written = 0, lines = 0, rotations = 0, failed = 0, samples = 0;
    int last_state = -1, last_cause = -1; uint32_t last_tick = 0;

    while (duration == 0 || ms_now() - start < duration) {
        if (crash_after && ms_now() - start >= crash_after) {
            printf("logger: crashing on purpose\n"); fflush(stdout);
            *(volatile int *)0 = 1;
        }
        motion_state_t s; uint32_t seq = 0;
        if (motion_shm_read(shm, &s, 100, &seq)) {
            bool changed = (int)s.fsm_state != last_state || (int)s.fsm_cause != last_cause;
            if (changed || samples % 5 == 0) {
                int n = fprintf(f, "t=%lu seq=%u tick=%u state=%s cause=%u hb_miss=%u e2e=%u pos=%d%s\n",
                                ms_now() - start, seq, s.tick, safety_state_name((safety_state_t)s.fsm_state),
                                s.fsm_cause, s.hb_missed, s.e2e_errors, s.position, changed ? " CHANGE" : "");
                if (n > 0) { written += (unsigned long)n; lines++; }
                fflush(f);
                if (written >= max_bytes) {
                    fclose(f); rename(out, rotated); f = fopen(out, "w"); written = 0; rotations++;
                    if (!f) { perror("logger: reopen"); return 2; }
                }
            }
            last_state = (int)s.fsm_state; last_cause = (int)s.fsm_cause; last_tick = s.tick;
        } else {
            failed++;
        }
        samples++;
        struct timespec ts = { (time_t)(period / 1000), (long)(period % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }
    fclose(f);
    printf("LOGGER samples=%lu lines=%lu rotations=%lu failed_reads=%lu last_tick=%u\n",
           samples, lines, rotations, failed, last_tick);
    return 0;
}
