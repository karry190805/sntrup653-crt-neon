/* B18 自检与内部测试钩子
 *
 * crypto_core_multsntrup653.c 第 1159–1386 行，共 228 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
#ifdef SNTRUP653_INTERNAL_TEST_HOOKS
int sntrup653_internal_selftest(unsigned random_tests, unsigned seed) {
    unsigned trial;
    int i, j, k, m, r, s, idx;
    Blocks77 in SNTRUP_ALIGN64;
    Blocks77 dense SNTRUP_ALIGN64;
    Blocks77 opt SNTRUP_ALIGN64;
    Blocks77 back SNTRUP_ALIGN64;
    Fq out_scalar[p];
    Fq out_opt[p];
    int clow[LOW_N];

    selftest_rng = seed ? (uint32_t)seed : UINT32_C(1);
    init_tables();
    if (ROOT77 == 0 || pow_q(ROOT77, 77) != 1 || pow_q(ROOT77, 7) == 1
            || pow_q(ROOT77, 11) == 1 || mul_q_raw(2, INV2) != 1
            || mul_q_raw(77, INV77) != 1) return 10;
    for (m = 0; m < BLOCKS; m++) {
        if (mul_q_raw(SQRT_LAMBDA77[m], SQRT_LAMBDA77[m]) != LAMBDA77[m]) return 12;
        if (mul_q_raw(mul_q_raw(2, SQRT_LAMBDA77[m]),
                      INV2_SQRT_LAMBDA77[m]) != 1) return 13;
    }
    for (i = 0; i < 7; i++) {
        int seen[11] = {0};
        int expected_count = 0;
        if (ACTIVE_COLS_PER_R[i] < 0 || ACTIVE_COLS_PER_R[i] > MAX_ACTIVE_COLS) return 11;
        for (j = 0; j < ACTIVE_COLS_PER_R[i]; j++) {
            int col = ACTIVE_COL_LIST[i][j];
            if (col < 0 || col >= 11 || seen[col]) return 11;
            seen[col] = 1;
        }
        for (j = 0; j < 11; j++) {
            int active = ((22 * i + 56 * j) % 77) < ACTIVE_BLOCKS;
            expected_count += active;
            if (seen[j] != active) return 11;
        }
        if (expected_count != ACTIVE_COLS_PER_R[i] || ACTIVE_PAIR_MASK11[i][0] != 1) return 11;
        for (j = 1; j <= 5; j++) {
            if (ACTIVE_PAIR_MASK11[i][j] != (seen[j] || seen[11 - j])) return 11;
        }
    }
    /* The run-time table and the compile-time predicate must agree on every
     * (r,s) cell, otherwise the specialised schedule would read the wrong
     * columns. */
    for (r = 0; r < 7; r++) {
        int expected_cnt = 0;
        for (s = 0; s < 11; s++) {
            int act = SNTRUP_GT_ACT(r, s) ? 1 : 0;
            if (act) expected_cnt++;
            if (act != (GT_IDX_RS[s][r] < ACTIVE_BLOCKS ? 1 : 0)) return 14;
            if (SNTRUP_GT_IDX(r, s) != GT_IDX_RS[s][r]) return 15;
        }
        if (ACTIVE_COLS_PER_R[r] != expected_cnt) return 16;
        for (j = 1; j <= 5; j++) {
            int mask = 0;
            for (s = 0; s < 11; s++) {
                if (SNTRUP_GT_ACT(r, s) && (s == j || s == 11 - j)) mask = 1;
            }
            if (mask != ACTIVE_PAIR_MASK11[r][j]) return 17;
        }
    }

    for (k = 0; k < BLOCKS; k++) {
        zero_blocks(in);
        in[k][0] = 1;
        good_thomas_77_ntt_zs(dense, in, 0, 0);
        good_thomas_77_ntt_zs_neon(opt, in, 0,
                                   SNTRUP653_PUBLIC_ZS_MODE && k < ACTIVE_BLOCKS);
        if (!compare_blocks_detail("gt-forward-unit", k, dense, opt)) return 20;
        good_thomas_77_ntt_zs_neon(back, opt, 1, 0);
        if (!compare_blocks_detail("gt-inverse-unit", k, in, back)) return 21;
    }

    for (trial = 0; trial < random_tests; trial++) {
        for (i = 0; i < BLOCKS; i++) {
            for (j = 0; j < BS; j++) in[i][j] = (int)(selftest_rand32() % (uint32_t)q);
        }
        good_thomas_77_ntt_zs(dense, in, 0, 0);
        good_thomas_77_ntt_zs_neon(opt, in, 0, 0);
        if (!compare_blocks_detail("gt-forward-random", (int)trial, dense, opt)) return 30;
        good_thomas_77_ntt_zs_neon(back, opt, 1, 0);
        if (!compare_blocks_detail("gt-inverse-random", (int)trial, in, back)) return 31;
    }

    /* The actual production precondition: dense random values on every public
     * non-padding block, exact zeros on blocks >= ACTIVE_BLOCKS.  This drives
     * the public_zs_forward == 1 schedule (specialised in mode 1, masking in
     * mode 0) and compares it against the dense oracle. */
    for (trial = 0; trial < random_tests; trial++) {
        zero_blocks(in);
        for (i = 0; i < ACTIVE_BLOCKS; i++) {
            for (j = 0; j < BS; j++) in[i][j] = (int)(selftest_rand32() % (uint32_t)q);
        }
        good_thomas_77_ntt_zs(dense, in, 0, 0);
        good_thomas_77_ntt_zs_neon(opt, in, 0, 1);
        if (!compare_blocks_detail("gt-forward-padded-random", (int)trial, dense, opt)) return 32;
        good_thomas_77_ntt_zs_neon(back, opt, 1, 0);
        if (!compare_blocks_detail("gt-inverse-padded-random", (int)trial, in, back)) return 33;
    }

    for (trial = 0; trial < random_tests; trial++) {
        int a[BS], b[BS], ref[BS], tmvp[BS], got[BS];
        int alias_a[BS], alias_b[BS];
        int weight_index = (int)(selftest_rand32() % BLOCKS);
        int lambda = LAMBDA77[weight_index];
        for (i = 0; i < BS; i++) {
            a[i] = (int)(selftest_rand32() % (uint32_t)q);
            b[i] = (int)(selftest_rand32() % (uint32_t)q);
        }
        weighted_mul16_scalar(ref, a, b, lambda);
        weighted_mul16_2ntt_tmvp8_neon(tmvp, a, b,
                                       SQRT_LAMBDA77[weight_index], INV2,
                                       INV2_SQRT_LAMBDA77[weight_index]);
        weighted_mul16(got, a, b, weight_index);
        if (memcmp(ref, tmvp, sizeof(ref)) != 0) return 42;
        if (memcmp(ref, got, sizeof(ref)) != 0) return 45;
        memcpy(alias_a, a, sizeof(alias_a));
        memcpy(alias_b, b, sizeof(alias_b));
        weighted_mul16(alias_a, alias_a, alias_b, weight_index);
        if (memcmp(ref, alias_a, sizeof(ref)) != 0) return 43;
        memcpy(alias_a, a, sizeof(alias_a));
        memcpy(alias_b, b, sizeof(alias_b));
        weighted_mul16(alias_b, alias_a, alias_b, weight_index);
        if (memcmp(ref, alias_b, sizeof(ref)) != 0) return 46;
    }

    /* Exhaustive monomial-pair coverage catches every TMVP row/column and
     * every wrap position for all 77 public weights. */
    for (m = 0; m < BLOCKS; m++) {
        for (i = 0; i < BS; i++) {
            for (j = 0; j < BS; j++) {
                int a[BS] = {0};
                int b[BS] = {0};
                int ref[BS], tmvp[BS];
                a[i] = 1;
                b[j] = 1;
                weighted_mul16_scalar(ref, a, b, LAMBDA77[m]);
                weighted_mul16_2ntt_tmvp8_neon(tmvp, a, b,
                                               SQRT_LAMBDA77[m], INV2,
                                               INV2_SQRT_LAMBDA77[m]);
                if (memcmp(ref, tmvp, sizeof(ref)) != 0) return 44;
            }
        }
    }

    for (i = 0; i < BLOCKS; i++) {
        for (j = 0; j < BS; j++) in[i][j] = (int)(selftest_rand32() % (uint32_t)q);
    }
    for (i = 0; i < LOW_N; i++) clow[i] = (int)(selftest_rand32() % (uint32_t)q);
    final_folding_scalar(out_scalar, in, clow);
    final_folding_neon(out_opt, in, clow);
    if (memcmp(out_scalar, out_opt, sizeof(out_scalar)) != 0) return 50;

    /* Exhaustive basis for the low-73 recovery.  low_part73 reads only
     * f[0..LOW_N-1] and g[0..LOW_N-1], and the result is F_q-bilinear in
     * (f, g).  Sweeping every monomial pair (e_i, +-e_j) for i, j < LOW_N
     * therefore covers the whole input space of this stage rather than a
     * sample of it; the reversed operand mapping and the sliding window are
     * both pinned down. */
    {
        Fq f73[p];
        small g73[p];
        int ref73[LOW_N], got73[LOW_N];
        int sign;
        for (i = 0; i < LOW_N; i++) {
            for (j = 0; j < LOW_N; j++) {
                for (sign = -1; sign <= 1; sign += 2) {
                    memset(f73, 0, sizeof(f73));
                    memset(g73, 0, sizeof(g73));
                    f73[i] = 1;
                    g73[j] = (small)sign;
                    low_part73_scalar(ref73, f73, g73);
                    low_part73_neon(got73, f73, g73);
                    if (memcmp(ref73, got73, sizeof(ref73)) != 0) return 70;
                }
            }
        }
    }

    /* Exhaustive basis for the final fold.  final_folding_scalar and
     * final_folding_neon realise the same F_q-linear map of the 2p-1
     * coefficients D(x), so the 2p-1 unit vectors D = x^idx cover every
     * possible input.  The sweep visits all three index regions of D_idx
     * (below LOW_N, inside the 1232-coefficient main block, and the tail
     * reconstructed from main - clow) and therefore also r = p-LOW_N-1 = 579,
     * the only position where the two fold-back terms sit one index apart. */
    for (idx = 0; idx < 2 * p - 1; idx++) {
        int fold_low[LOW_N];
        zero_blocks(in);
        memset(fold_low, 0, sizeof(fold_low));
        if (idx < LOW_N) {
            /* D_idx(idx) reads clow[idx] while D_idx(MAIN_N+idx) reads
             * main[idx]-clow[idx], so both must carry the unit. */
            fold_low[idx] = 1;
            in[idx / BS][idx % BS] = 1;
        } else if (idx < MAIN_N) {
            in[idx / BS][idx % BS] = 1;
        } else {
            int tail = idx - MAIN_N;
            in[tail / BS][tail % BS] = 1;
        }
        final_folding_scalar(out_scalar, in, fold_low);
        final_folding_neon(out_opt, in, fold_low);
        if (memcmp(out_scalar, out_opt, sizeof(out_scalar)) != 0) return 71;
    }

    for (m = 0; m < BLOCKS; m++) {
        int a[BS] = {0};
        int b[BS] = {0};
        int ref[BS], tmvp[BS], got[BS];
        for (i = 0; i < BS; i++) {
            a[i] = q - 1;
            b[i] = q - 1;
        }
        weighted_mul16_scalar(ref, a, b, LAMBDA77[m]);
        weighted_mul16_2ntt_tmvp8_neon(tmvp, a, b,
                                       SQRT_LAMBDA77[m], INV2,
                                       INV2_SQRT_LAMBDA77[m]);
        weighted_mul16(got, a, b, m);
        if (memcmp(ref, tmvp, sizeof(ref)) != 0) return 62;
        if (memcmp(ref, got, sizeof(ref)) != 0) return 63;
    }

    return 0;
}

#endif
