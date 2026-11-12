#ifndef M3_SAFETY_FSM_H
#define M3_SAFETY_FSM_H

/*
 * Supervisor safety FSM (ARCH section 4). OS-free: no clock, no I/O, no malloc.
 * The caller invokes safety_fsm_step() once per 10 ms supervisor tick (timer
 * pulse); all timing is counted in these ticks, never read from a clock.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAFETY_HB_DEGRADED_MISSES  3U   /* RUN -> DEGRADED  (30 ms nominal)  */
#define SAFETY_HB_SAFE_STOP_MISSES 10U  /* DEGRADED -> SAFE_STOP (100 ms)    */
#define SAFETY_HB_RECOVER_GOOD     10U  /* DEGRADED -> RUN                   */
#define SAFETY_E2E_WINDOW_TICKS    10U  /* 100 ms window                     */
#define SAFETY_E2E_ERROR_LIMIT     3U
#define SAFETY_DEGRADED_VEL_PCT    20U

typedef enum {
    SAFETY_INIT = 0,
    SAFETY_READY,
    SAFETY_RUN,
    SAFETY_DEGRADED,
    SAFETY_SAFE_STOP
} safety_state_t;

typedef enum {
    SAFETY_CAUSE_NONE = 0,
    SAFETY_CAUSE_ESTOP,
    SAFETY_CAUSE_LIMIT,
    SAFETY_CAUSE_NODE_FAULT,
    SAFETY_CAUSE_HB_TIMEOUT
} safety_cause_t;

/* Inputs for one tick. Commands are already authorised/validated upstream. */
typedef struct {
    bool self_test_ok;
    bool hb_valid;            /* a valid heartbeat was accepted this tick     */
    uint8_t e2e_errors;       /* E2E errors seen this tick                    */
    bool estop;               /* E-stop request or active level               */
    bool limit_violation;     /* position/speed limit exceeded                */
    bool node_fault;          /* fault record / node reports fault            */
    bool cmd_enable;
    bool cmd_disable;
    bool cmd_reset_fault;
} safety_inputs_t;

typedef struct {
    safety_state_t state;
    safety_cause_t cause;     /* latched while in SAFE_STOP                   */
    bool output_enable;       /* drive output allowed                         */
    uint8_t vel_limit_pct;    /* 100 RUN, 20 DEGRADED, 0 otherwise            */
    bool send_estop;          /* one-tick pulse on entering SAFE_STOP         */
    bool reset_rejected;      /* one-tick pulse: RESET_FAULT refused          */
} safety_outputs_t;

typedef struct {
    safety_state_t state;
    safety_cause_t cause;
    uint8_t misses;           /* consecutive missed heartbeats (saturating)   */
    uint8_t good;             /* consecutive valid heartbeats (saturating)    */
    uint8_t err_ring[SAFETY_E2E_WINDOW_TICKS];
    uint8_t err_pos;
} safety_fsm_t;

void safety_fsm_init(safety_fsm_t *fsm);

/* Returns false (outputs forced to SAFE_STOP values) on NULL arguments. */
bool safety_fsm_step(safety_fsm_t *fsm, const safety_inputs_t *in, safety_outputs_t *out);

const char *safety_state_name(safety_state_t state);

#ifdef __cplusplus
}
#endif

#endif
