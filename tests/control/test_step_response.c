/* Automated closed-loop step-response checks (M1 Part 5). Metrics in samples of the 1 ms model tick. */
#include "sim_loop.h"

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            return 1;                                                         \
        }                                                                     \
    } while (0)

#define MAX_OVERSHOOT_PCT 10.0f
#define MAX_SETTLING_S    0.200f   /* 200 model ticks */
#define MAX_SS_COUNTS     1.0f
#define MAX_SS_COUNTS_NOISE 2.0f   /* noise sigma ~1 count: quantisation+noise floor */

static int report(const char *n, const sim_result_t *r)
{
    printf("%s: overshoot=%.2f%% settling=%.0f ticks ss_err=%.3f counts maxdev_load=%.4f rad\n",
           n, (double)r->overshoot_pct, (double)(r->settling_s / SIM_DT_S), (double)r->ss_error_counts,
           (double)r->max_dev_after_load_rad);
    return 0;
}

int main(void)
{
    sim_cfg_t c;
    sim_result_t r;

    sim_default_cfg(&c);
    CHECK(sim_run(&c, &r, NULL) == 0, "sim step failed");
    report("step", &r);
    CHECK(r.overshoot_pct < MAX_OVERSHOOT_PCT, "step overshoot >= 10%");
    CHECK(r.settling_s <= MAX_SETTLING_S, "step settling > 200 ticks");
    CHECK(r.ss_error_counts <= MAX_SS_COUNTS, "step steady-state error > 1 count");
    CHECK(r.v_peak <= 12.0f + 1e-4f, "voltage exceeded Vmax");

    sim_default_cfg(&c);
    c.load_nm = 0.05f; c.t_end_s = 1.5f;
    CHECK(sim_run(&c, &r, NULL) == 0, "sim load failed");
    report("load", &r);
    CHECK(r.overshoot_pct < MAX_OVERSHOOT_PCT, "load: step overshoot >= 10%");
    CHECK(r.ss_error_counts <= MAX_SS_COUNTS, "load: does not return to ref (<=1 count)");
    CHECK(r.max_dev_after_load_rad < 0.5f, "load: deviation too large (stiffness)");

    sim_default_cfg(&c);
    c.noise_std_rad = 0.0015f;
    CHECK(sim_run(&c, &r, NULL) == 0, "sim noise failed");
    report("noise", &r);
    CHECK(r.overshoot_pct < MAX_OVERSHOOT_PCT, "noise: overshoot >= 10%");
    CHECK(r.settling_s <= MAX_SETTLING_S + 0.050f, "noise: settling too long");
    CHECK(r.ss_error_counts <= MAX_SS_COUNTS_NOISE, "noise: steady-state error too large");

    /* Determinism: same seed, same result. */
    sim_result_t r2;
    sim_run(&c, &r2, NULL);
    CHECK(r.overshoot_pct == r2.overshoot_pct && r.ss_error_counts == r2.ss_error_counts, "not deterministic");

    printf("test_step_response: PASS\n");
    return 0;
}
