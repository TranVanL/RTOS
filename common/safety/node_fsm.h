#ifndef M4_NODE_FSM_H
#define M4_NODE_FSM_H

/*
 * FreeRTOS motion node safety FSM (ARCH 4/5.3, M4). OS-free: no clock, no I/O.
 * Called once per 1 ms control tick; every deadline is counted in these ticks.
 *
 *   INIT      -> READY      first tick (self-test result passed by caller)
 *   READY     -> RUN        valid CMD with mode != DISABLE
 *   RUN       -> DEGRADED   no valid CMD for NODE_CMD_DEGRADED_TICKS ticks
 *   DEGRADED  -> RUN        valid CMD again
 *   RUN/DEG   -> SAFE_STOP  no valid CMD for NODE_CMD_TIMEOUT_TICKS (50) ticks
 *   any       -> SAFE_STOP  E-stop request, node fault
 *   SAFE_STOP -> READY      NODE_RECOVER_CMDS consecutive valid CMDs with mode DISABLE
 *                           (and no active fault); an E-stop in the same tick wins
 *
 * Output is enabled only in RUN and DEGRADED. The timeout is armed only while in
 * RUN/DEGRADED, so a node that never received a CMD simply stays READY, disabled.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NODE_CMD_TIMEOUT_TICKS   50U   /* ARCH 7: output disabled after 50 own ticks without CMD */
#define NODE_CMD_DEGRADED_TICKS  25U
#define NODE_RECOVER_CMDS        5U

typedef enum {
    NODE_INIT = 0,
    NODE_READY,
    NODE_RUN,
    NODE_DEGRADED,
    NODE_SAFE_STOP
} node_state_t;

typedef enum {
    NODE_CAUSE_NONE = 0,
    NODE_CAUSE_ESTOP,
    NODE_CAUSE_CMD_TIMEOUT,
    NODE_CAUSE_FAULT
} node_cause_t;

typedef enum {
    NODE_MODE_DISABLE = 0,
    NODE_MODE_POSITION = 1,
    NODE_MODE_HOLD = 2
} node_mode_t;

typedef struct {
    bool self_test_ok;
    bool cmd_valid;        /* a new valid CMD was accepted since the previous tick */
    uint8_t cmd_mode;      /* mode of that CMD (meaningful when cmd_valid)         */
    bool estop;            /* a valid E-stop frame was accepted since last tick    */
    bool fault;            /* node-level fault (watchdog/MPU/stack), level         */
} node_inputs_t;

typedef struct {
    node_state_t state;
    node_cause_t cause;    /* latched while SAFE_STOP                              */
    bool output_enable;
    uint8_t vel_limit_pct; /* 100 RUN, 20 DEGRADED, 0 otherwise                    */
    uint8_t mode;          /* last accepted mode, DISABLE unless RUN/DEGRADED      */
} node_outputs_t;

typedef struct {
    node_state_t state;
    node_cause_t cause;
    uint32_t ticks_since_cmd;  /* saturating                                       */
    uint8_t recover_cmds;
    uint8_t mode;
} node_fsm_t;

void node_fsm_init(node_fsm_t *f);

/* Returns false (outputs forced to SAFE_STOP values) on NULL arguments. */
bool node_fsm_step(node_fsm_t *f, const node_inputs_t *in, node_outputs_t *out);

const char *node_state_name(node_state_t s);

#ifdef __cplusplus
}
#endif

#endif
