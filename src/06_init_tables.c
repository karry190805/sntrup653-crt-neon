/* ---------------------------------------------------------------------------
 * B06  表初始化（77 次单位根、7/11 点 DFT 常数）  —— 论文 §3.3
 * 源文件第 274--420 行（共 147 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
static Fq Fq_freeze(int32 x) {
    x -= q * ((q18 * x) >> 18);
    x -= q * ((q27 * x + 67108864) >> 27);
    return (Fq)x;
}

static int pow_q(int a, int e) {
    int64_t r = 1;
    int64_t x = mod_q_i64_raw(a);
    while (e > 0) {
        if (e & 1) r = (r * x) % q;
        x = (x * x) % q;
        e >>= 1;
    }
    return (int)r;
}

static int inv_q(int a) {
    return pow_q(a, q - 2);
}

static int find_root77(void) {
    int g;
    for (g = 2; g < q; g++) {
        int r = pow_q(g, (q - 1) / 77);
        if (pow_q(r, 77) == 1 && pow_q(r, 7) != 1 && pow_q(r, 11) != 1) return r;
    }
    return 0;
}

static void init_pair_tables7(int Ptab[7][7], int PairP[4][4], int PairM[4][4]) {
    int inv2 = inv_q(2);
    int k, j;
    for (k = 1; k <= 3; k++) {
        for (j = 1; j <= 3; j++) {
            int wp = Ptab[k][j];
            int wm = Ptab[7 - k][j];
            PairP[k][j] = mul_q_raw(add_q_raw(wp, wm), inv2);
            PairM[k][j] = mul_q_raw(sub_q_raw(wp, wm), inv2);
        }
    }
}

static void init_pair_tables11(int Ptab[11][11], int PairP[6][6], int PairM[6][6]) {
    int inv2 = inv_q(2);
    int k, j;
    for (k = 1; k <= 5; k++) {
        for (j = 1; j <= 5; j++) {
            int wp = Ptab[k][j];
            int wm = Ptab[11 - k][j];
            PairP[k][j] = mul_q_raw(add_q_raw(wp, wm), inv2);
            PairM[k][j] = mul_q_raw(sub_q_raw(wp, wm), inv2);
        }
    }
}

static void init_tables(void) {
    int k, j, s, r, u, v;
    int root7_f, root11_f, root7_i, root11_i;
    int expected = 0;
    if (atomic_load_explicit(&tables_state, memory_order_acquire) == 2) return;
    if (!atomic_compare_exchange_strong_explicit(&tables_state, &expected, 1,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        while (atomic_load_explicit(&tables_state, memory_order_acquire) != 2) {
            /* First-call initialization is short and happens outside timing. */
        }
        return;
    }

    ROOT77 = find_root77();
    INV2 = inv_q(2);
    INV77 = inv_q(77);
    root7_f = pow_q(ROOT77, 11);
    root11_f = pow_q(ROOT77, 7);
    root7_i = inv_q(root7_f);
    root11_i = inv_q(root11_f);

    for (k = 0; k < 7; k++) {
        for (j = 0; j < 7; j++) {
            POW7_F[k][j] = pow_q(root7_f, (k * j) % 7);
            POW7_I[k][j] = pow_q(root7_i, (k * j) % 7);
        }
    }
    for (k = 0; k < 11; k++) {
        for (j = 0; j < 11; j++) {
            POW11_F[k][j] = pow_q(root11_f, (k * j) % 11);
            POW11_I[k][j] = pow_q(root11_i, (k * j) % 11);
        }
    }
    init_pair_tables7(POW7_F, PAIR7_P_F, PAIR7_M_F);
    init_pair_tables7(POW7_I, PAIR7_P_I, PAIR7_M_I);
    init_pair_tables11(POW11_F, PAIR11_P_F, PAIR11_M_F);
    init_pair_tables11(POW11_I, PAIR11_P_I, PAIR11_M_I);
    for (s = 0; s < 11; s++) {
        for (r = 0; r < 7; r++) {
            int idx = (22 * r + 56 * s) % 77;
            GT_IDX_RS[s][r] = idx;
        }
    }

    /* For the 11-point-first Good--Thomas order, the first layer fixes r
     * and performs 11-point DFTs over s. The lists below contain only public
     * non-padding positions; no secret value controls this schedule.
     * They are the run-time mirror of the SNTRUP_GT_ACT() constant expression
     * and are still used by the legacy (SNTRUP653_PUBLIC_ZS_RUNTIME_MASK=1)
     * path, by the dense path and by the internal self test. */
    for (r = 0; r < 7; r++) {
        int active_cnt = 0;
        for (s = 0; s < 11; s++) {
            int idx = GT_IDX_RS[s][r];
            if (idx < ACTIVE_BLOCKS) {
                assert(active_cnt < MAX_ACTIVE_COLS);
                ACTIVE_COL_LIST[r][active_cnt++] = s;
            }
        }
        assert(active_cnt == SNTRUP_GT_ACT_COUNT(r));
        ACTIVE_COLS_PER_R[r] = active_cnt;
        ACTIVE_PAIR_MASK11[r][0] = 1;
        for (j = 1; j <= 5; j++) {
            int pos = 0;
            for (s = 0; s < active_cnt; s++) {
                int ss = ACTIVE_COL_LIST[r][s];
                if (ss == j || ss == 11 - j) pos = 1;
            }
            ACTIVE_PAIR_MASK11[r][j] = pos;
        }
    }
    for (u = 0; u < 7; u++) {
        for (v = 0; v < 11; v++) {
            GT_K_UV[u][v] = (11 * u + 7 * v) % 77;
        }
    }

    LAMBDA77[0] = 1;
    for (k = 1; k < BLOCKS; k++) LAMBDA77[k] = mul_q_raw(LAMBDA77[k - 1], ROOT77);
    /* LAMBDA77[k] lies in a subgroup of odd order 77.  Since
     * 2*39 = 1 (mod 77), s=lambda^39 is a square root of lambda. */
    for (k = 0; k < BLOCKS; k++) {
        int sroot = pow_q(LAMBDA77[k], 39);
        SQRT_LAMBDA77[k] = sroot;
        INV2_SQRT_LAMBDA77[k] = inv_q(mul_q_raw(2, sroot));
        assert(mul_q_raw(sroot, sroot) == LAMBDA77[k]);
    }
    atomic_store_explicit(&tables_state, 2, memory_order_release);
}
