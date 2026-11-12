#include "node_fsm.h"

#include <stddef.h>

void node_fsm_init(node_fsm_t *f)
{
    if (f == NULL) { return; }
    f->state = NODE_INIT;
    f->cause = NODE_CAUSE_NONE;
    f->ticks_since_cmd = 0;
    f->recover_cmds = 0;
    f->mode = NODE_MODE_DISABLE;
}

static void enter_safe(node_fsm_t *f, node_cause_t cause)
{
    f->state = NODE_SAFE_STOP;
    f->cause = cause;
    f->recover_cmds = 0;
    f->mode = NODE_MODE_DISABLE;
}

static void fill(const node_fsm_t *f, node_outputs_t *out)
{
    bool active = f->state == NODE_RUN || f->state == NODE_DEGRADED;
    out->state = f->state;
    out->cause = f->state == NODE_SAFE_STOP ? f->cause : NODE_CAUSE_NONE;
    out->output_enable = active;
    out->vel_limit_pct = f->state == NODE_RUN ? 100U : f->state == NODE_DEGRADED ? 20U : 0U;
    out->mode = active ? f->mode : (uint8_t)NODE_MODE_DISABLE;
}

bool node_fsm_step(node_fsm_t *f, const node_inputs_t *in, node_outputs_t *out)
{
    if (f == NULL || in == NULL || out == NULL) {
        if (out != NULL) {
            out->state = NODE_SAFE_STOP; out->cause = NODE_CAUSE_FAULT; out->output_enable = false;
            out->vel_limit_pct = 0; out->mode = NODE_MODE_DISABLE;
        }
        return false;
    }

    if (in->cmd_valid) {
        f->ticks_since_cmd = 0;
    } else if (f->ticks_since_cmd < UINT32_MAX) {
        f->ticks_since_cmd++;
    }

    /* Highest priority first: E-stop, then node fault. Valid from every state. */
    if (in->estop) {
        enter_safe(f, NODE_CAUSE_ESTOP);
        fill(f, out);
        return true;
    }
    if (in->fault) {
        enter_safe(f, NODE_CAUSE_FAULT);
        fill(f, out);
        return true;
    }

    switch (f->state) {
    case NODE_INIT:
        if (in->self_test_ok) { f->state = NODE_READY; }
        break;
    case NODE_READY:
        if (in->cmd_valid && in->cmd_mode != NODE_MODE_DISABLE && in->cmd_mode <= NODE_MODE_HOLD) {
            f->state = NODE_RUN;
            f->mode = in->cmd_mode;
            f->ticks_since_cmd = 0;
        }
        break;
    case NODE_RUN:
    case NODE_DEGRADED:
        if (in->cmd_valid) {
            if (in->cmd_mode == NODE_MODE_DISABLE) { f->state = NODE_READY; f->mode = NODE_MODE_DISABLE; }
            else if (in->cmd_mode <= NODE_MODE_HOLD) { f->mode = in->cmd_mode; f->state = NODE_RUN; }
            /* unknown mode: ignored, the CMD still counted as alive */
        } else if (f->ticks_since_cmd >= NODE_CMD_TIMEOUT_TICKS) {
            enter_safe(f, NODE_CAUSE_CMD_TIMEOUT);
        } else if (f->ticks_since_cmd >= NODE_CMD_DEGRADED_TICKS) {
            f->state = NODE_DEGRADED;
        }
        break;
    case NODE_SAFE_STOP:
        if (in->cmd_valid && in->cmd_mode == NODE_MODE_DISABLE) {
            if (++f->recover_cmds >= NODE_RECOVER_CMDS) {
                f->state = NODE_READY; f->cause = NODE_CAUSE_NONE; f->recover_cmds = 0;
            }
        } else if (in->cmd_valid) {
            f->recover_cmds = 0;        /* an ENABLE-type CMD restarts the recovery count */
        }
        break;
    default:
        enter_safe(f, NODE_CAUSE_FAULT);
        break;
    }
    fill(f, out);
    return true;
}

const char *node_state_name(node_state_t s)
{
    switch (s) {
    case NODE_INIT: return "INIT";
    case NODE_READY: return "READY";
    case NODE_RUN: return "RUN";
    case NODE_DEGRADED: return "DEGRADED";
    case NODE_SAFE_STOP: return "SAFE_STOP";
    default: return "?";
    }
}
