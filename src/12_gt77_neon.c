/* ---------------------------------------------------------------------------
 * B12  NEON 版 77 点变换主循环  —— 论文 §3.3
 * 源文件第 817--898 行（共 82 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
static void good_thomas_77_ntt_zs_neon(Blocks77 out, const Blocks77 in, int invert, int public_zs_forward) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const int (*P7P)[4] = invert ? PAIR7_P_I : PAIR7_P_F;
    const int (*P7M)[4] = invert ? PAIR7_M_I : PAIR7_M_F;
    const int (*P11P)[6] = invert ? PAIR11_P_I : PAIR11_P_F;
    const int (*P11M)[6] = invert ? PAIR11_M_I : PAIR11_M_F;
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
                    store_mod_q_s32x4(&tmp[r][v][cbase], out11[v]);
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
                    store_mod_q_s32x4(&tmp[r][v][cbase], out11[v]);
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
            dft7_pairwise_4lane(out7, in7, P7P, P7M);
            for (u = 0; u < 7; u++) {
                k = GT_K_UV[u][v];
                store_mod_q_inv77_s32x4(&out[k][cbase], out7[u], invert);
            }
        }
    }
#else
    good_thomas_77_ntt_zs(out, in, invert, public_zs_forward);
#endif
}
