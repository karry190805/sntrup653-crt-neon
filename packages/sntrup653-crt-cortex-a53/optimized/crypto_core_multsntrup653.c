#include "crypto_core_multsntrup653.h"

/* ---------------------------------------------------------------------------
 * Diagnostic stage marks.
 *
 * SNTRUP653_STAGE_PROFILE is defined only for the dedicated profiling binary
 * (build_stage_profile.sh).  Each mark appends one cntvct_el0 timestamp to a
 * shared buffer; the driver converts the sequence into per-stage durations.
 * That is a single store per mark, so it neither calls out of the translation
 * unit nor forces NEON register spills.  Every delivered build expands
 * SNTRUP_STAGE_MARK to a no-op, so this instrumentation cannot change code
 * generation.
 * ------------------------------------------------------------------------- */
#if defined(SNTRUP653_STAGE_PROFILE)
#include "stage_profile_ids.h"
static inline void sntrup_stage_mark_inline(int id) {
    unsigned long long t;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(t));
    sntrup653_stage_ts[sntrup653_stage_idx] = t;
    sntrup653_stage_ids[sntrup653_stage_idx] = (unsigned char)id;
    sntrup653_stage_idx++;
}
#define SNTRUP_STAGE_MARK(NAME) sntrup_stage_mark_inline(SNTRUP_STAGE_##NAME)
#else
#define SNTRUP_STAGE_MARK(NAME) ((void)0)
#endif
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
/* The dense first-layer kernels are kept at O3 in sanitizer builds, where the
 * translation unit is otherwise compiled at -O1.  The 11-point inner step is
 * written out with SNTRUP_DFT11_STEP16 below, so the lane indices are literal
 * constants for every compiler and every optimisation level. */
#if defined(__GNUC__) && !defined(__clang__)
#define SNTRUP_UNROLL_ATTR __attribute__((optimize("O3", "unroll-loops")))
#else
#define SNTRUP_UNROLL_ATTR
#endif
#define SNTRUP_ALIGN64 __attribute__((aligned(64)))
#define SNTRUP_MAYBE_UNUSED __attribute__((unused))
#else
#define SNTRUP_ALIGN64
#define SNTRUP_MAYBE_UNUSED
#endif

/* One j of the 11-point first-layer DFT, with a literal lane number.
 *
 * vmlal_laneq_s16() requires its lane index to be a compile-time constant.
 * GCC's unroll pragma supplies one at -O3 but not at -O0, and Clang does not
 * unroll at -O0 at all; both then reject j-1.  Expanding the five steps through
 * this macro keeps the generated lane numbers identical to the fully unrolled
 * loop while making the source valid for every compiler and optimisation
 * level, including clang -O0 and gcc -O0.
 *
 * The macro reads c[], qv, elo, ehi, olo and ohi from the enclosing scope. */
#define SNTRUP_DFT11_STEP16(PV, MV, J) do {                                  \
    int16x8_t sj_ = vaddq_s16(c[(J)], c[11 - (J)]);                         \
    uint16x8_t raw_ = vreinterpretq_u16_s16(                                \
        vsubq_s16(c[(J)], c[11 - (J)]));                                    \
    int16x8_t dj_ = vreinterpretq_s16_u16(                                  \
        vminq_u16(raw_, vaddq_u16(raw_, vreinterpretq_u16_s16(qv))));       \
    elo = vmlal_laneq_s16(elo, vget_low_s16(sj_), (PV), (J) - 1);           \
    ehi = vmlal_high_laneq_s16(ehi, sj_, (PV), (J) - 1);                    \
    olo = vmlal_laneq_s16(olo, vget_low_s16(dj_), (MV), (J) - 1);           \
    ohi = vmlal_high_laneq_s16(ohi, dj_, (MV), (J) - 1);                    \
} while (0)

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
    LOW_NP = 80,
    BLOCKS = 77,
    BS = 16,
    PAIRWISE_QOFF_MULT = 30000,
    MAX_ACTIVE_COLS = 6,
    ACTIVE_BLOCKS = (p + BS - 1) / BS,
    LAST_ACTIVE_BLOCK = ACTIVE_BLOCKS - 1,
    LAST_ACTIVE_USED = p - LAST_ACTIVE_BLOCK * BS
};

_Static_assert(INT32_MAX > 533300000, "pairwise NTT accumulator requires int32");
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
static int INV2_INV77 = 0;
static int INV2_SQRT_LAMBDA_INV77[BLOCKS];
static uint16_t SQRT_LAMBDA_SHOUP[BLOCKS];
static uint16_t QMSQRT_LAMBDA_SHOUP[BLOCKS];
static uint16_t INV2_SHOUP = 0;
static uint16_t INV2_INV77_SHOUP = 0;
static uint16_t INV2_SQRT_SHOUP[BLOCKS];
static uint16_t INV2_SQRT_INV77_SHOUP[BLOCKS];
/* Pre-broadcast Shoup constants.  Every 16-bit Shoup multiply needs its two
 * multipliers replicated over eight lanes; doing that with ldr + fmov + vdup
 * costs three issue slots per use, while loading an eight-lane constant from
 * this table costs one 16-byte load.  Entry layout per block:
 *   0 sqrt(lambda), 1 its Shoup multiplier, 2 Shoup(q-sqrt(lambda)),
 *   3 INV2*INV77*sqrt(lambda), 4 its Shoup multiplier,
 *   5 INV2*sqrt(lambda), 6 its Shoup multiplier.
 * W16_INV2 / W16_INV2_INV77 hold the two block-independent constants. */
enum { W16C_S = 0, W16C_SP, W16C_QSP, W16C_I2S77, W16C_I2SP77,
       W16C_I2S, W16C_I2SP, W16C_N };
static int16_t W16C[BLOCKS][W16C_N * 8] SNTRUP_ALIGN64;
static int16_t W16_INV2[8] SNTRUP_ALIGN64;
static int16_t W16_INV2_SHOUP[8] SNTRUP_ALIGN64;
static int16_t W16_INV2_INV77[8] SNTRUP_ALIGN64;
static int16_t W16_INV2_INV77_SHOUP[8] SNTRUP_ALIGN64;

/* Shoup multiplier for the 16-bit VQRDMULH path: floor(c * 2^15 / q).
 * With qhat = round(x * c' / 2^15), 0 <= x < q, one gets
 * qhat in {floor(x*c/q), floor(x*c/q)+1}, hence x*c - qhat*q is in (-q,q)
 * and one unsigned min canonicalises it. */
static uint16_t shoup16(int c) {
    return (uint16_t)(((uint32_t)c << 15) / (uint32_t)q);
}
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
/* 2q * sum_j M[k][j]: the constant that replaces the per-D "+2q" bias of
 * the 7-point kernel.  Summing M[k][j]*(a_j-b_j) on top of this initial value
 * gives exactly sum_j M[k][j]*(a_j-b_j+2q), but the bias costs one broadcast
 * per k instead of two ALU per D lane. */
static int PAIR7_ODDINIT_F[4];
static int PAIR7_ODDINIT_I[4];
static int PAIR7_P_F[4][4];
static int PAIR7_M_F[4][4];
static int PAIR7_P_I[4][4];
static int PAIR7_M_I[4][4];
static int PAIR11_P_F[6][6];
static int PAIR11_M_F[6][6];
static int PAIR11_P_I[6][6];
static int PAIR11_M_I[6][6];
/* Packed pairwise-DFT constants: one vector holds the three (or four)
 * coefficients of a k row, so one 16-byte load replaces three or four scalar
 * table loads.  MLA by element then indexes the lane directly. */
/* 16-bit pair tables.  One eight-lane vector holds the five (or three) j
 * coefficients of one k row, so a whole row of the kernel costs one load. */
static int16_t P11Q_F[6][8], M11Q_F[6][8], P11Q_I[6][8], M11Q_I[6][8];
static int16_t P7Q_F[4][8], M7Q_F[4][8], P7Q_I[4][8], M7Q_I[4][8];
static int32x4_t P7V_F[3], M7V_F[3], P7V_I[3], M7V_I[3];

static int GT_IDX_RS[11][7];
static int GT_K_UV[7][11];
static int ACTIVE_COLS_PER_R[7];
static int ACTIVE_COL_LIST[7][11];
static int ACTIVE_PAIR_MASK11[7][6];

#ifndef USE_BARRETT_REDUCE
#define USE_BARRETT_REDUCE 1
#endif

/* Opt-in 16-bit transform data path.  It is only valid for the delivered
 * configuration (in-place buffers, build-time public-zero pruning, NEON
 * low-part and folding, Barrett reduction), because it reuses those
 * preconditions; every other variant keeps the validated 32-bit path. */
#ifndef SNTRUP653_FAST16
#if SNTRUP653_USE_INPLACE_BUFFERS == 1 && SNTRUP653_PUBLIC_ZS_MODE == 1 && \
    SNTRUP653_PUBLIC_ZS_RUNTIME_MASK == 0 && SNTRUP653_LOW73_NEON == 1 && \
    SNTRUP653_FINAL_FOLD_NEON == 1 && USE_BARRETT_REDUCE == 1
#define SNTRUP653_FAST16 1
#else
#define SNTRUP653_FAST16 0
#endif
#endif

#if SNTRUP653_FAST16 && SNTRUP653_USE_INPLACE_BUFFERS == 0
#error "SNTRUP653_FAST16 requires in-place buffers; no_inplace is not a FAST16 arm"
#endif
#if SNTRUP653_FAST16 && SNTRUP653_PUBLIC_ZS_MODE == 0
#error "SNTRUP653_FAST16 is not range-safe for dense transform; use FAST16=0 for no_zs"
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
static const int32_t  BARRETT_MU_Q31 = 464722;              /* floor(2^31 / 4621) */

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

/* Reduction for the low-73 accumulator, whose absolute value is below
 * LOW_N*(q-1) < 2^19.  A 32-bit constant-divisor reduction is much cheaper
 * than the 64-bit Barrett path. */
static inline int mod_q_i32_small(int32_t x) {
    int32_t r = x % q;
    return r < 0 ? r + q : r;
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
    for (k = 0; k <= 3; k++) {
        int sp = 0, si = 0;
        if (k >= 1) {
            sp = PAIR7_M_F[k][1] + PAIR7_M_F[k][2] + PAIR7_M_F[k][3];
            si = PAIR7_M_I[k][1] + PAIR7_M_I[k][2] + PAIR7_M_I[k][3];
        }
        PAIR7_ODDINIT_F[k] = 2 * q * sp;
        PAIR7_ODDINIT_I[k] = 2 * q * si;
    }
    {
        int k, t[4];
        for (k = 1; k <= 3; k++) {
            t[0] = PAIR7_P_F[k][1]; t[1] = PAIR7_P_F[k][2];
            t[2] = PAIR7_P_F[k][3]; t[3] = 0;
            P7V_F[k - 1] = vld1q_s32(t);
            t[0] = PAIR7_M_F[k][1]; t[1] = PAIR7_M_F[k][2];
            t[2] = PAIR7_M_F[k][3]; t[3] = 0;
            M7V_F[k - 1] = vld1q_s32(t);
            t[0] = PAIR7_P_I[k][1]; t[1] = PAIR7_P_I[k][2];
            t[2] = PAIR7_P_I[k][3]; t[3] = 0;
            P7V_I[k - 1] = vld1q_s32(t);
            t[0] = PAIR7_M_I[k][1]; t[1] = PAIR7_M_I[k][2];
            t[2] = PAIR7_M_I[k][3]; t[3] = 0;
            M7V_I[k - 1] = vld1q_s32(t);
        }
    }
    init_pair_tables11(POW11_F, PAIR11_P_F, PAIR11_M_F);
    init_pair_tables11(POW11_I, PAIR11_P_I, PAIR11_M_I);
    for (k = 1; k <= 5; k++) {
        for (j = 1; j <= 5; j++) {
            P11Q_F[k][j - 1] = (int16_t)PAIR11_P_F[k][j];
            M11Q_F[k][j - 1] = (int16_t)PAIR11_M_F[k][j];
            P11Q_I[k][j - 1] = (int16_t)PAIR11_P_I[k][j];
            M11Q_I[k][j - 1] = (int16_t)PAIR11_M_I[k][j];
        }
    }
    for (k = 1; k <= 3; k++) {
        for (j = 1; j <= 3; j++) {
            P7Q_F[k][j - 1] = (int16_t)PAIR7_P_F[k][j];
            M7Q_F[k][j - 1] = (int16_t)PAIR7_M_F[k][j];
            P7Q_I[k][j - 1] = (int16_t)PAIR7_P_I[k][j];
            M7Q_I[k][j - 1] = (int16_t)PAIR7_M_I[k][j];
        }
    }
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
    /* The inverse GT ends with a multiplication by 1/77.  The pointwise
     * weighted product is linear and feeds the inverse directly, so that
     * constant can be folded into the two reconstruction multipliers of the
     * production weighted_mul16 instead of being applied to all 1232
     * coefficients in the inverse transform. */
    INV2_INV77 = mul_q_raw(INV2, INV77);
    for (k = 0; k < BLOCKS; k++) {
        INV2_SQRT_LAMBDA_INV77[k] =
            mul_q_raw(INV2_SQRT_LAMBDA77[k], INV77);
    }
    INV2_SHOUP = shoup16(INV2);
    INV2_INV77_SHOUP = shoup16(INV2_INV77);
    for (k = 0; k < BLOCKS; k++) {
        SQRT_LAMBDA_SHOUP[k] = shoup16(SQRT_LAMBDA77[k]);
        QMSQRT_LAMBDA_SHOUP[k] = shoup16(q - SQRT_LAMBDA77[k]);
        INV2_SQRT_SHOUP[k] = shoup16(INV2_SQRT_LAMBDA77[k]);
        INV2_SQRT_INV77_SHOUP[k] = shoup16(INV2_SQRT_LAMBDA_INV77[k]);
    }
    {
        int a, b;
        uint16_t v[W16C_N];
        for (a = 0; a < 8; a++) {
            W16_INV2[a] = (int16_t)INV2;
            W16_INV2_SHOUP[a] = (int16_t)INV2_SHOUP;
            W16_INV2_INV77[a] = (int16_t)INV2_INV77;
            W16_INV2_INV77_SHOUP[a] = (int16_t)INV2_INV77_SHOUP;
        }
        for (k = 0; k < BLOCKS; k++) {
            v[W16C_S] = (uint16_t)SQRT_LAMBDA77[k];
            v[W16C_SP] = SQRT_LAMBDA_SHOUP[k];
            v[W16C_QSP] = QMSQRT_LAMBDA_SHOUP[k];
            v[W16C_I2S77] = (uint16_t)INV2_SQRT_LAMBDA_INV77[k];
            v[W16C_I2SP77] = INV2_SQRT_INV77_SHOUP[k];
            v[W16C_I2S] = (uint16_t)INV2_SQRT_LAMBDA77[k];
            v[W16C_I2SP] = INV2_SQRT_SHOUP[k];
            for (b = 0; b < W16C_N; b++) {
                int a2;
                for (a2 = 0; a2 < 8; a2++) {
                    W16C[k][b * 8 + a2] = (int16_t)v[b];
                }
            }
        }
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
    /* Every caller has 0 <= x < 2^31, so x is a positive signed 32-bit
     * value.  With M = floor(2^31/q), SQDMULH(x, M) computes
     * floor(2*x*M/2^32) = floor(x*M/2^31), which is <= floor(x/q) because
     * M <= 2^31/q, and at most one below it because x < 2^31.  Hence
     * r = x - qhat*q is in [0,2q) and one min(r, r-q) canonicalises it. */
    const uint32x4_t qvec = vdupq_n_u32((uint32_t)q);
    int32x4_t qhat = vqdmulhq_s32(vreinterpretq_s32_u32(x),
                                  vdupq_n_s32(BARRETT_MU_Q31));
    uint32x4_t r = vmlsq_n_u32(x, vreinterpretq_u32_s32(qhat),
                               (uint32_t)q);
    return vminq_u32(r, vsubq_u32(r, qvec));
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

/* Precondition: all lanes in x are non-negative DFT accumulators.
 * Lazy variant: only divide out q, leaving the representative in [0,2q).
 * The next transform layer reduces modulo q anyway, so the final
 * canonicalising min can be deferred. */
static inline void store_mod_q2_s32x4(int *dst, int32x4_t x) {
#if USE_BARRETT_REDUCE
    uint32x4_t xu = vreinterpretq_u32_s32(x);
    int32x4_t qhat = vqdmulhq_s32(vreinterpretq_s32_u32(xu),
                                  vdupq_n_s32(BARRETT_MU_Q31));
    uint32x4_t r = vmlsq_n_u32(xu, vreinterpretq_u32_s32(qhat),
                               (uint32_t)q);
    vst1q_s32(dst, vreinterpretq_s32_u32(r));
#else
    /* Canonical [0,q) is also a valid [0,2q) representative. */
    vst1q_s32(dst, vreinterpretq_s32_u32(
                       vmod_q_u32_barrett(vreinterpretq_u32_s32(x))));
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
/* Same trick with the wider range produced by the lazy first layer:
 * x = a - b with a,b in [0,2q), so x in (-2q,2q) and adding 2q canonicalises. */
static inline int32x4_t add_q2_if_negative_s32x4(int32x4_t x) {
    uint32x4_t u = vreinterpretq_u32_s32(x);
    uint32x4_t qv = vdupq_n_u32((uint32_t)(2 * q));
    return vreinterpretq_s32_u32(vminq_u32(u, vaddq_u32(u, qv)));
}

static inline int32x4_t add_q_if_negative_s32x4(int32x4_t x) {
    /* x is a difference of two values in [0,q), so x is in (-q,q).  Viewed
     * as unsigned, a negative lane wraps near 2^32; x+q then wraps back to
     * x+q, which is smaller, while for x>=0 the wrapped x+q is larger than
     * x.  min(x, x+q) therefore performs the conditional add in two
     * instructions. */
    uint32x4_t u = vreinterpretq_u32_s32(x);
    uint32x4_t qv = vdupq_n_u32((uint32_t)q);
    return vreinterpretq_s32_u32(vminq_u32(u, vaddq_u32(u, qv)));
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
                                       int32x4_t p0, int32x4_t p1, int32x4_t p2,
                                       int32x4_t m0, int32x4_t m1, int32x4_t m2,
                                       int32x4_t o0, int32x4_t o1, int32x4_t o2) {
    const int32x4_t qoff = vdupq_n_s32(q * (2 * PAIRWISE_QOFF_MULT));
    int32x4_t S1 = vaddq_s32(inv[1], inv[6]);
    int32x4_t S2 = vaddq_s32(inv[2], inv[5]);
    int32x4_t S3 = vaddq_s32(inv[3], inv[4]);
    int32x4_t D1 = vsubq_s32(inv[1], inv[6]);
    int32x4_t D2 = vsubq_s32(inv[2], inv[5]);
    int32x4_t D3 = vsubq_s32(inv[3], inv[4]);
    int32x4_t even, odd;

    outv[0] = vaddq_s32(vaddq_s32(vaddq_s32(inv[0], S1), S2), S3);

    even = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(inv[0], S1, p0, 0),
                                           S2, p0, 1), S3, p0, 2);
    odd  = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(o0, D1, m0, 0),
                                           D2, m0, 1), D3, m0, 2);
    outv[1] = vaddq_s32(even, odd);
    outv[6] = vaddq_s32(vsubq_s32(even, odd), qoff);

    even = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(inv[0], S1, p1, 0),
                                           S2, p1, 1), S3, p1, 2);
    odd  = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(o1, D1, m1, 0),
                                           D2, m1, 1), D3, m1, 2);
    outv[2] = vaddq_s32(even, odd);
    outv[5] = vaddq_s32(vsubq_s32(even, odd), qoff);

    even = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(inv[0], S1, p2, 0),
                                           S2, p2, 1), S3, p2, 2);
    odd  = vmlaq_laneq_s32(vmlaq_laneq_s32(vmlaq_laneq_s32(o2, D1, m2, 0),
                                           D2, m2, 1), D3, m2, 2);
    outv[3] = vaddq_s32(even, odd);
    outv[4] = vaddq_s32(vsubq_s32(even, odd), qoff);
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
 *     Y_k = even_k + odd_k ,   Y_{11-k} = even_k - odd_k + q*30000
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
        store_mod_q2_s32x4(&tmp[RR][KV][cbase], vaddq_s32(even_, odd_));        \
        store_mod_q2_s32x4(&tmp[RR][11 - (KV)][cbase],                          \
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
            store_mod_q2_s32x4(&tmp[RR][0][cbase], acc0_);                     \
            SNTRUP_L1_K(RR, 1); SNTRUP_L1_K(RR, 2); SNTRUP_L1_K(RR, 3);       \
            SNTRUP_L1_K(RR, 4); SNTRUP_L1_K(RR, 5);                           \
        }                                                                     \
    } while (0)
#endif

#if SNTRUP653_FAST16
/* ---------------------------------------------------------------------------
 * 16-bit forward Good--Thomas transform.
 *
 * Every value the forward half touches is a non-negative representative in
 * [0, 2q) = [0, 9242), so it fits in a signed 16-bit lane.  Processing eight
 * lanes per vector (instead of four) halves the number of table-constant
 * loads, block loads and partial-sum computations, while the products are
 * still accumulated in 32 bits by vmlal so the arithmetic is bit-identical to
 * the 32-bit kernel: the same operands, the same constants and the same
 * Barrett step produce the same int32 accumulator, which is then narrowed.
 *
 * Bounds (inputs < q, first-layer outputs lazy < 2q):
 *   S_j = a_j + a_{11-j}            < 2q
 *   D_j = a_j - a_{11-j} + q        in [0, q)   (two-instruction min trick)
 *   even_k = a_0 + sum P*S          < 6*q*2q = 2.56e8
 *   odd_k  = sum M*D                < 5*q*q  = 1.07e8
 *   Y_k = even+odd, Y_{11-k} = even-odd+q*30000, both in [0, 2^31).
 * ------------------------------------------------------------------------- */
static inline void store_lazy_s16(int16_t *dst, int32x4_t lo, int32x4_t hi) {
#if USE_BARRETT_REDUCE
    const int32x4_t mu = vdupq_n_s32(BARRETT_MU_Q31);
    uint32x4_t rlo = vmlsq_n_u32(vreinterpretq_u32_s32(lo),
                                 vreinterpretq_u32_s32(vqdmulhq_s32(lo, mu)),
                                 (uint32_t)q);
    uint32x4_t rhi = vmlsq_n_u32(vreinterpretq_u32_s32(hi),
                                 vreinterpretq_u32_s32(vqdmulhq_s32(hi, mu)),
                                 (uint32_t)q);
#else
    uint32x4_t rlo = vmod_q_u32_barrett(vreinterpretq_u32_s32(lo));
    uint32x4_t rhi = vmod_q_u32_barrett(vreinterpretq_u32_s32(hi));
#endif
    vst1q_s16(dst, vcombine_s16(vmovn_s32(vreinterpretq_s32_u32(rlo)),
                                vmovn_s32(vreinterpretq_s32_u32(rhi))));
}

/* acc += x * c[lane], with c a table row replicated over eight lanes. */
#define SNTRUP_ACC16(ELO, EHI, OLO, OHI, SJ, DJ, PV, MV, LANE)                \
    do {                                                                      \
        ELO = vmlal_laneq_s16(ELO, vget_low_s16(SJ), PV, LANE);               \
        EHI = vmlal_high_laneq_s16(EHI, SJ, PV, LANE);                        \
        OLO = vmlal_laneq_s16(OLO, vget_low_s16(DJ), MV, LANE);               \
        OHI = vmlal_high_laneq_s16(OHI, DJ, MV, LANE);                        \
    } while (0)

#define SNTRUP_L1_LOAD16(RR, SS, VAR)                                         \
    int16x8_t SNTRUP_MAYBE_UNUSED VAR = SNTRUP_GT_ACT(RR, SS)                 \
        ? vld1q_s16(&in[SNTRUP_GT_IDX(RR, SS)][cbase])                        \
        : vdupq_n_s16(0);

#define SNTRUP_L1_DC16(RR, SS, VAR)                                           \
    if (SNTRUP_GT_ACT(RR, SS)) {                                              \
        acc0lo_ = vaddq_s32(acc0lo_, vmovl_s16(vget_low_s16(VAR)));            \
        acc0hi_ = vaddq_s32(acc0hi_, vmovl_high_s16(VAR));                     \
    }

/* One (j, 11-j) pair contribution to one output pair. */
#define SNTRUP_L1_PAIR16(RR, JJ, AJ, AM, PV, MV)                              \
    do {                                                                      \
        if (SNTRUP_GT_ACT(RR, JJ)) {                                          \
            if (SNTRUP_GT_ACT(RR, 11 - (JJ))) {                               \
                int16x8_t sj_ = vaddq_s16(AJ, AM);                            \
                int16x8_t dj_ = vreinterpretq_s16_u16(vminq_u16(              \
                    vreinterpretq_u16_s16(vsubq_s16(AJ, AM)),                 \
                    vaddq_u16(vreinterpretq_u16_s16(vsubq_s16(AJ, AM)),       \
                              vreinterpretq_u16_s16(qv_))));                  \
                SNTRUP_ACC16(elo_, ehi_, olo_, ohi_, sj_, dj_, PV, MV,        \
                             (JJ) - 1);                                       \
            } else {                                                          \
                SNTRUP_ACC16(elo_, ehi_, olo_, ohi_, AJ, AJ, PV, MV,          \
                             (JJ) - 1);                                       \
            }                                                                 \
        } else if (SNTRUP_GT_ACT(RR, 11 - (JJ))) {                            \
            SNTRUP_ACC16(elo_, ehi_, olo_, ohi_, AM, vsubq_s16(qv_, AM),      \
                         PV, MV, (JJ) - 1);                                   \
        }                                                                     \
    } while (0)

#define SNTRUP_L1_K16(RR, KV, PV, MV)                                         \
    do {                                                                      \
        int32x4_t elo_, ehi_, olo_, ohi_;                                     \
        elo_ = vmovl_s16(vget_low_s16(c0));                                   \
        ehi_ = vmovl_high_s16(c0);                                            \
        olo_ = vdupq_n_s32(0);                                                \
        ohi_ = vdupq_n_s32(0);                                                \
        SNTRUP_L1_PAIR16(RR, 1, p1, m1, PV, MV);                              \
        SNTRUP_L1_PAIR16(RR, 2, p2, m2, PV, MV);                              \
        SNTRUP_L1_PAIR16(RR, 3, p3, m3, PV, MV);                              \
        SNTRUP_L1_PAIR16(RR, 4, p4, m4, PV, MV);                              \
        SNTRUP_L1_PAIR16(RR, 5, p5, m5, PV, MV);                              \
        store_lazy_s16(&tmp[RR][KV][cbase], vaddq_s32(elo_, olo_),            \
                       vaddq_s32(ehi_, ohi_));                                \
        store_lazy_s16(&tmp[RR][11 - (KV)][cbase],                            \
                       vaddq_s32(vsubq_s32(elo_, olo_), qoff_),               \
                       vaddq_s32(vsubq_s32(ehi_, ohi_), qoff_));              \
    } while (0)

#define SNTRUP_L1_ROW16(RR)                                                   \
    do {                                                                      \
        for (cbase = 0; cbase < BS; cbase += 8) {                             \
            int32x4_t acc0lo_, acc0hi_;                                       \
            const int16x8_t qv_ = vdupq_n_s16((int16_t)q);                    \
            const int32x4_t qoff_ = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);      \
            SNTRUP_L1_LOAD16(RR, 0, c0)                                       \
            SNTRUP_L1_LOAD16(RR, 1, p1)  SNTRUP_L1_LOAD16(RR, 10, m1)         \
            SNTRUP_L1_LOAD16(RR, 2, p2)  SNTRUP_L1_LOAD16(RR, 9, m2)          \
            SNTRUP_L1_LOAD16(RR, 3, p3)  SNTRUP_L1_LOAD16(RR, 8, m3)          \
            SNTRUP_L1_LOAD16(RR, 4, p4)  SNTRUP_L1_LOAD16(RR, 7, m4)          \
            SNTRUP_L1_LOAD16(RR, 5, p5)  SNTRUP_L1_LOAD16(RR, 6, m5)          \
            acc0lo_ = vdupq_n_s32(0);                                         \
            acc0hi_ = vdupq_n_s32(0);                                         \
            SNTRUP_L1_DC16(RR, 0, c0)                                         \
            SNTRUP_L1_DC16(RR, 1, p1)   SNTRUP_L1_DC16(RR, 2, p2)             \
            SNTRUP_L1_DC16(RR, 3, p3)   SNTRUP_L1_DC16(RR, 4, p4)             \
            SNTRUP_L1_DC16(RR, 5, p5)   SNTRUP_L1_DC16(RR, 6, m5)             \
            SNTRUP_L1_DC16(RR, 7, m4)   SNTRUP_L1_DC16(RR, 8, m3)             \
            SNTRUP_L1_DC16(RR, 9, m2)   SNTRUP_L1_DC16(RR, 10, m1)            \
            store_lazy_s16(&tmp[RR][0][cbase], acc0lo_, acc0hi_);             \
            {                                                                 \
                const int16x8_t pv1 = vld1q_s16(P11Q_F[1]);                   \
                const int16x8_t mv1 = vld1q_s16(M11Q_F[1]);                   \
                SNTRUP_L1_K16(RR, 1, pv1, mv1);                               \
            }                                                                 \
            {                                                                 \
                const int16x8_t pv2 = vld1q_s16(P11Q_F[2]);                   \
                const int16x8_t mv2 = vld1q_s16(M11Q_F[2]);                   \
                SNTRUP_L1_K16(RR, 2, pv2, mv2);                               \
            }                                                                 \
            {                                                                 \
                const int16x8_t pv3 = vld1q_s16(P11Q_F[3]);                   \
                const int16x8_t mv3 = vld1q_s16(M11Q_F[3]);                   \
                SNTRUP_L1_K16(RR, 3, pv3, mv3);                               \
            }                                                                 \
            {                                                                 \
                const int16x8_t pv4 = vld1q_s16(P11Q_F[4]);                   \
                const int16x8_t mv4 = vld1q_s16(M11Q_F[4]);                   \
                SNTRUP_L1_K16(RR, 4, pv4, mv4);                               \
            }                                                                 \
            {                                                                 \
                const int16x8_t pv5 = vld1q_s16(P11Q_F[5]);                   \
                const int16x8_t mv5 = vld1q_s16(M11Q_F[5]);                   \
                SNTRUP_L1_K16(RR, 5, pv5, mv5);                               \
            }                                                                 \
        }                                                                     \
    } while (0)

/* Dense first layer: every column present (used by the dense self test). */
static SNTRUP_UNROLL_ATTR void gt_l1_dense16(Matrix16 tmp, const Blocks16 in) {
    const int16x8_t qv = vdupq_n_s16((int16_t)q);
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int r, cbase, k;
    for (r = 0; r < 7; r++) {
        for (cbase = 0; cbase < BS; cbase += 8) {
            int16x8_t c[11];
            int32x4_t acc0lo, acc0hi;
            for (k = 0; k < 11; k++) c[k] = vld1q_s16(&in[GT_IDX_RS[k][r]][cbase]);
            acc0lo = vmovl_s16(vget_low_s16(c[0]));
            acc0hi = vmovl_high_s16(c[0]);
            for (k = 1; k <= 5; k++) {
                acc0lo = vaddw_s16(acc0lo, vget_low_s16(c[k]));
                acc0lo = vaddw_s16(acc0lo, vget_low_s16(c[11 - k]));
                acc0hi = vaddw_s16(acc0hi, vget_high_s16(c[k]));
                acc0hi = vaddw_s16(acc0hi, vget_high_s16(c[11 - k]));
            }
            store_lazy_s16(&tmp[r][0][cbase], acc0lo, acc0hi);
            for (k = 1; k <= 5; k++) {
                const int16x8_t pv_ = vld1q_s16(P11Q_F[k]);
                const int16x8_t mv_ = vld1q_s16(M11Q_F[k]);
                int32x4_t elo = vmovl_s16(vget_low_s16(c[0]));
                int32x4_t ehi = vmovl_high_s16(c[0]);
                int32x4_t olo = vdupq_n_s32(0), ohi = vdupq_n_s32(0);
#if defined(__GNUC__) && !defined(__clang__)
                int j;
#pragma GCC unroll 5
                for (j = 1; j <= 5; j++) {
                    int16x8_t sj = vaddq_s16(c[j], c[11 - j]);
                    uint16x8_t raw = vreinterpretq_u16_s16(vsubq_s16(c[j], c[11 - j]));
                    int16x8_t dj = vreinterpretq_s16_u16(
                        vminq_u16(raw, vaddq_u16(raw, vreinterpretq_u16_s16(qv))));
                    elo = vmlal_laneq_s16(elo, vget_low_s16(sj), pv_, j - 1);
                    ehi = vmlal_high_laneq_s16(ehi, sj, pv_, j - 1);
                    olo = vmlal_laneq_s16(olo, vget_low_s16(dj), mv_, j - 1);
                    ohi = vmlal_high_laneq_s16(ohi, dj, mv_, j - 1);
                }
#else
                SNTRUP_DFT11_STEP16(pv_, mv_, 1);
                SNTRUP_DFT11_STEP16(pv_, mv_, 2);
                SNTRUP_DFT11_STEP16(pv_, mv_, 3);
                SNTRUP_DFT11_STEP16(pv_, mv_, 4);
                SNTRUP_DFT11_STEP16(pv_, mv_, 5);
#endif
                store_lazy_s16(&tmp[r][k][cbase], vaddq_s32(elo, olo),
                               vaddq_s32(ehi, ohi));
                store_lazy_s16(&tmp[r][11 - k][cbase],
                               vaddq_s32(vsubq_s32(elo, olo), qoff),
                               vaddq_s32(vsubq_s32(ehi, ohi), qoff));
            }
        }
    }
}

/* Second layer: 7-point DFTs over the 16-bit tmp array, int32 output. */
static void gt_layer2_fwd16(Blocks16 out, const Matrix16 tmp) {
    const int16x8_t q2 = vdupq_n_s16((int16_t)(2 * q));
    const int32x4_t qoff = vdupq_n_s32(q * (2 * PAIRWISE_QOFF_MULT));
    int v, cbase, k;
    for (v = 0; v < 11; v++) {
#pragma GCC unroll 2
        for (cbase = 0; cbase < BS; cbase += 8) {
            int16x8_t x0 = vld1q_s16(&tmp[0][v][cbase]);
            int16x8_t x1 = vld1q_s16(&tmp[1][v][cbase]);
            int16x8_t x2 = vld1q_s16(&tmp[2][v][cbase]);
            int16x8_t x3 = vld1q_s16(&tmp[3][v][cbase]);
            int16x8_t x4 = vld1q_s16(&tmp[4][v][cbase]);
            int16x8_t x5 = vld1q_s16(&tmp[5][v][cbase]);
            int16x8_t x6 = vld1q_s16(&tmp[6][v][cbase]);
            int16x8_t S1 = vaddq_s16(x1, x6);
            int16x8_t S2 = vaddq_s16(x2, x5);
            int16x8_t S3 = vaddq_s16(x3, x4);
            int16x8_t D1 = vaddq_s16(vsubq_s16(x1, x6), q2);
            int16x8_t D2 = vaddq_s16(vsubq_s16(x2, x5), q2);
            int16x8_t D3 = vaddq_s16(vsubq_s16(x3, x4), q2);
            int32x4_t dl, dh;
            int32x4_t el, eh, ol, oh;
            int32x4_t y0lo, y0hi;

            y0lo = vaddw_s16(vaddw_s16(vaddw_s16(vmovl_s16(vget_low_s16(x0)),
                                                vget_low_s16(S1)),
                                       vget_low_s16(S2)), vget_low_s16(S3));
            y0hi = vaddw_s16(vaddw_s16(vaddw_s16(vmovl_high_s16(x0),
                                                 vget_high_s16(S1)),
                                       vget_high_s16(S2)), vget_high_s16(S3));
            store_lazy_s16(&out[GT_K_UV[0][v]][cbase], y0lo, y0hi);
            for (k = 1; k <= 3; k++) {
                const int16x8_t pv = vld1q_s16(P7Q_F[k]);
                const int16x8_t mv = vld1q_s16(M7Q_F[k]);
                el = vmovl_s16(vget_low_s16(x0));
                eh = vmovl_high_s16(x0);
                ol = vdupq_n_s32(0);
                oh = vdupq_n_s32(0);
                SNTRUP_ACC16(el, eh, ol, oh, S1, D1, pv, mv, 0);
                SNTRUP_ACC16(el, eh, ol, oh, S2, D2, pv, mv, 1);
                SNTRUP_ACC16(el, eh, ol, oh, S3, D3, pv, mv, 2);
                dl = vaddq_s32(el, ol);
                dh = vaddq_s32(eh, oh);
                store_lazy_s16(&out[GT_K_UV[k][v]][cbase], dl, dh);
                dl = vaddq_s32(vsubq_s32(el, ol), qoff);
                dh = vaddq_s32(vsubq_s32(eh, oh), qoff);
                store_lazy_s16(&out[GT_K_UV[7 - k][v]][cbase], dl, dh);
            }
        }
    }
}

static void good_thomas_77_ntt_fwd16(Blocks16 out, const Blocks16 in,
                                     int public_zs_forward) {
    Matrix16 tmp;
    int cbase;
    SNTRUP_STAGE_MARK(fwd_dft11);
    if (public_zs_forward) {
        SNTRUP_L1_ROW16(0);
        SNTRUP_L1_ROW16(1);
        SNTRUP_L1_ROW16(2);
        SNTRUP_L1_ROW16(3);
        SNTRUP_L1_ROW16(4);
        SNTRUP_L1_ROW16(5);
        SNTRUP_L1_ROW16(6);
    } else {
        gt_l1_dense16(tmp, in);
    }
    SNTRUP_STAGE_MARK(fwd_dft7);
    gt_layer2_fwd16(out, tmp);
}

/* ---------------------------------------------------------------------------
 * 16-bit inverse Good--Thomas transform.
 *
 * The weighted product leaves canonical representatives in [0,q), so the
 * dense inverse first layer keeps the same int16 range as the forward path
 * (S < 2q, D in [0,q)) and stores its lazy [0,2q) output narrowed.  The
 * second layer accumulates in 32 bits and stores the same non-negative raw
 * accumulators as the 32-bit skip_final_reduce path, because final_folding_*
 * reduces the sums once at the end.
 * ------------------------------------------------------------------------- */
static SNTRUP_UNROLL_ATTR void gt_l1_dense_inv16(Matrix16 tmp, const Blocks16 in) {
    const int16x8_t qv = vdupq_n_s16((int16_t)q);
    const int32x4_t qoff = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);
    int r, cbase, k;
    for (r = 0; r < 7; r++) {
        for (cbase = 0; cbase < BS; cbase += 8) {
            int16x8_t c[11];
            int32x4_t acc0lo, acc0hi;
            for (k = 0; k < 11; k++) c[k] = vld1q_s16(&in[GT_IDX_RS[k][r]][cbase]);
            acc0lo = vmovl_s16(vget_low_s16(c[0]));
            acc0hi = vmovl_high_s16(c[0]);
            for (k = 1; k <= 5; k++) {
                acc0lo = vaddw_s16(acc0lo, vget_low_s16(c[k]));
                acc0lo = vaddw_s16(acc0lo, vget_low_s16(c[11 - k]));
                acc0hi = vaddw_s16(acc0hi, vget_high_s16(c[k]));
                acc0hi = vaddw_s16(acc0hi, vget_high_s16(c[11 - k]));
            }
            store_lazy_s16(&tmp[r][0][cbase], acc0lo, acc0hi);
            for (k = 1; k <= 5; k++) {
                const int16x8_t pv = vld1q_s16(P11Q_I[k]);
                const int16x8_t mv = vld1q_s16(M11Q_I[k]);
                int32x4_t elo = vmovl_s16(vget_low_s16(c[0]));
                int32x4_t ehi = vmovl_high_s16(c[0]);
                int32x4_t olo = vdupq_n_s32(0), ohi = vdupq_n_s32(0);
#if defined(__GNUC__) && !defined(__clang__)
                int j;
#pragma GCC unroll 5
                for (j = 1; j <= 5; j++) {
                    int16x8_t sj = vaddq_s16(c[j], c[11 - j]);
                    uint16x8_t raw = vreinterpretq_u16_s16(vsubq_s16(c[j], c[11 - j]));
                    int16x8_t dj = vreinterpretq_s16_u16(
                        vminq_u16(raw, vaddq_u16(raw, vreinterpretq_u16_s16(qv))));
                    elo = vmlal_laneq_s16(elo, vget_low_s16(sj), pv, j - 1);
                    ehi = vmlal_high_laneq_s16(ehi, sj, pv, j - 1);
                    olo = vmlal_laneq_s16(olo, vget_low_s16(dj), mv, j - 1);
                    ohi = vmlal_high_laneq_s16(ohi, dj, mv, j - 1);
                }
#else
                SNTRUP_DFT11_STEP16(pv, mv, 1);
                SNTRUP_DFT11_STEP16(pv, mv, 2);
                SNTRUP_DFT11_STEP16(pv, mv, 3);
                SNTRUP_DFT11_STEP16(pv, mv, 4);
                SNTRUP_DFT11_STEP16(pv, mv, 5);
#endif
                store_lazy_s16(&tmp[r][k][cbase], vaddq_s32(elo, olo),
                               vaddq_s32(ehi, ohi));
                store_lazy_s16(&tmp[r][11 - k][cbase],
                               vaddq_s32(vsubq_s32(elo, olo), qoff),
                               vaddq_s32(vsubq_s32(ehi, ohi), qoff));
            }
        }
    }
}

static void gt_layer2_inv16(Blocks77 out, const Matrix16 tmp) {
    const int16x8_t q2 = vdupq_n_s16((int16_t)(2 * q));
    const int32x4_t qoff = vdupq_n_s32(q * (2 * PAIRWISE_QOFF_MULT));
    int v, cbase, k;
    for (v = 0; v < 11; v++) {
#pragma GCC unroll 2
        for (cbase = 0; cbase < BS; cbase += 8) {
            int16x8_t x0 = vld1q_s16(&tmp[0][v][cbase]);
            int16x8_t x1 = vld1q_s16(&tmp[1][v][cbase]);
            int16x8_t x2 = vld1q_s16(&tmp[2][v][cbase]);
            int16x8_t x3 = vld1q_s16(&tmp[3][v][cbase]);
            int16x8_t x4 = vld1q_s16(&tmp[4][v][cbase]);
            int16x8_t x5 = vld1q_s16(&tmp[5][v][cbase]);
            int16x8_t x6 = vld1q_s16(&tmp[6][v][cbase]);
            int16x8_t S1 = vaddq_s16(x1, x6);
            int16x8_t S2 = vaddq_s16(x2, x5);
            int16x8_t S3 = vaddq_s16(x3, x4);
            int16x8_t D1 = vaddq_s16(vsubq_s16(x1, x6), q2);
            int16x8_t D2 = vaddq_s16(vsubq_s16(x2, x5), q2);
            int16x8_t D3 = vaddq_s16(vsubq_s16(x3, x4), q2);
            int32x4_t dl, dh, el, eh, ol, oh, y0lo, y0hi;

            y0lo = vaddw_s16(vaddw_s16(vaddw_s16(vmovl_s16(vget_low_s16(x0)),
                                                vget_low_s16(S1)),
                                       vget_low_s16(S2)), vget_low_s16(S3));
            y0hi = vaddw_s16(vaddw_s16(vaddw_s16(vmovl_high_s16(x0),
                                                 vget_high_s16(S1)),
                                       vget_high_s16(S2)), vget_high_s16(S3));
            vst1q_s32(&out[GT_K_UV[0][v]][cbase], y0lo);
            vst1q_s32(&out[GT_K_UV[0][v]][cbase + 4], y0hi);
            for (k = 1; k <= 3; k++) {
                const int16x8_t pv = vld1q_s16(P7Q_I[k]);
                const int16x8_t mv = vld1q_s16(M7Q_I[k]);
                el = vmovl_s16(vget_low_s16(x0));
                eh = vmovl_high_s16(x0);
                ol = vdupq_n_s32(0);
                oh = vdupq_n_s32(0);
                SNTRUP_ACC16(el, eh, ol, oh, S1, D1, pv, mv, 0);
                SNTRUP_ACC16(el, eh, ol, oh, S2, D2, pv, mv, 1);
                SNTRUP_ACC16(el, eh, ol, oh, S3, D3, pv, mv, 2);
                dl = vaddq_s32(el, ol);
                dh = vaddq_s32(eh, oh);
                vst1q_s32(&out[GT_K_UV[k][v]][cbase], dl);
                vst1q_s32(&out[GT_K_UV[k][v]][cbase + 4], dh);
                dl = vaddq_s32(vsubq_s32(el, ol), qoff);
                dh = vaddq_s32(vsubq_s32(eh, oh), qoff);
                vst1q_s32(&out[GT_K_UV[7 - k][v]][cbase], dl);
                vst1q_s32(&out[GT_K_UV[7 - k][v]][cbase + 4], dh);
            }
        }
    }
}

static void good_thomas_77_ntt_inv16(Blocks77 out, const Blocks16 in) {
    Matrix16 tmp;
    SNTRUP_STAGE_MARK(inv_dft11);
    gt_l1_dense_inv16(tmp, in);
    SNTRUP_STAGE_MARK(inv_dft7);
    gt_layer2_inv16(out, tmp);
}

#endif

static void good_thomas_77_ntt_zs_neon(Blocks77 out, const Blocks77 in, int invert, int public_zs_forward, int scale_inv77, int skip_final_reduce, int lazy_forward) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const int32x4_t *P7v = invert ? P7V_I : P7V_F;
    const int32x4_t *M7v = invert ? M7V_I : M7V_F;
    const int (*P11P)[6] = invert ? PAIR11_P_I : PAIR11_P_F;
    const int (*P11M)[6] = invert ? PAIR11_M_I : PAIR11_M_F;
    const int32x4_t P7c0 = P7v[0], P7c1 = P7v[1], P7c2 = P7v[2];
    const int32x4_t M7c0 = M7v[0], M7c1 = M7v[1], M7c2 = M7v[2];
    const int *o7 = invert ? PAIR7_ODDINIT_I : PAIR7_ODDINIT_F;
    const int32x4_t O7c0 = vdupq_n_s32(o7[1]), O7c1 = vdupq_n_s32(o7[2]),
                    O7c2 = vdupq_n_s32(o7[3]);
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
                    store_mod_q2_s32x4(&tmp[r][v][cbase], out11[v]);
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
                    store_mod_q2_s32x4(&tmp[r][v][cbase], out11[v]);
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
            dft7_pairwise_4lane(out7, in7, P7c0, P7c1, P7c2, M7c0, M7c1, M7c2,
                                O7c0, O7c1, O7c2);
            for (u = 0; u < 7; u++) {
                k = GT_K_UV[u][v];
                if (invert && skip_final_reduce) {
                    /* The only consumer is final_folding_*(), which adds up to
                     * three such coefficients and reduces once.  Storing the
                     * non-negative DFT accumulator keeps the same residue and
                     * removes one Barrett reduction per coefficient. */
                    vst1q_s32(&out[k][cbase], out7[u]);
                } else if (invert && scale_inv77) {
                    store_mod_q_inv77_s32x4(&out[k][cbase], out7[u], 1);
                } else if (lazy_forward) {
                    /* Only the weighted multiply consumes this transform's
                     * output.  It narrows to int16 and works modulo q, so a
                     * non-negative representative in [0,2q) is enough and the
                     * canonicalising min is unnecessary. */
                    store_mod_q2_s32x4(&out[k][cbase], out7[u]);
                } else {
                    store_mod_q_s32x4(&out[k][cbase], out7[u]);
                }
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

/* Same as w16_reduce_s32_pair(), but without the canonicalising min: the
 * caller only needs a representative in [0,2q) (still < 2^15, so the int16
 * narrowing is exact) because the following wide add/sub-mod folds the
 * doubled range back into a valid Shoup operand.  2 ALU per int32 vector. */
static inline int16x8_t w16_reduce_s32_pair_lazy(int32x4_t lo, int32x4_t hi) {
#if USE_BARRETT_REDUCE
    const int32x4_t mu = vdupq_n_s32(BARRETT_MU_Q31);
    uint32x4_t rlo = vmlsq_n_u32(vreinterpretq_u32_s32(lo),
                                 vreinterpretq_u32_s32(vqdmulhq_s32(lo, mu)),
                                 (uint32_t)q);
    uint32x4_t rhi = vmlsq_n_u32(vreinterpretq_u32_s32(hi),
                                 vreinterpretq_u32_s32(vqdmulhq_s32(hi, mu)),
                                 (uint32_t)q);
#else
    uint32x4_t rlo = vmod_q_u32_barrett(vreinterpretq_u32_s32(lo));
    uint32x4_t rhi = vmod_q_u32_barrett(vreinterpretq_u32_s32(hi));
#endif
    return vcombine_s16(vmovn_s32(vreinterpretq_s32_u32(rlo)),
                        vmovn_s32(vreinterpretq_s32_u32(rhi)));
}

static inline int16x8_t w16_reduce_s32_pair(int32x4_t lo, int32x4_t hi) {
    uint32x4_t rlo = vmod_q_u32_barrett(vreinterpretq_u32_s32(lo));
    uint32x4_t rhi = vmod_q_u32_barrett(vreinterpretq_u32_s32(hi));
    return vcombine_s16(vmovn_s32(vreinterpretq_s32_u32(rlo)),
                        vmovn_s32(vreinterpretq_s32_u32(rhi)));
}

/* Constant multiplication by c mod q using a 16-bit Shoup multiplier.
 * c' = floor(c * 2^15 / q).  For 0 <= x < q, VQRDMULH(x,c') computes
 * qhat = round(x*c'/2^15) in {floor(x*c/q), floor(x*c/q)+1}; therefore
 * r = x*c - qhat*q lies in (-q,q).  The low 16 bits of this subtraction are
 * exact modulo 2^16, and min_u16(r, r+q) turns them into the canonical
 * residue: for r>=0 it picks r, for r<0 the wrapped r+q is smaller.
 * Only five NEON instructions per eight lanes, with no 32-bit widening. */
static inline int16x8_t w16_mul_const_shoup(int16x8_t x, uint16_t c,
                                            uint16_t cprime) {
    /* Modulo-2^16 wraparound is intentional (see the comment above), so the
     * product and the qhat*q subtraction are done on unsigned 16-bit lanes,
     * where the C semantics are the hardware wraparound. */
    const uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vmulq_u16(vreinterpretq_u16_s16(x), vdupq_n_u16(c));
    uint16x8_t qhat = vreinterpretq_u16_s16(
        vqrdmulhq_s16(x, vdupq_n_s16((int16_t)cprime)));
    r = vmlsq_u16(r, qhat, qv);
    return vreinterpretq_s16_u16(vminq_u16(r, vaddq_u16(r, qv)));
}

/* Same as w16_mul_const_shoup(), but the two multipliers are already
 * replicated over eight lanes. */
static inline int16x8_t w16_mul_const_shoup_v(int16x8_t x, int16x8_t c,
                                              int16x8_t cprime) {
    const uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vmulq_u16(vreinterpretq_u16_s16(x),
                             vreinterpretq_u16_s16(c));
    uint16x8_t qhat = vreinterpretq_u16_s16(vqrdmulhq_s16(x, cprime));
    r = vmlsq_u16(r, qhat, qv);
    return vreinterpretq_s16_u16(vminq_u16(r, vaddq_u16(r, qv)));
}

static inline int16x8_t w16_add_mod(int16x8_t a, int16x8_t b) {
    /* a+b < 2q.  As unsigned, r-q wraps near 2^16 when r<q, so
     * min_u16(r, r-q) is the canonical residue. */
    uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vreinterpretq_u16_s16(vaddq_s16(a, b));
    return vreinterpretq_s16_u16(vminq_u16(r, vsubq_u16(r, qv)));
}

/* Same as w16_sub_mod(), but valid for a,b in [0,2q).  The lazy forward
 * transform stores representatives in [0,2q); a-b+2q then lies in (0,4q) and
 * one conditional subtraction brings it back to [0,3q), which is a safe
 * operand range for the TMVP (its 8-term accumulator stays below 2^31). */
static inline int16x8_t w16_sub_mod_wide(int16x8_t a, int16x8_t b) {
    uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vreinterpretq_u16_s16(
        vaddq_s16(vsubq_s16(a, b), vdupq_n_s16((int16_t)(2 * q))));
    return vreinterpretq_s16_u16(vminq_u16(r, vsubq_u16(r, qv)));
}

static inline int16x8_t w16_sub_mod(int16x8_t a, int16x8_t b) {
    /* a-b is in (-q,q).  As unsigned, a negative lane wraps near 2^16 and
     * adding q wraps back into [0,q), so min_u16(r, r+q) canonicalises. */
    uint16x8_t qv = vdupq_n_u16((uint16_t)q);
    uint16x8_t r = vreinterpretq_u16_s16(vsubq_s16(a, b));
    return vreinterpretq_s16_u16(vminq_u16(r, vaddq_u16(r, qv)));
}

/* Accumulate T(upper,col)*vec. col is the first matrix column, while
 * upper[j] supplies the wrapped upper diagonal for displacement j. */
static inline void w16_tmvp8_mla(int16x8_t upper, int16x8_t col, int16x8_t vec,
                                 int32x4_t *acc_lo, int32x4_t *acc_hi) {
    /* Four independent accumulator pairs: the eight 8x8 columns would
     * otherwise form one depth-8 vmlal dependency chain. */
    int32x4_t lo0 = *acc_lo, hi0 = *acc_hi;
    int32x4_t lo1 = vdupq_n_s32(0), hi1 = vdupq_n_s32(0);
#define W16_MLA_COLUMN(ALO, AHI, LANE, COLUMN) do {                            \
        ALO = vmlal_laneq_s16(ALO, vget_low_s16(COLUMN), vec, LANE);           \
        AHI = vmlal_high_laneq_s16(AHI, COLUMN, vec, LANE);                    \
    } while (0)
    W16_MLA_COLUMN(lo0, hi0, 0, col);
    W16_MLA_COLUMN(lo0, hi0, 1, vextq_s16(upper, col, 7));
    W16_MLA_COLUMN(lo0, hi0, 2, vextq_s16(upper, col, 6));
    W16_MLA_COLUMN(lo0, hi0, 3, vextq_s16(upper, col, 5));
    W16_MLA_COLUMN(lo1, hi1, 4, vextq_s16(upper, col, 4));
    W16_MLA_COLUMN(lo1, hi1, 5, vextq_s16(upper, col, 3));
    W16_MLA_COLUMN(lo1, hi1, 6, vextq_s16(upper, col, 2));
    W16_MLA_COLUMN(lo1, hi1, 7, vextq_s16(upper, col, 1));
#undef W16_MLA_COLUMN
    *acc_lo = vaddq_s32(lo0, lo1);
    *acc_hi = vaddq_s32(hi0, hi1);
}

/* As above, with the weight's Shoup multipliers supplied as eight-lane
 * constants. */
static inline int16x8_t w16_weighted_tmvp8_lazy_v(int16x8_t a, int16x8_t b,
                                                  int16x8_t weight,
                                                  int16x8_t weight_shoup) {
    int16x8_t weighted_b = w16_mul_const_shoup_v(b, weight, weight_shoup);
    int32x4_t acc_lo = vdupq_n_s32(0);
    int32x4_t acc_hi = vdupq_n_s32(0);

    w16_tmvp8_mla(weighted_b, b, a, &acc_lo, &acc_hi);
    return w16_reduce_s32_pair_lazy(acc_lo, acc_hi);
}

/* As w16_weighted_tmvp8(), but the returned coefficients are in [0,2q)
 * instead of [0,q). */
static inline int16x8_t w16_weighted_tmvp8_lazy(int16x8_t a, int16x8_t b,
                                                int weight, uint16_t weight_shoup) {
    int16x8_t weighted_b = w16_mul_const_shoup(b, (uint16_t)weight, weight_shoup);
    int32x4_t acc_lo = vdupq_n_s32(0);
    int32x4_t acc_hi = vdupq_n_s32(0);

    w16_tmvp8_mla(weighted_b, b, a, &acc_lo, &acc_hi);
    return w16_reduce_s32_pair_lazy(acc_lo, acc_hi);
}

/* Product in F_q[x]/(x^8-weight), expressed as one 8-by-8 TMVP. */
static inline int16x8_t w16_weighted_tmvp8(int16x8_t a, int16x8_t b,
                                           int weight, uint16_t weight_shoup) {
    int16x8_t weighted_b = w16_mul_const_shoup(b, (uint16_t)weight, weight_shoup);
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
static inline void weighted_mul16_2ntt_tmvp8_neon_core(
        int out[16], const int a[16], const int b[16],
        int16x8_t c_s, int16x8_t c_sp, int16x8_t c_qsp,
        int16x8_t c_i2, int16x8_t c_i2p,
        int16x8_t c_i2s, int16x8_t c_i2sp) {
    int16x8_t a0, a1, b0, b1;
    int16x8_t sa1, sb1, ap, am, bp, bm, hp, hm, h0, h1;
    const int16x8_t c_qsp2 = vsubq_s16(vdupq_n_s16((int16_t)q), c_s);

    /* Narrow two adjacent int32 vectors into each eight-coefficient half. */
    a0 = vcombine_s16(vmovn_s32(vld1q_s32(a)), vmovn_s32(vld1q_s32(a + 4)));
    a1 = vcombine_s16(vmovn_s32(vld1q_s32(a + 8)), vmovn_s32(vld1q_s32(a + 12)));
    b0 = vcombine_s16(vmovn_s32(vld1q_s32(b)), vmovn_s32(vld1q_s32(b + 4)));
    b1 = vcombine_s16(vmovn_s32(vld1q_s32(b + 8)), vmovn_s32(vld1q_s32(b + 12)));

    sa1 = w16_mul_const_shoup_v(a1, c_s, c_sp);
    sb1 = w16_mul_const_shoup_v(b1, c_s, c_sp);
    /* Range-lazy split: the operands of the first TMVP only need to keep the
     * 8-term int32 accumulator below 2^31, so ap/bp in [0,3q) and am in
     * (q,4q) are fine and two of the three conditional subtractions vanish.
     *   ap = a0 + sa1      < 3q    (a is the `vec` side of the TMVP)
     *   bp = b0 + sb1      < 3q    (b side: still a valid Shoup operand)
     *   am = a0 - sa1 + 2q < 4q    (a side, 8*4q*3q = 2.05e9 < 2^31)
     *   bm = b0 - sb1 + 2q < 4q -> one min -> [0,3q) (b side needs < 3q)
     * The Shoup multiply on the b side canonicalises, so only an upper bound
     * of 3q is required there. */
    ap = vaddq_s16(a0, sa1);
    am = vaddq_s16(vsubq_s16(a0, sa1), vdupq_n_s16((int16_t)(2 * q)));
    bp = vaddq_s16(b0, sb1);
    bm = w16_sub_mod_wide(b0, sb1);

    /* The TMVPs only need [0,2q), which the wide reconstruction below folds
     * back into range (the final Shoup multiply canonicalises anyway). */
    hp = w16_weighted_tmvp8_lazy_v(ap, bp, c_s, c_sp);
    hm = w16_weighted_tmvp8_lazy_v(am, bm, c_qsp2, c_qsp);

    h0 = w16_mul_const_shoup_v(w16_add_mod(hp, hm), c_i2, c_i2p);
    h1 = w16_mul_const_shoup_v(w16_sub_mod_wide(hp, hm), c_i2s, c_i2sp);
    vst1q_s32(out, vmovl_s16(vget_low_s16(h0)));
    vst1q_s32(out + 4, vmovl_s16(vget_high_s16(h0)));
    vst1q_s32(out + 8, vmovl_s16(vget_low_s16(h1)));
    vst1q_s32(out + 12, vmovl_s16(vget_high_s16(h1)));
}

#if SNTRUP653_FAST16
/* Same arithmetic, but the two transformed operands and the result are all
 * int16: the forward path stores lazy representatives in [0,2q) and the
 * inverse first layer accepts exactly that range, so the int32 packing and
 * unpacking around each block disappears. */
static inline void weighted_mul16_2ntt_tmvp8_neon_core16(
        int16_t out[16], const int16_t a[16], const int16_t b[16],
        int16x8_t c_s, int16x8_t c_sp, int16x8_t c_qsp,
        int16x8_t c_i2, int16x8_t c_i2p,
        int16x8_t c_i2s, int16x8_t c_i2sp) {
    const int16x8_t a0 = vld1q_s16(a);
    const int16x8_t a1 = vld1q_s16(a + 8);
    const int16x8_t b0 = vld1q_s16(b);
    const int16x8_t b1 = vld1q_s16(b + 8);
    int16x8_t sa1, sb1, ap, am, bp, bm, hp, hm, h0, h1;
    const int16x8_t c_qsp2 = vsubq_s16(vdupq_n_s16((int16_t)q), c_s);

    sa1 = w16_mul_const_shoup_v(a1, c_s, c_sp);
    sb1 = w16_mul_const_shoup_v(b1, c_s, c_sp);
    ap = vaddq_s16(a0, sa1);
    am = vaddq_s16(vsubq_s16(a0, sa1), vdupq_n_s16((int16_t)(2 * q)));
    bp = vaddq_s16(b0, sb1);
    bm = w16_sub_mod_wide(b0, sb1);

    hp = w16_weighted_tmvp8_lazy_v(ap, bp, c_s, c_sp);
    hm = w16_weighted_tmvp8_lazy_v(am, bm, c_qsp2, c_qsp);

    h0 = w16_mul_const_shoup_v(w16_add_mod(hp, hm), c_i2, c_i2p);
    h1 = w16_mul_const_shoup_v(w16_sub_mod_wide(hp, hm), c_i2s, c_i2sp);
    vst1q_s16(out, h0);
    vst1q_s16(out + 8, h1);
}

static inline __attribute__((always_inline))
void weighted_mul16_scaled16(int16_t out[BS], const int16_t a[BS],
                             const int16_t b[BS], int weight_index,
                             int scale_inv77) {
    const int16_t *c = W16C[weight_index];
    const int16x8x4_t cc = vld1q_s16_x4(c);   /* S, SP, QSP, I2S77 */
    int16x8_t c_i2s, c_i2sp;
    if (scale_inv77) {
        c_i2s = cc.val[3];
        c_i2sp = vld1q_s16(c + W16C_I2SP77 * 8);
    } else {
        c_i2s = vld1q_s16(c + W16C_I2S * 8);
        c_i2sp = vld1q_s16(c + W16C_I2SP * 8);
    }
    weighted_mul16_2ntt_tmvp8_neon_core16(
        out, a, b,
        cc.val[0], cc.val[1], cc.val[2],
        scale_inv77 ? vld1q_s16(W16_INV2_INV77) : vld1q_s16(W16_INV2),
        scale_inv77 ? vld1q_s16(W16_INV2_INV77_SHOUP)
                    : vld1q_s16(W16_INV2_SHOUP),
        c_i2s, c_i2sp);
}

#endif

/* Test-only entry point: derives the Shoup multipliers at run time. */
static inline void weighted_mul16_2ntt_tmvp8_neon(
        int out[16], const int a[16], const int b[16],
        int sqrt_lambda, int inv2, int inv2_sqrt_lambda) {
    weighted_mul16_2ntt_tmvp8_neon_core(
        out, a, b,
        vdupq_n_s16((int16_t)sqrt_lambda),
        vdupq_n_s16((int16_t)shoup16(sqrt_lambda)),
        vdupq_n_s16((int16_t)shoup16(q - sqrt_lambda)),
        vdupq_n_s16((int16_t)inv2),
        vdupq_n_s16((int16_t)shoup16(inv2)),
        vdupq_n_s16((int16_t)inv2_sqrt_lambda),
        vdupq_n_s16((int16_t)shoup16(inv2_sqrt_lambda)));
}

/* The production weighted16 path is fixed: radix-2 + two NEON TMVP8s. */
static inline __attribute__((always_inline))
void weighted_mul16_scaled(int out[BS], const int a[BS], const int b[BS],
                           int weight_index, int scale_inv77) {
    const int16_t *c = W16C[weight_index];
    weighted_mul16_2ntt_tmvp8_neon_core(
        out, a, b,
        vld1q_s16(c + W16C_S * 8),
        vld1q_s16(c + W16C_SP * 8),
        vld1q_s16(c + W16C_QSP * 8),
        scale_inv77 ? vld1q_s16(W16_INV2_INV77) : vld1q_s16(W16_INV2),
        scale_inv77 ? vld1q_s16(W16_INV2_INV77_SHOUP)
                    : vld1q_s16(W16_INV2_SHOUP),
        vld1q_s16(c + (scale_inv77 ? W16C_I2S77 : W16C_I2S) * 8),
        vld1q_s16(c + (scale_inv77 ? W16C_I2SP77 : W16C_I2SP) * 8));
}

static void weighted_mul16(int out[BS], const int a[BS], const int b[BS], int weight_index) {
    weighted_mul16_scaled(out, a, b, weight_index, 0);
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
static inline int32_t dot_i16_neon_4lane(const int16_t *x, const int16_t *y, int n) {
    /* Four independent accumulators break the vmlal latency chain; the
     * 73 low-part dots are run one after another, so a single dependent
     * accumulator used to serialise almost the whole stage. */
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

static SNTRUP_MAYBE_UNUSED void low_part73_scalar(int clow[LOW_NP], const Fq f[p], const small g[p]) {
    int kk, i;
    for (kk = 0; kk < LOW_N; kk++) {
        int64_t acc = 0;
        for (i = 0; i <= kk; i++) acc += (int64_t)f[i] * g[kk - i];
        clow[kk] = mod_q_i64_raw(acc);
    }
}

static void low_part73_neon(int clow[LOW_NP], const Fq f[p], const small g[p]) {
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && SNTRUP653_LOW73_NEON
    /* Triangular convolution clow[k] = sum_{i<=k} f[i]*g[k-i], evaluated
     * eight outputs at a time.  For output chunk c (k = 8c..8c+7) the term
     * i contributes f[i] times the eight consecutive g values
     * g[8c-i .. 8c+7-i]; negative indices land in a zero prefix, so every
     * lane may be accumulated unconditionally.  Four independent int32
     * accumulator pairs keep the vmlal chains short. */
    enum { GPAD = 8 + 80 };
    int16_t gpad[GPAD] SNTRUP_ALIGN64;
    const int32_t bias = (int32_t)(LOW_N * q);
    int i, c;
    for (i = 0; i < GPAD; i++) gpad[i] = 0;
    for (i = 0; i < LOW_N; i++) gpad[8 + i] = (int16_t)g[i];
    for (c = 0; c < LOW_NP / 8; c++) {
        const int base = 8 * c;
        const int hi = base + 7;
        int32x4_t a0 = vdupq_n_s32(0), a1 = vdupq_n_s32(0);
        int32x4_t b0 = vdupq_n_s32(0), b1 = vdupq_n_s32(0);
        int32x4_t c0 = vdupq_n_s32(0), c1 = vdupq_n_s32(0);
        int32x4_t d0 = vdupq_n_s32(0), d1 = vdupq_n_s32(0);
        for (i = 0; i + 3 <= hi; i += 4) {
            int16x8_t g0 = vld1q_s16(&gpad[8 + base - i]);
            int16x8_t g1 = vld1q_s16(&gpad[8 + base - i - 1]);
            int16x8_t g2 = vld1q_s16(&gpad[8 + base - i - 2]);
            int16x8_t g3 = vld1q_s16(&gpad[8 + base - i - 3]);
            int16x8_t fv = vld1q_s16(&f[i]);
            a0 = vmlal_laneq_s16(a0, vget_low_s16(g0), fv, 0);
            a1 = vmlal_high_laneq_s16(a1, g0, fv, 0);
            b0 = vmlal_laneq_s16(b0, vget_low_s16(g1), fv, 1);
            b1 = vmlal_high_laneq_s16(b1, g1, fv, 1);
            c0 = vmlal_laneq_s16(c0, vget_low_s16(g2), fv, 2);
            c1 = vmlal_high_laneq_s16(c1, g2, fv, 2);
            d0 = vmlal_laneq_s16(d0, vget_low_s16(g3), fv, 3);
            d1 = vmlal_high_laneq_s16(d1, g3, fv, 3);
        }
        a0 = vaddq_s32(vaddq_s32(a0, b0), vaddq_s32(c0, d0));
        a1 = vaddq_s32(vaddq_s32(a1, b1), vaddq_s32(c1, d1));
        /* Bias by LOW_N*q (== 0 mod q) so the possibly negative convolution
         * is fed to the unsigned Barrett reducer. */
        a0 = vaddq_s32(a0, vdupq_n_s32(bias));
        a1 = vaddq_s32(a1, vdupq_n_s32(bias));
        a0 = vreinterpretq_s32_u32(vmod_q_u32_barrett(vreinterpretq_u32_s32(a0)));
        a1 = vreinterpretq_s32_u32(vmod_q_u32_barrett(vreinterpretq_u32_s32(a1)));
        vst1q_s32(&clow[base], a0);
        vst1q_s32(&clow[base + 4], a1);
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

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
/* Reduce four non-negative-or-slightly-negative fold sums to centered Fq.
 * The fixed 4q bias makes every lane non-negative; the Barrett step then
 * returns [0,q) and the compare/subtract centers it exactly like
 * center_q_raw(). */
static inline int32x4_t center4(int32x4_t v) {
    const int32x4_t half = vdupq_n_s32(q / 2);
    const int32x4_t qv = vdupq_n_s32(q);
    return vbslq_s32(vcgtq_s32(v, half), vsubq_s32(v, qv), v);
}

/* Same, for fold sums already known to be non-negative (sums of two or three
 * non-negative inverse-transform accumulators plus, at most, one clow term). */
static inline void store_centered4_nb(Fq *dst, int32x4_t s) {
    vst1_s16(dst, vmovn_s32(center4(vreinterpretq_s32_u32(
        vmod_q_u32_barrett(vreinterpretq_u32_s32(s))))));
}

static inline void store_centered4(Fq *dst, int32x4_t s) {
    /* May receive unreduced inverse-transform accumulators (< 2^28).  The
     * only negative terms are of the form main[j]-clow[j] with main[j] >= 0,
     * so a 4q bias still makes every lane non-negative and the total stays
     * well below 2^31. */
    const int32x4_t bias = vdupq_n_s32(4 * q);
    const int32x4_t half = vdupq_n_s32(q / 2);
    const uint32x4_t qv = vdupq_n_u32((uint32_t)q);
    uint32x4_t r = vmod_q_u32_barrett(vreinterpretq_u32_s32(vaddq_s32(s, bias)));
    int32x4_t v = vreinterpretq_s32_u32(r);
    uint32x4_t gt = vcgtq_s32(v, half);
    v = vsubq_s32(v, vreinterpretq_s32_u32(vandq_u32(gt, qv)));
    vst1_s16(dst, vmovn_s32(v));
}
#endif

static void final_folding_neon(Fq out[p], const Blocks77 Cblk, const int clow[LOW_N]) {
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && SNTRUP653_FINAL_FOLD_NEON
    const int *main = &Cblk[0][0];
    int kk;
    out[0] = center_q_raw((int64_t)clow[0] + main[p]);

    for (kk = 1; kk + 3 <= LOW_N - 1; kk += 4) {
        int32x4_t s = vaddq_s32(vld1q_s32(&clow[kk]), vld1q_s32(&main[p - 1 + kk]));
        s = vaddq_s32(s, vld1q_s32(&main[p + kk]));
        store_centered4_nb(&out[kk], s);
    }
    for (; kk < LOW_N; kk++) {
        out[kk] = center_q_raw((int64_t)clow[kk] + main[p - 1 + kk] + main[p + kk]);
    }

    for (kk = LOW_N; kk + 3 <= p - LOW_N - 2; kk += 4) {
        int32x4_t s = vaddq_s32(vld1q_s32(&main[kk]), vld1q_s32(&main[p - 1 + kk]));
        s = vaddq_s32(s, vld1q_s32(&main[p + kk]));
        store_centered4_nb(&out[kk], s);
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
        s = vaddq_s32(vaddq_s32(s, t0), t1);
        store_centered4(&out[kk], s);
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
/* ---------------------------------------------------------------------------
 * Math fingerprint.
 *
 * The fingerprint hashes every parameter and every derived constant that the
 * polynomial multiplication depends on: the field, the 77-point Good--Thomas
 * index maps, the direct 7/11-point DFT tables, the packed 16-bit Shoup
 * tables, the radix-2 / TMVP weights, and the public pruning schedule.  It is
 * independent of the optimisation switches (those choose code paths, not
 * mathematics), so all build variants and all compilers must print the same
 * value.  This makes "the device-specific optimisation did not touch the
 * mathematics" a checkable byte-level claim instead of an assertion.
 *
 * Test-only: the whole function lives behind SNTRUP653_INTERNAL_TEST_HOOKS and
 * is removed from the production view by check_selftest_isolation.py.
 * ------------------------------------------------------------------------- */
static uint64_t math_fp_mix(uint64_t h, const void *data, size_t len) {
    const unsigned char *ptr = (const unsigned char *)data;
    while (len--) {
        h ^= (uint64_t)*ptr++;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

#define MATH_FP(H, V) (H) = math_fp_mix((H), &(V), sizeof(V))

uint64_t sntrup653_math_fingerprint(void) {
    uint64_t h = UINT64_C(14695981039346656037);
    int params[9];
    int i;
    init_tables();
    params[0] = p;
    params[1] = q;
    params[2] = w;
    params[3] = MAIN_N;
    params[4] = LOW_N;
    params[5] = LOW_NP;
    params[6] = BLOCKS;
    params[7] = BS;
    params[8] = PAIRWISE_QOFF_MULT;
    MATH_FP(h, params);
    MATH_FP(h, ROOT77);
    MATH_FP(h, INV2);
    MATH_FP(h, INV77);
    MATH_FP(h, INV2_INV77);
    MATH_FP(h, INV2_SHOUP);
    MATH_FP(h, INV2_INV77_SHOUP);
    MATH_FP(h, LAMBDA77);
    MATH_FP(h, SQRT_LAMBDA77);
    MATH_FP(h, INV2_SQRT_LAMBDA77);
    MATH_FP(h, INV2_SQRT_LAMBDA_INV77);
    MATH_FP(h, SQRT_LAMBDA_SHOUP);
    MATH_FP(h, QMSQRT_LAMBDA_SHOUP);
    MATH_FP(h, INV2_SQRT_SHOUP);
    MATH_FP(h, INV2_SQRT_INV77_SHOUP);
    MATH_FP(h, POW7_F);
    MATH_FP(h, POW7_I);
    MATH_FP(h, POW11_F);
    MATH_FP(h, POW11_I);
    MATH_FP(h, PAIR7_ODDINIT_F);
    MATH_FP(h, PAIR7_ODDINIT_I);
    MATH_FP(h, PAIR7_P_F);
    MATH_FP(h, PAIR7_M_F);
    MATH_FP(h, PAIR7_P_I);
    MATH_FP(h, PAIR7_M_I);
    MATH_FP(h, PAIR11_P_F);
    MATH_FP(h, PAIR11_M_F);
    MATH_FP(h, PAIR11_P_I);
    MATH_FP(h, PAIR11_M_I);
    MATH_FP(h, P11Q_F);
    MATH_FP(h, M11Q_F);
    MATH_FP(h, P11Q_I);
    MATH_FP(h, M11Q_I);
    MATH_FP(h, P7Q_F);
    MATH_FP(h, M7Q_F);
    MATH_FP(h, P7Q_I);
    MATH_FP(h, M7Q_I);
    MATH_FP(h, W16C);
    MATH_FP(h, W16_INV2);
    MATH_FP(h, W16_INV2_SHOUP);
    MATH_FP(h, W16_INV2_INV77);
    MATH_FP(h, W16_INV2_INV77_SHOUP);
    MATH_FP(h, GT_IDX_RS);
    MATH_FP(h, GT_K_UV);
    MATH_FP(h, ACTIVE_COLS_PER_R);
    MATH_FP(h, ACTIVE_COL_LIST);
    MATH_FP(h, ACTIVE_PAIR_MASK11);
    for (i = 0; i < 3; i++) {
        MATH_FP(h, P7V_F[i]);
        MATH_FP(h, M7V_F[i]);
        MATH_FP(h, P7V_I[i]);
        MATH_FP(h, M7V_I[i]);
    }
    return h;
}

int sntrup653_internal_selftest(unsigned random_tests, unsigned seed) {
    unsigned trial;
    int i, j, k, m, r, s, idx;
    Blocks77 in SNTRUP_ALIGN64;
    Blocks77 dense SNTRUP_ALIGN64;
    Blocks77 opt SNTRUP_ALIGN64;
    Blocks77 back SNTRUP_ALIGN64;
    Fq out_scalar[p];
    Fq out_opt[p];
    int clow[LOW_NP];

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
                                   SNTRUP653_PUBLIC_ZS_MODE && k < ACTIVE_BLOCKS, 0, 0, 0);
        if (!compare_blocks_detail("gt-forward-unit", k, dense, opt)) return 20;
        good_thomas_77_ntt_zs_neon(back, opt, 1, 0, 1, 0, 0);
        if (!compare_blocks_detail("gt-inverse-unit", k, in, back)) return 21;
    }

    for (trial = 0; trial < random_tests; trial++) {
        for (i = 0; i < BLOCKS; i++) {
            for (j = 0; j < BS; j++) in[i][j] = (int)(selftest_rand32() % (uint32_t)q);
        }
        good_thomas_77_ntt_zs(dense, in, 0, 0);
        good_thomas_77_ntt_zs_neon(opt, in, 0, 0, 0, 0, 0);
        if (!compare_blocks_detail("gt-forward-random", (int)trial, dense, opt)) return 30;
        good_thomas_77_ntt_zs_neon(back, opt, 1, 0, 1, 0, 0);
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
        good_thomas_77_ntt_zs_neon(opt, in, 0, 1, 0, 0, 0);
        if (!compare_blocks_detail("gt-forward-padded-random", (int)trial, dense, opt)) return 32;
        good_thomas_77_ntt_zs_neon(back, opt, 1, 0, 1, 0, 0);
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
        int ref73[LOW_NP], got73[LOW_NP];
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
                    if (memcmp(ref73, got73, LOW_N * sizeof(int)) != 0) {
                        return 70;
                    }
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

#if SNTRUP653_FAST16
    /* 16-bit transform data path: forward, weighted multiply and inverse all
     * run on int16 lanes, in place, exactly as the delivered core does.  Each
     * stage must agree with the 32-bit path modulo q, both for the dense
     * schedule and for the build-time pruned one.  The two Blocks16 buffers
     * are the only extra storage; the four Blocks77 arrays above are reused
     * so the internal selftest stays inside the stack budget. */
    for (trial = 0; trial < random_tests; trial++) {
        Blocks16 a16 SNTRUP_ALIGN64, b16 SNTRUP_ALIGN64;
        int public_zs = (int)(selftest_rand32() & 1u);
        int active = public_zs ? ACTIVE_BLOCKS : BLOCKS;

        memset(a16, 0, sizeof(a16));
        memset(b16, 0, sizeof(b16));
        zero_blocks(in);
        zero_blocks(dense);
        for (i = 0; i < active; i++) {
            for (j = 0; j < BS; j++) {
                int va = (int)(selftest_rand32() % (uint32_t)q);
                int vb = (int)(selftest_rand32() % (uint32_t)q);
                a16[i][j] = (int16_t)va;
                in[i][j] = va;
                b16[i][j] = (int16_t)vb;
                dense[i][j] = vb;
            }
        }

        good_thomas_77_ntt_fwd16(a16, a16, public_zs);
        good_thomas_77_ntt_fwd16(b16, b16, public_zs);
        good_thomas_77_ntt_zs_neon(in, in, 0, public_zs, 0, 0, 0);
        good_thomas_77_ntt_zs_neon(dense, dense, 0, public_zs, 0, 0, 0);
        if (!compare_blocks_mod_q16("fwd16-a", (int)trial, in, a16)) return 80;
        if (!compare_blocks_mod_q16("fwd16-b", (int)trial, dense, b16)) return 81;

        for (m = 0; m < BLOCKS; m++) {
            weighted_mul16_scaled16(a16[m], a16[m], b16[m], m, 1);
            weighted_mul16_scaled(in[m], in[m], dense[m], m, 1);
            if (!compare_block_mod_q16("mul16", (int)trial, m, in[m], a16[m])) return 82;
        }

        good_thomas_77_ntt_inv16(back, a16);
        good_thomas_77_ntt_zs_neon(opt, in, 1, 0, 0, 1, 0);
        if (!compare_blocks77_mod_q("inv16", (int)trial, opt, back)) return 83;
    }
#endif

    return 0;
}

#endif

/* Decode the packed 16-bit input, reduce it into [0,q) and lay it out in the
 * blockified representation, all in one pass.  The two arrays that the rest
 * of the core consumes are written together:
 *   - out[]  the flat block layout, coefficients as canonical int32 residues;
 *   - f[]    the same residues as int16, kept for low_part73_neon().
 * Decoding a uint16 pair and sign-extending it matches what
 * crypto_decode_pxint16() + Fq_freeze() produced modulo q, and every consumer
 * works modulo q, so the final result is bit-identical. */
static void blockify_A_bytes(Blocks77 out, Fq f[p], const unsigned char *inbytes) {
    int i;
    int *dst = &out[0][0];
    blockify_clear_padding(out);
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    {
        /* A signed 16-bit input only spans x/q in [-7.09, 7.09], so the
         * 16-bit Shoup/Barratt pair with M = floor(2^15/q) = 7 satisfies
         * qhat = round(x*M/2^15) in {floor(x/q), floor(x/q)+1} over the
         * whole int16 range.  Then r = x - qhat*q is in (-q,q) and one
         * unsigned min canonicalises it, all on eight lanes at a time. */
        const uint16x8_t qv = vdupq_n_u16((uint16_t)q);
        const int16x8_t mu = vdupq_n_s16((int16_t)(((uint32_t)1 << 15) / (uint32_t)q));
        for (i = 0; i + 8 <= p; i += 8) {
            int16x8_t x = vreinterpretq_s16_u16(
                vreinterpretq_u16_u8(vld1q_u8(inbytes + 2 * i)));
            int16x8_t qhat = vqrdmulhq_s16(x, mu);
            uint16x8_t u = vreinterpretq_u16_s16(vmlsq_s16(x, qhat,
                                             vreinterpretq_s16_u16(qv)));
            int16x8_t r = vreinterpretq_s16_u16(vminq_u16(u, vaddq_u16(u, qv)));
            vst1q_s32(dst + i, vmovl_s16(vget_low_s16(r)));
            vst1q_s32(dst + i + 4, vmovl_high_s16(r));
            vst1q_s16(&f[i], r);
        }
    }
#endif
    for (; i < p; i++) {
        int v = (int16_t)(inbytes[2 * i] | ((unsigned)inbytes[2 * i + 1] << 8));
        int r = mod_q_i64_raw(v);
        dst[i] = r;
        f[i] = (Fq)r;
    }
}

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
    int clow[LOW_NP];
    int i, m;
#if SNTRUP653_FAST16
    Blocks16 A16 SNTRUP_ALIGN64;
    Blocks16 B16 SNTRUP_ALIGN64;
#endif

    init_tables();
#if SNTRUP653_FAST16
    SNTRUP_STAGE_MARK(blockify_A);
    blockify_A_bytes16(A16, f, inbytes);
#else
    blockify_A_bytes(Ablk, f, inbytes);
#endif
    /* The key decode runs here, so it belongs to the blockify_B row. */
    SNTRUP_STAGE_MARK(blockify_B);
    for (i = 0; i < p; ++i) {
        small gi = (small)kbytes[i];
        small gi0 = gi & 1;
        g[i] = (small)(gi0 - (gi & (gi0 << 1)));
    }

#if SNTRUP653_FAST16
    blockify_B16(B16, g);
    good_thomas_77_ntt_fwd16(A16, A16, SNTRUP653_PUBLIC_ZS_MODE);
    good_thomas_77_ntt_fwd16(B16, B16, SNTRUP653_PUBLIC_ZS_MODE);
    SNTRUP_STAGE_MARK(pointwise);
    /* opt22-pointwise-batch */
    for (m = 0; m + 1 < BLOCKS; m += 2) {
        weighted_mul16_scaled16(A16[m + 0], A16[m + 0], B16[m + 0], m + 0, 1);
        weighted_mul16_scaled16(A16[m + 1], A16[m + 1], B16[m + 1], m + 1, 1);
    }
    weighted_mul16_scaled16(A16[m + 0], A16[m + 0], B16[m + 0], m + 0, 1);
    good_thomas_77_ntt_inv16(Ablk, A16);
#else
    blockify_B(Bblk, g);
#if SNTRUP653_USE_INPLACE_BUFFERS
    good_thomas_77_ntt_zs_neon(Ablk, Ablk, 0, SNTRUP653_PUBLIC_ZS_MODE, 0, 0, 1);
    good_thomas_77_ntt_zs_neon(Bblk, Bblk, 0, SNTRUP653_PUBLIC_ZS_MODE, 0, 0, 1);
    for (m = 0; m + 3 < BLOCKS; m += 4) {
        weighted_mul16_scaled(Ablk[m], Ablk[m], Bblk[m], m, 1);
        weighted_mul16_scaled(Ablk[m + 1], Ablk[m + 1], Bblk[m + 1], m + 1, 1);
        weighted_mul16_scaled(Ablk[m + 2], Ablk[m + 2], Bblk[m + 2], m + 2, 1);
        weighted_mul16_scaled(Ablk[m + 3], Ablk[m + 3], Bblk[m + 3], m + 3, 1);
    }
    weighted_mul16_scaled(Ablk[BLOCKS - 1], Ablk[BLOCKS - 1],
                          Bblk[BLOCKS - 1], BLOCKS - 1, 1);
    good_thomas_77_ntt_zs_neon(Ablk, Ablk, 1, 0, 0, SNTRUP653_FINAL_FOLD_NEON, 0);
#else
    good_thomas_77_ntt_zs_neon(Ahat, Ablk, 0, SNTRUP653_PUBLIC_ZS_MODE, 0, 0, 1);
    good_thomas_77_ntt_zs_neon(Bhat, Bblk, 0, SNTRUP653_PUBLIC_ZS_MODE, 0, 0, 1);
    for (m = 0; m < BLOCKS; m++) weighted_mul16_scaled(Hhat[m], Ahat[m], Bhat[m], m, 1);
    good_thomas_77_ntt_zs_neon(Cblk, Hhat, 1, 0, 0, SNTRUP653_FINAL_FOLD_NEON, 0);
#endif
#endif

    SNTRUP_STAGE_MARK(low73);
    low_part73_neon(clow, f, g);
    SNTRUP_STAGE_MARK(final_fold);
#if SNTRUP653_USE_INPLACE_BUFFERS
    final_folding_neon(out, Ablk, clow);
#else
    final_folding_neon(out, Cblk, clow);
#endif
    SNTRUP_STAGE_MARK(encode);
    crypto_encode_pxint16(outbytes, out);
    SNTRUP_STAGE_MARK(end);
    return 0;
}
