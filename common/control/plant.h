/*
 * OS-free DC motor plant model (M1 Part 2).
 *
 * Physical model (PM DC motor, electrical + mechanical state), units are SI:
 *   L * di/dt     = V - R*i - Ke*omega
 *   J * domega/dt = Kt*i - b*omega - load_torque
 *   dtheta/dt     = omega
 *
 * This header has no dependency on FreeRTOS or any OS: it is included and
 * built both by host unit tests and later by the FreeRTOS control task.
 * Do not add OS, driver, or I/O includes to this file.
 */
#ifndef M1_CONTROL_PLANT_H
#define M1_CONTROL_PLANT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Physical parameters of the motor plant. Illustrative SIL values only;
 * not identified from a real motor. */
typedef struct {
    float resistance_ohm;           /* R  [ohm] */
    float inductance_h;             /* L  [H] */
    float back_emf_v_s_per_rad;     /* Ke [V*s/rad] */
    float torque_constant_nm_per_a; /* Kt [N*m/A] */
    float inertia_kg_m2;            /* J  [kg*m^2] */
    float viscous_friction_nm_s_per_rad; /* b [N*m*s/rad] */
    float max_voltage_v;            /* +-Vmax [V], must be > 0 */
    float load_torque_nm;           /* tau_load [N*m], signed, subtracted from motor torque */
    uint32_t substeps;              /* internal Euler substeps per plant_step() call, >= 1 */
} plant_params_t;

/* Mutable plant state. Caller owns and stores this; plant_step() updates it
 * in place and never allocates memory. */
typedef struct {
    float current_a;       /* i     [A] */
    float velocity_rad_s;   /* omega [rad/s] */
    float position_rad;     /* theta [rad] */
} plant_state_t;

/* Fills params with illustrative defaults documented in docs/m1_plan.md
 * Part 2 (R=2 ohm, L=10 mH, Ke=Kt=0.05, J=5e-5, b=2e-4, Vmax=12 V,
 * load=0, substeps=10). Does not validate; caller may override fields
 * before use. */
void plant_default_params(plant_params_t *params);

/* Resets state to rest (i=0, omega=0, theta=0). */
void plant_reset(plant_state_t *state);

/*
 * Advances the plant by dt_s using substeps internal forward-Euler steps.
 * Clamps the applied voltage to +-max_voltage_v before integrating.
 *
 * Returns false (and leaves *state unchanged) if state/params are NULL,
 * dt_s <= 0, substeps == 0, any physical parameter is non-positive where
 * required, or voltage_v is not finite. Returns false if integration
 * produces a non-finite state (does not update *state in that case either).
 * Returns true otherwise, with *state updated to the new values.
 */
bool plant_step(
    plant_state_t *state,
    const plant_params_t *params,
    float voltage_v,
    float dt_s
);

#ifdef __cplusplus
}
#endif

#endif /* M1_CONTROL_PLANT_H */
