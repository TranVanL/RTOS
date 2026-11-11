#include "encoder.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            return 1;                                                       \
        }                                                                    \
    } while (0)

#define TEST_PI 3.14159265358979323846f

static int32_t expected_count(float theta_rad, uint32_t counts_per_rev)
{
    const double counts_per_rad = (double)counts_per_rev / (2.0 * (double)TEST_PI);
    return (int32_t)floor((double)theta_rad * counts_per_rad);
}

static int test_zero_noise_exact_floor(void)
{
    encoder_params_t params = {
        .counts_per_rev = 4096U,
        .noise_std_rad = 0.0f,
        .seed = 1234U,
        .force_stuck = false,
    };

    encoder_state_t state;
    encoder_reset(&state, &params);

    const float thetas[] = { 0.0f, 0.37f, -2.9f, 13.5f, -13.5f, 1000.0f };
    for (size_t i = 0; i < sizeof(thetas) / sizeof(thetas[0]); ++i) {
        const int32_t count = encoder_step(&state, &params, thetas[i]);
        CHECK(count == expected_count(thetas[i], params.counts_per_rev),
              "zero-noise count did not match floor(theta*N/2pi)");
    }

    return 0;
}

static int test_noise_statistics(void)
{
    encoder_params_t params = {
        .counts_per_rev = 4096U,
        .noise_std_rad = 0.01f,
        .seed = 42U,
        .force_stuck = false,
    };

    encoder_state_t state;
    encoder_reset(&state, &params);

    const int n_samples = 100000;
    const float rad_per_count = 2.0f * TEST_PI / (float)params.counts_per_rev;

    double sum = 0.0;
    double sum_sq = 0.0;

    for (int i = 0; i < n_samples; ++i) {
        const int32_t count = encoder_step(&state, &params, 0.0f);
        const double measured_angle = (double)count * (double)rad_per_count;
        sum += measured_angle;
        sum_sq += measured_angle * measured_angle;
    }

    const double mean = sum / (double)n_samples;
    const double variance = (sum_sq / (double)n_samples) - (mean * mean);
    const double std_dev = sqrt(variance);

    CHECK(fabs(mean) < 0.001, "noise mean deviates too far from 0");

    const double rel_std_error = fabs(std_dev - (double)params.noise_std_rad)
                                  / (double)params.noise_std_rad;
    CHECK(rel_std_error < 0.10, "noise std-dev deviates more than 10% from configured sigma");

    return 0;
}

static int test_determinism(void)
{
    encoder_params_t params = {
        .counts_per_rev = 4096U,
        .noise_std_rad = 0.02f,
        .seed = 777U,
        .force_stuck = false,
    };

    encoder_state_t state_a;
    encoder_state_t state_b;
    encoder_reset(&state_a, &params);
    encoder_reset(&state_b, &params);

    for (int k = 0; k < 500; ++k) {
        const float theta = 0.001f * (float)k;
        const int32_t count_a = encoder_step(&state_a, &params, theta);
        const int32_t count_b = encoder_step(&state_b, &params, theta);
        CHECK(count_a == count_b, "same seed produced diverging count sequences");
    }

    return 0;
}

static int test_force_stuck(void)
{
    encoder_params_t params = {
        .counts_per_rev = 4096U,
        .noise_std_rad = 0.0f,
        .seed = 9U,
        .force_stuck = true,
    };

    encoder_state_t state;
    encoder_reset(&state, &params);

    const int32_t first = encoder_step(&state, &params, 0.0f);
    CHECK(state.stuck, "stuck flag not set when force_stuck is true");

    for (int k = 1; k <= 10; ++k) {
        const int32_t count = encoder_step(&state, &params, (float)k);
        CHECK(count == first, "count changed while force_stuck was true");
        CHECK(state.stuck, "stuck flag dropped during force_stuck sequence");
    }

    return 0;
}

static int test_speed_estimator_tracks_constant_velocity(void)
{
    const uint32_t counts_per_rev = 4096U;
    const float dt = 0.001f;
    const float true_omega = 50.0f; /* rad/s */
    const float counts_per_step =
        true_omega * dt * (float)counts_per_rev / (2.0f * TEST_PI);

    speed_estimator_t est;
    speed_estimator_reset(&est, 0.2f);

    float accumulated_counts = 0.0f;
    float last_filtered = 0.0f;

    for (int k = 0; k < 2000; ++k) {
        accumulated_counts += counts_per_step;
        const int32_t count = (int32_t)floorf(accumulated_counts);
        last_filtered = speed_estimator_step(&est, count, dt, counts_per_rev);
    }

    const float rel_error = fabsf(last_filtered - true_omega) / true_omega;
    CHECK(rel_error < 0.05f, "speed estimator did not converge to true velocity within 5%");

    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_zero_noise_exact_floor();
    failures += test_noise_statistics();
    failures += test_determinism();
    failures += test_force_stuck();
    failures += test_speed_estimator_tracks_constant_velocity();

    if (failures != 0) {
        fprintf(stderr, "%d encoder test case(s) failed\n", failures);
        return 1;
    }

    return 0;
}
