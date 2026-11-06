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
                                       int32x4_t p0, int32x4_t p1, int32x4_t p2,
                                       int32x4_t m0, int32x4_t m1, int32x4_t m2,
                                       int32x4_t o0, int32x4_t o1, int32x4_t o2) {
    const int32x4_t qoff = vdupq_n_s32(q * (2 * PAIRWISE_QOFF_MULT));
    int32x4_t S1 = vaddq_s32(inv[1], inv[6]);
    int32x4_t S2 = vaddq_s32(inv[2], inv[5]);
    int32x4_t S3 = vaddq_s32(inv[3], inv[4]);
    int32x4_t D1 = vsubq_s32(inv[1], inv[6]);
    int32x4_t D2 = vsubq_s32(inv[2], inv[5]);
    int32x4_t D3 = vsubq_s32(inv[3], inv[4]);
    int32x4_t even, odd;

    outv[0] = vaddq_s32(vaddq_s32(vaddq_s32(inv[0], S1), S2), S3);

    even = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(inv[0], S1, p0, 0),
                                           S2, p0, 1), S3, p0, 2);
    odd  = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(o0, D1, m0, 0),
                                           D2, m0, 1), D3, m0, 2);
    outv[1] = vaddq_s32(even, odd);
    outv[6] = vaddq_s32(vsubq_s32(even, odd), qoff);

    even = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(inv[0], S1, p1, 0),
                                           S2, p1, 1), S3, p1, 2);
    odd  = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(o1, D1, m1, 0),
                                           D2, m1, 1), D3, m1, 2);
    outv[2] = vaddq_s32(even, odd);
    outv[5] = vaddq_s32(vsubq_s32(even, odd), qoff);

    even = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(inv[0], S1, p2, 0),
                                           S2, p2, 1), S3, p2, 2);
    odd  = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(o2, D1, m2, 0),
                                           D2, m2, 1), D3, m2, 2);
    outv[3] = vaddq_s32(even, odd);
    outv[4] = vaddq_s32(vsubq_s32(even, odd), qoff);
}

/* ---------------------------------------------------------------------------
 * Compile-time specialised first layer (SNTRUP653_PUBLIC_ZS_MODE = 1).
 *
 * One fully unrolled instance per r is emitted below.  Because r, s and k are
 * literals in every instance, SNTRUP_GT_ACT() and SNTRUP_GT_IDX() fold to
 * constants and the compiler can
 *
 *   (1) delete the load of every padding column      - no vld1q, no zero init;
 *   (2) delete every pair term whose two columns are both padding
 *       (7 of the 35 (r,j) pair slots for p = 653);
 *   (3) collapse the 19 "half" slots, i.e. the pairs with exactly one padding
 *       column.  Let rho(x) = (x < 0 ? x + q : x) be the fix-up that the dense
 *       kernel applies.  Every block value is already reduced into [0, q), so
 *         - column 11-j is padding (a_{11-j} = 0):  S_j = a_j,
 *                                                    D_j = rho(a_j) = a_j;
 *         - column j is padding    (a_j = 0):        S_j = a_{11-j},
 *                                                    D_j = rho(-a_{11-j})
 *                                                      = q - a_{11-j}.
 *       In both cases the vaddq_s32, the vsubq_s32 and the conditional
 *       add_q_if_negative_s32x4 all disappear, leaving only the two
 *       vmlaq_n_s32 that carry the actual arithmetic.  For the second case the
 *       dense kernel accumulates M[k][j] * (q - a_{11-j}); the specialised
 *       kernel uses the coefficient q - M[k][j] on a_{11-j}, which is
 *       congruent modulo q (M*q == 0, so (q - M)*a == -M*a == M*(q - a)) and
 *       is still non-negative because M[k][j] in [0, q), so the accumulator
 *       stays within the non-negative precondition of store_mod_q_s32x4();
 *   (4) keep the surviving column vectors in NEON registers (named c0, p1..p5,
 *       m1..m5 - never an indirect index);
 *   (5) drop the pair-mask load, the compare and the branch that the old
 *       run-time loop spent on every (k,j) step, and drop the j/k loop
 *       control entirely.
 *
 * Steps (2)+(3) act on 130 of the 175 (r,j,k) term bodies (35 (r,j) slots
 * times 5 values of k): the 95 bodies in half slots fall from 5 vector ops to
 * 2, and the 35 bodies in padding pairs fall from 5 to 0.  Per lane group the
 * pair arithmetic drops from 875 to 415 vector ops, a 52.6% reduction; steps
 * (1)+(4)+(5) remove the run-time overhead that used to eat that saving.
 *
 * Every instance evaluates the same expression
 *     even_k = a_0 + sum_j P[k][j] * S_j ,   odd_k = sum_j M[k][j] * D_j ,
 *     Y_k = even_k + odd_k ,   Y_{11-k} = even_k - odd_k + q*25000
 * as the dense kernel, with the same PAIR11_P_F / PAIR11_M_F table entries and
 * the same accumulator bounds, so the stored values are bit-identical to
 * SNTRUP653_PUBLIC_ZS_MODE = 0.
 * ------------------------------------------------------------------------- */

/* Load column s of row r into VAR, or the zero vector when it is padding. */
