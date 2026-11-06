static SNTRUP_MAYBE_UNUSED void low_part73_scalar(int clow[LOW_NP], const Fq f[p], const small g[p]) {
    int kk, i;
    for (kk = 0; kk < LOW_N; kk++) {
        int64_t acc = 0;
        for (i = 0; i <= kk; i++) acc += (int64_t)f[i] * g[kk - i];
        clow[kk] = mod_q_i64_raw(acc);
    }
}

static void low_part73_neon(int clow[LOW_NP], const Fq f[p], const small g[p]) {
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && SNTRUP653_LOW73_NEON
    /* Triangular convolution clow[k] = sum_{i<=k} f[i]*g[k-i], evaluated
     * eight outputs at a time.  For output chunk c (k = 8c..8c+7) the term
     * i contributes f[i] times the eight consecutive g values
     * g[8c-i .. 8c+7-i]; negative indices land in a zero prefix, so every
     * lane may be accumulated unconditionally.  Four independent int32
     * accumulator pairs keep the vmlal chains short. */
    enum { GPAD = 8 + 80 };
    int16_t gpad[GPAD] SNTRUP_ALIGN64;
    const int32_t bias = (int32_t)(LOW_N * q);
    int i, c;
    for (i = 0; i < GPAD; i++) gpad[i] = 0;
    for (i = 0; i < LOW_N; i++) gpad[8 + i] = (int16_t)g[i];
    for (c = 0; c < LOW_NP / 8; c++) {
        const int base = 8 * c;
        const int hi = base + 7;
        int32x4_t a0 = vdupq_n_s32(0), a1 = vdupq_n_s32(0);
        int32x4_t b0 = vdupq_n_s32(0), b1 = vdupq_n_s32(0);
        int32x4_t c0 = vdupq_n_s32(0), c1 = vdupq_n_s32(0);
        int32x4_t d0 = vdupq_n_s32(0), d1 = vdupq_n_s32(0);
        for (i = 0; i + 3 <= hi; i += 4) {
            int16x8_t g0 = vld1q_s16(&gpad[8 + base - i]);
            int16x8_t g1 = vld1q_s16(&gpad[8 + base - i - 1]);
            int16x8_t g2 = vld1q_s16(&gpad[8 + base - i - 2]);
            int16x8_t g3 = vld1q_s16(&gpad[8 + base - i - 3]);
            int16x8_t fv = vld1q_s16(&f[i]);
            a0 = vmlal_laneq_s16(a0, vget_low_s16(g0), fv, 0);
            a1 = vmlal_high_laneq_s16(a1, g0, fv, 0);
            b0 = vmlal_laneq_s16(b0, vget_low_s16(g1), fv, 1);
            b1 = vmlal_high_laneq_s16(b1, g1, fv, 1);
            c0 = vmlal_laneq_s16(c0, vget_low_s16(g2), fv, 2);
            c1 = vmlal_high_laneq_s16(c1, g2, fv, 2);
            d0 = vmlal_laneq_s16(d0, vget_low_s16(g3), fv, 3);
            d1 = vmlal_high_laneq_s16(d1, g3, fv, 3);
        }
        a0 = vaddq_s32(vaddq_s32(a0, b0), vaddq_s32(c0, d0));
        a1 = vaddq_s32(vaddq_s32(a1, b1), vaddq_s32(c1, d1));
        /* Bias by LOW_N*q (== 0 mod q) so the possibly negative convolution
         * is fed to the unsigned Barrett reducer. */
        a0 = vaddq_s32(a0, vdupq_n_s32(bias));
        a1 = vaddq_s32(a1, vdupq_n_s32(bias));
        a0 = vreinterpretq_s32_u32(vmod_q_u32_barrett(vreinterpretq_u32_s32(a0)));
        a1 = vreinterpretq_s32_u32(vmod_q_u32_barrett(vreinterpretq_u32_s32(a1)));
        vst1q_s32(&clow[base], a0);
        vst1q_s32(&clow[base + 4], a1);
    }
#else
    low_part73_scalar(clow, f, g);
#endif
}

