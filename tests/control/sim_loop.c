#include "sim_loop.h"

#include <math.h>

#define TWO_PI 6.28318530718f

void sim_default_cfg(sim_cfg_t *c)
{
    c->step_rad = 1.0f;
    c->t_end_s = 1.0f;
    c->load_nm = 0.0f;
    c->load_t_s = 0.5f;
    c->noise_std_rad = 0.0f;
    c->seed = 1U;
    c->speed_alpha = 0.5f;
    cascade_default_params(&c->ctrl);
    plant_default_params(&c->plant);
}

int sim_run(const sim_cfg_t *cfg, sim_result_t *out, FILE *csv)
{
    plant_params_t pp = cfg->plant;
    plant_state_t ps;
    plant_reset(&ps);
    cascade_state_t cs;
    cascade_reset(&cs);
    encoder_params_t ep = { .counts_per_rev = SIM_COUNTS_PER_REV, .noise_std_rad = cfg->noise_std_rad,
                            .seed = cfg->seed, .force_stuck = false };
    encoder_state_t es;
    encoder_reset(&es, &ep);
    speed_estimator_t se;
    speed_estimator_reset(&se, cfg->speed_alpha);

    const int n = (int)(cfg->t_end_s / SIM_DT_S + 0.5f);
    const float band = 0.02f * fabsf(cfg->step_rad);
    float peak = 0.0f, vpeak = 0.0f, maxdev = 0.0f;
    int last_out = 0;
    float v = 0.0f;

    pp.load_torque_nm = 0.0f;
    for (int k = 0; k < n; k++) {
        const float t = (float)k * SIM_DT_S;
        if (cfg->load_nm != 0.0f && t >= cfg->load_t_s) {
            pp.load_torque_nm = cfg->load_nm;
        }
        const int32_t count = encoder_step(&es, &ep, ps.position_rad);
        const float vel_meas = speed_estimator_step(&se, count, SIM_DT_S, SIM_COUNTS_PER_REV);
        const float pos_meas = (float)count * TWO_PI / (float)SIM_COUNTS_PER_REV;
        v = cascade_step(&cs, &cfg->ctrl, cfg->step_rad, pos_meas, vel_meas);
        if (!plant_step(&ps, &pp, v, SIM_DT_S)) {
            return 1;
        }
        const float err = cfg->step_rad - ps.position_rad;
        if (ps.position_rad > peak) { peak = ps.position_rad; }
        if (fabsf(v) > vpeak) { vpeak = fabsf(v); }
        if (fabsf(err) > band) { last_out = k + 1; }
        if (pp.load_torque_nm != 0.0f && fabsf(err) > maxdev) { maxdev = fabsf(err); }
        if (csv != NULL) {
            fprintf(csv, "%.4f,%.6f,%.6f,%.6f,%.6f,%d\n", (double)(t + SIM_DT_S),
                    (double)cfg->step_rad, (double)ps.position_rad,
                    (double)ps.velocity_rad_s, (double)v, (int)count);
        }
    }
    const float step = cfg->step_rad;
    out->overshoot_pct = (step != 0.0f) ? fmaxf(0.0f, (peak - step) / step * 100.0f) : 0.0f;
    out->settling_s = (float)last_out * SIM_DT_S;
    out->ss_error_counts = fabsf(cfg->step_rad - ps.position_rad) * (float)SIM_COUNTS_PER_REV / TWO_PI;
    out->max_dev_after_load_rad = maxdev;
    out->v_peak = vpeak;
    return 0;
}
