/* ---------------------------------------------------------------------------
 * B14  双 8 维 NTT + TMVP8 加权卷积主核  —— 论文 §3.4
 * 源文件第 984--1025 行（共 42 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
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
