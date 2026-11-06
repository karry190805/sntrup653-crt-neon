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
#if SNTRUP653_FAST16
typedef int16_t Blocks16[BLOCKS][BS];
typedef int16_t Matrix16[7][11][BS];
#endif

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

#if SNTRUP653_FAST16
/* The 16-bit path may pick a different representative of the same residue
 * class as the 32-bit path (the half-slot TMVP case uses M*(q-AM) instead of
 * AM*(q-M)), so stage-level comparisons are modulo q.  The end-to-end byte
 * equality is covered by the core-equivalence test. */
static int compare_block_mod_q16(const char *stage, int case_id, int block,
                                 const int a[BS], const int16_t b[BS]) {
    int c;
    for (c = 0; c < BS; c++) {
        int d = (int)(((long long)a[c] - (long long)b[c]) % q);
        if (d < 0) d += q;
        if (d != 0) {
            fprintf(stderr,
                    "internal mismatch stage=%s case=%d block=%d lane=%d ref=%d got=%d\n",
                    stage, case_id, block, c, a[c], (int)b[c]);
            return 0;
        }
    }
    return 1;
}

static int compare_blocks_mod_q16(const char *stage, int case_id,
                                  const Blocks77 a, const Blocks16 b) {
    int i;
    for (i = 0; i < BLOCKS; i++) {
        if (!compare_block_mod_q16(stage, case_id, i, a[i], b[i])) return 0;
    }
    return 1;
}

/* Both inverse paths return 32-bit accumulators, so compare them directly. */
static int compare_blocks77_mod_q(const char *stage, int case_id,
                                  const Blocks77 a, const Blocks77 b) {
    int i, c;
    for (i = 0; i < BLOCKS; i++) {
        for (c = 0; c < BS; c++) {
            int d = (int)(((long long)a[i][c] - (long long)b[i][c]) % q);
            if (d < 0) d += q;
            if (d != 0) {
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
#endif

static void zero_blocks(Blocks77 a) {
    memset(a, 0, sizeof(int) * BLOCKS * BS);
}

/* A blockified input is only ever consumed by the public-zero-pruned first
 * layer in the delivered build: it reads the 41 active blocks and nothing
 * else.  The padding lanes of the last active block still have to be zero,
 * but blocks 41..76 are dead before the first layer overwrites them. */
static void blockify_clear_padding(Blocks77 out) {
#if SNTRUP653_PUBLIC_ZS_MODE == 1
    int i;
    for (i = p; i < ACTIVE_BLOCKS * BS; i++) ((int *)out)[i] = 0;
#else
    zero_blocks(out);
#endif
}

static SNTRUP_MAYBE_UNUSED void blockify_A(Blocks77 out, const Fq a[p]) {
    int i;
    int *dst = &out[0][0];
    blockify_clear_padding(out);
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    {
        const int32x4_t qv = vdupq_n_s32(q);
        for (i = 0; i + 8 <= p; i += 8) {
            int16x8_t x = vld1q_s16(&a[i]);
            int32x4_t lo = vmovl_s16(vget_low_s16(x));
            int32x4_t hi = vmovl_s16(vget_high_s16(x));
            lo = vaddq_s32(lo, vandq_s32(vshrq_n_s32(lo, 31), qv));
            hi = vaddq_s32(hi, vandq_s32(vshrq_n_s32(hi, 31), qv));
            vst1q_s32(dst + i, lo);
            vst1q_s32(dst + i + 4, hi);
        }
    }
#endif
    for (; i < p; i++) {
        int v = a[i];
        dst[i] = v < 0 ? v + q : v;
    }
}

static void blockify_B(Blocks77 out, const small b[p]) {
    int i;
    int *dst = &out[0][0];
    blockify_clear_padding(out);
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    {
        const int16x8_t qv = vdupq_n_s16((int16_t)q);
        for (i = 0; i + 8 <= p; i += 8) {
            int16x8_t x = vmovl_s8(vld1_s8(&b[i]));
            x = vaddq_s16(x, vandq_s16(vshrq_n_s16(x, 15), qv));
            vst1q_s32(dst + i, vmovl_s16(vget_low_s16(x)));
            vst1q_s32(dst + i + 4, vmovl_high_s16(x));
        }
    }
#endif
    for (; i < p; i++) {
        int v = b[i];
        dst[i] = v < 0 ? v + q : v;
    }
}

#if SNTRUP653_FAST16
static void blockify_clear_padding16(Blocks16 out) {
#if SNTRUP653_PUBLIC_ZS_MODE
    int i;
    for (i = p; i < ACTIVE_BLOCKS * BS; i++) ((int16_t *)out)[i] = 0;
#else
    /* Dense mode reads every block, including the public-zero tail. */
    memset(out, 0, sizeof(Blocks16));
#endif
}

/* Decode the packed int16 input straight into the 16-bit block layout, and
 * keep the flat copy that low_part73_neon() consumes. */
static void blockify_A_bytes16(Blocks16 out, Fq f[p], const unsigned char *inbytes) {
    int i;
    int16_t *dst = &out[0][0];
    blockify_clear_padding16(out);
    {
        const uint16x8_t qv = vdupq_n_u16((uint16_t)q);
        const int16x8_t mu =
            vdupq_n_s16((int16_t)(((uint32_t)1 << 15) / (uint32_t)q));
        for (i = 0; i + 8 <= p; i += 8) {
            int16x8_t x = vreinterpretq_s16_u16(
                vreinterpretq_u16_u8(vld1q_u8(inbytes + 2 * i)));
            int16x8_t qhat = vqrdmulhq_s16(x, mu);
            uint16x8_t u = vreinterpretq_u16_s16(
                vmlsq_s16(x, qhat, vreinterpretq_s16_u16(qv)));
            int16x8_t r = vreinterpretq_s16_u16(vminq_u16(u, vaddq_u16(u, qv)));
            vst1q_s16(dst + i, r);
            vst1q_s16(&f[i], r);
        }
    }
    for (; i < p; i++) {
        int v = (int16_t)(inbytes[2 * i] | ((unsigned)inbytes[2 * i + 1] << 8));
        int r = mod_q_i64_raw(v);
        dst[i] = (int16_t)r;
        f[i] = (Fq)r;
    }
}

static void blockify_B16(Blocks16 out, const small b[p]) {
    int i;
    int16_t *dst = &out[0][0];
    blockify_clear_padding16(out);
    {
        const int16x8_t qv = vdupq_n_s16((int16_t)q);
        for (i = 0; i + 8 <= p; i += 8) {
            int16x8_t x = vmovl_s8(vld1_s8(&b[i]));
            x = vaddq_s16(x, vandq_s16(vshrq_n_s16(x, 15), qv));
            vst1q_s16(dst + i, x);
        }
    }
    for (; i < p; i++) {
        int v = b[i];
        dst[i] = (int16_t)(v < 0 ? v + q : v);
    }
}
#endif

