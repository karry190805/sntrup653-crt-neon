#!/usr/bin/env python3
"""Insert SNTRUP653_STAGE_PROFILE stage marks into a core source file.

Usage: add_stage_marks.py path/to/optimized/crypto_core_multsntrup653.c

The marks expand to nothing unless SNTRUP653_STAGE_PROFILE is defined, so the
delivered binaries are unaffected.  They only exist to build the diagnostic
bench_stage_profile binary.

The current scheme is the inline id + cntvct_el0 one:

    SNTRUP_STAGE_MARK(blockify_A);

where the macro appends (id, timestamp) to arrays declared in
shared/stage_profile_ids.h.  A tree that already carries that scheme is left
untouched.  A tree that carries an older scheme is rejected so the generator
never silently mixes two instrumentation shapes.

Each stage is marked exactly once per call: the transform functions mark their
own first and second layers, so the call site must NOT add a second inv_dft11
mark (that used to produce 15 slots instead of 14).
"""
import sys

p = sys.argv[1]
s = open(p).read()

CURRENT = 'sntrup653_stage_ids[sntrup653_stage_idx]'

if CURRENT in s:
    print('already marked (inline id scheme):', p)
    sys.exit(0)
if 'SNTRUP_STAGE_MARK' in s:
    sys.exit('error: %s carries an older stage-mark scheme; regenerate the tree '
             'from an up-to-date base' % p)

# 1) the hook macro, placed right after the core header include
anchor = '#include "crypto_core_multsntrup653.h"\n'
assert s.count(anchor) == 1, 'include anchor'
hook = anchor + '''
/* ---------------------------------------------------------------------------
 * Diagnostic stage marks.
 *
 * SNTRUP653_STAGE_PROFILE is defined only for the dedicated profiling binary
 * (build_stage_profile.sh).  Each mark appends one compile-time id and one
 * cntvct_el0 timestamp to arrays in shared/stage_profile_ids.h; the driver
 * validates the id sequence and computes per-stage durations.  Every delivered
 * build expands SNTRUP_STAGE_MARK to a no-op, so this instrumentation cannot
 * change code generation.
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
'''
s = s.replace(anchor, hook)

# 2) forward transform: L1 = DFT-11, layer 2 = DFT-7
old = '''static void good_thomas_77_ntt_fwd16(Blocks16 out, const Blocks16 in,
                                     int public_zs_forward) {
    Matrix16 tmp;
    int cbase;
    if (public_zs_forward) {'''
assert s.count(old) == 1, 'fwd16 head'
new = '''static void good_thomas_77_ntt_fwd16(Blocks16 out, const Blocks16 in,
                                     int public_zs_forward) {
    Matrix16 tmp;
    int cbase;
    SNTRUP_STAGE_MARK(fwd_dft11);
    if (public_zs_forward) {'''
s = s.replace(old, new)

old = '''    } else {
        gt_l1_dense16(tmp, in);
    }
    gt_layer2_fwd16(out, tmp);
}
'''
assert s.count(old) == 1, 'fwd16 tail'
new = '''    } else {
        gt_l1_dense16(tmp, in);
    }
    SNTRUP_STAGE_MARK(fwd_dft7);
    gt_layer2_fwd16(out, tmp);
}
'''
s = s.replace(old, new)

# 3) inverse transform: L1 = DFT-11, layer 2 = DFT-7.  These marks are the only
# ones for the inverse transform; the production call site adds none.
old = '''static void good_thomas_77_ntt_inv16(Blocks77 out, const Blocks16 in) {
    Matrix16 tmp;
    gt_l1_dense_inv16(tmp, in);
    gt_layer2_inv16(out, tmp);
}
'''
assert s.count(old) == 1, 'inv16'
new = '''static void good_thomas_77_ntt_inv16(Blocks77 out, const Blocks16 in) {
    Matrix16 tmp;
    SNTRUP_STAGE_MARK(inv_dft11);
    gt_l1_dense_inv16(tmp, in);
    SNTRUP_STAGE_MARK(inv_dft7);
    gt_layer2_inv16(out, tmp);
}
'''
s = s.replace(old, new)

# 4) production path marks (FAST16 branch only, plus the shared tail)
old = '''    init_tables();
#if SNTRUP653_FAST16
    blockify_A_bytes16(A16, f, inbytes);
#else'''
assert s.count(old) == 1, 'blockify anchor'
new = '''    init_tables();
#if SNTRUP653_FAST16
    SNTRUP_STAGE_MARK(blockify_A);
    blockify_A_bytes16(A16, f, inbytes);
#else'''
s = s.replace(old, new)

old = '''#if SNTRUP653_FAST16
    blockify_B16(B16, g);
    good_thomas_77_ntt_fwd16(A16, A16, SNTRUP653_PUBLIC_ZS_MODE);'''
assert s.count(old) == 1, 'blockify B anchor'
new = '''#if SNTRUP653_FAST16
    /* The key decode runs above, so it belongs to the blockify_B row. */
    SNTRUP_STAGE_MARK(blockify_B);
    blockify_B16(B16, g);
    good_thomas_77_ntt_fwd16(A16, A16, SNTRUP653_PUBLIC_ZS_MODE);'''
s = s.replace(old, new)

# close the forward-transform segment before the pointwise loop
old = '    good_thomas_77_ntt_fwd16(B16, B16, SNTRUP653_PUBLIC_ZS_MODE);\n'
assert s.count(old) == 1, 'pointwise anchor'
new = old + '    SNTRUP_STAGE_MARK(pointwise);\n'
s = s.replace(old, new)

old = '''    low_part73_neon(clow, f, g);
#if SNTRUP653_USE_INPLACE_BUFFERS
    final_folding_neon(out, Ablk, clow);
#else
    final_folding_neon(out, Cblk, clow);
#endif

    crypto_encode_pxint16(outbytes, out);
    return 0;
}'''
assert s.count(old) == 1, 'tail anchor'
new = '''    SNTRUP_STAGE_MARK(low73);
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
}'''
s = s.replace(old, new)

open(p, 'w').write(s)
print('marked', p)
