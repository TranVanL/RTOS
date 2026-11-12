#include "safety_fsm.h"

#include <stddef.h>

static void clear_counters(safety_fsm_t *fsm)
{
    fsm->misses = 0U;
    fsm->good = 0U;
    for (unsigned i = 0U; i < SAFETY_E2E_WINDOW_TICKS; ++i) {
        fsm->err_ring[i] = 0U;
    }
    fsm->err_pos = 0U;
}

void safety_fsm_init(safety_fsm_t *fsm)
{
    if (fsm == NULL) {
        return;
    }
    fsm->state = SAFETY_INIT;
    fsm->cause = SAFETY_CAUSE_NONE;
    clear_counters(fsm);
}

static void fill_outputs(const safety_fsm_t *fsm, safety_outputs_t *out)
{
    out->state = fsm->state;
    out->cause = fsm->cause;
    out->output_enable = (fsm->state == SAFETY_RUN || fsm->state == SAFETY_DEGRADED);
    out->vel_limit_pct = (fsm->state == SAFETY_RUN) ? 100U
                       : (fsm->state == SAFETY_DEGRADED) ? SAFETY_DEGRADED_VEL_PCT : 0U;
}

static void enter_safe_stop(safety_fsm_t *fsm, safety_outputs_t *out, safety_cause_t cause)
{
    fsm->state = SAFETY_SAFE_STOP;
    fsm->cause = cause;
    out->send_estop = true;
}

bool safety_fsm_step(safety_fsm_t *fsm, const safety_inputs_t *in, safety_outputs_t *out)
{
    if (out == NULL) {
        return false;
    }
    out->send_estop = false;
    out->reset_rejected = false;
    if (fsm == NULL || in == NULL) {
        out->state = SAFETY_SAFE_STOP;
        out->cause = SAFETY_CAUSE_NONE;
        out->output_enable = false;
        out->vel_limit_pct = 0U;
        return false;
    }

    /* Heartbeat / E2E bookkeeping runs only while the loop is closed. */
    bool closed = (fsm->state == SAFETY_RUN || fsm->state == SAFETY_DEGRADED);
    unsigned err_sum = 0U;
    if (closed) {
        if (in->hb_valid) {
            fsm->misses = 0U;
            if (fsm->good < 255U) { fsm->good++; }
        } else {
            fsm->good = 0U;
            if (fsm->misses < 255U) { fsm->misses++; }
        }
        fsm->err_ring[fsm->err_pos] = in->e2e_errors;
        fsm->err_pos = (uint8_t)((fsm->err_pos + 1U) % SAFETY_E2E_WINDOW_TICKS);
        for (unsigned i = 0U; i < SAFETY_E2E_WINDOW_TICKS; ++i) {
            err_sum += fsm->err_ring[i];
        }
    }

    /* Highest priority: E-stop / limit / node fault from any state. */
    if (fsm->state != SAFETY_SAFE_STOP) {
        if (in->estop) {
            enter_safe_stop(fsm, out, SAFETY_CAUSE_ESTOP);
        } else if (in->limit_violation) {
            enter_safe_stop(fsm, out, SAFETY_CAUSE_LIMIT);
        } else if (in->node_fault) {
            enter_safe_stop(fsm, out, SAFETY_CAUSE_NODE_FAULT);
        }
        if (fsm->state == SAFETY_SAFE_STOP) {
            fill_outputs(fsm, out);
            return true;
        }
    }

    switch (fsm->state) {
    case SAFETY_INIT:
        if (in->self_test_ok) {
            fsm->state = SAFETY_READY;
        }
        break;
    case SAFETY_READY:
        if (in->cmd_enable) {
            clear_counters(fsm);
            fsm->state = SAFETY_RUN;
        }
        break;
    case SAFETY_RUN:
        if (in->cmd_disable) {
            fsm->state = SAFETY_READY;
        } else if (fsm->misses >= SAFETY_HB_DEGRADED_MISSES ||
                   err_sum >= SAFETY_E2E_ERROR_LIMIT) {
            fsm->state = SAFETY_DEGRADED;
            fsm->good = 0U;
        }
        break;
    case SAFETY_DEGRADED:
        if (in->cmd_disable) {
            fsm->state = SAFETY_READY;
        } else if (fsm->misses >= SAFETY_HB_SAFE_STOP_MISSES) {
            enter_safe_stop(fsm, out, SAFETY_CAUSE_HB_TIMEOUT);
        } else if (fsm->good >= SAFETY_HB_RECOVER_GOOD && err_sum < SAFETY_E2E_ERROR_LIMIT) {
            fsm->state = SAFETY_RUN;
        }
        break;
    case SAFETY_SAFE_STOP:
        if (in->cmd_reset_fault) {
            bool cause_gone = !in->estop && !in->limit_violation && !in->node_fault && in->hb_valid;
            if (cause_gone) {
                fsm->state = SAFETY_READY;
                fsm->cause = SAFETY_CAUSE_NONE;
                clear_counters(fsm);
            } else {
                out->reset_rejected = true;
            }
        }
        break;
    }

    fill_outputs(fsm, out);
    return true;
}

const char *safety_state_name(safety_state_t state)
{
    switch (state) {
    case SAFETY_INIT: return "INIT";
    case SAFETY_READY: return "READY";
    case SAFETY_RUN: return "RUN";
    case SAFETY_DEGRADED: return "DEGRADED";
    case SAFETY_SAFE_STOP: return "SAFE_STOP";
    }
    return "?";
}
