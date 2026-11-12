#include "node_fsm.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static node_fsm_t f;
static node_outputs_t out;

static void step(bool cmd, uint8_t mode, bool estop, bool fault)
{
    node_inputs_t in = { .self_test_ok = true, .cmd_valid = cmd, .cmd_mode = mode, .estop = estop, .fault = fault };
    CHECK(node_fsm_step(&f, &in, &out));
}
static void idle(unsigned n) { while (n--) { step(false, 0, false, false); } }
static void to_run(void)
{
    node_fsm_init(&f);
    CHECK(f.state == NODE_INIT);
    step(false, 0, false, false);  CHECK(out.state == NODE_READY); CHECK(!out.output_enable);
    step(true, NODE_MODE_POSITION, false, false);
    CHECK(out.state == NODE_RUN); CHECK(out.output_enable); CHECK(out.vel_limit_pct == 100U);
}

static void test_ready_never_times_out(void)
{
    node_fsm_init(&f);
    idle(1);
    idle(500);
    CHECK(out.state == NODE_READY); CHECK(!out.output_enable);
}

static void test_cmd_timeout_exactly_50_ticks(void)
{
    to_run();
    idle(NODE_CMD_TIMEOUT_TICKS - 1U);
    CHECK(out.output_enable);                    /* 49 ticks without CMD: still enabled (DEGRADED) */
    CHECK(out.state == NODE_DEGRADED); CHECK(out.vel_limit_pct == 20U);
    idle(1);                                     /* 50th tick */
    CHECK(out.state == NODE_SAFE_STOP); CHECK(!out.output_enable);
    CHECK(out.cause == NODE_CAUSE_CMD_TIMEOUT);
}

static void test_degraded_recovers_on_cmd(void)
{
    to_run();
    idle(NODE_CMD_DEGRADED_TICKS);
    CHECK(out.state == NODE_DEGRADED);
    step(true, NODE_MODE_POSITION, false, false);
    CHECK(out.state == NODE_RUN); CHECK(out.vel_limit_pct == 100U);
}

static void test_estop_from_every_state(void)
{
    node_fsm_init(&f); step(false, 0, true, false);          /* INIT */
    CHECK(out.state == NODE_SAFE_STOP); CHECK(out.cause == NODE_CAUSE_ESTOP); CHECK(!out.output_enable);
    node_fsm_init(&f); idle(1); step(false, 0, true, false); /* READY */
    CHECK(out.state == NODE_SAFE_STOP);
    to_run(); step(false, 0, true, false);                   /* RUN: disabled in the same tick */
    CHECK(out.state == NODE_SAFE_STOP); CHECK(!out.output_enable); CHECK(out.mode == NODE_MODE_DISABLE);
    to_run(); idle(NODE_CMD_DEGRADED_TICKS); step(false, 0, true, false); /* DEGRADED */
    CHECK(out.state == NODE_SAFE_STOP);
    to_run(); idle(NODE_CMD_TIMEOUT_TICKS); step(false, 0, true, false);  /* SAFE_STOP stays, cause updated */
    CHECK(out.state == NODE_SAFE_STOP); CHECK(out.cause == NODE_CAUSE_ESTOP);
}

static void test_estop_is_idempotent(void)
{
    to_run();
    for (int i = 0; i < 3; ++i) { step(false, 0, true, false); CHECK(out.state == NODE_SAFE_STOP); CHECK(!out.output_enable); }
}

static void test_estop_wins_over_cmd(void)
{
    to_run(); step(true, NODE_MODE_POSITION, true, false);
    CHECK(out.state == NODE_SAFE_STOP); CHECK(!out.output_enable);
}

static void test_recovery_needs_consecutive_disable_cmds(void)
{
    to_run(); step(false, 0, true, false);
    for (unsigned i = 0; i < NODE_RECOVER_CMDS - 1U; ++i) { step(true, NODE_MODE_DISABLE, false, false); }
    CHECK(out.state == NODE_SAFE_STOP);
    step(true, NODE_MODE_POSITION, false, false);            /* enable-type CMD restarts the count */
    CHECK(out.state == NODE_SAFE_STOP); CHECK(!out.output_enable);
    for (unsigned i = 0; i < NODE_RECOVER_CMDS; ++i) { step(true, NODE_MODE_DISABLE, false, false); }
    CHECK(out.state == NODE_READY); CHECK(out.cause == NODE_CAUSE_NONE); CHECK(!out.output_enable);
}

static void test_disable_cmd_returns_to_ready(void)
{
    to_run(); step(true, NODE_MODE_DISABLE, false, false);
    CHECK(out.state == NODE_READY); CHECK(!out.output_enable);
}

static void test_fault_latches_safe_stop(void)
{
    to_run(); step(false, 0, false, true);
    CHECK(out.state == NODE_SAFE_STOP); CHECK(out.cause == NODE_CAUSE_FAULT);
}

static void test_invalid_mode_is_ignored_but_alive(void)
{
    to_run(); idle(40); step(true, 9, false, false);
    CHECK(out.state == NODE_RUN || out.state == NODE_DEGRADED);
    idle(NODE_CMD_DEGRADED_TICKS - 1U);
    CHECK(out.output_enable);                    /* counter was reset by the (ignored-mode) CMD */
    CHECK(out.mode == NODE_MODE_POSITION);
}

static void test_ready_ignores_unknown_mode(void)
{
    node_fsm_init(&f); idle(1); step(true, 7, false, false);
    CHECK(out.state == NODE_READY); CHECK(!out.output_enable);
}

static void test_null_args(void)
{
    node_inputs_t in; memset(&in, 0, sizeof in);
    CHECK(!node_fsm_step(NULL, &in, &out)); CHECK(out.state == NODE_SAFE_STOP); CHECK(!out.output_enable);
    CHECK(!node_fsm_step(&f, NULL, &out));
    CHECK(!node_fsm_step(&f, &in, NULL));
}

int main(void)
{
    test_ready_never_times_out(); test_cmd_timeout_exactly_50_ticks(); test_degraded_recovers_on_cmd();
    test_estop_from_every_state(); test_estop_is_idempotent(); test_estop_wins_over_cmd();
    test_recovery_needs_consecutive_disable_cmds(); test_disable_cmd_returns_to_ready();
    test_fault_latches_safe_stop(); test_invalid_mode_is_ignored_but_alive(); test_ready_ignores_unknown_mode();
    test_null_args();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("node_fsm: all tests passed\n");
    return 0;
}
