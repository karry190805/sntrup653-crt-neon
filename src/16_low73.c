/* ---------------------------------------------------------------------------
 * B16  低位分支（73 项截断卷积）  —— 论文 §3.5
 * 源文件第 1060--1086 行（共 27 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
static SNTRUP_MAYBE_UNUSED void low_part73_scalar(int clow[LOW_N], const Fq f[p], const small g[p]) {
    int kk, i;
    for (kk = 0; kk < LOW_N; kk++) {
        int64_t acc = 0;
        for (i = 0; i <= kk; i++) acc += (int64_t)f[i] * g[kk - i];
        clow[kk] = mod_q_i64_raw(acc);
    }
}

static void low_part73_neon(int clow[LOW_N], const Fq f[p], const small g[p]) {
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && SNTRUP653_LOW73_NEON
    int16_t fa[LOW_N] SNTRUP_ALIGN64;
    int16_t gr[LOW_N] SNTRUP_ALIGN64;
    int i, kk;
    for (i = 0; i < LOW_N; i++) {
        fa[i] = (int16_t)f[i];
        gr[LOW_N - 1 - i] = (int16_t)g[i];
    }
    for (kk = 0; kk < LOW_N; kk++) {
        int64_t acc = dot_i16_neon_4lane(fa, gr + (LOW_N - 1 - kk), kk + 1);
        clow[kk] = mod_q_i64_raw(acc);
    }
#else
    low_part73_scalar(clow, f, g);
#endif
}
