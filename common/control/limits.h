/*
 * OS-free limiting helpers (M1 Part 4): clamp and rate limit.
 * No FreeRTOS/OS dependency; shared by host tests and the control task.
 */
#ifndef M1_CONTROL_LIMITS_H
#define M1_CONTROL_LIMITS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Clamps x into [lo, hi]. If lo > hi the bounds are swapped. NaN input
 * returns 0.0f (fail-safe neutral output). */
float limits_clamp(float x, float lo, float hi);

/* Moves from prev toward target by at most max_delta (>= 0) per call.
 * max_delta <= 0 disables the limit (returns target). NaN target returns prev. */
float limits_rate_limit(float prev, float target, float max_delta);

#ifdef __cplusplus
}
#endif

#endif /* M1_CONTROL_LIMITS_H */
