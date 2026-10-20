/* ---------------------------------------------------------------------------
 * B01  文件头、环参数与静态断言  —— 论文 §3.1 §3.2
 * 源文件第 1--67 行（共 67 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
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
