/* B14 十六维局部乘法：两个 8 维加权 TMVP 分支与重构
 *
 * crypto_core_multsntrup653.c 第 984–1025 行，共 42 行。
 * 生产路径实际调用的局部乘法内核。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
/*
 * Let A=A0+x^8 A1 and B=B0+x^8 B1 in F_q[x]/(x^16-lambda), and let
 * s^2=lambda.  Evaluation at x^8=+s and x^8=-s gives two length-8
 * products.  The inverse radix-2 step recovers the low/high halves.
 */
static inline void weighted_mul16_2ntt_tmvp8_neon(
        int out[16], const int a[16], const int b[16],
        int sqrt_lambda, int inv2, int inv2_sqrt_lambda) {
    int16x8_t a0, a1, b0, b1;
    int16x8_t sa1, sb1, ap, am, bp, bm, hp, hm, h0, h1;

    /* Narrow two adjacent int32 vectors into each eight-coefficient half. */
    a0 = vcombine_s16(vmovn_s32(vld1q_s32(a)), vmovn_s32(vld1q_s32(a + 4)));
    a1 = vcombine_s16(vmovn_s32(vld1q_s32(a + 8)), vmovn_s32(vld1q_s32(a + 12)));
    b0 = vcombine_s16(vmovn_s32(vld1q_s32(b)), vmovn_s32(vld1q_s32(b + 4)));
    b1 = vcombine_s16(vmovn_s32(vld1q_s32(b + 8)), vmovn_s32(vld1q_s32(b + 12)));

    sa1 = w16_mul_const(a1, sqrt_lambda);
    sb1 = w16_mul_const(b1, sqrt_lambda);
    ap = w16_add_mod(a0, sa1);
    am = w16_sub_mod(a0, sa1);
    bp = w16_add_mod(b0, sb1);
    bm = w16_sub_mod(b0, sb1);

    hp = w16_weighted_tmvp8(ap, bp, sqrt_lambda);
    hm = w16_weighted_tmvp8(am, bm, q - sqrt_lambda);

    h0 = w16_mul_const(w16_add_mod(hp, hm), inv2);
    h1 = w16_mul_const(w16_sub_mod(hp, hm), inv2_sqrt_lambda);
    vst1q_s32(out, vmovl_s16(vget_low_s16(h0)));
    vst1q_s32(out + 4, vmovl_s16(vget_high_s16(h0)));
    vst1q_s32(out + 8, vmovl_s16(vget_low_s16(h1)));
    vst1q_s32(out + 12, vmovl_s16(vget_high_s16(h1)));
}

/* The production weighted16 path is fixed: radix-2 + two NEON TMVP8s. */
static void weighted_mul16(int out[BS], const int a[BS], const int b[BS], int weight_index) {
    weighted_mul16_2ntt_tmvp8_neon(out, a, b,
                                   SQRT_LAMBDA77[weight_index], INV2,
                                   INV2_SQRT_LAMBDA77[weight_index]);
}
