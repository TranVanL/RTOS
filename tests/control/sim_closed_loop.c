/* CLI: sim_closed_loop <step|load|noise> > out.csv ; metrics to stderr. */
#include "sim_loop.h"
#include <string.h>

int main(int argc, char **argv)
{
    sim_cfg_t c;
    sim_default_cfg(&c);
    const char *sc = (argc > 1) ? argv[1] : "step";
    if (strcmp(sc, "load") == 0) { c.load_nm = 0.05f; c.t_end_s = 1.5f; }
    else if (strcmp(sc, "noise") == 0) { c.noise_std_rad = 0.0015f; }
    else if (strcmp(sc, "step") != 0) { fprintf(stderr, "usage: %s step|load|noise\n", argv[0]); return 2; }
    sim_result_t r;
    fprintf(stdout, "t,ref,pos,vel,v,count\n");
    if (sim_run(&c, &r, stdout) != 0) { fprintf(stderr, "sim failed\n"); return 1; }
    fprintf(stderr, "%s: overshoot=%.2f%% settling=%.0f ms ss_err=%.3f counts vpeak=%.2f V maxdev_load=%.4f rad\n",
            sc, (double)r.overshoot_pct, (double)(r.settling_s * 1000.0f), (double)r.ss_error_counts,
            (double)r.v_peak, (double)r.max_dev_after_load_rad);
    return 0;
}
