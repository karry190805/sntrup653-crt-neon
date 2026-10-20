/* B03 编译期开关（消融实验）
 *
 * crypto_core_multsntrup653.c 第 105–148 行，共 44 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
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
