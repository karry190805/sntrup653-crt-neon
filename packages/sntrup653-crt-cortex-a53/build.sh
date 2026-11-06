#!/usr/bin/env bash
# build.sh — build the baseline/optimized binaries used by every runner.
#
# Optimization switches (all default to 1 = the delivered configuration):
#   SNTRUP653_USE_INPLACE_BUFFERS  in-place time-domain buffers
#   SNTRUP653_PUBLIC_ZS_MODE       skip public all-zero blocks in the first layer
#   SNTRUP653_LOW73_NEON           NEON dot product for the 73-coefficient low part
#   SNTRUP653_FINAL_FOLD_NEON      NEON vector add/sub for the modulo x^653-x-1 fold
#   USE_BARRETT_REDUCE             Barrett reduction (1) vs % q fallback (0)
#
# SNTRUP653_FAST16 is derived from the switches above: it is auto-enabled only
# for the delivered configuration (in-place, build-time public-zero pruning, no
# runtime mask, NEON low73/fold, Barrett).  Set -DSNTRUP653_FAST16=0 to force the
# int32 transform path for validation.
#
# env:
#   WITH_TSAN=1  also build the ThreadSanitizer variant of the concurrent
#                first-call test (set to 0 where TSan is unavailable)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

CPU_PIN="${CPU_PIN:-}"
CC="${CC:-gcc}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
BASELINE="baseline"
OPTIMIZED="optimized"

SNTRUP653_USE_INPLACE_BUFFERS="${SNTRUP653_USE_INPLACE_BUFFERS:-1}"
SNTRUP653_PUBLIC_ZS_MODE="${SNTRUP653_PUBLIC_ZS_MODE:-1}"
SNTRUP653_LOW73_NEON="${SNTRUP653_LOW73_NEON:-1}"
SNTRUP653_FINAL_FOLD_NEON="${SNTRUP653_FINAL_FOLD_NEON:-1}"
USE_BARRETT_REDUCE="${USE_BARRETT_REDUCE:-1}"
SNTRUP653_FAST16="${SNTRUP653_FAST16:-auto}"
case "$SNTRUP653_FAST16" in
  0|1|auto) ;;
  *) echo "error: SNTRUP653_FAST16 must be 0, 1 or auto" >&2; exit 2 ;;
esac
WEIGHTED16_TAG="radix2-2xtmvp8"
CONFIG_ID="inplace${SNTRUP653_USE_INPLACE_BUFFERS}-zs${SNTRUP653_PUBLIC_ZS_MODE}-w16-${WEIGHTED16_TAG}-low${SNTRUP653_LOW73_NEON}-fold${SNTRUP653_FINAL_FOLD_NEON}-barrett${USE_BARRETT_REDUCE}-fast16"
OPTIMIZED_DEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=$SNTRUP653_USE_INPLACE_BUFFERS"
OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DSNTRUP653_PUBLIC_ZS_MODE=$SNTRUP653_PUBLIC_ZS_MODE"
OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DSNTRUP653_LOW73_NEON=$SNTRUP653_LOW73_NEON"
OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DSNTRUP653_FINAL_FOLD_NEON=$SNTRUP653_FINAL_FOLD_NEON"
OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DUSE_BARRETT_REDUCE=$USE_BARRETT_REDUCE"
if [ "$SNTRUP653_FAST16" != "auto" ]; then
  OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DSNTRUP653_FAST16=$SNTRUP653_FAST16"
fi

# BUILD_TARGETS=bench builds only the polymul/KEM benchmark binaries, which is
# useful for a quick A/B loop; the default "all" keeps the delivered behaviour
# and additionally builds the test/transcript binaries.
BUILD_TARGETS="${BUILD_TARGETS:-all}"
case "$BUILD_TARGETS" in
  all|bench) ;;
  *) echo "error: BUILD_TARGETS must be 'all' or 'bench' (got '$BUILD_TARGETS')" >&2; exit 2 ;;
esac

if [ "${PORTABLE_SANITY:-0}" = "1" ]; then
  echo "error: scalar portable mode removed; use AArch64 native or CROSS_BUILD=1" >&2
  exit 2
fi
if [ "${CROSS_BUILD:-0}" != "1" ]; then bash ./check_host.sh; fi
CFLAGS="${CFLAGS:--O3 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=${CPU_TARGET} -mtune=${CPU_TARGET} -fomit-frame-pointer -fno-lto}"
# Optimized-core-only flags.  The baseline is deliberately left on the plain
# CFLAGS so that the A/B comparison measures the source change, not a build
# difference.  Compiler-driven loop unrolling, an unconstrained vectorizer
# cost model, a profile-free branch layout, the modulo scheduler and live-range
# shrinking are worth ~1.5k cycles per core call on the Cortex-X1C; see the
# delivery-level OPTIMIZATION_NOTES_zh.md (opt21-r2 section).
OPT_CFLAGS="${OPT_CFLAGS:--funroll-loops -fvect-cost-model=unlimited -fno-guess-branch-probability -fno-tree-slp-vectorize -falign-functions=32 -fmodulo-sched}"
REQUIRE_NEON="-DSNTRUP653_REQUIRE_AARCH64_NEON=1"
# STRICT_SAME_FLAGS=1 compiles the baseline core with exactly the same flags
# as the optimized core, so an A/B run measures only the source difference.
# This is the default.  Set STRICT_SAME_FLAGS=0 only for an explicitly
# non-strict diagnostic build; the ablation still measures only the optimized
# arms and does not depend on this switch.
STRICT_SAME_FLAGS="${STRICT_SAME_FLAGS:-1}"
case "$STRICT_SAME_FLAGS" in
  0|1) ;;
  *) echo "error: STRICT_SAME_FLAGS must be 0 or 1" >&2; exit 2 ;;
esac
if [ "${USE_LTO:-0}" = "1" ]; then
  CFLAGS="${CFLAGS} -flto"
fi

command -v "$CC" >/dev/null 2>&1 || { echo "error: compiler '$CC' not found" >&2; exit 3; }
mkdir -p "$BUILD_DIR"

# The two source trees must differ in exactly one file: the polynomial core.
check_boundary() {
  local f name optimized_path changed=0
  for f in "$BASELINE"/*; do
    [ -f "$f" ] || continue
    name="${f##*/}"
    optimized_path="$OPTIMIZED/$name"
    [ -f "$optimized_path" ] || { echo "error: optimized tree is missing $name" >&2; return 1; }
    if ! cmp -s "$f" "$optimized_path"; then
      echo "$name" >> "$BUILD_DIR/baseline_diff.tmp"
      changed=$((changed + 1))
    fi
  done
  for f in "$OPTIMIZED"/*; do
    [ -f "$f" ] || continue
    name="${f##*/}"
    [ -f "$BASELINE/$name" ] || { echo "error: extra file in optimized tree: $name" >&2; return 1; }
  done
  if [ "$changed" -ne 1 ] || ! grep -qx 'crypto_core_multsntrup653.c' "$BUILD_DIR/baseline_diff.tmp"; then
    echo "error: isolated A/B boundary violated; inspect $BUILD_DIR/baseline_diff.tmp" >&2
    return 1
  fi
  cp "$BUILD_DIR/baseline_diff.tmp" BASELINE_DIFF.txt
  rm -f "$BUILD_DIR/baseline_diff.tmp"
}

rm -f "$BUILD_DIR/baseline_diff.tmp"
check_boundary

{
  echo "generated_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || true)"
  echo "cc=$(command -v "$CC" 2>/dev/null || echo "$CC")"
  echo "package_revision=release-20261103-opt22"
  echo "source_tree_sha256=$(bash ./source_tree_sha256.sh)"
  echo "core_sha256=$(sha256sum "$OPTIMIZED/crypto_core_multsntrup653.c" | cut -d' ' -f1)"
  echo "baseline_core_sha256=$(sha256sum "$BASELINE/crypto_core_multsntrup653.c" | cut -d' ' -f1)"
  echo "cflags=$CFLAGS"
  echo "opt_cflags=$OPT_CFLAGS"
  echo "strict_same_flags=$STRICT_SAME_FLAGS"
  echo "portable_sanity=${PORTABLE_SANITY:-0}"
  echo "cross_build=${CROSS_BUILD:-0}"
  cat platform.conf
  echo "use_lto=${USE_LTO:-0}"
  echo "config_id=$CONFIG_ID"
  echo "sntrup653_use_inplace_buffers=$SNTRUP653_USE_INPLACE_BUFFERS"
  echo "sntrup653_public_zs_mode=$SNTRUP653_PUBLIC_ZS_MODE"
  echo "sntrup653_low73_neon=$SNTRUP653_LOW73_NEON"
  echo "sntrup653_final_fold_neon=$SNTRUP653_FINAL_FOLD_NEON"
  echo "use_barrett_reduce=$USE_BARRETT_REDUCE"
  if [ "$SNTRUP653_FAST16" = "auto" ]; then
    if [ "$SNTRUP653_USE_INPLACE_BUFFERS" = "1" ] && [ "$SNTRUP653_PUBLIC_ZS_MODE" = "1" ] \
       && [ "$SNTRUP653_LOW73_NEON" = "1" ] && [ "$SNTRUP653_FINAL_FOLD_NEON" = "1" ] \
       && [ "$USE_BARRETT_REDUCE" = "1" ]; then
      echo "sntrup653_fast16=1 (auto)"
    else
      echo "sntrup653_fast16=0 (auto, 32-bit transform path)"
    fi
  else
    echo "sntrup653_fast16=$SNTRUP653_FAST16 (explicit)"
  fi
  "$CC" --version 2>/dev/null | head -5 || true
} | tee "$BUILD_DIR/build_metadata.txt"

printf '' | "$CC" $CFLAGS -dM -E -x c - > "$BUILD_DIR/compiler_macros.txt"
grep -q '__aarch64__' "$BUILD_DIR/compiler_macros.txt" || { echo "error: compiler does not target AArch64" >&2; exit 4; }
grep -Eq '__ARM_NEON(__)?' "$BUILD_DIR/compiler_macros.txt" || { echo "error: compiler does not enable NEON" >&2; exit 4; }

if [ "$BUILD_TARGETS" = "all" ]; then
echo "[build] byte-wise core equivalence objects"
base_core_flags="$CFLAGS"
if [ "$STRICT_SAME_FLAGS" = "1" ]; then
  base_core_flags="$CFLAGS $OPT_CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS"
fi
"$CC" $base_core_flags \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=baseline_crypto_core_multsntrup653 \
  -I"$BASELINE" -c "$BASELINE/crypto_core_multsntrup653.c" -o "$BUILD_DIR/baseline_core.o"
"$CC" $CFLAGS $OPT_CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS -fstack-usage \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=optimized_crypto_core_multsntrup653 \
  -I"$OPTIMIZED" -c "$OPTIMIZED/crypto_core_multsntrup653.c" -o "$BUILD_DIR/optimized_core.o"
"$CC" $CFLAGS -I"$BASELINE" -c "$BASELINE/crypto_encode_653xint16.c" -o "$BUILD_DIR/encode.o"
"$CC" $CFLAGS -I"$BASELINE" -c "$BASELINE/crypto_decode_653xint16.c" -o "$BUILD_DIR/decode.o"
"$CC" $CFLAGS -I"$BASELINE" -Ishared shared/test_core_equiv.c \
  "$BUILD_DIR/baseline_core.o" "$BUILD_DIR/optimized_core.o" \
  "$BUILD_DIR/encode.o" "$BUILD_DIR/decode.o" -o "$BUILD_DIR/test_core_equiv"

echo "[build] internal construction selftest"
# Keep OPT_CFLAGS on the core translation unit only.  Applying it to the
# codec files as well would make the optimized arm differ from the baseline
# arm in two ways (source and flags) for files that are byte-identical.
core_obj="$BUILD_DIR/test_internal_construction_core.o"
"$CC" $CFLAGS $OPT_CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS -fstack-usage -DSNTRUP653_INTERNAL_TEST_HOOKS=1 \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=internal_crypto_core_multsntrup653 \
  -I"$OPTIMIZED" -c "$OPTIMIZED/crypto_core_multsntrup653.c" -o "$core_obj"
"$CC" $CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS -fstack-usage \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=internal_crypto_core_multsntrup653 \
  -I"$OPTIMIZED" shared/test_internal_construction.c \
  "$OPTIMIZED/crypto_encode_653xint16.c" "$OPTIMIZED/crypto_decode_653xint16.c" \
  "$core_obj" -o "$BUILD_DIR/test_internal_construction"

echo "[build] math fingerprint"
core_obj="$BUILD_DIR/test_math_fingerprint_core.o"
"$CC" $CFLAGS $OPT_CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS -DSNTRUP653_INTERNAL_TEST_HOOKS=1 \
  -I"$OPTIMIZED" -c "$OPTIMIZED/crypto_core_multsntrup653.c" -o "$core_obj"
"$CC" $CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS -I"$OPTIMIZED" -Ishared \
  shared/test_math_fingerprint.c \
  "$OPTIMIZED/crypto_encode_653xint16.c" "$OPTIMIZED/crypto_decode_653xint16.c" \
  "$core_obj" -o "$BUILD_DIR/test_math_fingerprint"

# The end-to-end oracle computes A*B mod (q, x^653-x-1) from the definition and
# never consults baseline/, so its verdict does not depend on the reference
# implementation being correct.  The two codec objects are on the link line
# only because the core objects it drives call them internally; the oracle
# itself does its own encoding.  Those two files are byte-identical in both
# trees (build.sh's boundary check enforces that), so one copy serves both.
echo "[build] end-to-end oracle"
"$CC" $CFLAGS -I"$OPTIMIZED" -Ishared shared/test_endtoend_oracle.c \
  "$BUILD_DIR/baseline_core.o" "$BUILD_DIR/optimized_core.o" \
  "$BUILD_DIR/encode.o" "$BUILD_DIR/decode.o" \
  -o "$BUILD_DIR/test_endtoend_oracle"

# Concurrent first call: plain and ThreadSanitizer builds.  The sanitised build
# is what actually proves the lazy table initialisation is race free; the plain
# build only compares the parallel results with a later serial replay.
echo "[build] concurrent first-call"
core_obj="$BUILD_DIR/test_concurrent_firstcall_core.o"
"$CC" $CFLAGS $OPT_CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS -I"$OPTIMIZED" -Ishared \
  -c "$OPTIMIZED/crypto_core_multsntrup653.c" -o "$core_obj"
"$CC" $CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS -I"$OPTIMIZED" -Ishared \
  shared/test_concurrent_firstcall.c \
  "$OPTIMIZED/crypto_encode_653xint16.c" "$OPTIMIZED/crypto_decode_653xint16.c" \
  "$core_obj" -pthread -o "$BUILD_DIR/test_concurrent_firstcall"
if [ "${WITH_TSAN:-1}" = "1" ]; then
  echo "[build] concurrent first-call (thread sanitizer)"
  "$CC" $CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS \
    -O1 -g -fno-omit-frame-pointer -fsanitize=thread \
    -I"$OPTIMIZED" -Ishared shared/test_concurrent_firstcall.c \
    "$OPTIMIZED/crypto_core_multsntrup653.c" \
    "$OPTIMIZED/crypto_encode_653xint16.c" "$OPTIMIZED/crypto_decode_653xint16.c" \
    -pthread -o "$BUILD_DIR/test_concurrent_firstcall_tsan"
fi
fi # BUILD_TARGETS = all

build_bench() {
  local dir="$1" output_tag="$2" bench_tag="$3" backend_tag="$4" extra_defs="${5:-}"
  local target_defs="" core_defs="" core_obj f srcs=()
  if [ "$STRICT_SAME_FLAGS" = "1" ]; then
    target_defs="$REQUIRE_NEON"
    core_defs="$REQUIRE_NEON $OPT_CFLAGS"
  elif [ "$output_tag" != "baseline" ]; then
    target_defs="$REQUIRE_NEON"
    core_defs="$REQUIRE_NEON $OPT_CFLAGS"
  fi
  for f in "$dir"/*.c; do
    [ "${f##*/}" = "crypto_core_multsntrup653.c" ] && continue
    srcs+=("$f")
  done
  core_obj="$BUILD_DIR/bench_${output_tag}_core.o"
  echo "[build] $output_tag core object"
  "$CC" $CFLAGS $core_defs $extra_defs -I"$dir" -Ishared \
    -c "$dir/crypto_core_multsntrup653.c" -o "$core_obj"
  echo "[build] $output_tag polymul benchmark"
  "$CC" $CFLAGS $target_defs $extra_defs -DBENCH_IMPL_TAG="$bench_tag" -DBENCH_BACKEND_TAG="$backend_tag" \
    shared/bench_core_mult.c shared/randombytes.c shared/sha2.c "${srcs[@]}" "$core_obj" \
    -I"$dir" -Ishared -o "$BUILD_DIR/bench_core_$output_tag"
  echo "[build] $output_tag KEM benchmark"
  "$CC" $CFLAGS $target_defs $extra_defs -DBENCH_IMPL_TAG="$bench_tag" -DBENCH_BACKEND_TAG="$backend_tag" \
    shared/bench_kem.c shared/randombytes.c shared/sha2.c "${srcs[@]}" "$core_obj" \
    -I"$dir" -Ishared -o "$BUILD_DIR/bench_kem_$output_tag"
}

build_transcript() {
  local dir="$1" output_tag="$2" bench_tag="$3" backend_tag="$4" extra_defs="${5:-}"
  local target_defs="" core_defs="" core_obj f srcs=()
  if [ "$STRICT_SAME_FLAGS" = "1" ]; then
    target_defs="$REQUIRE_NEON"
    core_defs="$REQUIRE_NEON $OPT_CFLAGS"
  elif [ "$output_tag" != "baseline" ]; then
    target_defs="$REQUIRE_NEON"
    core_defs="$REQUIRE_NEON $OPT_CFLAGS"
  fi
  for f in "$dir"/*.c; do
    [ "${f##*/}" = "crypto_core_multsntrup653.c" ] && continue
    srcs+=("$f")
  done
  core_obj="$BUILD_DIR/transcript_${output_tag}_core.o"
  echo "[build] $output_tag transcript core object"
  "$CC" $CFLAGS $core_defs $extra_defs -I"$dir" -Ishared \
    -c "$dir/crypto_core_multsntrup653.c" -o "$core_obj"
  echo "[build] $output_tag KEM transcript"
  "$CC" $CFLAGS $target_defs $extra_defs -DBENCH_IMPL_TAG="$bench_tag" -DBENCH_BACKEND_TAG="$backend_tag" \
    shared/kem_transcript.c shared/randombytes.c shared/sha2.c "${srcs[@]}" "$core_obj" \
    -I"$dir" -Ishared -o "$BUILD_DIR/kem_transcript_$output_tag"
}

base_extra_defs=""
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
    echo "core_flags_common=$CFLAGS $REQUIRE_NEON $OPT_CFLAGS $OPTIMIZED_DEFS"
    echo "baseline_core=$CC $CFLAGS $REQUIRE_NEON $OPT_CFLAGS $OPTIMIZED_DEFS -I$BASELINE -Ishared -c $BASELINE/crypto_core_multsntrup653.c -o $BUILD_DIR/bench_baseline_core.o"
    echo "optimized_core=$CC $CFLAGS $REQUIRE_NEON $OPT_CFLAGS $OPTIMIZED_DEFS -I$OPTIMIZED -Ishared -c $OPTIMIZED/crypto_core_multsntrup653.c -o $BUILD_DIR/bench_optimized_core.o"
    echo "core_flag_diff=NONE"
    echo "note=bench/kem/transcript link lines still carry distinct -DBENCH_*_TAG labels; they are string tags, not flags that affect code generation."
  } > "$BUILD_DIR/flags_baseline_optimized.txt"
fi

if command -v sha256sum >/dev/null 2>&1; then
  (
    cd "$BUILD_DIR"
    sha256sum bench_core_* bench_kem_* kem_transcript_* \
      2>/dev/null > binary_sha256.txt || true
    sha256sum test_core_equiv test_internal_construction \
      test_math_fingerprint test_endtoend_oracle \
      test_concurrent_firstcall* \
      >> binary_sha256.txt 2>/dev/null || true
  )
fi

echo "build=PASS build_dir=$BUILD_DIR config_id=$CONFIG_ID with_tsan=${WITH_TSAN:-1}"
