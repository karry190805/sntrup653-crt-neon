static SNTRUP_UNROLL_ATTR void gt_l1_dense16(Matrix16 tmp, const Blocks16 in) {
    const int16x8_t qv = vdupq_n_s16((int16_t)q);
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int r, cbase, k;
    for (r = 0; r < 7; r++) {
        for (cbase = 0; cbase < BS; cbase += 8) {
            int16x8_t c[11];
            int32x4_t acc0lo, acc0hi;
            for (k = 0; k < 11; k++) c[k] = vld1q_s16(&in[GT_IDX_RS[k][r]][cbase]);
            acc0lo = vmovl_s16(vget_low_s16(c[0]));
            acc0hi = vmovl_high_s16(c[0]);
            for (k = 1; k <= 5; k++) {
                acc0lo = vaddw_s16(acc0lo, vget_low_s16(c[k]));
                acc0lo = vaddw_s16(acc0lo, vget_low_s16(c[11 - k]));
                acc0hi = vaddw_s16(acc0hi, vget_high_s16(c[k]));
                acc0hi = vaddw_s16(acc0hi, vget_high_s16(c[11 - k]));
            }
            store_lazy_s16(&tmp[r][0][cbase], acc0lo, acc0hi);
            for (k = 1; k <= 5; k++) {
                const int16x8_t pv_ = vld1q_s16(P11Q_F[k]);
                const int16x8_t mv_ = vld1q_s16(M11Q_F[k]);
                int32x4_t elo = vmovl_s16(vget_low_s16(c[0]));
                int32x4_t ehi = vmovl_high_s16(c[0]);
                int32x4_t olo = vdupq_n_s32(0), ohi = vdupq_n_s32(0);
#if defined(__GNUC__) && !defined(__clang__)
                int j;
#pragma GCC unroll 5
                for (j = 1; j <= 5; j++) {
                    int16x8_t sj = vaddq_s16(c[j], c[11 - j]);
                    uint16x8_t raw = vreinterpretq_u16_s16(vsubq_s16(c[j], c[11 - j]));
                    int16x8_t dj = vreinterpretq_s16_u16(
                        vminq_u16(raw, vaddq_u16(raw, vreinterpretq_u16_s16(qv))));
                    elo = vmlal_laneq_s16(elo, vget_low_s16(sj), pv_, j - 1);
                    ehi = vmlal_high_laneq_s16(ehi, sj, pv_, j - 1);
                    olo = vmlal_laneq_s16(olo, vget_low_s16(dj), mv_, j - 1);
                    ohi = vmlal_high_laneq_s16(ohi, dj, mv_, j - 1);
                }
#else
                SNTRUP_DFT11_STEP16(pv_, mv_, 1);
                SNTRUP_DFT11_STEP16(pv_, mv_, 2);
                SNTRUP_DFT11_STEP16(pv_, mv_, 3);
                SNTRUP_DFT11_STEP16(pv_, mv_, 4);
                SNTRUP_DFT11_STEP16(pv_, mv_, 5);
#endif
                store_lazy_s16(&tmp[r][k][cbase], vaddq_s32(elo, olo),
                               vaddq_s32(ehi, ohi));
                store_lazy_s16(&tmp[r][11 - k][cbase],
                               vaddq_s32(vsubq_s32(elo, olo), qoff),
                               vaddq_s32(vsubq_s32(ehi, ohi), qoff));
            }
        }
    }
}

/* Second layer: 7-point DFTs over the 16-bit tmp array, int32 output. */
static void gt_layer2_fwd16(Blocks16 out, const Matrix16 tmp) {
    const int16x8_t q2 = vdupq_n_s16((int16_t)(2 * q));
    const int32x4_t qoff = vdupq_n_s32(q * (2 * PAIRWISE_QOFF_MULT));
    int v, cbase, k;
    for (v = 0; v < 11; v++) {
        for (cbase = 0; cbase < BS; cbase += 8) {
            int16x8_t x0 = vld1q_s16(&tmp[0][v][cbase]);
            int16x8_t x1 = vld1q_s16(&tmp[1][v][cbase]);
            int16x8_t x2 = vld1q_s16(&tmp[2][v][cbase]);
            int16x8_t x3 = vld1q_s16(&tmp[3][v][cbase]);
            int16x8_t x4 = vld1q_s16(&tmp[4][v][cbase]);
            int16x8_t x5 = vld1q_s16(&tmp[5][v][cbase]);
            int16x8_t x6 = vld1q_s16(&tmp[6][v][cbase]);
            int16x8_t S1 = vaddq_s16(x1, x6);
            int16x8_t S2 = vaddq_s16(x2, x5);
            int16x8_t S3 = vaddq_s16(x3, x4);
            int16x8_t D1 = vaddq_s16(vsubq_s16(x1, x6), q2);
            int16x8_t D2 = vaddq_s16(vsubq_s16(x2, x5), q2);
            int16x8_t D3 = vaddq_s16(vsubq_s16(x3, x4), q2);
            int32x4_t dl, dh;
            int32x4_t el, eh, ol, oh;
            int32x4_t y0lo, y0hi;

            y0lo = vaddw_s16(vaddw_s16(vaddw_s16(vmovl_s16(vget_low_s16(x0)),
                                                vget_low_s16(S1)),
                                       vget_low_s16(S2)), vget_low_s16(S3));
            y0hi = vaddw_s16(vaddw_s16(vaddw_s16(vmovl_high_s16(x0),
                                                 vget_high_s16(S1)),
                                       vget_high_s16(S2)), vget_high_s16(S3));
            store_lazy_s16(&out[GT_K_UV[0][v]][cbase], y0lo, y0hi);
            for (k = 1; k <= 3; k++) {
                const int16x8_t pv = vld1q_s16(P7Q_F[k]);
                const int16x8_t mv = vld1q_s16(M7Q_F[k]);
                el = vmovl_s16(vget_low_s16(x0));
                eh = vmovl_high_s16(x0);
                ol = vdupq_n_s32(0);
                oh = vdupq_n_s32(0);
                SNTRUP_ACC16(el, eh, ol, oh, S1, D1, pv, mv, 0);
                SNTRUP_ACC16(el, eh, ol, oh, S2, D2, pv, mv, 1);
                SNTRUP_ACC16(el, eh, ol, oh, S3, D3, pv, mv, 2);
                dl = vaddq_s32(el, ol);
                dh = vaddq_s32(eh, oh);
                store_lazy_s16(&out[GT_K_UV[k][v]][cbase], dl, dh);
                dl = vaddq_s32(vsubq_s32(el, ol), qoff);
                dh = vaddq_s32(vsubq_s32(eh, oh), qoff);
                store_lazy_s16(&out[GT_K_UV[7 - k][v]][cbase], dl, dh);
            }
        }
    }
}

static void good_thomas_77_ntt_fwd16(Blocks16 out, const Blocks16 in,
                                     int public_zs_forward) {
    Matrix16 tmp;
    int cbase;
    SNTRUP_STAGE_MARK(fwd_dft11);
    if (public_zs_forward) {
        SNTRUP_L1_ROW16(0);
        SNTRUP_L1_ROW16(1);
        SNTRUP_L1_ROW16(2);
        SNTRUP_L1_ROW16(3);
        SNTRUP_L1_ROW16(4);
        SNTRUP_L1_ROW16(5);
        SNTRUP_L1_ROW16(6);
    } else {
        gt_l1_dense16(tmp, in);
    }
    SNTRUP_STAGE_MARK(fwd_dft7);
    gt_layer2_fwd16(out, tmp);
}

/* ---------------------------------------------------------------------------
 * 16-bit inverse Good--Thomas transform.
 *
 * The weighted product leaves canonical representatives in [0,q), so the
 * dense inverse first layer keeps the same int16 range as the forward path
 * (S < 2q, D in [0,q)) and stores its lazy [0,2q) output narrowed.  The
 * second layer accumulates in 32 bits and stores the same non-negative raw
 * accumulators as the 32-bit skip_final_reduce path, because final_folding_*
 * reduces the sums once at the end.
 * ------------------------------------------------------------------------- */
static SNTRUP_UNROLL_ATTR void gt_l1_dense_inv16(Matrix16 tmp, const Blocks16 in) {
    const int16x8_t qv = vdupq_n_s16((int16_t)q);
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int r, cbase, k;
    for (r = 0; r < 7; r++) {
        for (cbase = 0; cbase < BS; cbase += 8) {
            int16x8_t c[11];
            int32x4_t acc0lo, acc0hi;
            for (k = 0; k < 11; k++) c[k] = vld1q_s16(&in[GT_IDX_RS[k][r]][cbase]);
            acc0lo = vmovl_s16(vget_low_s16(c[0]));
            acc0hi = vmovl_high_s16(c[0]);
            for (k = 1; k <= 5; k++) {
                acc0lo = vaddw_s16(acc0lo, vget_low_s16(c[k]));
                acc0lo = vaddw_s16(acc0lo, vget_low_s16(c[11 - k]));
                acc0hi = vaddw_s16(acc0hi, vget_high_s16(c[k]));
                acc0hi = vaddw_s16(acc0hi, vget_high_s16(c[11 - k]));
            }
            store_lazy_s16(&tmp[r][0][cbase], acc0lo, acc0hi);
            for (k = 1; k <= 5; k++) {
                const int16x8_t pv = vld1q_s16(P11Q_I[k]);
                const int16x8_t mv = vld1q_s16(M11Q_I[k]);
                int32x4_t elo = vmovl_s16(vget_low_s16(c[0]));
                int32x4_t ehi = vmovl_high_s16(c[0]);
                int32x4_t olo = vdupq_n_s32(0), ohi = vdupq_n_s32(0);
#if defined(__GNUC__) && !defined(__clang__)
                int j;
#pragma GCC unroll 5
                for (j = 1; j <= 5; j++) {
                    int16x8_t sj = vaddq_s16(c[j], c[11 - j]);
                    uint16x8_t raw = vreinterpretq_u16_s16(vsubq_s16(c[j], c[11 - j]));
                    int16x8_t dj = vreinterpretq_s16_u16(
                        vminq_u16(raw, vaddq_u16(raw, vreinterpretq_u16_s16(qv))));
                    elo = vmlal_laneq_s16(elo, vget_low_s16(sj), pv, j - 1);
                    ehi = vmlal_high_laneq_s16(ehi, sj, pv, j - 1);
                    olo = vmlal_laneq_s16(olo, vget_low_s16(dj), mv, j - 1);
                    ohi = vmlal_high_laneq_s16(ohi, dj, mv, j - 1);
                }
#else
                SNTRUP_DFT11_STEP16(pv, mv, 1);
                SNTRUP_DFT11_STEP16(pv, mv, 2);
                SNTRUP_DFT11_STEP16(pv, mv, 3);
                SNTRUP_DFT11_STEP16(pv, mv, 4);
                SNTRUP_DFT11_STEP16(pv, mv, 5);
#endif
                store_lazy_s16(&tmp[r][k][cbase], vaddq_s32(elo, olo),
                               vaddq_s32(ehi, ohi));
                store_lazy_s16(&tmp[r][11 - k][cbase],
                               vaddq_s32(vsubq_s32(elo, olo), qoff),
                               vaddq_s32(vsubq_s32(ehi, ohi), qoff));
            }
        }
    }
}

static void gt_layer2_inv16(Blocks77 out, const Matrix16 tmp) {
    const int16x8_t q2 = vdupq_n_s16((int16_t)(2 * q));
    const int32x4_t qoff = vdupq_n_s32(q * (2 * PAIRWISE_QOFF_MULT));
    int v, cbase, k;
    for (v = 0; v < 11; v++) {
        for (cbase = 0; cbase < BS; cbase += 8) {
            int16x8_t x0 = vld1q_s16(&tmp[0][v][cbase]);
            int16x8_t x1 = vld1q_s16(&tmp[1][v][cbase]);
            int16x8_t x2 = vld1q_s16(&tmp[2][v][cbase]);
            int16x8_t x3 = vld1q_s16(&tmp[3][v][cbase]);
            int16x8_t x4 = vld1q_s16(&tmp[4][v][cbase]);
            int16x8_t x5 = vld1q_s16(&tmp[5][v][cbase]);
            int16x8_t x6 = vld1q_s16(&tmp[6][v][cbase]);
            int16x8_t S1 = vaddq_s16(x1, x6);
            int16x8_t S2 = vaddq_s16(x2, x5);
            int16x8_t S3 = vaddq_s16(x3, x4);
            int16x8_t D1 = vaddq_s16(vsubq_s16(x1, x6), q2);
            int16x8_t D2 = vaddq_s16(vsubq_s16(x2, x5), q2);
            int16x8_t D3 = vaddq_s16(vsubq_s16(x3, x4), q2);
            int32x4_t dl, dh, el, eh, ol, oh, y0lo, y0hi;

            y0lo = vaddw_s16(vaddw_s16(vaddw_s16(vmovl_s16(vget_low_s16(x0)),
                                                vget_low_s16(S1)),
                                       vget_low_s16(S2)), vget_low_s16(S3));
            y0hi = vaddw_s16(vaddw_s16(vaddw_s16(vmovl_high_s16(x0),
                                                 vget_high_s16(S1)),
                                       vget_high_s16(S2)), vget_high_s16(S3));
            vst1q_s32(&out[GT_K_UV[0][v]][cbase], y0lo);
            vst1q_s32(&out[GT_K_UV[0][v]][cbase + 4], y0hi);
            for (k = 1; k <= 3; k++) {
                const int16x8_t pv = vld1q_s16(P7Q_I[k]);
                const int16x8_t mv = vld1q_s16(M7Q_I[k]);
                el = vmovl_s16(vget_low_s16(x0));
                eh = vmovl_high_s16(x0);
                ol = vdupq_n_s32(0);
                oh = vdupq_n_s32(0);
                SNTRUP_ACC16(el, eh, ol, oh, S1, D1, pv, mv, 0);
                SNTRUP_ACC16(el, eh, ol, oh, S2, D2, pv, mv, 1);
                SNTRUP_ACC16(el, eh, ol, oh, S3, D3, pv, mv, 2);
                dl = vaddq_s32(el, ol);
                dh = vaddq_s32(eh, oh);
                vst1q_s32(&out[GT_K_UV[k][v]][cbase], dl);
                vst1q_s32(&out[GT_K_UV[k][v]][cbase + 4], dh);
                dl = vaddq_s32(vsubq_s32(el, ol), qoff);
                dh = vaddq_s32(vsubq_s32(eh, oh), qoff);
                vst1q_s32(&out[GT_K_UV[7 - k][v]][cbase], dl);
                vst1q_s32(&out[GT_K_UV[7 - k][v]][cbase + 4], dh);
            }
        }
    }
}

static void good_thomas_77_ntt_inv16(Blocks77 out, const Blocks16 in) {
    Matrix16 tmp;
    SNTRUP_STAGE_MARK(inv_dft11);
    gt_l1_dense_inv16(tmp, in);
    SNTRUP_STAGE_MARK(inv_dft7);
    gt_layer2_inv16(out, tmp);
}

#endif

static void good_thomas_77_ntt_zs_neon(Blocks77 out, const Blocks77 in, int invert, int public_zs_forward, int scale_inv77, int skip_final_reduce, int lazy_forward) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const int32x4_t *P7v = invert ? P7V_I : P7V_F;
    const int32x4_t *M7v = invert ? M7V_I : M7V_F;
    const int (*P11P)[6] = invert ? PAIR11_P_I : PAIR11_P_F;
    const int (*P11M)[6] = invert ? PAIR11_M_I : PAIR11_M_F;
    const int32x4_t P7c0 = P7v[0], P7c1 = P7v[1], P7c2 = P7v[2];
    const int32x4_t M7c0 = M7v[0], M7c1 = M7v[1], M7c2 = M7v[2];
    const int *o7 = invert ? PAIR7_ODDINIT_I : PAIR7_ODDINIT_F;
    const int32x4_t O7c0 = vdupq_n_s32(o7[1]), O7c1 = vdupq_n_s32(o7[2]),
                    O7c2 = vdupq_n_s32(o7[3]);
    MatrixGT tmp SNTRUP_ALIGN64;
    int s, u, v, r, cbase, k;

    if (public_zs_forward) {
#if SNTRUP653_PUBLIC_ZS_MODE == 1 && !SNTRUP653_PUBLIC_ZS_RUNTIME_MASK
        /* Public-zero pruning resolved at build time: seven specialised,
         * fully unrolled 11-point kernels, one per r.  No mask test, no table
         * indirection and no data-independent run-time branch survives. */
        SNTRUP_L1_ROW(0);
        SNTRUP_L1_ROW(1);
        SNTRUP_L1_ROW(2);
        SNTRUP_L1_ROW(3);
        SNTRUP_L1_ROW(4);
        SNTRUP_L1_ROW(5);
        SNTRUP_L1_ROW(6);
#else
        int t, ns;
        for (r = 0; r < 7; r++) {
            ns = ACTIVE_COLS_PER_R[r];
            for (cbase = 0; cbase < BS; cbase += 4) {
                int32x4_t in11[11];
                int32x4_t out11[11];
                for (s = 0; s < 11; s++) in11[s] = vdupq_n_s32(0);
                for (t = 0; t < ns; t++) {
                    int ss = ACTIVE_COL_LIST[r][t];
                    int idx = GT_IDX_RS[ss][r];
                    in11[ss] = vld1q_s32(&in[idx][cbase]);
                }
#if SNTRUP653_PUBLIC_ZS_MODE == 1
                dft11_pairwise_pruned_4lane(out11, in11, P11P, P11M, ACTIVE_PAIR_MASK11[r]);
#else
                dft11_pairwise_4lane(out11, in11, P11P, P11M);
#endif
                for (v = 0; v < 11; v++) {
                    store_mod_q2_s32x4(&tmp[r][v][cbase], out11[v]);
                }
            }
        }
#endif
    } else {
        for (r = 0; r < 7; r++) {
            for (cbase = 0; cbase < BS; cbase += 4) {
                int32x4_t in11[11];
                int32x4_t out11[11];
                for (s = 0; s < 11; s++) {
                    int idx = GT_IDX_RS[s][r];
                    in11[s] = vld1q_s32(&in[idx][cbase]);
                }
                dft11_pairwise_4lane(out11, in11, P11P, P11M);
                for (v = 0; v < 11; v++) {
                    store_mod_q2_s32x4(&tmp[r][v][cbase], out11[v]);
                }
            }
        }
    }

    /* Second layer: 11 independent 7-point direct DFTs, fixed-unrolled. */
    for (v = 0; v < 11; v++) {
        for (cbase = 0; cbase < BS; cbase += 4) {
            int32x4_t in7[7];
            int32x4_t out7[7];
            for (r = 0; r < 7; r++) {
                in7[r] = vld1q_s32(&tmp[r][v][cbase]);
            }
            dft7_pairwise_4lane(out7, in7, P7c0, P7c1, P7c2, M7c0, M7c1, M7c2,
                                O7c0, O7c1, O7c2);
            for (u = 0; u < 7; u++) {
                k = GT_K_UV[u][v];
                if (invert && skip_final_reduce) {
                    /* The only consumer is final_folding_*(), which adds up to
                     * three such coefficients and reduces once.  Storing the
                     * non-negative DFT accumulator keeps the same residue and
                     * removes one Barrett reduction per coefficient. */
                    vst1q_s32(&out[k][cbase], out7[u]);
                } else if (invert && scale_inv77) {
                    store_mod_q_inv77_s32x4(&out[k][cbase], out7[u], 1);
                } else if (lazy_forward) {
                    /* Only the weighted multiply consumes this transform's
                     * output.  It narrows to int16 and works modulo q, so a
                     * non-negative representative in [0,2q) is enough and the
                     * canonicalising min is unnecessary. */
                    store_mod_q2_s32x4(&out[k][cbase], out7[u]);
                } else {
                    store_mod_q_s32x4(&out[k][cbase], out7[u]);
                }
            }
        }
    }
#else
    good_thomas_77_ntt_zs(out, in, invert, public_zs_forward);
#endif
}

#ifdef SNTRUP653_INTERNAL_TEST_HOOKS
/* Independent schoolbook oracle; never selected by the production entry point. */
