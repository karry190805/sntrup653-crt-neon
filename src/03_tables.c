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
