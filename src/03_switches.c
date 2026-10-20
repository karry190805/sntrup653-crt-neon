/* ---------------------------------------------------------------------------
 * B03  编译期开关（消融入口）  —— 论文 §4.3
 * 源文件第 105--148 行（共 44 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
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
