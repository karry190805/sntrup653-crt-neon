/* B10 11 点/7 点配对 DFT（四通道）
 *
 * crypto_core_multsntrup653.c 第 588–700 行，共 113 行。
 * 配对 DFT 的基础函数，由 B12 调用。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
static inline void dft11_pairwise_4lane(int32x4_t outv[11], const int32x4_t inv[11],
                                        const int PairP[6][6], const int PairM[6][6]) {
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int k;
    int32x4_t acc0 = inv[0];
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[1], inv[10]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[2], inv[9]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[3], inv[8]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[4], inv[7]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[5], inv[6]));
    outv[0] = acc0;

    for (k = 1; k <= 5; k++) {
        int32x4_t even = inv[0];
        int32x4_t odd = vdupq_n_s32(0);
        int32x4_t s, d;

        s = vaddq_s32(inv[1], inv[10]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[1], inv[10]));
        even = vmlaq_n_s32(even, s, PairP[k][1]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][1]);

        s = vaddq_s32(inv[2], inv[9]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[2], inv[9]));
        even = vmlaq_n_s32(even, s, PairP[k][2]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][2]);

        s = vaddq_s32(inv[3], inv[8]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[3], inv[8]));
        even = vmlaq_n_s32(even, s, PairP[k][3]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][3]);

        s = vaddq_s32(inv[4], inv[7]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[4], inv[7]));
        even = vmlaq_n_s32(even, s, PairP[k][4]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][4]);

        s = vaddq_s32(inv[5], inv[6]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[5], inv[6]));
        even = vmlaq_n_s32(even, s, PairP[k][5]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][5]);

        outv[k] = vaddq_s32(even, odd);
        outv[11 - k] = vaddq_s32(vsubq_s32(even, odd), qoff);
    }
}

/* Legacy public-zero pruning: still loads only the active columns, but the
 * pair schedule is decided at run time by pair_mask, whose test sits inside
 * the j loop.  Kept only for SNTRUP653_PUBLIC_ZS_RUNTIME_MASK = 1 (A/B runs).
 * The pruning masks depend only on public p=653 padding.
 * A pair is skipped only when both public input positions are zero. */
static inline void dft11_pairwise_pruned_4lane(int32x4_t outv[11], const int32x4_t inv[11],
                                               const int PairP[6][6], const int PairM[6][6],
                                               const int pair_mask[6]) {
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int k, j;
    int32x4_t acc0 = inv[0];
    for (j = 1; j <= 5; j++) {
        if (pair_mask[j]) acc0 = vaddq_s32(acc0, vaddq_s32(inv[j], inv[11 - j]));
    }
    outv[0] = acc0;
    for (k = 1; k <= 5; k++) {
        int32x4_t even = inv[0];
        int32x4_t odd = vdupq_n_s32(0);
        for (j = 1; j <= 5; j++) {
            int32x4_t s, d;
            if (!pair_mask[j]) continue;
            s = vaddq_s32(inv[j], inv[11 - j]);
            d = add_q_if_negative_s32x4(vsubq_s32(inv[j], inv[11 - j]));
            even = vmlaq_n_s32(even, s, PairP[k][j]);
            odd = vmlaq_n_s32(odd, d, PairM[k][j]);
        }
        outv[k] = vaddq_s32(even, odd);
        outv[11 - k] = vaddq_s32(vsubq_s32(even, odd), qoff);
    }
}

static inline void dft7_pairwise_4lane(int32x4_t outv[7], const int32x4_t inv[7],
                                       const int PairP[4][4], const int PairM[4][4]) {
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int k;
    int32x4_t acc0 = inv[0];
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[1], inv[6]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[2], inv[5]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[3], inv[4]));
    outv[0] = acc0;

    for (k = 1; k <= 3; k++) {
        int32x4_t even = inv[0];
        int32x4_t odd = vdupq_n_s32(0);
        int32x4_t s, d;

        s = vaddq_s32(inv[1], inv[6]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[1], inv[6]));
        even = vmlaq_n_s32(even, s, PairP[k][1]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][1]);

        s = vaddq_s32(inv[2], inv[5]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[2], inv[5]));
        even = vmlaq_n_s32(even, s, PairP[k][2]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][2]);

        s = vaddq_s32(inv[3], inv[4]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[3], inv[4]));
        even = vmlaq_n_s32(even, s, PairP[k][3]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][3]);

        outv[k] = vaddq_s32(even, odd);
        outv[7 - k] = vaddq_s32(vsubq_s32(even, odd), qoff);
    }
}
