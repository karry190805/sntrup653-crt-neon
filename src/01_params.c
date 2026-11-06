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

