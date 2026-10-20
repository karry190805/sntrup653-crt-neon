/* B17 最终折回与中心化
 *
 * crypto_core_multsntrup653.c 第 1087–1158 行，共 72 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
static SNTRUP_MAYBE_UNUSED void final_folding_scalar(Fq out[p], const Blocks77 Cblk, const int clow[LOW_N]) {
    int kk;
    {
        int v0 = add_q_raw(D_idx(Cblk, clow, 0), D_idx(Cblk, clow, p));
        out[0] = center_q_raw(v0);
    }
    for (kk = 1; kk <= p - 2; kk++) {
        int v = add_q_raw(D_idx(Cblk, clow, kk), D_idx(Cblk, clow, p - 1 + kk));
        v = add_q_raw(v, D_idx(Cblk, clow, p + kk));
        out[kk] = center_q_raw(v);
    }
    out[p - 1] = center_q_raw(add_q_raw(D_idx(Cblk, clow, p - 1), D_idx(Cblk, clow, 2 * p - 2)));
}

static void final_folding_neon(Fq out[p], const Blocks77 Cblk, const int clow[LOW_N]) {
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && SNTRUP653_FINAL_FOLD_NEON
    const int *main = &Cblk[0][0];
    int kk;
    out[0] = center_q_raw((int64_t)clow[0] + main[p]);

    for (kk = 1; kk + 3 <= LOW_N - 1; kk += 4) {
        int32x4_t s = vaddq_s32(vld1q_s32(&clow[kk]), vld1q_s32(&main[p - 1 + kk]));
        int32_t t[4];
        int lane;
        s = vaddq_s32(s, vld1q_s32(&main[p + kk]));
        vst1q_s32(t, s);
        for (lane = 0; lane < 4; lane++) out[kk + lane] = center_q_raw(t[lane]);
    }
    for (; kk < LOW_N; kk++) {
        out[kk] = center_q_raw((int64_t)clow[kk] + main[p - 1 + kk] + main[p + kk]);
    }

    for (kk = LOW_N; kk + 3 <= p - LOW_N - 2; kk += 4) {
        int32x4_t s = vaddq_s32(vld1q_s32(&main[kk]), vld1q_s32(&main[p - 1 + kk]));
        int32_t t[4];
        int lane;
        s = vaddq_s32(s, vld1q_s32(&main[p + kk]));
        vst1q_s32(t, s);
        for (lane = 0; lane < 4; lane++) out[kk + lane] = center_q_raw(t[lane]);
    }
    for (; kk <= p - LOW_N - 2; kk++) {
        out[kk] = center_q_raw((int64_t)main[kk] + main[p - 1 + kk] + main[p + kk]);
    }

    kk = p - LOW_N - 1;
    out[kk] = center_q_raw((int64_t)main[kk] + main[MAIN_N - 1]
                           + main[0] - clow[0]);

    for (kk++; kk + 3 <= p - 2; kk += 4) {
        int j0 = kk - (p - LOW_N);
        int j1 = kk - (p - LOW_N - 1);
        int32x4_t s = vld1q_s32(&main[kk]);
        int32x4_t t0 = vsubq_s32(vld1q_s32(&main[j0]), vld1q_s32(&clow[j0]));
        int32x4_t t1 = vsubq_s32(vld1q_s32(&main[j1]), vld1q_s32(&clow[j1]));
        int32_t t[4];
        int lane;
        s = vaddq_s32(vaddq_s32(s, t0), t1);
        vst1q_s32(t, s);
        for (lane = 0; lane < 4; lane++) out[kk + lane] = center_q_raw(t[lane]);
    }
    for (; kk <= p - 2; kk++) {
        int j0 = kk - (p - LOW_N);
        int j1 = kk - (p - LOW_N - 1);
        out[kk] = center_q_raw((int64_t)main[kk] + main[j0] - clow[j0]
                               + main[j1] - clow[j1]);
    }
    out[p - 1] = center_q_raw((int64_t)main[p - 1] + main[LOW_N - 1] - clow[LOW_N - 1]);
#else
    final_folding_scalar(out, Cblk, clow);
#endif
}
