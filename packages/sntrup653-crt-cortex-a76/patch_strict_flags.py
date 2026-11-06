#!/usr/bin/env python3
"""Add STRICT_SAME_FLAGS support to a build.sh in place.

Usage: patch_strict_flags.py path/to/build.sh
"""
import sys

p = sys.argv[1]
s = open(p).read()

if 'STRICT_SAME_FLAGS' in s:
    print('already patched:', p)
    sys.exit(0)

# 1) declare the mode right after OPT_CFLAGS
anchor = 'REQUIRE_NEON="-DSNTRUP653_REQUIRE_AARCH64_NEON=1"\n'
assert s.count(anchor) == 1, 'REQUIRE_NEON anchor'
add = anchor + '''# STRICT_SAME_FLAGS=1 compiles the baseline core with exactly the same flags
# as the optimized core, so an A/B run measures only the source difference.
# The default (0) keeps the delivered opt21-r2 behaviour: OPT_CFLAGS applies to
# the optimized core only.
STRICT_SAME_FLAGS="${STRICT_SAME_FLAGS:-0}"
case "$STRICT_SAME_FLAGS" in
  0|1) ;;
  *) echo "error: STRICT_SAME_FLAGS must be 0 or 1" >&2; exit 2 ;;
esac
'''
s = s.replace(anchor, add)

# 2) record it in build metadata
anchor = '  echo "opt_cflags=$OPT_CFLAGS"\n'
assert s.count(anchor) == 1, 'metadata anchor'
s = s.replace(anchor, anchor + '  echo "strict_same_flags=$STRICT_SAME_FLAGS"\n')

# 3) baseline core object for the equivalence test uses the same flags in strict mode
old = '''echo "[build] byte-wise core equivalence objects"
"$CC" $CFLAGS \\
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=baseline_crypto_core_multsntrup653 \\
  -I"$BASELINE" -c "$BASELINE/crypto_core_multsntrup653.c" -o "$BUILD_DIR/baseline_core.o"
'''
assert s.count(old) == 1, 'equivalence baseline object'
new = '''echo "[build] byte-wise core equivalence objects"
base_core_flags="$CFLAGS"
if [ "$STRICT_SAME_FLAGS" = "1" ]; then
  base_core_flags="$CFLAGS $OPT_CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS"
fi
"$CC" $base_core_flags \\
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=baseline_crypto_core_multsntrup653 \\
  -I"$BASELINE" -c "$BASELINE/crypto_core_multsntrup653.c" -o "$BUILD_DIR/baseline_core.o"
'''
s = s.replace(old, new)

# 4) bench/transcript builders: same core flags on both arms in strict mode
old = '''  local target_defs="" core_defs="" core_obj f srcs=()
  if [ "$output_tag" != "baseline" ]; then
    target_defs="$REQUIRE_NEON"
    core_defs="$REQUIRE_NEON $OPT_CFLAGS"
  fi
'''
assert s.count(old) == 2, 'build_bench/build_transcript core flags (%d)' % s.count(old)
new = '''  local target_defs="" core_defs="" core_obj f srcs=()
  if [ "$STRICT_SAME_FLAGS" = "1" ]; then
    target_defs="$REQUIRE_NEON"
    core_defs="$REQUIRE_NEON $OPT_CFLAGS"
  elif [ "$output_tag" != "baseline" ]; then
    target_defs="$REQUIRE_NEON"
    core_defs="$REQUIRE_NEON $OPT_CFLAGS"
  fi
'''
s = s.replace(old, new)

# 5) in strict mode hand the baseline arm the same config defines as optimized
old = '''build_bench "$BASELINE" baseline baseline pqclean-round3-clean-c-auto-vectorization-possible
build_bench "$OPTIMIZED" optimized optimized "gt77-pairwise711-$CONFIG_ID" "$OPTIMIZED_DEFS"
if [ "$BUILD_TARGETS" = "all" ]; then
  build_transcript "$BASELINE" baseline baseline pqclean-round3-clean-c-auto-vectorization-possible
  build_transcript "$OPTIMIZED" optimized optimized "gt77-pairwise711-$CONFIG_ID" "$OPTIMIZED_DEFS"
fi
'''
assert s.count(old) == 1, 'call sites'
new = '''base_extra_defs=""
if [ "$STRICT_SAME_FLAGS" = "1" ]; then
  base_extra_defs="$OPTIMIZED_DEFS"
fi
build_bench "$BASELINE" baseline baseline pqclean-round3-clean-c-auto-vectorization-possible "$base_extra_defs"
build_bench "$OPTIMIZED" optimized optimized "gt77-pairwise711-$CONFIG_ID" "$OPTIMIZED_DEFS"
if [ "$BUILD_TARGETS" = "all" ]; then
  build_transcript "$BASELINE" baseline baseline pqclean-round3-clean-c-auto-vectorization-possible "$base_extra_defs"
  build_transcript "$OPTIMIZED" optimized optimized "gt77-pairwise711-$CONFIG_ID" "$OPTIMIZED_DEFS"
fi

if [ "$STRICT_SAME_FLAGS" = "1" ]; then
  {
    echo "# Strict same-flags A/B (STRICT_SAME_FLAGS=1)"
    echo "# Both core translation units are compiled with one identical flag set."
    echo "# Only the source path and the -o object path differ between the arms."
    echo "strict_same_flags=1"
    echo "cflags=$CFLAGS"
    echo "opt_cflags=$OPT_CFLAGS"
    echo "require_neon=$REQUIRE_NEON"
    echo "config_defs=$OPTIMIZED_DEFS"
    echo "core_compile_common=$CC $CFLAGS $REQUIRE_NEON $OPT_CFLAGS $OPTIMIZED_DEFS -Ishared"
    echo "baseline_core=$CC $CFLAGS $REQUIRE_NEON $OPT_CFLAGS $OPTIMIZED_DEFS -I$BASELINE -Ishared -c $BASELINE/crypto_core_multsntrup653.c -o $BUILD_DIR/bench_baseline_core.o"
    echo "optimized_core=$CC $CFLAGS $REQUIRE_NEON $OPT_CFLAGS $OPTIMIZED_DEFS -I$OPTIMIZED -Ishared -c $OPTIMIZED/crypto_core_multsntrup653.c -o $BUILD_DIR/bench_optimized_core.o"
    echo "core_flag_diff=NONE"
    echo "note=bench/kem/transcript link lines still carry distinct -DBENCH_*_TAG labels; they are string tags, not flags that affect code generation."
  } > "$BUILD_DIR/flags_baseline_optimized.txt"
fi
'''
s = s.replace(old, new)

open(p, 'w').write(s)
print('patched', p)
