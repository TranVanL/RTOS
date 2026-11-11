/*
 * OS-free incremental encoder model (M1 Part 3). See encoder.h for the
 * contract. No FreeRTOS, no I/O, no dynamic allocation.
 */
#include "encoder.h"

#include <math.h>
#include <stddef.h>

#define ENCODER_PI 3.14159265358979323846f

static uint32_t xorshift32(uint32_t *rng_state)
{
    uint32_t x = *rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng_state = x;
    return x;
}

/* Returns a uniform sample in (0, 1], never exactly 0 so logf() stays
 * finite in rand_gaussian(). */
static float rand_uniform01(uint32_t *rng_state)
{
    const uint32_t r = xorshift32(rng_state);
    const double u = ((double)r + 1.0) / 4294967296.0; /* (0, 1] */
    return (float)u;
}

/* Box-Muller transform: two independent uniforms -> one standard normal
 * sample. Simple and deterministic given the PRNG state; re-derives both
 * uniforms per call rather than caching the paired sample, trading a
 * little efficiency for simpler state. */
static float rand_gaussian(uint32_t *rng_state)
{
    const float u1 = rand_uniform01(rng_state);
    const float u2 = rand_uniform01(rng_state);
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * ENCODER_PI * u2);
}

void encoder_reset(encoder_state_t *state, const encoder_params_t *params)
{
    if (state == NULL) {
        return;
    }

    const uint32_t seed = (params != NULL) ? params->seed : 0U;
    /* xorshift32 is stuck at 0 forever if seeded with 0; remap to a fixed
     * nonzero value so seed == 0 still behaves deterministically. */
    state->rng_state = (seed != 0U) ? seed : 0x9E3779B9U;
    state->last_count = 0;
    state->stuck = false;
}

int32_t encoder_step(
    encoder_state_t *state,
    const encoder_params_t *params,
    float theta_rad
)
{
    if (state == NULL || params == NULL) {
        return 0;
    }

    if (params->force_stuck) {
        state->stuck = true;
        return state->last_count;
    }

    state->stuck = false;

    float theta_noisy = theta_rad;
    if (params->noise_std_rad > 0.0f) {
        theta_noisy += params->noise_std_rad * rand_gaussian(&state->rng_state);
    }

    const float counts_per_rad = (float)params->counts_per_rev / (2.0f * ENCODER_PI);
    const int32_t count = (int32_t)floorf(theta_noisy * counts_per_rad);

    state->last_count = count;
    return count;
}

void speed_estimator_reset(speed_estimator_t *estimator, float alpha)
{
    if (estimator == NULL) {
        return;
    }

    estimator->alpha = alpha;
    estimator->filtered_omega = 0.0f;
    estimator->last_count = 0;
    estimator->has_last_count = false;
}

float speed_estimator_step(
    speed_estimator_t *estimator,
    int32_t count,
    float dt_s,
    uint32_t counts_per_rev
)
{
    if (estimator == NULL) {
        return 0.0f;
    }

    if (!(dt_s > 0.0f) || counts_per_rev == 0U) {
        return estimator->filtered_omega;
    }

    if (!estimator->has_last_count) {
        estimator->last_count = count;
        estimator->has_last_count = true;
        estimator->filtered_omega = 0.0f;
        return estimator->filtered_omega;
    }

    const float rad_per_count = (2.0f * ENCODER_PI) / (float)counts_per_rev;
    const float raw_omega = (float)(count - estimator->last_count) * rad_per_count / dt_s;

    estimator->filtered_omega =
        estimator->alpha * raw_omega + (1.0f - estimator->alpha) * estimator->filtered_omega;
    estimator->last_count = count;

    return estimator->filtered_omega;
}
