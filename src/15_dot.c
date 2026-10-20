/* B15 系数索引与四通道点积
 *
 * crypto_core_multsntrup653.c 第 1026–1059 行，共 34 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
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
