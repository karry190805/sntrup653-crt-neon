/* B16 低位分支（73 项截断卷积）
 *
 * crypto_core_multsntrup653.c 第 1060–1086 行，共 27 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
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
