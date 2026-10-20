/* B09 NEON 约减与存储辅助函数
 *
 * crypto_core_multsntrup653.c 第 527–587 行，共 61 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static inline uint32x4_t vmod_q_u32_barrett(uint32x4_t x) {
#if USE_BARRETT_REDUCE
    uint32x2_t xlo = vget_low_u32(x);
    uint32x2_t xhi = vget_high_u32(x);
    uint64x2_t prod_lo = vmull_n_u32(xlo, BARRETT_MU_Q32);
    uint64x2_t prod_hi = vmull_n_u32(xhi, BARRETT_MU_Q32);
    uint32x2_t qhat_lo = vshrn_n_u64(prod_lo, 32);
    uint32x2_t qhat_hi = vshrn_n_u64(prod_hi, 32);
    uint32x4_t qhat = vcombine_u32(qhat_lo, qhat_hi);
    uint32x4_t qvec = vdupq_n_u32((uint32_t)q);
    uint32x4_t r = vsubq_u32(x, vmulq_u32(qhat, qvec));
    uint32x4_t ge = vcgeq_u32(r, qvec);
    r = vsubq_u32(r, vandq_u32(ge, qvec));
    ge = vcgeq_u32(r, qvec);
    r = vsubq_u32(r, vandq_u32(ge, qvec));
    return r;
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
static inline int32x4_t add_q_if_negative_s32x4(int32x4_t x) {
    uint32x4_t neg = vcltq_s32(x, vdupq_n_s32(0));
    int32x4_t add = vreinterpretq_s32_u32(vandq_u32(neg, vdupq_n_u32((uint32_t)q)));
    return vaddq_s32(x, add);
}

/* Pairwise direct DFT over block16 lanes.
 * Computes Y_k and Y_{n-k} together using
 *   S_j = A_j + A_{n-j}, D_j = A_j - A_{n-j},
 *   P_{k,j}=(w^{kj}+w^{-kj})/2, M_{k,j}=(w^{kj}-w^{-kj})/2.
 * The D_j branch is represented modulo q to keep NEON accumulators non-negative.
 */