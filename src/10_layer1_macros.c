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
        store_mod_q2_s32x4(&tmp[RR][KV][cbase], vaddq_s32(even_, odd_));        \
        store_mod_q2_s32x4(&tmp[RR][11 - (KV)][cbase],                          \
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
            store_mod_q2_s32x4(&tmp[RR][0][cbase], acc0_);                     \
            SNTRUP_L1_K(RR, 1); SNTRUP_L1_K(RR, 2); SNTRUP_L1_K(RR, 3);       \
            SNTRUP_L1_K(RR, 4); SNTRUP_L1_K(RR, 5);                           \
        }                                                                     \
    } while (0)
#endif

#if SNTRUP653_FAST16
/* ---------------------------------------------------------------------------
 * 16-bit forward Good--Thomas transform.
 *
 * Every value the forward half touches is a non-negative representative in
 * [0, 2q) = [0, 9242), so it fits in a signed 16-bit lane.  Processing eight
 * lanes per vector (instead of four) halves the number of table-constant
 * loads, block loads and partial-sum computations, while the products are
 * still accumulated in 32 bits by vmlal so the arithmetic is bit-identical to
 * the 32-bit kernel: the same operands, the same constants and the same
 * Barrett step produce the same int32 accumulator, which is then narrowed.
 *
 * Bounds (inputs < q, first-layer outputs lazy < 2q):
 *   S_j = a_j + a_{11-j}            < 2q
 *   D_j = a_j - a_{11-j} + q        in [0, q)   (two-instruction min trick)
 *   even_k = a_0 + sum P*S          < 6*q*2q = 2.56e8
 *   odd_k  = sum M*D                < 5*q*q  = 1.07e8
 *   Y_k = even+odd, Y_{11-k} = even-odd+q*30000, both in [0, 2^31).
 * ------------------------------------------------------------------------- */
