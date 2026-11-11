/*
 * Host unit tests for the DC motor plant model (M1 Part 2).
 *
 * This file is test code, not part of the OS-free core: it may use printf
 * and the C standard library freely. common/control/plant.c itself must
 * stay free of such dependencies.
 */
#include "plant.h"

#include <math.h>
#include <stdio.h>

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            return 1;                                                       \
        }                                                                    \
    } while (0)

static float expected_no_load_omega_ss(const plant_params_t *p, float v)
{
    const float denom =
        p->resistance_ohm * p->viscous_friction_nm_s_per_rad
        + p->torque_constant_nm_per_a * p->back_emf_v_s_per_rad;
    return (p->torque_constant_nm_per_a * v) / denom;
}

static float expected_loaded_omega_ss(const plant_params_t *p, float v)
{
    const float denom =
        p->resistance_ohm * p->viscous_friction_nm_s_per_rad
        + p->torque_constant_nm_per_a * p->back_emf_v_s_per_rad;
    return (p->torque_constant_nm_per_a * v
            - p->resistance_ohm * p->load_torque_nm)
           / denom;
}

static int test_voltage_saturation(void)
{
    plant_params_t params;
    plant_default_params(&params);

    plant_state_t clamped_input;
    plant_state_t exact_vmax;
    plant_reset(&clamped_input);
    plant_reset(&exact_vmax);

    const float dt = 0.001f;
    for (int k = 0; k < 50; ++k) {
        CHECK(plant_step(&clamped_input, &params, 100.0f, dt),
              "plant_step rejected over-voltage input");
        CHECK(plant_step(&exact_vmax, &params, params.max_voltage_v, dt),
              "plant_step rejected Vmax input");
    }

    CHECK(clamped_input.current_a == exact_vmax.current_a,
          "voltage above Vmax was not clamped identically (current)");
    CHECK(clamped_input.velocity_rad_s == exact_vmax.velocity_rad_s,
          "voltage above Vmax was not clamped identically (velocity)");
    CHECK(clamped_input.position_rad == exact_vmax.position_rad,
          "voltage above Vmax was not clamped identically (position)");

    return 0;
}

static int test_no_load_steady_state_speed(void)
{
    plant_params_t params;
    plant_default_params(&params);

    plant_state_t state;
    plant_reset(&state);

    const float dt = 0.001f;
    const float v = params.max_voltage_v;

    /* 2 s of simulated time is well beyond the mechanical time constant
     * (tens of ms), so the response should have settled. */
    for (int k = 0; k < 2000; ++k) {
        CHECK(plant_step(&state, &params, v, dt), "plant_step failed");
    }

    const float expected = expected_no_load_omega_ss(&params, v);
    const float rel_error = fabsf(state.velocity_rad_s - expected) / expected;

    CHECK(rel_error < 0.01f, "no-load steady-state speed error exceeds 1%");

    return 0;
}

static int test_loaded_steady_state_speed(void)
{
    plant_params_t params;
    plant_default_params(&params);
    params.load_torque_nm = 0.00005f; /* opposing torque, same units as Kt*i */

    plant_state_t state;
    plant_reset(&state);

    const float dt = 0.001f;
    const float v = params.max_voltage_v;

    for (int k = 0; k < 2000; ++k) {
        CHECK(plant_step(&state, &params, v, dt), "plant_step failed");
    }

    const float expected = expected_loaded_omega_ss(&params, v);
    const float rel_error = fabsf(state.velocity_rad_s - expected) / expected;

    CHECK(rel_error < 0.01f, "loaded steady-state speed error exceeds 1%");
    CHECK(state.velocity_rad_s < expected_no_load_omega_ss(&params, v),
          "load did not reduce steady-state speed as expected");

    return 0;
}

static int test_electrical_time_constant(void)
{
    /* Isolate the electrical subsystem: decoupling Ke/Kt from the mechanical
     * equations means velocity stays at 0, so current follows the textbook
     * RL step response i(t) = (V/R) * (1 - exp(-t*R/L)) exactly. */
    plant_params_t params;
    plant_default_params(&params);
    params.back_emf_v_s_per_rad = 0.0f;
    params.torque_constant_nm_per_a = 0.0f;
    params.substeps = 100U; /* fine substeps for a tight accuracy check */

    plant_state_t state;
    plant_reset(&state);

    const float v = 5.0f;
    const float dt = 0.001f;
    const float tau_e = params.inductance_h / params.resistance_ohm;

    /* Step forward in 1 ms ticks until we pass tau_e, then compare the
     * closest sample to the analytical response at exactly tau_e. */
    int steps_to_tau = (int)(tau_e / dt + 0.5f);
    CHECK(steps_to_tau > 0, "tau_e resolves to zero ticks at dt=1ms");

    for (int k = 0; k < steps_to_tau; ++k) {
        CHECK(plant_step(&state, &params, v, dt), "plant_step failed");
    }

    const float i_final = v / params.resistance_ohm;
    const float expected_i = i_final * (1.0f - expf(-1.0f)); /* t == tau_e */
    const float rel_error = fabsf(state.current_a - expected_i) / i_final;

    CHECK(rel_error < 0.05f, "electrical time constant error exceeds 5%");
    CHECK(fabsf(state.velocity_rad_s) == 0.0f,
          "decoupled electrical test unexpectedly moved velocity");

    return 0;
}

static int test_no_explosion_under_max_voltage(void)
{
    plant_params_t params;
    plant_default_params(&params);

    plant_state_t state;
    plant_reset(&state);

    const float dt = 0.001f;
    const float v = params.max_voltage_v;
    const float omega_ss = expected_no_load_omega_ss(&params, v);

    for (int k = 0; k < 10000; ++k) {
        CHECK(plant_step(&state, &params, v, dt), "plant_step failed");
        CHECK(isfinite(state.current_a), "current became non-finite");
        CHECK(isfinite(state.velocity_rad_s), "velocity became non-finite");
        CHECK(isfinite(state.position_rad), "position became non-finite");
        /* Stable system should not overshoot steady state by more than a
         * generous margin; this is a sanity bound, not a tight tolerance. */
        CHECK(fabsf(state.velocity_rad_s) < omega_ss * 1.5f,
              "velocity exceeded sane bound under sustained Vmax");
    }

    return 0;
}

static int test_determinism(void)
{
    plant_params_t params;
    plant_default_params(&params);

    plant_state_t state_a;
    plant_state_t state_b;
    plant_reset(&state_a);
    plant_reset(&state_b);

    const float dt = 0.001f;

    for (int k = 0; k < 200; ++k) {
        /* Simple deterministic but non-constant input sequence. */
        const float v = (k % 2 == 0) ? 6.0f : -3.0f;

        CHECK(plant_step(&state_a, &params, v, dt), "plant_step failed (a)");
        CHECK(plant_step(&state_b, &params, v, dt), "plant_step failed (b)");

        CHECK(state_a.current_a == state_b.current_a,
              "determinism violated: current diverged");
        CHECK(state_a.velocity_rad_s == state_b.velocity_rad_s,
              "determinism violated: velocity diverged");
        CHECK(state_a.position_rad == state_b.position_rad,
              "determinism violated: position diverged");
    }

    return 0;
}

static int test_rejects_invalid_input(void)
{
    plant_params_t params;
    plant_default_params(&params);

    plant_state_t state;
    plant_reset(&state);
    const plant_state_t before = state;

    CHECK(!plant_step(&state, &params, 1.0f, 0.0f),
          "plant_step accepted dt_s == 0");
    CHECK(!plant_step(&state, &params, 1.0f, -0.001f),
          "plant_step accepted negative dt_s");

    plant_params_t bad_substeps = params;
    bad_substeps.substeps = 0U;
    CHECK(!plant_step(&state, &bad_substeps, 1.0f, 0.001f),
          "plant_step accepted substeps == 0");

    plant_params_t bad_resistance = params;
    bad_resistance.resistance_ohm = 0.0f;
    CHECK(!plant_step(&state, &bad_resistance, 1.0f, 0.001f),
          "plant_step accepted resistance_ohm == 0");

    const float not_finite = 1.0f / 0.0f; /* +inf, deliberate for this test */
    CHECK(!plant_step(&state, &params, not_finite, 0.001f),
          "plant_step accepted non-finite voltage");

    CHECK(state.current_a == before.current_a &&
              state.velocity_rad_s == before.velocity_rad_s &&
              state.position_rad == before.position_rad,
          "state was modified despite rejected input");

    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_voltage_saturation();
    failures += test_no_load_steady_state_speed();
    failures += test_loaded_steady_state_speed();
    failures += test_electrical_time_constant();
    failures += test_no_explosion_under_max_voltage();
    failures += test_determinism();
    failures += test_rejects_invalid_input();

    if (failures != 0) {
        fprintf(stderr, "%d plant test case(s) failed\n", failures);
        return 1;
    }

    return 0;
}
