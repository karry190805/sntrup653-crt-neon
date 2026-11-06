static inline uint32x4_t vmod_q_u32_barrett(uint32x4_t x) {
#if USE_BARRETT_REDUCE
    /* Every caller has 0 <= x < 2^31, so x is a positive signed 32-bit
     * value.  With M = floor(2^31/q), SQDMULH(x, M) computes
     * floor(2*x*M/2^32) = floor(x*M/2^31), which is <= floor(x/q) because
     * M <= 2^31/q, and at most one below it because x < 2^31.  Hence
     * r = x - qhat*q is in [0,2q) and one min(r, r-q) canonicalises it. */
    const uint32x4_t qvec = vdupq_n_u32((uint32_t)q);
    int32x4_t qhat = vqdmulhq_s32(vreinterpretq_s32_u32(x),
                                  vdupq_n_s32(BARRETT_MU_Q31));
    uint32x4_t r = vmlsq_n_u32(x, vreinterpretq_u32_s32(qhat),
                               (uint32_t)q);
    return vminq_u32(r, vsubq_u32(r, qvec));
#else
    uint32_t t[4];
    vst1q_u32(t, x);
    t[0] = (uint32_t)mod_q_i64_raw((int64_t)t[0]);
    t[1] = (uint32_t)mod_q_i64_raw((int64_t)t[1]);
    t[2] = (uint32_t)mod_q_i64_raw((int64_t)t[2]);
    t[3] = (uint32_t)mod_q_i64_raw((int64_t)t[3]);
    return vld1q_u32(t);
#endif
}

/* Precondition: all lanes in x are non-negative DFT accumulators.
 * Lazy variant: only divide out q, leaving the representative in [0,2q).
 * The next transform layer reduces modulo q anyway, so the final
 * canonicalising min can be deferred. */
static inline void store_mod_q2_s32x4(int *dst, int32x4_t x) {
#if USE_BARRETT_REDUCE
    uint32x4_t xu = vreinterpretq_u32_s32(x);
    int32x4_t qhat = vqdmulhq_s32(vreinterpretq_s32_u32(xu),
                                  vdupq_n_s32(BARRETT_MU_Q31));
    uint32x4_t r = vmlsq_n_u32(xu, vreinterpretq_u32_s32(qhat),
                               (uint32_t)q);
    vst1q_s32(dst, vreinterpretq_s32_u32(r));
#else
    /* Canonical [0,q) is also a valid [0,2q) representative. */
    vst1q_s32(dst, vreinterpretq_s32_u32(
                       vmod_q_u32_barrett(vreinterpretq_u32_s32(x))));
#endif
}

/* Precondition: all lanes in x are non-negative DFT accumulators. */
static inline void store_mod_q_s32x4(int *dst, int32x4_t x) {
    uint32x4_t red = vmod_q_u32_barrett(vreinterpretq_u32_s32(x));
    vst1q_s32(dst, vreinterpretq_s32_u32(red));
}

/* Precondition: all lanes in x are non-negative DFT accumulators. */
static inline void store_mod_q_inv77_s32x4(int *dst, int32x4_t x, int invert) {
    uint32x4_t red = vmod_q_u32_barrett(vreinterpretq_u32_s32(x));
    if (invert) {
        uint32x4_t invv = vdupq_n_u32((uint32_t)INV77);
        red = vmod_q_u32_barrett(vmulq_u32(red, invv));
    }
    vst1q_s32(dst, vreinterpretq_s32_u32(red));
}
#endif


/* Pairwise-output NEON 11/7-point DFT subkernels. */

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
/* Same trick with the wider range produced by the lazy first layer:
 * x = a - b with a,b in [0,2q), so x in (-2q,2q) and adding 2q canonicalises. */
static inline int32x4_t add_q2_if_negative_s32x4(int32x4_t x) {
    uint32x4_t u = vreinterpretq_u32_s32(x);
    uint32x4_t qv = vdupq_n_u32((uint32_t)(2 * q));
    return vreinterpretq_s32_u32(vminq_u32(u, vaddq_u32(u, qv)));
}

static inline int32x4_t add_q_if_negative_s32x4(int32x4_t x) {
    /* x is a difference of two values in [0,q), so x is in (-q,q).  Viewed
     * as unsigned, a negative lane wraps near 2^32; x+q then wraps back to
     * x+q, which is smaller, while for x>=0 the wrapped x+q is larger than
     * x.  min(x, x+q) therefore performs the conditional add in two
     * instructions. */
    uint32x4_t u = vreinterpretq_u32_s32(x);
    uint32x4_t qv = vdupq_n_u32((uint32_t)q);
    return vreinterpretq_s32_u32(vminq_u32(u, vaddq_u32(u, qv)));
}

/* Pairwise direct DFT over block16 lanes.
 * Computes Y_k and Y_{n-k} together using
 *   S_j = A_j + A_{n-j}, D_j = A_j - A_{n-j},
 *   P_{k,j}=(w^{kj}+w^{-kj})/2, M_{k,j}=(w^{kj}-w^{-kj})/2.
 * The D_j branch is represented modulo q to keep NEON accumulators non-negative.
 */
