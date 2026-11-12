#ifndef M3_MOTION_SHM_H
#define M3_MOTION_SHM_H

/*
 * Single-writer seqlock for the `state` shared-memory object (ARCH 3.1).
 * OS-free (C11 atomics only). The writer maps read/write; readers map the
 * same object O_RDONLY/PROT_READ and never write: motion_shm_read() and
 * motion_shm_validate() take a const pointer.
 * All payload fields are 32-bit so the payload is copied as atomic words.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_SHM_MAGIC   0x4D534D31UL  /* 'MSM1' */
#define MOTION_SHM_VERSION 1UL
#define MOTION_STATE_WORDS 12U

typedef struct {
    uint32_t fsm_state;      /* safety_state_t                    */
    uint32_t fsm_cause;      /* safety_cause_t                    */
    uint32_t node_state;     /* last heartbeat NodeState          */
    uint32_t fault_code;     /* last heartbeat FaultCode          */
    uint32_t tick;           /* supervisor tick (10 ms pulses)    */
    uint32_t hb_good;        /* valid heartbeats accepted         */
    uint32_t hb_missed;      /* heartbeat misses (cumulative)     */
    uint32_t e2e_errors;     /* E2E errors (cumulative)           */
    int32_t  target_pos;     /* count                             */
    int32_t  position;       /* count (telemetry)                 */
    int32_t  velocity;       /* telemetry                         */
    uint32_t cmd_counter;    /* E2E counter of last CMD sent      */
} motion_state_t;

_Static_assert(sizeof(motion_state_t) == MOTION_STATE_WORDS * sizeof(uint32_t),
               "motion_state_t must be exactly MOTION_STATE_WORDS 32-bit words");

typedef struct {
    uint32_t magic;
    uint32_t version;
    _Atomic uint32_t seq;    /* odd while the writer is inside an update */
    uint32_t reserved;
    _Atomic uint32_t words[MOTION_STATE_WORDS];
} motion_shm_t;

/* Writer only, before readers attach. Zeroes the state, seq = 0. */
void motion_shm_init(motion_shm_t *shm);

/* Reader-safe check of magic/version. */
bool motion_shm_validate(const motion_shm_t *shm);

/* Writer only (exactly one writer thread/process). */
void motion_shm_write(motion_shm_t *shm, const motion_state_t *state);

/*
 * Lock-free snapshot. Retries while the writer is mid-update; returns false
 * after `max_retries` failed attempts (never blocks), or on bad arguments.
 * `*seq_out` (optional) receives the even sequence of the snapshot.
 */
bool motion_shm_read(const motion_shm_t *shm, motion_state_t *out,
                     unsigned max_retries, uint32_t *seq_out);

#ifdef __cplusplus
}
#endif

#endif
