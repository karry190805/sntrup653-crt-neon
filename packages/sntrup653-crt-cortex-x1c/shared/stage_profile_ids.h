#ifndef SNTRUP653_STAGE_PROFILE_IDS_H
#define SNTRUP653_STAGE_PROFILE_IDS_H

/* Shared definitions for the diagnostic stage profiler.
 *
 * Only used when SNTRUP653_STAGE_PROFILE is defined.  Every mark appends its
 * compile-time id and one cntvct_el0 timestamp; the driver validates the id
 * sequence and turns the timestamps into per-stage durations.
 */
#define SNTRUP653_STAGE_MAX 32

enum sntrup653_stage_id {
    SNTRUP_STAGE_blockify_A = 0,
    SNTRUP_STAGE_blockify_B,
    SNTRUP_STAGE_fwd_dft11,
    SNTRUP_STAGE_fwd_dft7,
    SNTRUP_STAGE_pointwise,
    SNTRUP_STAGE_inv_dft11,
    SNTRUP_STAGE_inv_dft7,
    SNTRUP_STAGE_low73,
    SNTRUP_STAGE_final_fold,
    SNTRUP_STAGE_encode,
    SNTRUP_STAGE_end,
    SNTRUP_STAGE_COUNT
};

extern unsigned long long sntrup653_stage_ts[SNTRUP653_STAGE_MAX];
extern unsigned char sntrup653_stage_ids[SNTRUP653_STAGE_MAX];
extern int sntrup653_stage_idx;

#endif
