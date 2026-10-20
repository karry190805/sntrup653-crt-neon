#include "crypto_core_multsntrup653.h"
#include "params.h"
#include "crypto_decode_653xint16.h"
#include "crypto_encode_653xint16.h"

#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#if !defined(__aarch64__) || (!defined(__ARM_NEON) && !defined(__ARM_NEON__))
#error "The optimized core requires AArch64 NEON."
#endif
#ifdef SNTRUP653_INTERNAL_TEST_HOOKS
#include <stdio.h>
#endif

#if defined(SNTRUP653_REQUIRE_AARCH64_NEON)
#if !defined(__aarch64__) || (!defined(__ARM_NEON) && !defined(__ARM_NEON__))
#error "The optimized benchmark build requires AArch64 Advanced SIMD (NEON)."
#endif
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#define int8 int8_t
#define int16 int16_t
#define int32 int32_t
typedef int8 small;
typedef int16 Fq;

#if defined(__GNUC__) || defined(__clang__)
#define SNTRUP_ALIGN64 __attribute__((aligned(64)))
#define SNTRUP_MAYBE_UNUSED __attribute__((unused))
#else
#define SNTRUP_ALIGN64
#define SNTRUP_MAYBE_UNUSED
#endif

/* Unified A53/A76/X1 implementation: pairwise 7/11-point NEON transforms,
 * public-zero pruning, fixed radix-2 + two-TMVP8 weighted16 products,
 * NEON low-73 recovery, and NEON final folding.
 *
 * The public-zero pruning of the first Good--Thomas layer is realised by
 * compile-time specialisation (one fully unrolled 11-point kernel per r)
 * rather than by a run-time mask test, see the SNTRUP_L1_* macros below. */

enum {
    MAIN_N = 1232,
    LOW_N = 73,
    BLOCKS = 77,
    BS = 16,
    PAIRWISE_QOFF_MULT = 25000,
    MAX_ACTIVE_COLS = 6,
    ACTIVE_BLOCKS = (p + BS - 1) / BS,
    LAST_ACTIVE_BLOCK = ACTIVE_BLOCKS - 1,
    LAST_ACTIVE_USED = p - LAST_ACTIVE_BLOCK * BS
};

_Static_assert(INT32_MAX > 328973620, "pairwise NTT accumulator requires int32");
_Static_assert(INT64_MAX > INT64_C(1578119558400), "weighted16 accumulator requires int64");
_Static_assert((int64_t)q * PAIRWISE_QOFF_MULT < INT32_MAX,
               "pairwise q offset must fit signed int32");
_Static_assert(ACTIVE_BLOCKS == 41, "public-mask layout assumes 41 active blocks");

/* ---------------------------------------------------------------------------
 * Public padding pattern as integer constant expressions.
 *
 * The first layer of the 11-point-first Good--Thomas order fixes r and runs a
 * DFT over s, reading block
 *
 *     SNTRUP_GT_IDX(r,s) = (22*r + 56*s) mod 77 .
 *
 * A column is public padding exactly when that index is >= ACTIVE_BLOCKS.
 * Both the index and the predicate are integer constant expressions as soon as
 * r and s are literals, which is what makes the specialisation below possible.
 * The pattern depends only on the public parameter p = 653: no key material,
 * no ciphertext and no run-time value takes part in it.
 * ------------------------------------------------------------------------- */
#define SNTRUP_GT_IDX(RR, SS) (((22 * (RR)) + (56 * (SS))) % 77)
#define SNTRUP_GT_ACT(RR, SS) (SNTRUP_GT_IDX(RR, SS) < ACTIVE_BLOCKS)
#define SNTRUP_GT_ACT_COUNT(RR)                                     \
    (SNTRUP_GT_ACT(RR, 0) + SNTRUP_GT_ACT(RR, 1) +                  \
     SNTRUP_GT_ACT(RR, 2) + SNTRUP_GT_ACT(RR, 3) +                  \
     SNTRUP_GT_ACT(RR, 4) + SNTRUP_GT_ACT(RR, 5) +                  \
     SNTRUP_GT_ACT(RR, 6) + SNTRUP_GT_ACT(RR, 7) +                  \
     SNTRUP_GT_ACT(RR, 8) + SNTRUP_GT_ACT(RR, 9) +                  \
     SNTRUP_GT_ACT(RR, 10))

/* 6+6+6+6+6+6+5 = 41 = ACTIVE_BLOCKS.  If this ever fails, the specialised
 * schedule and the ACTIVE_COL_LIST table no longer describe the same columns. */
_Static_assert(SNTRUP_GT_ACT_COUNT(0) + SNTRUP_GT_ACT_COUNT(1) +
               SNTRUP_GT_ACT_COUNT(2) + SNTRUP_GT_ACT_COUNT(3) +
               SNTRUP_GT_ACT_COUNT(4) + SNTRUP_GT_ACT_COUNT(5) +
               SNTRUP_GT_ACT_COUNT(6) == ACTIVE_BLOCKS,
               "specialised first layer must cover every active block");
_Static_assert(SNTRUP_GT_ACT_COUNT(0) == 6 && SNTRUP_GT_ACT_COUNT(1) == 6 &&
               SNTRUP_GT_ACT_COUNT(2) == 6 && SNTRUP_GT_ACT_COUNT(3) == 6 &&
               SNTRUP_GT_ACT_COUNT(4) == 6 && SNTRUP_GT_ACT_COUNT(5) == 6 &&
               SNTRUP_GT_ACT_COUNT(6) == 5,
               "unexpected public padding pattern for p = 653");

#ifndef SNTRUP653_USE_INPLACE_BUFFERS
#define SNTRUP653_USE_INPLACE_BUFFERS 1
#endif

#ifndef SNTRUP653_PUBLIC_ZS_MODE
#define SNTRUP653_PUBLIC_ZS_MODE 1
#endif

/* Legacy selector, kept for A/B measurement only.
 * 0 (default): the public-zero pruning of the first layer is resolved at build
 *              time (one specialised, fully unrolled kernel per r).
 * 1          : the historical shape is kept - only the active columns are
 *              loaded and the pairs are skipped by a run-time mask test inside
 *              the j loop of dft11_pairwise_pruned_4lane().
 * The meaning of SNTRUP653_PUBLIC_ZS_MODE (0 = dense, 1 = public-zero pruning)
 * is not affected by this switch. */
#ifndef SNTRUP653_PUBLIC_ZS_RUNTIME_MASK
#define SNTRUP653_PUBLIC_ZS_RUNTIME_MASK 0
#endif

#ifndef SNTRUP653_LOW73_NEON
#define SNTRUP653_LOW73_NEON 1
#endif

#ifndef SNTRUP653_FINAL_FOLD_NEON
#define SNTRUP653_FINAL_FOLD_NEON 1
#endif

#if SNTRUP653_USE_INPLACE_BUFFERS != 0 && SNTRUP653_USE_INPLACE_BUFFERS != 1
#error "SNTRUP653_USE_INPLACE_BUFFERS must be 0 or 1"
#endif
#if SNTRUP653_PUBLIC_ZS_MODE < 0 || SNTRUP653_PUBLIC_ZS_MODE > 1
#error "SNTRUP653_PUBLIC_ZS_MODE must be 0 or 1"
#endif
#if SNTRUP653_PUBLIC_ZS_RUNTIME_MASK != 0 && SNTRUP653_PUBLIC_ZS_RUNTIME_MASK != 1
#error "SNTRUP653_PUBLIC_ZS_RUNTIME_MASK must be 0 or 1"
#endif
#if SNTRUP653_LOW73_NEON != 0 && SNTRUP653_LOW73_NEON != 1
#error "SNTRUP653_LOW73_NEON must be 0 or 1"
#endif
#if SNTRUP653_FINAL_FOLD_NEON != 0 && SNTRUP653_FINAL_FOLD_NEON != 1
#error "SNTRUP653_FINAL_FOLD_NEON must be 0 or 1"
#endif

static atomic_int tables_state = ATOMIC_VAR_INIT(0);
static int INV2 = 0;
static int INV77 = 0;
static int ROOT77 = 0;
static int LAMBDA77[BLOCKS];
static int SQRT_LAMBDA77[BLOCKS];
static int INV2_SQRT_LAMBDA77[BLOCKS];
static int POW7_F[7][7];
static int POW7_I[7][7];
static int POW11_F[11][11];
static int POW11_I[11][11];
/* Pairwise direct DFT constants: P=(w^{kj}+w^{-kj})/2, M=(w^{kj}-w^{-kj})/2.
 * Indices use k=1..(n-1)/2 and j=1..(n-1)/2. */
static int PAIR7_P_F[4][4];
static int PAIR7_M_F[4][4];
static int PAIR7_P_I[4][4];
static int PAIR7_M_I[4][4];
static int PAIR11_P_F[6][6];
static int PAIR11_M_F[6][6];
static int PAIR11_P_I[6][6];
static int PAIR11_M_I[6][6];
static int GT_IDX_RS[11][7];
static int GT_K_UV[7][11];
static int ACTIVE_COLS_PER_R[7];
static int ACTIVE_COL_LIST[7][11];
static int ACTIVE_PAIR_MASK11[7][6];

#ifndef USE_BARRETT_REDUCE
#define USE_BARRETT_REDUCE 1
#endif

#if USE_BARRETT_REDUCE
/*
 * Fixed-modulus Barrett reduction for q = 4621.
 * USE_BARRETT_REDUCE defaults to 1 in this package.
 * Set -DUSE_BARRETT_REDUCE=0 to fall back to the conservative % q path
 * for validation.
 * Scalar correction and sign masks use explicit AArch64 instructions.
 * This fixes secret-dependent branches, not all possible side channels.
 */
static const uint64_t BARRETT_MU_Q64 = 3991937691778738ULL; /* floor(2^64 / 4621) */
static const uint32_t BARRETT_MU_Q32 = 929445U;             /* floor(2^32 / 4621) */

static inline uint64_t ct_sub_q_u64(uint64_t x) {
    uint64_t r;
    __asm__ ("subs %0, %1, %2\n\tcsel %0, %0, %1, hs"
             : "=&r"(r) : "r"(x), "r"((uint64_t)q) : "cc");
    return r;
}

static inline uint64_t ct_sign_mask_i64(int64_t x) {
    uint64_t mask;
    __asm__ ("asr %0, %1, #63" : "=r"(mask) : "r"(x));
    return mask;
}

static inline int mod_q_u64_barrett(uint64_t x) {
    uint64_t t = (uint64_t)(((unsigned __int128)x * BARRETT_MU_Q64) >> 64);
    uint64_t r = x - t * (uint64_t)q;
    r = ct_sub_q_u64(r);
    r = ct_sub_q_u64(r);
    return (int)r;
}

static inline int mod_q_u32_barrett(uint32_t x) {
    uint32_t t = (uint32_t)(((uint64_t)x * BARRETT_MU_Q32) >> 32);
    uint32_t r = x - t * (uint32_t)q;
    r = (uint32_t)ct_sub_q_u64(r);
    r = (uint32_t)ct_sub_q_u64(r);
    return (int)r;
}
#endif

static inline int mod_q_i64_raw(int64_t x) {
#if USE_BARRETT_REDUCE
    uint64_t mask = ct_sign_mask_i64(x);
    uint64_t ax = ((uint64_t)x ^ mask) - mask;
    uint64_t r = (uint64_t)mod_q_u64_barrett(ax);
    uint64_t neg_r = ct_sub_q_u64((uint64_t)q - r);
    return (int)((r & ~mask) | (neg_r & mask));
#else
    int64_t r = x % q;
    if (r < 0) r += q;
    return (int)r;
#endif
}

static inline int add_q_raw(int a, int b) {
#if USE_BARRETT_REDUCE
    return (int)ct_sub_q_u64((uint64_t)(a + b));
#else
    int r = a + b;
    if (r >= q) r -= q;
    return r;
#endif
}

static inline int sub_q_raw(int a, int b) {
#if USE_BARRETT_REDUCE
    return (int)ct_sub_q_u64((uint64_t)(a + q - b));
#else
    int r = a - b;
    if (r < 0) r += q;
    return r;
#endif
}

static inline int mul_q_raw(int a, int b) {
    return mod_q_i64_raw((int64_t)a * b);
}

static inline Fq center_q_raw(int64_t x) {
    int r = mod_q_i64_raw(x);
#if USE_BARRETT_REDUCE
    int centered;
    __asm__ ("sub %w0, %w1, %w2\n\tcmp %w1, %w3\n\tcsel %w0, %w0, %w1, hi"
             : "=&r"(centered) : "r"(r), "r"(q), "r"(q / 2) : "cc");
    return (Fq)centered;
#else
    if (r > q / 2) r -= q;
    return (Fq)r;
#endif
}

/* Match the centered input normalization used by the PQClean clean core. */
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

static SNTRUP_MAYBE_UNUSED void good_thomas_77_ntt_zs(Blocks77 out, const Blocks77 in, int invert, int public_zs_forward) {
    const int (*P7tab)[7] = invert ? POW7_I : POW7_F;
    const int (*P11tab)[11] = invert ? POW11_I : POW11_F;
    MatrixGT tmp SNTRUP_ALIGN64;
    int s, u, v, r, c, t, k;

    if (public_zs_forward) {
        for (r = 0; r < 7; r++) {
            int ns = ACTIVE_COLS_PER_R[r];
            for (v = 0; v < 11; v++) {
                int64_t acc[BS] = {0};
                for (t = 0; t < ns; t++) {
                    int ss = ACTIVE_COL_LIST[r][t];
                    int idx = GT_IDX_RS[ss][r];
                    int coeff = P11tab[v][ss];
                    for (c = 0; c < BS; c++) acc[c] += (int64_t)in[idx][c] * coeff;
                }
                for (c = 0; c < BS; c++) tmp[r][v][c] = mod_q_i64_raw(acc[c]);
            }
        }
    } else {
        for (r = 0; r < 7; r++) {
            for (v = 0; v < 11; v++) {
                int64_t acc[BS] = {0};
                for (s = 0; s < 11; s++) {
                    int idx = GT_IDX_RS[s][r];
                    int coeff = P11tab[v][s];
                    for (c = 0; c < BS; c++) acc[c] += (int64_t)in[idx][c] * coeff;
                }
                for (c = 0; c < BS; c++) tmp[r][v][c] = mod_q_i64_raw(acc[c]);
            }
        }
    }

    /* Second layer: 11 independent 7-point direct DFTs. */
    for (u = 0; u < 7; u++) {
        for (v = 0; v < 11; v++) {
            int64_t acc[BS] = {0};
            k = GT_K_UV[u][v];
            for (r = 0; r < 7; r++) {
                int coeff = P7tab[u][r];
                for (c = 0; c < BS; c++) acc[c] += (int64_t)tmp[r][v][c] * coeff;
            }
            for (c = 0; c < BS; c++) {
                int val = mod_q_i64_raw(acc[c]);
                if (invert) val = mul_q_raw(val, INV77);
                out[k][c] = val;
            }
        }
    }
}


/* NEON Good--Thomas helpers vectorize across the 16 lanes in each block. */

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static inline uint32x4_t vmod_q_u32_barrett(uint32x4_t x) {
#if USE_BARRETT_REDUCE
    uint32x2_t xlo = vget_low_u32(x);
    uint32x2_t xhi = vget_high_u32(x);
    uint64x2_t prod_lo = vmull_n_u32(xlo, BARRETT_MU_Q32);
    uint64x2_t prod_hi = vmull_n_u32(xhi, BARRETT_MU_Q32);
    uint32x2_t qhat_lo = vshrn_n_u64(prod_lo, 32);
    uint32x2_t qhat_hi = vshrn_n_u64(prod_hi, 32);
    uint32x4_t qhat = vcombine_u32(qhat_lo, qhat_hi);
    uint32x4_t qvec = vdupq_n_u32((uint32_t)q);
    uint32x4_t r = vsubq_u32(x, vmulq_u32(qhat, qvec));
    uint32x4_t ge = vcgeq_u32(r, qvec);
    r = vsubq_u32(r, vandq_u32(ge, qvec));
    ge = vcgeq_u32(r, qvec);
    r = vsubq_u32(r, vandq_u32(ge, qvec));
    return r;
#else
    uint32_t t[4];
    vst1q_u32(t, x);
    t[0] = (uint32_t)mod_q_i64_raw((int64_t)t[0]);
    t[1] = (uint32_t)mod_q_i64_raw((int64_t)t[1]);
    t[2] = (uint32_t)mod_q_i64_raw((int64_t)t[2]);
    t[3] = (uint32_t)mod_q_i64_raw((int64_t)t[3]);
    return vld1q_u32(t);
#endif
}

/* Precondition: all lanes in x are non-negative DFT accumulators. */
static inline void store_mod_q_s32x4(int *dst, int32x4_t x) {
    uint32x4_t red = vmod_q_u32_barrett(vreinterpretq_u32_s32(x));
    vst1q_s32(dst, vreinterpretq_s32_u32(red));
}

/* Precondition: all lanes in x are non-negative DFT accumulators. */
static inline void store_mod_q_inv77_s32x4(int *dst, int32x4_t x, int invert) {
    uint32x4_t red = vmod_q_u32_barrett(vreinterpretq_u32_s32(x));
    if (invert) {
        uint32x4_t invv = vdupq_n_u32((uint32_t)INV77);
        red = vmod_q_u32_barrett(vmulq_u32(red, invv));
    }
    vst1q_s32(dst, vreinterpretq_s32_u32(red));
}
#endif


/* Pairwise-output NEON 11/7-point DFT subkernels. */

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static inline int32x4_t add_q_if_negative_s32x4(int32x4_t x) {
    uint32x4_t neg = vcltq_s32(x, vdupq_n_s32(0));
    int32x4_t add = vreinterpretq_s32_u32(vandq_u32(neg, vdupq_n_u32((uint32_t)q)));
    return vaddq_s32(x, add);
}

/* Pairwise direct DFT over block16 lanes.
 * Computes Y_k and Y_{n-k} together using
 *   S_j = A_j + A_{n-j}, D_j = A_j - A_{n-j},
 *   P_{k,j}=(w^{kj}+w^{-kj})/2, M_{k,j}=(w^{kj}-w^{-kj})/2.
 * The D_j branch is represented modulo q to keep NEON accumulators non-negative.
 */
static inline void dft11_pairwise_4lane(int32x4_t outv[11], const int32x4_t inv[11],
                                        const int PairP[6][6], const int PairM[6][6]) {
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int k;
    int32x4_t acc0 = inv[0];
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[1], inv[10]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[2], inv[9]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[3], inv[8]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[4], inv[7]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[5], inv[6]));
    outv[0] = acc0;

    for (k = 1; k <= 5; k++) {
        int32x4_t even = inv[0];
        int32x4_t odd = vdupq_n_s32(0);
        int32x4_t s, d;

        s = vaddq_s32(inv[1], inv[10]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[1], inv[10]));
        even = vmlaq_n_s32(even, s, PairP[k][1]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][1]);

        s = vaddq_s32(inv[2], inv[9]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[2], inv[9]));
        even = vmlaq_n_s32(even, s, PairP[k][2]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][2]);

        s = vaddq_s32(inv[3], inv[8]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[3], inv[8]));
        even = vmlaq_n_s32(even, s, PairP[k][3]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][3]);

        s = vaddq_s32(inv[4], inv[7]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[4], inv[7]));
        even = vmlaq_n_s32(even, s, PairP[k][4]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][4]);

        s = vaddq_s32(inv[5], inv[6]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[5], inv[6]));
        even = vmlaq_n_s32(even, s, PairP[k][5]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][5]);

        outv[k] = vaddq_s32(even, odd);
        outv[11 - k] = vaddq_s32(vsubq_s32(even, odd), qoff);
    }
}

/* Legacy public-zero pruning: still loads only the active columns, but the
 * pair schedule is decided at run time by pair_mask, whose test sits inside
 * the j loop.  Kept only for SNTRUP653_PUBLIC_ZS_RUNTIME_MASK = 1 (A/B runs).
 * The pruning masks depend only on public p=653 padding.
 * A pair is skipped only when both public input positions are zero. */
static inline void dft11_pairwise_pruned_4lane(int32x4_t outv[11], const int32x4_t inv[11],
                                               const int PairP[6][6], const int PairM[6][6],
                                               const int pair_mask[6]) {
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int k, j;
    int32x4_t acc0 = inv[0];
    for (j = 1; j <= 5; j++) {
        if (pair_mask[j]) acc0 = vaddq_s32(acc0, vaddq_s32(inv[j], inv[11 - j]));
    }
    outv[0] = acc0;
    for (k = 1; k <= 5; k++) {
        int32x4_t even = inv[0];
        int32x4_t odd = vdupq_n_s32(0);
        for (j = 1; j <= 5; j++) {
            int32x4_t s, d;
            if (!pair_mask[j]) continue;
            s = vaddq_s32(inv[j], inv[11 - j]);
            d = add_q_if_negative_s32x4(vsubq_s32(inv[j], inv[11 - j]));
            even = vmlaq_n_s32(even, s, PairP[k][j]);
            odd = vmlaq_n_s32(odd, d, PairM[k][j]);
        }
        outv[k] = vaddq_s32(even, odd);
        outv[11 - k] = vaddq_s32(vsubq_s32(even, odd), qoff);
    }
}

static inline void dft7_pairwise_4lane(int32x4_t outv[7], const int32x4_t inv[7],
                                       const int PairP[4][4], const int PairM[4][4]) {
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int k;
    int32x4_t acc0 = inv[0];
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[1], inv[6]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[2], inv[5]));
    acc0 = vaddq_s32(acc0, vaddq_s32(inv[3], inv[4]));
    outv[0] = acc0;

    for (k = 1; k <= 3; k++) {
        int32x4_t even = inv[0];
        int32x4_t odd = vdupq_n_s32(0);
        int32x4_t s, d;

        s = vaddq_s32(inv[1], inv[6]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[1], inv[6]));
        even = vmlaq_n_s32(even, s, PairP[k][1]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][1]);

        s = vaddq_s32(inv[2], inv[5]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[2], inv[5]));
        even = vmlaq_n_s32(even, s, PairP[k][2]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][2]);

        s = vaddq_s32(inv[3], inv[4]);
        d = add_q_if_negative_s32x4(vsubq_s32(inv[3], inv[4]));
        even = vmlaq_n_s32(even, s, PairP[k][3]);
        odd  = vmlaq_n_s32(odd,  d, PairM[k][3]);

        outv[k] = vaddq_s32(even, odd);
        outv[7 - k] = vaddq_s32(vsubq_s32(even, odd), qoff);
    }
}

/* ---------------------------------------------------------------------------
 * Compile-time specialised first layer (SNTRUP653_PUBLIC_ZS_MODE = 1).
 *
 * One fully unrolled instance per r is emitted below.  Because r, s and k are
 * literals in every instance, SNTRUP_GT_ACT() and SNTRUP_GT_IDX() fold to
 * constants and the compiler can
 *
 *   (1) delete the load of every padding column      - no vld1q, no zero init;
 *   (2) delete every pair term whose two columns are both padding
 *       (7 of the 35 (r,j) pair slots for p = 653);
 *   (3) collapse the 19 "half" slots, i.e. the pairs with exactly one padding
 *       column.  Let rho(x) = (x < 0 ? x + q : x) be the fix-up that the dense
 *       kernel applies.  Every block value is already reduced into [0, q), so
 *         - column 11-j is padding (a_{11-j} = 0):  S_j = a_j,
 *                                                    D_j = rho(a_j) = a_j;
 *         - column j is padding    (a_j = 0):        S_j = a_{11-j},
 *                                                    D_j = rho(-a_{11-j})
 *                                                      = q - a_{11-j}.
 *       In both cases the vaddq_s32, the vsubq_s32 and the conditional
 *       add_q_if_negative_s32x4 all disappear, leaving only the two
 *       vmlaq_n_s32 that carry the actual arithmetic.  For the second case the
 *       dense kernel accumulates M[k][j] * (q - a_{11-j}); the specialised
 *       kernel uses the coefficient q - M[k][j] on a_{11-j}, which is
 *       congruent modulo q (M*q == 0, so (q - M)*a == -M*a == M*(q - a)) and
 *       is still non-negative because M[k][j] in [0, q), so the accumulator
 *       stays within the non-negative precondition of store_mod_q_s32x4();
 *   (4) keep the surviving column vectors in NEON registers (named c0, p1..p5,
 *       m1..m5 - never an indirect index);
 *   (5) drop the pair-mask load, the compare and the branch that the old
 *       run-time loop spent on every (k,j) step, and drop the j/k loop
 *       control entirely.
 *
 * Steps (2)+(3) act on 130 of the 175 (r,j,k) term bodies (35 (r,j) slots
 * times 5 values of k): the 95 bodies in half slots fall from 5 vector ops to
 * 2, and the 35 bodies in padding pairs fall from 5 to 0.  Per lane group the
 * pair arithmetic drops from 875 to 415 vector ops, a 52.6% reduction; steps
 * (1)+(4)+(5) remove the run-time overhead that used to eat that saving.
 *
 * Every instance evaluates the same expression
 *     even_k = a_0 + sum_j P[k][j] * S_j ,   odd_k = sum_j M[k][j] * D_j ,
 *     Y_k = even_k + odd_k ,   Y_{11-k} = even_k - odd_k + q*25000
 * as the dense kernel, with the same PAIR11_P_F / PAIR11_M_F table entries and
 * the same accumulator bounds, so the stored values are bit-identical to
 * SNTRUP653_PUBLIC_ZS_MODE = 0.
 * ------------------------------------------------------------------------- */

/* Load column s of row r into VAR, or the zero vector when it is padding. */
#define SNTRUP_L1_LOAD(RR, SS, VAR)                                          \
    int32x4_t SNTRUP_MAYBE_UNUSED VAR = SNTRUP_GT_ACT(RR, SS)                 \
        ? vld1q_s32(&in[SNTRUP_GT_IDX(RR, SS)][cbase])                        \
        : vdupq_n_s32(0);

/* outv[0] == sum of the active columns   (j == 0 term of the DFT). */
#define SNTRUP_L1_DC(RR, SS, VAR)                                            \
    if (SNTRUP_GT_ACT(RR, SS)) acc0_ = vaddq_s32(acc0_, VAR);

/* Contribution of pair (j, 11-j) to even_k / odd_k. */
#define SNTRUP_L1_TERM(RR, KV, JJ, AJ, AM)                                   \
    do {                                                                      \
        if (SNTRUP_GT_ACT(RR, JJ)) {                                          \
            if (SNTRUP_GT_ACT(RR, 11 - (JJ))) {                               \
                int32x4_t sj_ = vaddq_s32(AJ, AM);                            \
                int32x4_t dj_ = add_q_if_negative_s32x4(vsubq_s32(AJ, AM));   \
                even_ = vmlaq_n_s32(even_, sj_, P11P[KV][JJ]);                \
                odd_ = vmlaq_n_s32(odd_, dj_, P11M[KV][JJ]);                  \
            } else {                                                          \
                even_ = vmlaq_n_s32(even_, AJ, P11P[KV][JJ]);                 \
                odd_ = vmlaq_n_s32(odd_, AJ, P11M[KV][JJ]);                   \
            }                                                                 \
        } else if (SNTRUP_GT_ACT(RR, 11 - (JJ))) {                            \
            even_ = vmlaq_n_s32(even_, AM, P11P[KV][JJ]);                     \
            odd_ = vmlaq_n_s32(odd_, AM, q - P11M[KV][JJ]);                   \
        }                                                                     \
    } while (0)

/* One output pair (k, 11-k), both halves sharing even_ / odd_. */
#define SNTRUP_L1_K(RR, KV)                                                   \
    do {                                                                      \
        even_ = c0;                                                           \
        odd_ = vdupq_n_s32(0);                                                \
        SNTRUP_L1_TERM(RR, KV, 1, p1, m1);                                    \
        SNTRUP_L1_TERM(RR, KV, 2, p2, m2);                                    \
        SNTRUP_L1_TERM(RR, KV, 3, p3, m3);                                    \
        SNTRUP_L1_TERM(RR, KV, 4, p4, m4);                                    \
        SNTRUP_L1_TERM(RR, KV, 5, p5, m5);                                    \
        store_mod_q_s32x4(&tmp[RR][KV][cbase], vaddq_s32(even_, odd_));        \
        store_mod_q_s32x4(&tmp[RR][11 - (KV)][cbase],                          \
                          vaddq_s32(vsubq_s32(even_, odd_), qoff_));           \
    } while (0)

/* The whole first layer for one fixed r. */
#define SNTRUP_L1_ROW(RR)                                                     \
    do {                                                                      \
        for (cbase = 0; cbase < BS; cbase += 4) {                             \
            int32x4_t even_, odd_, acc0_;                                     \
            const int32x4_t qoff_ = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);      \
            SNTRUP_L1_LOAD(RR, 0, c0)                                         \
            SNTRUP_L1_LOAD(RR, 1, p1)   SNTRUP_L1_LOAD(RR, 10, m1)            \
            SNTRUP_L1_LOAD(RR, 2, p2)   SNTRUP_L1_LOAD(RR, 9, m2)             \
            SNTRUP_L1_LOAD(RR, 3, p3)   SNTRUP_L1_LOAD(RR, 8, m3)             \
            SNTRUP_L1_LOAD(RR, 4, p4)   SNTRUP_L1_LOAD(RR, 7, m4)             \
            SNTRUP_L1_LOAD(RR, 5, p5)   SNTRUP_L1_LOAD(RR, 6, m5)             \
            acc0_ = vdupq_n_s32(0);                                           \
            SNTRUP_L1_DC(RR, 0, c0)   SNTRUP_L1_DC(RR, 1, p1)                 \
            SNTRUP_L1_DC(RR, 2, p2)   SNTRUP_L1_DC(RR, 3, p3)                 \
            SNTRUP_L1_DC(RR, 4, p4)   SNTRUP_L1_DC(RR, 5, p5)                 \
            SNTRUP_L1_DC(RR, 6, m5)   SNTRUP_L1_DC(RR, 7, m4)                 \
            SNTRUP_L1_DC(RR, 8, m3)   SNTRUP_L1_DC(RR, 9, m2)                 \
            SNTRUP_L1_DC(RR, 10, m1)                                          \
            store_mod_q_s32x4(&tmp[RR][0][cbase], acc0_);                     \
            SNTRUP_L1_K(RR, 1); SNTRUP_L1_K(RR, 2); SNTRUP_L1_K(RR, 3);       \
            SNTRUP_L1_K(RR, 4); SNTRUP_L1_K(RR, 5);                           \
        }                                                                     \
    } while (0)
#endif

static void good_thomas_77_ntt_zs_neon(Blocks77 out, const Blocks77 in, int invert, int public_zs_forward) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const int (*P7P)[4] = invert ? PAIR7_P_I : PAIR7_P_F;
    const int (*P7M)[4] = invert ? PAIR7_M_I : PAIR7_M_F;
    const int (*P11P)[6] = invert ? PAIR11_P_I : PAIR11_P_F;
    const int (*P11M)[6] = invert ? PAIR11_M_I : PAIR11_M_F;
    MatrixGT tmp SNTRUP_ALIGN64;
    int s, u, v, r, cbase, k;

    if (public_zs_forward) {
#if SNTRUP653_PUBLIC_ZS_MODE == 1 && !SNTRUP653_PUBLIC_ZS_RUNTIME_MASK
        /* Public-zero pruning resolved at build time: seven specialised,
         * fully unrolled 11-point kernels, one per r.  No mask test, no table
         * indirection and no data-independent run-time branch survives. */
        SNTRUP_L1_ROW(0);
        SNTRUP_L1_ROW(1);
        SNTRUP_L1_ROW(2);
        SNTRUP_L1_ROW(3);
        SNTRUP_L1_ROW(4);
        SNTRUP_L1_ROW(5);
        SNTRUP_L1_ROW(6);
#else
        int t, ns;
        for (r = 0; r < 7; r++) {
            ns = ACTIVE_COLS_PER_R[r];
            for (cbase = 0; cbase < BS; cbase += 4) {
                int32x4_t in11[11];
                int32x4_t out11[11];
                for (s = 0; s < 11; s++) in11[s] = vdupq_n_s32(0);
                for (t = 0; t < ns; t++) {
                    int ss = ACTIVE_COL_LIST[r][t];
                    int idx = GT_IDX_RS[ss][r];
                    in11[ss] = vld1q_s32(&in[idx][cbase]);
                }
#if SNTRUP653_PUBLIC_ZS_MODE == 1
                dft11_pairwise_pruned_4lane(out11, in11, P11P, P11M, ACTIVE_PAIR_MASK11[r]);
#else
                dft11_pairwise_4lane(out11, in11, P11P, P11M);
#endif
                for (v = 0; v < 11; v++) {
                    store_mod_q_s32x4(&tmp[r][v][cbase], out11[v]);
                }
            }
        }
#endif
    } else {
        for (r = 0; r < 7; r++) {
            for (cbase = 0; cbase < BS; cbase += 4) {
                int32x4_t in11[11];
                int32x4_t out11[11];
                for (s = 0; s < 11; s++) {
                    int idx = GT_IDX_RS[s][r];
                    in11[s] = vld1q_s32(&in[idx][cbase]);
                }
                dft11_pairwise_4lane(out11, in11, P11P, P11M);
                for (v = 0; v < 11; v++) {
                    store_mod_q_s32x4(&tmp[r][v][cbase], out11[v]);
                }
            }
        }
    }

    /* Second layer: 11 independent 7-point direct DFTs, fixed-unrolled. */
    for (v = 0; v < 11; v++) {
        for (cbase = 0; cbase < BS; cbase += 4) {
            int32x4_t in7[7];
            int32x4_t out7[7];
            for (r = 0; r < 7; r++) {
                in7[r] = vld1q_s32(&tmp[r][v][cbase]);
            }
            dft7_pairwise_4lane(out7, in7, P7P, P7M);
            for (u = 0; u < 7; u++) {
                k = GT_K_UV[u][v];
                store_mod_q_inv77_s32x4(&out[k][cbase], out7[u], invert);
            }
        }
    }
#else
    good_thomas_77_ntt_zs(out, in, invert, public_zs_forward);
#endif
}

#ifdef SNTRUP653_INTERNAL_TEST_HOOKS
/* Independent schoolbook oracle; never selected by the production entry point. */
static void weighted_mul16_scalar(int out[BS], const int a[BS], const int b[BS], int lambda) {
    int64_t conv[2 * BS - 1] = {0};
    int i, j;
    for (i = 0; i < BS; i++) {
        int ai = a[i];
        for (j = 0; j < BS; j++) conv[i + j] += (int64_t)ai * b[j];
    }
    for (i = 0; i < BS; i++) {
        int64_t v = conv[i];
        if (i + BS < 2 * BS - 1) v += (int64_t)lambda * conv[i + BS];
        out[i] = mod_q_i64_raw(v);
    }
}
#endif

/*
 * Independent C/NEON implementation of the radix-2 + 2xTMVP8 idea used by
 * CRT-with-a-monomial polynomial multiplication.
 *
 * Required from the including translation unit: q and
 * vmod_q_u32_barrett().
 * Inputs and outputs use canonical representatives in [0,q).
 */

static inline int16x8_t w16_reduce_s32_pair(int32x4_t lo, int32x4_t hi) {
    uint32x4_t rlo = vmod_q_u32_barrett(vreinterpretq_u32_s32(lo));
    uint32x4_t rhi = vmod_q_u32_barrett(vreinterpretq_u32_s32(hi));
    return vcombine_s16(vmovn_s32(vreinterpretq_s32_u32(rlo)),
                        vmovn_s32(vreinterpretq_s32_u32(rhi)));
}

static inline int16x8_t w16_mul_const(int16x8_t x, int c) {
    int32x4_t lo = vmull_n_s16(vget_low_s16(x), (int16_t)c);
    int32x4_t hi = vmull_high_n_s16(x, (int16_t)c);
    return w16_reduce_s32_pair(lo, hi);
}

static inline int16x8_t w16_add_mod(int16x8_t a, int16x8_t b) {
    int16x8_t r = vaddq_s16(a, b);
    int16x8_t qv = vdupq_n_s16((int16_t)q);
    int16x8_t mask = vreinterpretq_s16_u16(vcgeq_s16(r, qv));
    return vsubq_s16(r, vandq_s16(mask, qv));
}

static inline int16x8_t w16_sub_mod(int16x8_t a, int16x8_t b) {
    int16x8_t r = vsubq_s16(a, b);
    int16x8_t qv = vdupq_n_s16((int16_t)q);
    int16x8_t mask = vreinterpretq_s16_u16(vcltq_s16(r, vdupq_n_s16(0)));
    return vaddq_s16(r, vandq_s16(mask, qv));
}

/* Accumulate T(upper,col)*vec. col is the first matrix column, while
 * upper[j] supplies the wrapped upper diagonal for displacement j. */
static inline void w16_tmvp8_mla(int16x8_t upper, int16x8_t col, int16x8_t vec,
                                 int32x4_t *acc_lo, int32x4_t *acc_hi) {
#define W16_MLA_COLUMN(LANE, COLUMN) do {                                      \
        *acc_lo = vmlal_laneq_s16(*acc_lo, vget_low_s16(COLUMN), vec, LANE);   \
        *acc_hi = vmlal_high_laneq_s16(*acc_hi, COLUMN, vec, LANE);            \
    } while (0)
    W16_MLA_COLUMN(0, col);
    W16_MLA_COLUMN(1, vextq_s16(upper, col, 7));
    W16_MLA_COLUMN(2, vextq_s16(upper, col, 6));
    W16_MLA_COLUMN(3, vextq_s16(upper, col, 5));
    W16_MLA_COLUMN(4, vextq_s16(upper, col, 4));
    W16_MLA_COLUMN(5, vextq_s16(upper, col, 3));
    W16_MLA_COLUMN(6, vextq_s16(upper, col, 2));
    W16_MLA_COLUMN(7, vextq_s16(upper, col, 1));
#undef W16_MLA_COLUMN
}

/* Product in F_q[x]/(x^8-weight), expressed as one 8-by-8 TMVP. */
static inline int16x8_t w16_weighted_tmvp8(int16x8_t a, int16x8_t b,
                                           int weight) {
    int16x8_t weighted_b = w16_mul_const(b, weight);
    int32x4_t acc_lo = vdupq_n_s32(0);
    int32x4_t acc_hi = vdupq_n_s32(0);

    /* weighted_b[1..7] stores the upper diagonals in reverse-use order:
     * column j begins with weight*b[8-j], then b[0],b[1],... . */
    w16_tmvp8_mla(weighted_b, b, a, &acc_lo, &acc_hi);
    return w16_reduce_s32_pair(acc_lo, acc_hi);
}

/*
 * Let A=A0+x^8 A1 and B=B0+x^8 B1 in F_q[x]/(x^16-lambda), and let
 * s^2=lambda.  Evaluation at x^8=+s and x^8=-s gives two length-8
 * products.  The inverse radix-2 step recovers the low/high halves.
 */
static inline void weighted_mul16_2ntt_tmvp8_neon(
        int out[16], const int a[16], const int b[16],
        int sqrt_lambda, int inv2, int inv2_sqrt_lambda) {
    int16x8_t a0, a1, b0, b1;
    int16x8_t sa1, sb1, ap, am, bp, bm, hp, hm, h0, h1;

    /* Narrow two adjacent int32 vectors into each eight-coefficient half. */
    a0 = vcombine_s16(vmovn_s32(vld1q_s32(a)), vmovn_s32(vld1q_s32(a + 4)));
    a1 = vcombine_s16(vmovn_s32(vld1q_s32(a + 8)), vmovn_s32(vld1q_s32(a + 12)));
    b0 = vcombine_s16(vmovn_s32(vld1q_s32(b)), vmovn_s32(vld1q_s32(b + 4)));
    b1 = vcombine_s16(vmovn_s32(vld1q_s32(b + 8)), vmovn_s32(vld1q_s32(b + 12)));

    sa1 = w16_mul_const(a1, sqrt_lambda);
    sb1 = w16_mul_const(b1, sqrt_lambda);
    ap = w16_add_mod(a0, sa1);
    am = w16_sub_mod(a0, sa1);
    bp = w16_add_mod(b0, sb1);
    bm = w16_sub_mod(b0, sb1);

    hp = w16_weighted_tmvp8(ap, bp, sqrt_lambda);
    hm = w16_weighted_tmvp8(am, bm, q - sqrt_lambda);

    h0 = w16_mul_const(w16_add_mod(hp, hm), inv2);
    h1 = w16_mul_const(w16_sub_mod(hp, hm), inv2_sqrt_lambda);
    vst1q_s32(out, vmovl_s16(vget_low_s16(h0)));
    vst1q_s32(out + 4, vmovl_s16(vget_high_s16(h0)));
    vst1q_s32(out + 8, vmovl_s16(vget_low_s16(h1)));
    vst1q_s32(out + 12, vmovl_s16(vget_high_s16(h1)));
}

/* The production weighted16 path is fixed: radix-2 + two NEON TMVP8s. */
static void weighted_mul16(int out[BS], const int a[BS], const int b[BS], int weight_index) {
    weighted_mul16_2ntt_tmvp8_neon(out, a, b,
                                   SQRT_LAMBDA77[weight_index], INV2,
                                   INV2_SQRT_LAMBDA77[weight_index]);
}

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

int PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(unsigned char *outbytes, const unsigned char *inbytes, const unsigned char *kbytes) {
    Fq f[p], out[p];
    small g[p];
    Blocks77 Ablk SNTRUP_ALIGN64;
    Blocks77 Bblk SNTRUP_ALIGN64;
#if !SNTRUP653_USE_INPLACE_BUFFERS
    Blocks77 Ahat SNTRUP_ALIGN64;
    Blocks77 Bhat SNTRUP_ALIGN64;
    Blocks77 Hhat SNTRUP_ALIGN64;
    Blocks77 Cblk SNTRUP_ALIGN64;
#endif
    int clow[LOW_N];
    int i, m;

    init_tables();
    crypto_decode_pxint16(f, inbytes);
    for (i = 0; i < p; ++i) f[i] = Fq_freeze(f[i]);
    for (i = 0; i < p; ++i) {
        small gi = (small)kbytes[i];
        small gi0 = gi & 1;
        g[i] = (small)(gi0 - (gi & (gi0 << 1)));
    }

    blockify_A(Ablk, f);
    blockify_B(Bblk, g);
#if SNTRUP653_USE_INPLACE_BUFFERS
    good_thomas_77_ntt_zs_neon(Ablk, Ablk, 0, SNTRUP653_PUBLIC_ZS_MODE);
    good_thomas_77_ntt_zs_neon(Bblk, Bblk, 0, SNTRUP653_PUBLIC_ZS_MODE);
    for (m = 0; m < BLOCKS; m++) weighted_mul16(Ablk[m], Ablk[m], Bblk[m], m);
    good_thomas_77_ntt_zs_neon(Ablk, Ablk, 1, 0);
#else
    good_thomas_77_ntt_zs_neon(Ahat, Ablk, 0, SNTRUP653_PUBLIC_ZS_MODE);
    good_thomas_77_ntt_zs_neon(Bhat, Bblk, 0, SNTRUP653_PUBLIC_ZS_MODE);
    for (m = 0; m < BLOCKS; m++) weighted_mul16(Hhat[m], Ahat[m], Bhat[m], m);
    good_thomas_77_ntt_zs_neon(Cblk, Hhat, 1, 0);
#endif

    low_part73_neon(clow, f, g);
#if SNTRUP653_USE_INPLACE_BUFFERS
    final_folding_neon(out, Ablk, clow);
#else
    final_folding_neon(out, Cblk, clow);
#endif

    crypto_encode_pxint16(outbytes, out);
    return 0;
}
