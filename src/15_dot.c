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
static inline int32_t dot_i16_neon_4lane(const int16_t *x, const int16_t *y, int n) {
    /* Four independent accumulators break the vmlal latency chain; the
     * 73 low-part dots are run one after another, so a single dependent
     * accumulator would otherwise serialise almost the whole stage. */
    int32x4_t a0 = vdupq_n_s32(0), a1 = vdupq_n_s32(0);
    int32x4_t a2 = vdupq_n_s32(0), a3 = vdupq_n_s32(0);
    int i;
    int32_t sum;
    for (i = 0; i + 16 <= n; i += 16) {
        int16x8_t vx0 = vld1q_s16(x + i);
        int16x8_t vy0 = vld1q_s16(y + i);
        int16x8_t vx1 = vld1q_s16(x + i + 8);
        int16x8_t vy1 = vld1q_s16(y + i + 8);
        a0 = vmlal_s16(a0, vget_low_s16(vx0), vget_low_s16(vy0));
        a1 = vmlal_high_s16(a1, vx0, vy0);
        a2 = vmlal_s16(a2, vget_low_s16(vx1), vget_low_s16(vy1));
        a3 = vmlal_high_s16(a3, vx1, vy1);
    }
    if (i + 8 <= n) {
        int16x8_t vx = vld1q_s16(x + i);
        int16x8_t vy = vld1q_s16(y + i);
        a0 = vmlal_s16(a0, vget_low_s16(vx), vget_low_s16(vy));
        a1 = vmlal_high_s16(a1, vx, vy);
        i += 8;
    }
    for (; i + 4 <= n; i += 4) {
        a0 = vmlal_s16(a0, vld1_s16(x + i), vld1_s16(y + i));
    }
    sum = vaddvq_s32(vaddq_s32(vaddq_s32(a0, a1), vaddq_s32(a2, a3)));
    for (; i < n; i++) sum += (int32_t)x[i] * y[i];
    return sum;
}
#endif

