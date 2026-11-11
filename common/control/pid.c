#include "pid.h"

#include <stddef.h>

#include "limits.h"

static bool is_finite_f(float x)
{
    return (x == x) && (x - x == 0.0f);
}

void pid_reset(pid_state_t *s)
{
    if (s == NULL) {
        return;
    }
    s->integral = 0.0f;
    s->prev_meas = 0.0f;
    s->d_filt = 0.0f;
    s->has_prev = false;
    s->saturated = false;
}

float pid_step(pid_state_t *s, const pid_params_t *p, float ref, float meas)
{
    if (s == NULL || p == NULL || !(p->dt_s > 0.0f) || !is_finite_f(ref) || !is_finite_f(meas)) {
        return 0.0f;
    }

    const float err = ref - meas;
    const float pterm = p->kp * err;

    float d_term = 0.0f;
    if (s->has_prev && p->kd != 0.0f) {
        const float raw = -(meas - s->prev_meas) / p->dt_s;
        const float a = (p->d_alpha > 0.0f && p->d_alpha <= 1.0f) ? p->d_alpha : 1.0f;
        s->d_filt += a * (raw - s->d_filt);
        d_term = p->kd * s->d_filt;
    }
    s->prev_meas = meas;
    s->has_prev = true;

    float integ = s->integral + p->ki * err * p->dt_s;
    if (p->integral_limit > 0.0f) {
        integ = limits_clamp(integ, -p->integral_limit, p->integral_limit);
    }

    const float u_unsat = pterm + integ + d_term;
    const float u = limits_clamp(u_unsat, p->out_min, p->out_max);
    const bool sat = (u != u_unsat);

    /* Conditional clamping: freeze integral if saturated and error pushes further. */
    const bool pushing = (u_unsat > p->out_max && err > 0.0f)
                      || (u_unsat < p->out_min && err < 0.0f);
    if (!(sat && pushing)) {
        s->integral = integ;
    }
    s->saturated = sat;
    return u;
}

void cascade_reset(cascade_state_t *s)
{
    if (s == NULL) {
        return;
    }
    pid_reset(&s->pos);
    pid_reset(&s->vel);
    s->vel_setpoint = 0.0f;
    s->last_volt = 0.0f;
    s->tick = 0U;
}

void cascade_default_params(cascade_params_t *c)
{
    if (c == NULL) {
        return;
    }
    c->pos = (pid_params_t){ .kp = 20.0f, .ki = 0.0f, .kd = 0.0f, .dt_s = 0.001f,
                             .out_min = -30.0f, .out_max = 30.0f,
                             .integral_limit = 0.0f, .d_alpha = 1.0f };
    c->vel = (pid_params_t){ .kp = 0.15f, .ki = 4.0f, .kd = 0.0f, .dt_s = 0.001f,
                             .out_min = -12.0f, .out_max = 12.0f,
                             .integral_limit = 12.0f, .d_alpha = 1.0f };
    c->pos_min_rad = -6.2831853f;
    c->pos_max_rad = 6.2831853f;
    c->volt_rate_v_per_tick = 0.0f;
    c->outer_divider = 5U;
}

float cascade_step(cascade_state_t *s, const cascade_params_t *c,
                   float ref_pos, float pos, float vel)
{
    if (s == NULL || c == NULL || c->outer_divider == 0U
        || !is_finite_f(ref_pos) || !is_finite_f(pos) || !is_finite_f(vel)) {
        return 0.0f;
    }

    const float ref = limits_clamp(ref_pos, c->pos_min_rad, c->pos_max_rad);

    if ((s->tick % c->outer_divider) == 0U) {
        s->vel_setpoint = pid_step(&s->pos, &c->pos, ref, pos);
    }
    s->tick++;

    float v = pid_step(&s->vel, &c->vel, s->vel_setpoint, vel);
    v = limits_rate_limit(s->last_volt, v, c->volt_rate_v_per_tick);
    s->last_volt = v;
    return v;
}
