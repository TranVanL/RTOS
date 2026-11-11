/* Host unit tests for limits + PID + cascade (M1 Part 4). */
#include "pid.h"
#include "limits.h"
#include "plant.h"

#include <math.h>
#include <stdio.h>

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static pid_params_t base(void)
{
    pid_params_t p = { .kp = 2.0f, .ki = 0.0f, .kd = 0.0f, .dt_s = 0.001f,
                       .out_min = -100.0f, .out_max = 100.0f,
                       .integral_limit = 0.0f, .d_alpha = 1.0f };
    return p;
}

static int test_p_only(void)
{
    pid_state_t s; pid_reset(&s);
    pid_params_t p = base();
    CHECK(fabsf(pid_step(&s, &p, 3.0f, 1.0f) - 4.0f) < 1e-6f, "P output != Kp*e");
    CHECK(fabsf(pid_step(&s, &p, -1.0f, 1.0f) - (-4.0f)) < 1e-6f, "P negative");
    return 0;
}

static int test_integral_linear_and_antiwindup(void)
{
    pid_state_t s; pid_reset(&s);
    pid_params_t p = base();
    p.kp = 0.0f; p.ki = 10.0f;
    float u = 0.0f;
    for (int i = 0; i < 100; i++) { u = pid_step(&s, &p, 1.0f, 0.0f); }
    CHECK(fabsf(u - 1.0f) < 1e-4f, "integral not linear (Ki*e*t = 1.0 after 0.1 s)");

    /* Saturation: output limit 5, error held for a long time. */
    pid_reset(&s);
    p.out_max = 5.0f; p.out_min = -5.0f; p.integral_limit = 0.0f;
    for (int i = 0; i < 100000; i++) { u = pid_step(&s, &p, 1.0f, 0.0f); }
    CHECK(u == 5.0f, "output not saturated");
    CHECK(s.integral <= 5.0f + 1e-3f, "integral wound up beyond saturation");
    /* Recovery: reverse error must leave saturation immediately. */
    int n = 0;
    while (pid_step(&s, &p, -1.0f, 0.0f) >= 5.0f && n < 100000) { n++; }
    CHECK(n < 10, "slow recovery from saturation (windup)");

    /* Explicit integral_limit. */
    pid_reset(&s);
    p.out_max = 1000.0f; p.out_min = -1000.0f; p.integral_limit = 2.0f;
    for (int i = 0; i < 10000; i++) { pid_step(&s, &p, 1.0f, 0.0f); }
    CHECK(fabsf(s.integral - 2.0f) < 1e-5f, "integral_limit not enforced");
    return 0;
}

static int test_derivative_on_measurement(void)
{
    pid_state_t s; pid_reset(&s);
    pid_params_t p = base();
    p.kp = 0.0f; p.kd = 1.0f;
    pid_step(&s, &p, 0.0f, 0.0f);
    CHECK(fabsf(pid_step(&s, &p, 100.0f, 0.0f)) < 1e-6f, "derivative kick on ref step");
    float u = pid_step(&s, &p, 100.0f, 0.001f);   /* meas rises 1 rad/s */
    CHECK(fabsf(u - (-1.0f)) < 1e-3f, "D on measurement wrong sign/scale");
    return 0;
}

static int test_limits(void)
{
    CHECK(limits_clamp(5.0f, -1.0f, 1.0f) == 1.0f, "clamp hi");
    CHECK(limits_clamp(-5.0f, -1.0f, 1.0f) == -1.0f, "clamp lo");
    CHECK(limits_clamp(NAN, -1.0f, 1.0f) == 0.0f, "clamp NaN");
    float prev = 0.0f;
    for (int i = 0; i < 50; i++) {
        const float n = limits_rate_limit(prev, 10.0f, 0.5f);
        CHECK(fabsf(n - prev) <= 0.5f + 1e-6f, "rate limit exceeded");
        prev = n;
    }
    CHECK(fabsf(prev - 10.0f) < 1e-5f, "rate limit never reaches target");
    CHECK(limits_rate_limit(0.0f, -10.0f, 0.5f) == -0.5f, "rate limit negative");
    return 0;
}

static int test_cascade_limits(void)
{
    cascade_params_t c; cascade_default_params(&c);
    c.volt_rate_v_per_tick = 0.2f;
    c.pos_max_rad = 1.0f;
    cascade_state_t s; cascade_reset(&s);
    float prev = 0.0f;
    for (int i = 0; i < 200; i++) {
        const float v = cascade_step(&s, &c, 100.0f, 0.0f, 0.0f);
        CHECK(fabsf(v - prev) <= 0.2f + 1e-6f, "cascade rate limit exceeded");
        CHECK(fabsf(v) <= 12.0f + 1e-6f, "cascade saturation exceeded");
        prev = v;
    }
    /* Ref clamped to 1.0: with pos=1.0, vel=0 the position loop asks for 0 velocity. */
    cascade_reset(&s);
    c.volt_rate_v_per_tick = 0.0f;
    CHECK(fabsf(cascade_step(&s, &c, 100.0f, 1.0f, 0.0f)) < 1e-6f, "ref not clamped to soft limit");
    return 0;
}

static int test_independent_instances(void)
{
    cascade_params_t c; cascade_default_params(&c);
    cascade_state_t a, b; cascade_reset(&a); cascade_reset(&b);
    for (int i = 0; i < 50; i++) { cascade_step(&a, &c, 1.0f, 0.0f, 0.0f); }
    cascade_state_t fresh; cascade_reset(&fresh);
    const float vb = cascade_step(&b, &c, 1.0f, 0.0f, 0.0f);
    const float vf = cascade_step(&fresh, &c, 1.0f, 0.0f, 0.0f);
    CHECK(vb == vf, "instances share hidden state");
    return 0;
}

static int test_closed_loop_smoke(void)
{
    plant_params_t pp; plant_default_params(&pp);
    plant_state_t ps; plant_reset(&ps);
    cascade_params_t c; cascade_default_params(&c);
    cascade_state_t cs; cascade_reset(&cs);
    float peak = 0.0f;
    for (int i = 0; i < 2000; i++) {
        const float v = cascade_step(&cs, &c, 1.0f, ps.position_rad, ps.velocity_rad_s);
        CHECK(plant_step(&ps, &pp, v, 0.001f), "plant_step failed");
        if (ps.position_rad > peak) { peak = ps.position_rad; }
    }
    printf("closed-loop smoke: final=%.4f rad peak=%.4f rad (tuning is Part 5)\n",
           (double)ps.position_rad, (double)peak);
    CHECK(fabsf(ps.position_rad - 1.0f) < 0.05f, "closed loop did not approach reference");
    return 0;
}

int main(void)
{
    int f = 0;
    f += test_p_only();
    f += test_integral_linear_and_antiwindup();
    f += test_derivative_on_measurement();
    f += test_limits();
    f += test_cascade_limits();
    f += test_independent_instances();
    f += test_closed_loop_smoke();
    if (f == 0) { printf("test_pid: PASS\n"); }
    return f ? 1 : 0;
}
