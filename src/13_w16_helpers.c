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

/* Same as w16_reduce_s32_pair(), but without the canonicalising min: the
 * caller only needs a representative in [0,2q) (still < 2^15, so the int16
 * narrowing is exact) because the following wide add/sub-mod folds the
 * doubled range back into a valid Shoup operand.  2 ALU per int32 vector. */
static inline int16x8_t w16_reduce_s32_pair_lazy(int32x4_t lo, int32x4_t hi) {
#if USE_BARRETT_REDUCE
    const int32x4_t mu = vdupq_n_s32(BARRETT_MU_Q31);
    uint32x4_t rlo = vmlsq_n_u32(vreinterpretq_u32_s32(lo),
                                 vreinterpretq_u32_s32(vqdmulhq_s32(lo, mu)),
                                 (uint32_t)q);
    uint32x4_t rhi = vmlsq_n_u32(vreinterpretq_u32_s32(hi),
                                 vreinterpretq_u32_s32(vqdmulhq_s32(hi, mu)),
                                 (uint32_t)q);
#else
    uint32x4_t rlo = vmod_q_u32_barrett(vreinterpretq_u32_s32(lo));
    uint32x4_t rhi = vmod_q_u32_barrett(vreinterpretq_u32_s32(hi));
#endif
    return vcombine_s16(vmovn_s32(vreinterpretq_s32_u32(rlo)),
                        vmovn_s32(vreinterpretq_s32_u32(rhi)));
}

static inline int16x8_t w16_reduce_s32_pair(int32x4_t lo, int32x4_t hi) {
    uint32x4_t rlo = vmod_q_u32_barrett(vreinterpretq_u32_s32(lo));
    uint32x4_t rhi = vmod_q_u32_barrett(vreinterpretq_u32_s32(hi));
    return vcombine_s16(vmovn_s32(vreinterpretq_s32_u32(rlo)),
                        vmovn_s32(vreinterpretq_s32_u32(rhi)));
}

/* Constant multiplication by c mod q using a 16-bit Shoup multiplier.
 * c' = floor(c * 2^15 / q).  For 0 <= x < q, VQRDMULH(x,c') computes
 * qhat = round(x*c'/2^15) in {floor(x*c/q), floor(x*c/q)+1}; therefore
 * r = x*c - qhat*q lies in (-q,q).  The low 16 bits of this subtraction are
 * exact modulo 2^16, and min_u16(r, r+q) turns them into the canonical
 * residue: for r>=0 it picks r, for r<0 the wrapped r+q is smaller.
 * Only five NEON instructions per eight lanes, with no 32-bit widening. */
static inline int16x8_t w16_mul_const_shoup(int16x8_t x, uint16_t c,
                                            uint16_t cprime) {
    /* Modulo-2^16 wraparound is intentional (see the comment above), so the
     * product and the qhat*q subtraction are done on unsigned 16-bit lanes,
     * where the C semantics are the hardware wraparound. */
    const uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vmulq_u16(vreinterpretq_u16_s16(x), vdupq_n_u16(c));
    uint16x8_t qhat = vreinterpretq_u16_s16(
        vqrdmulhq_s16(x, vdupq_n_s16((int16_t)cprime)));
    r = vmlsq_u16(r, qhat, qv);
    return vreinterpretq_s16_u16(vminq_u16(r, vaddq_u16(r, qv)));
}

/* Same as w16_mul_const_shoup(), but the two multipliers are already
 * replicated over eight lanes. */
static inline int16x8_t w16_mul_const_shoup_v(int16x8_t x, int16x8_t c,
                                              int16x8_t cprime) {
    const uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vmulq_u16(vreinterpretq_u16_s16(x),
                             vreinterpretq_u16_s16(c));
    uint16x8_t qhat = vreinterpretq_u16_s16(vqrdmulhq_s16(x, cprime));
    r = vmlsq_u16(r, qhat, qv);
    return vreinterpretq_s16_u16(vminq_u16(r, vaddq_u16(r, qv)));
}

static inline int16x8_t w16_add_mod(int16x8_t a, int16x8_t b) {
    /* a+b < 2q.  As unsigned, r-q wraps near 2^16 when r<q, so
     * min_u16(r, r-q) is the canonical residue. */
    uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vreinterpretq_u16_s16(vaddq_s16(a, b));
    return vreinterpretq_s16_u16(vminq_u16(r, vsubq_u16(r, qv)));
}

/* Same as w16_sub_mod(), but valid for a,b in [0,2q).  The lazy forward
 * transform stores representatives in [0,2q); a-b+2q then lies in (0,4q) and
 * one conditional subtraction brings it back to [0,3q), which is a safe
 * operand range for the TMVP (its 8-term accumulator stays below 2^31). */
static inline int16x8_t w16_sub_mod_wide(int16x8_t a, int16x8_t b) {
    uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vreinterpretq_u16_s16(
        vaddq_s16(vsubq_s16(a, b), vdupq_n_s16((int16_t)(2 * q))));
    return vreinterpretq_s16_u16(vminq_u16(r, vsubq_u16(r, qv)));
}

static inline int16x8_t w16_sub_mod(int16x8_t a, int16x8_t b) {
    /* a-b is in (-q,q).  As unsigned, a negative lane wraps near 2^16 and
     * adding q wraps back into [0,q), so min_u16(r, r+q) canonicalises. */
    uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vreinterpretq_u16_s16(vsubq_s16(a, b));
    return vreinterpretq_s16_u16(vminq_u16(r, vaddq_u16(r, qv)));
}

/* Accumulate T(upper,col)*vec. col is the first matrix column, while
 * upper[j] supplies the wrapped upper diagonal for displacement j. */
static inline void w16_tmvp8_mla(int16x8_t upper, int16x8_t col, int16x8_t vec,
                                 int32x4_t *acc_lo, int32x4_t *acc_hi) {
    /* Four independent accumulator pairs: the eight 8x8 columns would
     * otherwise form one depth-8 vmlal dependency chain. */
    int32x4_t lo0 = *acc_lo, hi0 = *acc_hi;
    int32x4_t lo1 = vdupq_n_s32(0), hi1 = vdupq_n_s32(0);
#define W16_MLA_COLUMN(ALO, AHI, LANE, COLUMN) do {                            \
        ALO = vmlal_laneq_s16(ALO, vget_low_s16(COLUMN), vec, LANE);           \
        AHI = vmlal_high_laneq_s16(AHI, COLUMN, vec, LANE);                    \
    } while (0)
    W16_MLA_COLUMN(lo0, hi0, 0, col);
    W16_MLA_COLUMN(lo0, hi0, 1, vextq_s16(upper, col, 7));
    W16_MLA_COLUMN(lo0, hi0, 2, vextq_s16(upper, col, 6));
    W16_MLA_COLUMN(lo0, hi0, 3, vextq_s16(upper, col, 5));
    W16_MLA_COLUMN(lo1, hi1, 4, vextq_s16(upper, col, 4));
    W16_MLA_COLUMN(lo1, hi1, 5, vextq_s16(upper, col, 3));
    W16_MLA_COLUMN(lo1, hi1, 6, vextq_s16(upper, col, 2));
    W16_MLA_COLUMN(lo1, hi1, 7, vextq_s16(upper, col, 1));
#undef W16_MLA_COLUMN
    *acc_lo = vaddq_s32(lo0, lo1);
    *acc_hi = vaddq_s32(hi0, hi1);
}

/* As above, with the weight's Shoup multipliers supplied as eight-lane
 * constants. */
static inline int16x8_t w16_weighted_tmvp8_lazy_v(int16x8_t a, int16x8_t b,
                                                  int16x8_t weight,
                                                  int16x8_t weight_shoup) {
    int16x8_t weighted_b = w16_mul_const_shoup_v(b, weight, weight_shoup);
    int32x4_t acc_lo = vdupq_n_s32(0);
    int32x4_t acc_hi = vdupq_n_s32(0);

    w16_tmvp8_mla(weighted_b, b, a, &acc_lo, &acc_hi);
    return w16_reduce_s32_pair_lazy(acc_lo, acc_hi);
}

/* As w16_weighted_tmvp8(), but the returned coefficients are in [0,2q)
 * instead of [0,q). */
static inline int16x8_t w16_weighted_tmvp8_lazy(int16x8_t a, int16x8_t b,
                                                int weight, uint16_t weight_shoup) {
    int16x8_t weighted_b = w16_mul_const_shoup(b, (uint16_t)weight, weight_shoup);
    int32x4_t acc_lo = vdupq_n_s32(0);
    int32x4_t acc_hi = vdupq_n_s32(0);

    w16_tmvp8_mla(weighted_b, b, a, &acc_lo, &acc_hi);
    return w16_reduce_s32_pair_lazy(acc_lo, acc_hi);
}

/* Product in F_q[x]/(x^8-weight), expressed as one 8-by-8 TMVP. */
static inline int16x8_t w16_weighted_tmvp8(int16x8_t a, int16x8_t b,
                                           int weight, uint16_t weight_shoup) {
    int16x8_t weighted_b = w16_mul_const_shoup(b, (uint16_t)weight, weight_shoup);
    int32x4_t acc_lo = vdupq_n_s32(0);
    int32x4_t acc_hi = vdupq_n_s32(0);

    /* weighted_b[1..7] stores the upper diagonals in reverse-use order:
     * column j begins with weight*b[8-j], then b[0],b[1],... . */
    w16_tmvp8_mla(weighted_b, b, a, &acc_lo, &acc_hi);
    return w16_reduce_s32_pair(acc_lo, acc_hi);
}

/*
 * Let A=A0+x^8 A1 and B=B0+x^8 B1 in F_q[x]/(x^16-lambda), and let
 * s^2=lambda.  Evaluation at x^8=+s and x^8=-s gives two length-8
 * products.  The inverse radix-2 step recovers the low/high halves.
 */
