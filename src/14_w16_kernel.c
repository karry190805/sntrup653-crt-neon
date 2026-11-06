static inline void weighted_mul16_2ntt_tmvp8_neon_core(
        int out[16], const int a[16], const int b[16],
        int16x8_t c_s, int16x8_t c_sp, int16x8_t c_qsp,
        int16x8_t c_i2, int16x8_t c_i2p,
        int16x8_t c_i2s, int16x8_t c_i2sp) {
    int16x8_t a0, a1, b0, b1;
    int16x8_t sa1, sb1, ap, am, bp, bm, hp, hm, h0, h1;
    const int16x8_t c_qsp2 = vsubq_s16(vdupq_n_s16((int16_t)q), c_s);

    /* Narrow two adjacent int32 vectors into each eight-coefficient half. */
    a0 = vcombine_s16(vmovn_s32(vld1q_s32(a)), vmovn_s32(vld1q_s32(a + 4)));
    a1 = vcombine_s16(vmovn_s32(vld1q_s32(a + 8)), vmovn_s32(vld1q_s32(a + 12)));
    b0 = vcombine_s16(vmovn_s32(vld1q_s32(b)), vmovn_s32(vld1q_s32(b + 4)));
    b1 = vcombine_s16(vmovn_s32(vld1q_s32(b + 8)), vmovn_s32(vld1q_s32(b + 12)));

    sa1 = w16_mul_const_shoup_v(a1, c_s, c_sp);
    sb1 = w16_mul_const_shoup_v(b1, c_s, c_sp);
    /* Range-lazy split: the operands of the first TMVP only need to keep the
     * 8-term int32 accumulator below 2^31, so ap/bp in [0,3q) and am in
     * (q,4q) are fine and two of the three conditional subtractions vanish.
     *   ap = a0 + sa1      < 3q    (a is the `vec` side of the TMVP)
     *   bp = b0 + sb1      < 3q    (b side: still a valid Shoup operand)
     *   am = a0 - sa1 + 2q < 4q    (a side, 8*4q*3q = 2.05e9 < 2^31)
     *   bm = b0 - sb1 + 2q < 4q -> one min -> [0,3q) (b side needs < 3q)
     * The Shoup multiply on the b side canonicalises, so only an upper bound
     * of 3q is required there. */
    ap = vaddq_s16(a0, sa1);
    am = vaddq_s16(vsubq_s16(a0, sa1), vdupq_n_s16((int16_t)(2 * q)));
    bp = vaddq_s16(b0, sb1);
    bm = w16_sub_mod_wide(b0, sb1);

    /* The TMVPs only need [0,2q), which the wide reconstruction below folds
     * back into range (the final Shoup multiply canonicalises anyway). */
    hp = w16_weighted_tmvp8_lazy_v(ap, bp, c_s, c_sp);
    hm = w16_weighted_tmvp8_lazy_v(am, bm, c_qsp2, c_qsp);

    h0 = w16_mul_const_shoup_v(w16_add_mod(hp, hm), c_i2, c_i2p);
    h1 = w16_mul_const_shoup_v(w16_sub_mod_wide(hp, hm), c_i2s, c_i2sp);
    vst1q_s32(out, vmovl_s16(vget_low_s16(h0)));
    vst1q_s32(out + 4, vmovl_s16(vget_high_s16(h0)));
    vst1q_s32(out + 8, vmovl_s16(vget_low_s16(h1)));
    vst1q_s32(out + 12, vmovl_s16(vget_high_s16(h1)));
}

#if SNTRUP653_FAST16
/* Same arithmetic, but the two transformed operands and the result are all
 * int16: the forward path stores lazy representatives in [0,2q) and the
 * inverse first layer accepts exactly that range, so the int32 packing and
 * unpacking around each block disappears. */
static inline void weighted_mul16_2ntt_tmvp8_neon_core16(
        int16_t out[16], const int16_t a[16], const int16_t b[16],
        int16x8_t c_s, int16x8_t c_sp, int16x8_t c_qsp,
        int16x8_t c_i2, int16x8_t c_i2p,
        int16x8_t c_i2s, int16x8_t c_i2sp) {
    const int16x8_t a0 = vld1q_s16(a);
    const int16x8_t a1 = vld1q_s16(a + 8);
    const int16x8_t b0 = vld1q_s16(b);
    const int16x8_t b1 = vld1q_s16(b + 8);
    int16x8_t sa1, sb1, ap, am, bp, bm, hp, hm, h0, h1;
    const int16x8_t c_qsp2 = vsubq_s16(vdupq_n_s16((int16_t)q), c_s);

    sa1 = w16_mul_const_shoup_v(a1, c_s, c_sp);
    sb1 = w16_mul_const_shoup_v(b1, c_s, c_sp);
    ap = vaddq_s16(a0, sa1);
    am = vaddq_s16(vsubq_s16(a0, sa1), vdupq_n_s16((int16_t)(2 * q)));
    bp = vaddq_s16(b0, sb1);
    bm = w16_sub_mod_wide(b0, sb1);

    hp = w16_weighted_tmvp8_lazy_v(ap, bp, c_s, c_sp);
    hm = w16_weighted_tmvp8_lazy_v(am, bm, c_qsp2, c_qsp);

    h0 = w16_mul_const_shoup_v(w16_add_mod(hp, hm), c_i2, c_i2p);
    h1 = w16_mul_const_shoup_v(w16_sub_mod_wide(hp, hm), c_i2s, c_i2sp);
    vst1q_s16(out, h0);
    vst1q_s16(out + 8, h1);
}

static inline __attribute__((always_inline))
void weighted_mul16_scaled16(int16_t out[BS], const int16_t a[BS],
                             const int16_t b[BS], int weight_index,
                             int scale_inv77) {
    const int16_t *c = W16C[weight_index];
    const int16x8x4_t cc = vld1q_s16_x4(c);   /* S, SP, QSP, I2S77 */
    int16x8_t c_i2s, c_i2sp;
    if (scale_inv77) {
        c_i2s = cc.val[3];
        c_i2sp = vld1q_s16(c + W16C_I2SP77 * 8);
    } else {
        c_i2s = vld1q_s16(c + W16C_I2S * 8);
        c_i2sp = vld1q_s16(c + W16C_I2SP * 8);
    }
    weighted_mul16_2ntt_tmvp8_neon_core16(
        out, a, b,
        cc.val[0], cc.val[1], cc.val[2],
        scale_inv77 ? vld1q_s16(W16_INV2_INV77) : vld1q_s16(W16_INV2),
        scale_inv77 ? vld1q_s16(W16_INV2_INV77_SHOUP)
                    : vld1q_s16(W16_INV2_SHOUP),
        c_i2s, c_i2sp);
}

#endif

/* Test-only entry point: derives the Shoup multipliers at run time. */
static inline void weighted_mul16_2ntt_tmvp8_neon(
        int out[16], const int a[16], const int b[16],
        int sqrt_lambda, int inv2, int inv2_sqrt_lambda) {
    weighted_mul16_2ntt_tmvp8_neon_core(
        out, a, b,
        vdupq_n_s16((int16_t)sqrt_lambda),
        vdupq_n_s16((int16_t)shoup16(sqrt_lambda)),
        vdupq_n_s16((int16_t)shoup16(q - sqrt_lambda)),
        vdupq_n_s16((int16_t)inv2),
        vdupq_n_s16((int16_t)shoup16(inv2)),
        vdupq_n_s16((int16_t)inv2_sqrt_lambda),
        vdupq_n_s16((int16_t)shoup16(inv2_sqrt_lambda)));
}

/* The production weighted16 path is fixed: radix-2 + two NEON TMVP8s. */
static inline __attribute__((always_inline))
void weighted_mul16_scaled(int out[BS], const int a[BS], const int b[BS],
                           int weight_index, int scale_inv77) {
    const int16_t *c = W16C[weight_index];
    weighted_mul16_2ntt_tmvp8_neon_core(
        out, a, b,
        vld1q_s16(c + W16C_S * 8),
        vld1q_s16(c + W16C_SP * 8),
        vld1q_s16(c + W16C_QSP * 8),
        scale_inv77 ? vld1q_s16(W16_INV2_INV77) : vld1q_s16(W16_INV2),
        scale_inv77 ? vld1q_s16(W16_INV2_INV77_SHOUP)
                    : vld1q_s16(W16_INV2_SHOUP),
        vld1q_s16(c + (scale_inv77 ? W16C_I2S77 : W16C_I2S) * 8),
        vld1q_s16(c + (scale_inv77 ? W16C_I2SP77 : W16C_I2SP) * 8));
}

static void weighted_mul16(int out[BS], const int a[BS], const int b[BS], int weight_index) {
    weighted_mul16_scaled(out, a, b, weight_index, 0);
}

static inline int main_coeff(const Blocks77 Cblk, int idx) {
