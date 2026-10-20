/* ---------------------------------------------------------------------------
 * B07  分块表示与内部测试钩子  —— 论文 §3.2 §4.4
 * 源文件第 421--471 行（共 51 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
#ifdef SNTRUP653_INTERNAL_TEST_HOOKS
static uint32_t selftest_rng = UINT32_C(1);

static uint32_t selftest_rand32(void) {
    uint32_t x = selftest_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    selftest_rng = x;
    return x;
}

#endif

typedef int Blocks77[BLOCKS][BS];
typedef int MatrixGT[7][11][BS];

#ifdef SNTRUP653_INTERNAL_TEST_HOOKS
static int compare_blocks_detail(const char *stage, int case_id,
                                 const Blocks77 a, const Blocks77 b) {
    int i, c;
    for (i = 0; i < BLOCKS; i++) {
        for (c = 0; c < BS; c++) {
            if (a[i][c] != b[i][c]) {
                fprintf(stderr,
                        "internal mismatch stage=%s case=%d block=%d lane=%d ref=%d got=%d\n",
                        stage, case_id, i, c, a[i][c], b[i][c]);
                return 0;
            }
        }
    }
    return 1;
}
#endif

static void zero_blocks(Blocks77 a) {
    memset(a, 0, sizeof(int) * BLOCKS * BS);
}

static void blockify_A(Blocks77 out, const Fq a[p]) {
    int i;
    zero_blocks(out);
    for (i = 0; i < p; i++) out[i / BS][i % BS] = mod_q_i64_raw(a[i]);
}

static void blockify_B(Blocks77 out, const small b[p]) {
    int i;
    zero_blocks(out);
    for (i = 0; i < p; i++) out[i / BS][i % BS] = mod_q_i64_raw(b[i]);
}
