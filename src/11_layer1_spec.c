/* B11 第一层编译期特化（公开零裁剪）
 *
 * crypto_core_multsntrup653.c 第 701–816 行，共 116 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
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
#define SNTRUP_L1_LOAD(RR, SS, VAR)                                          \
    int32x4_t SNTRUP_MAYBE_UNUSED VAR = SNTRUP_GT_ACT(RR, SS)                 \
        ? vld1q_s32(&in[SNTRUP_GT_IDX(RR, SS)][cbase])                        \
        : vdupq_n_s32(0);

/* outv[0] == sum of the active columns   (j == 0 term of the DFT). */
#define SNTRUP_L1_DC(RR, SS, VAR)                                            \
    if (SNTRUP_GT_ACT(RR, SS)) acc0_ = vaddq_s32(acc0_, VAR);

/* Contribution of pair (j, 11-j) to even_k / odd_k. */
#define SNTRUP_L1_TERM(RR, KV, JJ, AJ, AM)                                   \
    do {                                                                      \
        if (SNTRUP_GT_ACT(RR, JJ)) {                                          \
            if (SNTRUP_GT_ACT(RR, 11 - (JJ))) {                               \
                int32x4_t sj_ = vaddq_s32(AJ, AM);                            \
                int32x4_t dj_ = add_q_if_negative_s32x4(vsubq_s32(AJ, AM));   \
                even_ = vmlaq_n_s32(even_, sj_, P11P[KV][JJ]);                \
                odd_ = vmlaq_n_s32(odd_, dj_, P11M[KV][JJ]);                  \
            } else {                                                          \
                even_ = vmlaq_n_s32(even_, AJ, P11P[KV][JJ]);                 \
                odd_ = vmlaq_n_s32(odd_, AJ, P11M[KV][JJ]);                   \
            }                                                                 \
        } else if (SNTRUP_GT_ACT(RR, 11 - (JJ))) {                            \
            even_ = vmlaq_n_s32(even_, AM, P11P[KV][JJ]);                     \
            odd_ = vmlaq_n_s32(odd_, AM, q - P11M[KV][JJ]);                   \
        }                                                                     \
    } while (0)

/* One output pair (k, 11-k), both halves sharing even_ / odd_. */
#define SNTRUP_L1_K(RR, KV)                                                   \
    do {                                                                      \
        even_ = c0;                                                           \
        odd_ = vdupq_n_s32(0);                                                \
        SNTRUP_L1_TERM(RR, KV, 1, p1, m1);                                    \
        SNTRUP_L1_TERM(RR, KV, 2, p2, m2);                                    \
        SNTRUP_L1_TERM(RR, KV, 3, p3, m3);                                    \
        SNTRUP_L1_TERM(RR, KV, 4, p4, m4);                                    \
        SNTRUP_L1_TERM(RR, KV, 5, p5, m5);                                    \
        store_mod_q_s32x4(&tmp[RR][KV][cbase], vaddq_s32(even_, odd_));        \
        store_mod_q_s32x4(&tmp[RR][11 - (KV)][cbase],                          \
                          vaddq_s32(vsubq_s32(even_, odd_), qoff_));           \
    } while (0)

/* The whole first layer for one fixed r. */
#define SNTRUP_L1_ROW(RR)                                                     \
    do {                                                                      \
        for (cbase = 0; cbase < BS; cbase += 4) {                             \
            int32x4_t even_, odd_, acc0_;                                     \
            const int32x4_t qoff_ = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);      \
            SNTRUP_L1_LOAD(RR, 0, c0)                                         \
            SNTRUP_L1_LOAD(RR, 1, p1)   SNTRUP_L1_LOAD(RR, 10, m1)            \
            SNTRUP_L1_LOAD(RR, 2, p2)   SNTRUP_L1_LOAD(RR, 9, m2)             \
            SNTRUP_L1_LOAD(RR, 3, p3)   SNTRUP_L1_LOAD(RR, 8, m3)             \
            SNTRUP_L1_LOAD(RR, 4, p4)   SNTRUP_L1_LOAD(RR, 7, m4)             \
            SNTRUP_L1_LOAD(RR, 5, p5)   SNTRUP_L1_LOAD(RR, 6, m5)             \
            acc0_ = vdupq_n_s32(0);                                           \
            SNTRUP_L1_DC(RR, 0, c0)   SNTRUP_L1_DC(RR, 1, p1)                 \
            SNTRUP_L1_DC(RR, 2, p2)   SNTRUP_L1_DC(RR, 3, p3)                 \
            SNTRUP_L1_DC(RR, 4, p4)   SNTRUP_L1_DC(RR, 5, p5)                 \
            SNTRUP_L1_DC(RR, 6, m5)   SNTRUP_L1_DC(RR, 7, m4)                 \
            SNTRUP_L1_DC(RR, 8, m3)   SNTRUP_L1_DC(RR, 9, m2)                 \
            SNTRUP_L1_DC(RR, 10, m1)                                          \
            store_mod_q_s32x4(&tmp[RR][0][cbase], acc0_);                     \
            SNTRUP_L1_K(RR, 1); SNTRUP_L1_K(RR, 2); SNTRUP_L1_K(RR, 3);       \
            SNTRUP_L1_K(RR, 4); SNTRUP_L1_K(RR, 5);                           \
        }                                                                     \
    } while (0)
#endif
