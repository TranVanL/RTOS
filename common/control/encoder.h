
#ifndef M1_CONTROL_ENCODER_H
#define M1_CONTROL_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t counts_per_rev; /* N, counts per mechanical revolution, e.g. 4096 */
    float noise_std_rad;     /* Gaussian angle noise std-dev [rad], 0 disables noise */
    uint32_t seed;           /* PRNG seed; 0 is remapped internally to a nonzero value */
    bool force_stuck;        /* test fixture: freeze count regardless of theta */
} encoder_params_t;

typedef struct {
    uint32_t rng_state; /* xorshift32 state */
    int32_t last_count; /* most recent absolute count, held while stuck */
    bool stuck;         /* true if the last encoder_step() was frozen */
} encoder_state_t;

/* Resets state: last_count = 0, stuck = false, PRNG re-seeded from params->seed. */
void encoder_reset(encoder_state_t *state, const encoder_params_t *params);

/*
 * Quantizes theta_rad (continuous, unwrapped shaft angle) into an absolute
 * count: count = floor((theta_rad + noise) * counts_per_rev / (2*pi)).
 * If params->force_stuck is true, returns the previously held count
 * unchanged and sets state->stuck = true instead of sampling theta_rad.
 * Returns 0 and leaves *state untouched if state or params is NULL.
 */
int32_t encoder_step(
    encoder_state_t *state,
    const encoder_params_t *params,
    float theta_rad
);

typedef struct {
    float alpha;           /* first-order filter coefficient, (0,1] */
    float filtered_omega;  /* filtered angular velocity estimate [rad/s] */
    int32_t last_count;    /* count seen on the previous step */
    bool has_last_count;   /* false until the first step() call */
} speed_estimator_t;

/* Resets the estimator: filtered_omega = 0, no previous count recorded. */
void speed_estimator_reset(speed_estimator_t *estimator, float alpha);

/*
 * Updates the velocity estimate from a new encoder count sample using a
 * first-order IIR low-pass filter on the raw finite-difference velocity.
 * The first call after reset only records the count as a reference point
 * and returns 0 (no velocity can be computed without a previous sample).
 * Returns the current filtered_omega unchanged (no NaN/crash) if estimator
 * is NULL, dt_s <= 0, or counts_per_rev == 0.
 */
float speed_estimator_step(
    speed_estimator_t *estimator,
    int32_t count,
    float dt_s,
    uint32_t counts_per_rev
);

#ifdef __cplusplus
}
#endif

#endif /* M1_CONTROL_ENCODER_H */
