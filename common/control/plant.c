#include "plant.h"

#include <math.h>
#include <stddef.h>
#include <stdbool.h>
void plant_default_params(plant_params_t *params)
{
    if (params == NULL) {
        return;
    }

    params->resistance_ohm = 2.0f;
    params->inductance_h = 0.01f;
    params->back_emf_v_s_per_rad = 0.05f;
    params->torque_constant_nm_per_a = 0.05f;
    params->inertia_kg_m2 = 0.00005f;
    params->viscous_friction_nm_s_per_rad = 0.0002f;
    params->max_voltage_v = 12.0f;
    params->load_torque_nm = 0.0f;
    params->substeps = 10U;
}

void plant_reset(plant_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->current_a = 0.0f;
    state->velocity_rad_s = 0.0f;
    state->position_rad = 0.0f;
}

static bool plant_params_valid(const plant_params_t *params)
{
    if (!(params->resistance_ohm > 0.0f) ||
        !(params->inductance_h > 0.0f) ||
        !(params->inertia_kg_m2 > 0.0f) ||
        !(params->max_voltage_v > 0.0f)) {
        return false;
    }

    if (!(params->viscous_friction_nm_s_per_rad >= 0.0f)) {
        return false;
    }

    if (params->substeps == 0U) {
        return false;
    }

    if (!isfinite(params->back_emf_v_s_per_rad) ||
        !isfinite(params->torque_constant_nm_per_a) ||
        !isfinite(params->load_torque_nm)) {
        return false;
    }

    return true;
}

static float clampf(float value, float limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

bool plant_step(
    plant_state_t *state,
    const plant_params_t *params,
    float voltage_v,
    float dt_s
)
{
    if (state == NULL || params == NULL) {
        return false;
    }

    if (!(dt_s > 0.0f) || !isfinite(dt_s)) {
        return false;
    }

    if (!isfinite(voltage_v)) {
        return false;
    }

    if (!plant_params_valid(params)) {
        return false;
    }

    const float applied_voltage_v = clampf(voltage_v, params->max_voltage_v);
    const float h = dt_s / (float)params->substeps;

    plant_state_t work = *state;

    for (uint32_t step = 0U; step < params->substeps; ++step) {
        const float di_dt =
            (applied_voltage_v
             - params->resistance_ohm * work.current_a
             - params->back_emf_v_s_per_rad * work.velocity_rad_s)
            / params->inductance_h;

        const float domega_dt =
            (params->torque_constant_nm_per_a * work.current_a
             - params->viscous_friction_nm_s_per_rad * work.velocity_rad_s
             - params->load_torque_nm)
            / params->inertia_kg_m2;

        const float dtheta_dt = work.velocity_rad_s;

        work.current_a += h * di_dt;
        work.velocity_rad_s += h * domega_dt;
        work.position_rad += h * dtheta_dt;

        if (!isfinite(work.current_a) ||
            !isfinite(work.velocity_rad_s) ||
            !isfinite(work.position_rad)) {
            return false;
        }
    }

    *state = work;
    return true;
}
