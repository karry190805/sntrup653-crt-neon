static SNTRUP_MAYBE_UNUSED void final_folding_scalar(Fq out[p], const Blocks77 Cblk, const int clow[LOW_N]) {
    int kk;
    {
        int v0 = add_q_raw(D_idx(Cblk, clow, 0), D_idx(Cblk, clow, p));
        out[0] = center_q_raw(v0);
    }
    for (kk = 1; kk <= p - 2; kk++) {
        int v = add_q_raw(D_idx(Cblk, clow, kk), D_idx(Cblk, clow, p - 1 + kk));
        v = add_q_raw(v, D_idx(Cblk, clow, p + kk));
        out[kk] = center_q_raw(v);
    }
    out[p - 1] = center_q_raw(add_q_raw(D_idx(Cblk, clow, p - 1), D_idx(Cblk, clow, 2 * p - 2)));
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
/* Reduce four non-negative-or-slightly-negative fold sums to centered Fq.
 * The fixed 4q bias makes every lane non-negative; the Barrett step then
 * returns [0,q) and the compare/subtract centers it exactly like
 * center_q_raw(). */
static inline int32x4_t center4(int32x4_t v) {
    const int32x4_t half = vdupq_n_s32(q / 2);
    const int32x4_t qv = vdupq_n_s32(q);
    return vbslq_s32(vcgtq_s32(v, half), vsubq_s32(v, qv), v);
}

/* Same, for fold sums already known to be non-negative (sums of two or three
 * non-negative inverse-transform accumulators plus, at most, one clow term). */
static inline void store_centered4_nb(Fq *dst, int32x4_t s) {
    vst1_s16(dst, vmovn_s32(center4(vreinterpretq_s32_u32(
        vmod_q_u32_barrett(vreinterpretq_u32_s32(s))))));
}

static inline void store_centered4(Fq *dst, int32x4_t s) {
    /* May receive unreduced inverse-transform accumulators (< 2^28).  The
     * only negative terms are of the form main[j]-clow[j] with main[j] >= 0,
     * so a 4q bias still makes every lane non-negative and the total stays
     * well below 2^31. */
    const int32x4_t bias = vdupq_n_s32(4 * q);
    const int32x4_t half = vdupq_n_s32(q / 2);
    const uint32x4_t qv = vdupq_n_u32((uint32_t)q);
    uint32x4_t r = vmod_q_u32_barrett(vreinterpretq_u32_s32(vaddq_s32(s, bias)));
    int32x4_t v = vreinterpretq_s32_u32(r);
    uint32x4_t gt = vcgtq_s32(v, half);
    v = vsubq_s32(v, vreinterpretq_s32_u32(vandq_u32(gt, qv)));
    vst1_s16(dst, vmovn_s32(v));
}
#endif

static void final_folding_neon(Fq out[p], const Blocks77 Cblk, const int clow[LOW_N]) {
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && SNTRUP653_FINAL_FOLD_NEON
    const int *main = &Cblk[0][0];
    int kk;
    out[0] = center_q_raw((int64_t)clow[0] + main[p]);

    for (kk = 1; kk + 3 <= LOW_N - 1; kk += 4) {
        int32x4_t s = vaddq_s32(vld1q_s32(&clow[kk]), vld1q_s32(&main[p - 1 + kk]));
        s = vaddq_s32(s, vld1q_s32(&main[p + kk]));
        store_centered4_nb(&out[kk], s);
    }
    for (; kk < LOW_N; kk++) {
        out[kk] = center_q_raw((int64_t)clow[kk] + main[p - 1 + kk] + main[p + kk]);
    }

    for (kk = LOW_N; kk + 3 <= p - LOW_N - 2; kk += 4) {
        int32x4_t s = vaddq_s32(vld1q_s32(&main[kk]), vld1q_s32(&main[p - 1 + kk]));
        s = vaddq_s32(s, vld1q_s32(&main[p + kk]));
        store_centered4_nb(&out[kk], s);
    }
    for (; kk <= p - LOW_N - 2; kk++) {
        out[kk] = center_q_raw((int64_t)main[kk] + main[p - 1 + kk] + main[p + kk]);
    }

    kk = p - LOW_N - 1;
    out[kk] = center_q_raw((int64_t)main[kk] + main[MAIN_N - 1]
                           + main[0] - clow[0]);

    for (kk++; kk + 3 <= p - 2; kk += 4) {
        int j0 = kk - (p - LOW_N);
        int j1 = kk - (p - LOW_N - 1);
        int32x4_t s = vld1q_s32(&main[kk]);
        int32x4_t t0 = vsubq_s32(vld1q_s32(&main[j0]), vld1q_s32(&clow[j0]));
        int32x4_t t1 = vsubq_s32(vld1q_s32(&main[j1]), vld1q_s32(&clow[j1]));
        s = vaddq_s32(vaddq_s32(s, t0), t1);
        store_centered4(&out[kk], s);
    }
    for (; kk <= p - 2; kk++) {
        int j0 = kk - (p - LOW_N);
        int j1 = kk - (p - LOW_N - 1);
        out[kk] = center_q_raw((int64_t)main[kk] + main[j0] - clow[j0]
                               + main[j1] - clow[j1]);
    }
    out[p - 1] = center_q_raw((int64_t)main[p - 1] + main[LOW_N - 1] - clow[LOW_N - 1]);
#else
    final_folding_scalar(out, Cblk, clow);
#endif
}

#ifdef SNTRUP653_INTERNAL_TEST_HOOKS
/* ---------------------------------------------------------------------------
 * Math fingerprint.
 *
 * The fingerprint hashes every parameter and every derived constant that the
 * polynomial multiplication depends on: the field, the 77-point Good--Thomas
 * index maps, the direct 7/11-point DFT tables, the packed 16-bit Shoup
 * tables, the radix-2 / TMVP weights, and the public pruning schedule.  It is
 * independent of the optimisation switches (those choose code paths, not
 * mathematics), so all build variants and all compilers must print the same
 * value.  This makes "the device-specific optimisation did not touch the
 * mathematics" a checkable byte-level claim instead of an assertion.
 *
 * Test-only: the whole function lives behind SNTRUP653_INTERNAL_TEST_HOOKS and
 * is removed from the production view by check_selftest_isolation.py.
 * ------------------------------------------------------------------------- */
