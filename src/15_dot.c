/* ---------------------------------------------------------------------------
 * B15  系数索引与四通道点积  —— 论文 §3.5
 * 源文件第 1026--1059 行（共 34 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
static inline int main_coeff(const Blocks77 Cblk, int idx) {
    return Cblk[idx / BS][idx % BS];
}

static int D_idx(const Blocks77 Cblk, const int clow[LOW_N], int idx) {
    if (idx < LOW_N) return clow[idx];
    if (idx < MAIN_N) return main_coeff(Cblk, idx);
    {
        int j = idx - MAIN_N;
        return sub_q_raw(main_coeff(Cblk, j), clow[j]);
    }
}


/* NEON low-part and final-folding helpers. */

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static inline int64_t dot_i16_neon_4lane(const int16_t *x, const int16_t *y, int n) {
    int32x4_t acc = vdupq_n_s32(0);
    int i;
    int32_t tmp[4];
    int64_t sum;
    for (i = 0; i + 4 <= n; i += 4) {
        int16x4_t vx = vld1_s16(x + i);
        int16x4_t vy = vld1_s16(y + i);
        acc = vmlal_s16(acc, vx, vy);
    }
    vst1q_s32(tmp, acc);
    sum = (int64_t)tmp[0] + tmp[1] + tmp[2] + tmp[3];
    for (; i < n; i++) sum += (int64_t)x[i] * y[i];
    return sum;
}
#endif
