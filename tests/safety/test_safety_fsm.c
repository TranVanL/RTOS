#include "safety_fsm.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static safety_fsm_t fsm;
static safety_outputs_t out;

static void tick(safety_inputs_t in)
{
    CHECK(safety_fsm_step(&fsm, &in, &out));
}
static safety_inputs_t hb(void) { safety_inputs_t i; memset(&i, 0, sizeof i); i.hb_valid = true; return i; }
static safety_inputs_t miss(void) { safety_inputs_t i; memset(&i, 0, sizeof i); return i; }

static void to_run(void)
{
    safety_inputs_t i = hb();
    safety_fsm_init(&fsm);
    CHECK(fsm.state == SAFETY_INIT);
    tick(i);                       CHECK(out.state == SAFETY_INIT);
    i.self_test_ok = true; tick(i); CHECK(out.state == SAFETY_READY);
    CHECK(!out.output_enable);
    i = hb(); i.cmd_enable = true; tick(i);
    CHECK(out.state == SAFETY_RUN); CHECK(out.output_enable); CHECK(out.vel_limit_pct == 100U);
}

static void to_degraded(void)
{
    to_run();
    for (int n = 0; n < 3; ++n) { tick(miss()); }
    CHECK(out.state == SAFETY_DEGRADED);
}

static void test_startup_and_disable(void)
{
    to_run();
    safety_inputs_t i = hb(); i.cmd_disable = true; tick(i);
    CHECK(out.state == SAFETY_READY); CHECK(!out.output_enable);
}

static void test_missed_heartbeats(void)
{
    to_run();
    tick(miss()); tick(miss());
    CHECK(out.state == SAFETY_RUN);          /* 2 misses: still RUN      */
    tick(hb());  tick(miss()); tick(miss());
    CHECK(out.state == SAFETY_RUN);          /* not consecutive          */
    tick(miss());
    CHECK(out.state == SAFETY_DEGRADED);     /* 3rd consecutive miss     */
    CHECK(out.output_enable); CHECK(out.vel_limit_pct == 20U);
    for (int n = 3; n < 9; ++n) { tick(miss()); CHECK(out.state == SAFETY_DEGRADED); }
    tick(miss());                            /* 10th consecutive miss    */
    CHECK(out.state == SAFETY_SAFE_STOP);
    CHECK(out.cause == SAFETY_CAUSE_HB_TIMEOUT);
    CHECK(out.send_estop); CHECK(!out.output_enable); CHECK(out.vel_limit_pct == 0U);
    tick(miss()); CHECK(!out.send_estop);    /* pulse only once          */
}

static void test_recovery(void)
{
    to_degraded();
    for (int n = 0; n < 9; ++n) { tick(hb()); CHECK(out.state == SAFETY_DEGRADED); }
    tick(hb());
    CHECK(out.state == SAFETY_RUN);          /* 10 valid heartbeats      */
    to_degraded();
    for (int n = 0; n < 5; ++n) { tick(hb()); }
    tick(miss());                            /* a miss restarts the count */
    for (int n = 0; n < 9; ++n) { tick(hb()); }
    CHECK(out.state == SAFETY_DEGRADED);
    tick(hb()); CHECK(out.state == SAFETY_RUN);
}

static void test_e2e_burst(void)
{
    to_run();
    safety_inputs_t e = hb(); e.e2e_errors = 1;
    tick(e); tick(e);
    CHECK(out.state == SAFETY_RUN);
    tick(e);
    CHECK(out.state == SAFETY_DEGRADED);     /* 3 errors within window   */

    to_run();
    tick(e); tick(e);
    for (int n = 0; n < 8; ++n) { tick(hb()); }  /* ring slides past them */
    tick(e);
    CHECK(out.state == SAFETY_RUN);          /* 2 old ones left window   */
    safety_inputs_t b = hb(); b.e2e_errors = 3;
    to_run(); tick(b);
    CHECK(out.state == SAFETY_DEGRADED);     /* burst within one tick    */
}

static void test_immediate_causes(void)
{
    safety_state_t states[] = { SAFETY_INIT, SAFETY_READY, SAFETY_RUN, SAFETY_DEGRADED };
    for (int cause = 0; cause < 3; ++cause) {
        for (unsigned s = 0; s < 4; ++s) {
            safety_inputs_t i = hb();
            if (states[s] == SAFETY_INIT) { safety_fsm_init(&fsm); }
            else if (states[s] == SAFETY_READY) { to_run(); i.cmd_disable = true; tick(i); i = hb(); }
            else if (states[s] == SAFETY_RUN) { to_run(); }
            else { to_degraded(); }
            CHECK(fsm.state == states[s]);
            if (cause == 0) { i.estop = true; }
            if (cause == 1) { i.limit_violation = true; }
            if (cause == 2) { i.node_fault = true; }
            tick(i);
            CHECK(out.state == SAFETY_SAFE_STOP);
            CHECK(out.cause == (cause == 0 ? SAFETY_CAUSE_ESTOP : cause == 1 ? SAFETY_CAUSE_LIMIT : SAFETY_CAUSE_NODE_FAULT));
            CHECK(out.send_estop); CHECK(!out.output_enable);
        }
    }
}

static void test_reset_fault(void)
{
    to_run();
    safety_inputs_t i = hb(); i.estop = true; tick(i);
    CHECK(out.state == SAFETY_SAFE_STOP);
    i = hb(); i.cmd_reset_fault = true; i.estop = true; tick(i);
    CHECK(out.state == SAFETY_SAFE_STOP); CHECK(out.reset_rejected);   /* cause active */
    i = miss(); i.cmd_reset_fault = true; tick(i);
    CHECK(out.state == SAFETY_SAFE_STOP); CHECK(out.reset_rejected);   /* no heartbeat */
    i = hb(); i.cmd_enable = true; tick(i);
    CHECK(out.state == SAFETY_SAFE_STOP);                              /* enable ignored */
    i = hb(); i.cmd_reset_fault = true; tick(i);
    CHECK(out.state == SAFETY_READY); CHECK(!out.reset_rejected); CHECK(out.cause == SAFETY_CAUSE_NONE);
    i = hb(); i.cmd_reset_fault = true; tick(i);                       /* reset only valid in SAFE_STOP */
    CHECK(out.state == SAFETY_READY);
}

static void test_invalid_arguments(void)
{
    safety_inputs_t i = hb();
    CHECK(!safety_fsm_step(&fsm, &i, NULL));
    CHECK(!safety_fsm_step(NULL, &i, &out));
    CHECK(out.state == SAFETY_SAFE_STOP && !out.output_enable);
    CHECK(!safety_fsm_step(&fsm, NULL, &out));
    safety_fsm_init(NULL);
    CHECK(strcmp(safety_state_name(SAFETY_DEGRADED), "DEGRADED") == 0);
}

int main(void)
{
    test_startup_and_disable();
    test_missed_heartbeats();
    test_recovery();
    test_e2e_burst();
    test_immediate_causes();
    test_reset_fault();
    test_invalid_arguments();
    if (failures == 0) { printf("safety_fsm: all tests passed\n"); return 0; }
    printf("safety_fsm: %d failures\n", failures);
    return 1;
}
