#include "limits.h"

float limits_clamp(float x, float lo, float hi)
{
    if (x != x) {
        return 0.0f;
    }
    if (lo > hi) {
        const float t = lo;
        lo = hi;
        hi = t;
    }
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

float limits_rate_limit(float prev, float target, float max_delta)
{
    if (target != target) {
        return prev;
    }
    if (max_delta <= 0.0f) {
        return target;
    }
    const float d = target - prev;
    if (d > max_delta) {
        return prev + max_delta;
    }
    if (d < -max_delta) {
        return prev - max_delta;
    }
    return target;
}
