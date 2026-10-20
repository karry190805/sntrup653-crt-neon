/* B13 十六维加权卷积：标量参考实现与 NEON 辅助函数
 *
 * crypto_core_multsntrup653.c 第 899–983 行，共 85 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
#ifdef SNTRUP653_INTERNAL_TEST_HOOKS
/* Independent schoolbook oracle; never selected by the production entry point. */
static void weighted_mul16_scalar(int out[BS], const int a[BS], const int b[BS], int lambda) {
    int64_t conv[2 * BS - 1] = {0};
    int i, j;
    for (i = 0; i < BS; i++) {
        int ai = a[i];
        for (j = 0; j < BS; j++) conv[i + j] += (int64_t)ai * b[j];
    }
    for (i = 0; i < BS; i++) {
        int64_t v = conv[i];
        if (i + BS < 2 * BS - 1) v += (int64_t)lambda * conv[i + BS];
        out[i] = mod_q_i64_raw(v);
    }
}
#endif

/*
 * Independent C/NEON implementation of the radix-2 + 2xTMVP8 idea used by
 * CRT-with-a-monomial polynomial multiplication.
 *
 * Required from the including translation unit: q and
 * vmod_q_u32_barrett().
 * Inputs and outputs use canonical representatives in [0,q).
 */

static inline int16x8_t w16_reduce_s32_pair(int32x4_t lo, int32x4_t hi) {
    uint32x4_t rlo = vmod_q_u32_barrett(vreinterpretq_u32_s32(lo));
    uint32x4_t rhi = vmod_q_u32_barrett(vreinterpretq_u32_s32(hi));
    return vcombine_s16(vmovn_s32(vreinterpretq_s32_u32(rlo)),
                        vmovn_s32(vreinterpretq_s32_u32(rhi)));
}

static inline int16x8_t w16_mul_const(int16x8_t x, int c) {
    int32x4_t lo = vmull_n_s16(vget_low_s16(x), (int16_t)c);
    int32x4_t hi = vmull_high_n_s16(x, (int16_t)c);
    return w16_reduce_s32_pair(lo, hi);
}

static inline int16x8_t w16_add_mod(int16x8_t a, int16x8_t b) {
    int16x8_t r = vaddq_s16(a, b);
    int16x8_t qv = vdupq_n_s16((int16_t)q);
    int16x8_t mask = vreinterpretq_s16_u16(vcgeq_s16(r, qv));
    return vsubq_s16(r, vandq_s16(mask, qv));
}

static inline int16x8_t w16_sub_mod(int16x8_t a, int16x8_t b) {
    int16x8_t r = vsubq_s16(a, b);
    int16x8_t qv = vdupq_n_s16((int16_t)q);
    int16x8_t mask = vreinterpretq_s16_u16(vcltq_s16(r, vdupq_n_s16(0)));
    return vaddq_s16(r, vandq_s16(mask, qv));
}

/* Accumulate T(upper,col)*vec. col is the first matrix column, while
 * upper[j] supplies the wrapped upper diagonal for displacement j. */
static inline void w16_tmvp8_mla(int16x8_t upper, int16x8_t col, int16x8_t vec,
                                 int32x4_t *acc_lo, int32x4_t *acc_hi) {
#define W16_MLA_COLUMN(LANE, COLUMN) do {                                      \
        *acc_lo = vmlal_laneq_s16(*acc_lo, vget_low_s16(COLUMN), vec, LANE);   \
        *acc_hi = vmlal_high_laneq_s16(*acc_hi, COLUMN, vec, LANE);            \
    } while (0)
    W16_MLA_COLUMN(0, col);
    W16_MLA_COLUMN(1, vextq_s16(upper, col, 7));
    W16_MLA_COLUMN(2, vextq_s16(upper, col, 6));
    W16_MLA_COLUMN(3, vextq_s16(upper, col, 5));
    W16_MLA_COLUMN(4, vextq_s16(upper, col, 4));
    W16_MLA_COLUMN(5, vextq_s16(upper, col, 3));
    W16_MLA_COLUMN(6, vextq_s16(upper, col, 2));
    W16_MLA_COLUMN(7, vextq_s16(upper, col, 1));
#undef W16_MLA_COLUMN
}

/* Product in F_q[x]/(x^8-weight), expressed as one 8-by-8 TMVP. */
static inline int16x8_t w16_weighted_tmvp8(int16x8_t a, int16x8_t b,
                                           int weight) {
    int16x8_t weighted_b = w16_mul_const(b, weight);
    int32x4_t acc_lo = vdupq_n_s32(0);
    int32x4_t acc_hi = vdupq_n_s32(0);

    /* weighted_b[1..7] stores the upper diagonals in reverse-use order:
     * column j begins with weight*b[8-j], then b[0],b[1],... . */
    w16_tmvp8_mla(weighted_b, b, a, &acc_lo, &acc_hi);
    return w16_reduce_s32_pair(acc_lo, acc_hi);
}
