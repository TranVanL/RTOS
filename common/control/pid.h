/*
 * OS-free PID and position->velocity cascade controller (M1 Part 4).
 * Units: rad, rad/s, V. Fixed dt, no globals, no allocation: state is
 * passed explicitly so two instances are fully independent.
 * Do not add OS, driver, or I/O includes to this file.
 */
#ifndef M1_CONTROL_PID_H
#define M1_CONTROL_PID_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kp;
    float ki;               /* [1/s] scaling applied to the integral of error */
    float kd;
    float dt_s;             /* fixed step, > 0 */
    float out_min;          /* saturation */
    float out_max;
    float integral_limit;   /* |integral term| clamp (anti-windup threshold), >= 0; 0 = no limit */
    float d_alpha;          /* derivative low-pass in (0,1]; 1 = no filtering */
} pid_params_t;

typedef struct {
    float integral;         /* integral TERM (already multiplied by ki) */
    float prev_meas;
    float d_filt;
    bool  has_prev;         /* false until first step (avoids derivative kick) */
    bool  saturated;        /* last output was clamped */
} pid_state_t;

void pid_reset(pid_state_t *state);

/*
 * One PID step. Derivative acts on the measurement (no kick on reference
 * change). Anti-windup: conditional clamping - the integral is not advanced
 * when the output is saturated and the error pushes further into saturation;
 * it is also clamped to +-integral_limit.
 * Returns 0.0f (and leaves state untouched) on NULL args, dt_s <= 0 or
 * non-finite ref/meas.
 */
float pid_step(pid_state_t *state, const pid_params_t *p, float ref, float meas);

typedef struct {
    pid_params_t pos;             /* outer loop (normally P only); out = velocity setpoint [rad/s] */
    pid_params_t vel;             /* inner loop (PI); out = voltage [V] */
    float pos_min_rad;            /* soft position limits applied to the reference */
    float pos_max_rad;
    float volt_rate_v_per_tick;   /* max |dV| per inner tick, 0 = off */
    unsigned outer_divider;       /* outer loop runs every N inner ticks, >= 1 */
} cascade_params_t;

typedef struct {
    pid_state_t pos;
    pid_state_t vel;
    float vel_setpoint;           /* held between outer updates */
    float last_volt;              /* for rate limit */
    unsigned tick;
} cascade_state_t;

void cascade_reset(cascade_state_t *state);

/* Default tuning, finalised in M1 Part 5 (speed estimator alpha 0.5): dt=1 ms, +-12 V, +-30 rad/s. */
void cascade_default_params(cascade_params_t *p);

/* One inner-loop tick: clamp ref to soft limits, (every outer_divider ticks)
 * run position loop, run velocity loop, rate-limit voltage. Returns volts.
 * Returns 0.0f on invalid args. */
float cascade_step(cascade_state_t *state, const cascade_params_t *p,
                   float ref_pos, float pos, float vel);

#ifdef __cplusplus
}
#endif

#endif /* M1_CONTROL_PID_H */
