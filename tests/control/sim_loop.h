/* Host closed-loop simulation: plant + encoder + speed estimator + cascade (M1 Part 5). Test/tool code only. */
#ifndef M1_SIM_LOOP_H
#define M1_SIM_LOOP_H

#include <stdio.h>
#include "pid.h"
#include "plant.h"
#include "encoder.h"

typedef struct {
    float step_rad;        /* position reference step at t=0 */
    float t_end_s;
    float load_nm;         /* external load torque step (0 = none) */
    float load_t_s;        /* time the load is applied */
    float noise_std_rad;   /* encoder noise, 0 = off */
    uint32_t seed;
    float speed_alpha;     /* speed estimator filter */
    cascade_params_t ctrl;
    plant_params_t plant;
} sim_cfg_t;

typedef struct {
    float overshoot_pct;   /* (peak - step)/step*100, <0 clamped to 0 */
    float settling_s;      /* last time outside +-2% band of step; (t_end if never settles) */
    float ss_error_counts; /* |ref - theta| at end, in encoder counts */
    float max_dev_after_load_rad; /* max |ref - theta| after load applied (0 if none) */
    float v_peak;
} sim_result_t;

#define SIM_DT_S 0.001f
#define SIM_COUNTS_PER_REV 4096U

/* Default scenario: step 1 rad, 1 s, default plant/controller, no load, no noise. */
void sim_default_cfg(sim_cfg_t *cfg);

/* Runs the scenario. If csv != NULL writes "t,ref,pos,vel,v,count" rows. Returns 0 on success. */
int sim_run(const sim_cfg_t *cfg, sim_result_t *out, FILE *csv);

#endif
