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

/* Compatibility selector, retained for A/B measurement only.
 * 0 (default): the public-zero pruning of the first layer is resolved at build
 *              time (one specialised, fully unrolled kernel per r).
 * 1          : the runtime-mask shape loads only active columns; the pairs
 *              are skipped by a run-time mask test inside
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

