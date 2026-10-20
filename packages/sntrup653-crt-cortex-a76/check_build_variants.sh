#!/usr/bin/env bash
# check_build_variants.sh — every optimisation switch, on and off.
#
# The delivered configuration sets five switches.  Four of them select between
# two implementations of the same mathematical step, and one selects between two
# reduction strategies.  If a switch were wired to the wrong helper, or if a
# fallback had drifted away from the vector path, the shipped configuration
# would still pass every test in the package: nothing else builds the other arm.
#
# This script builds all of them, runs the three correctness suites on each, and
# requires the core correctness hash to be identical across every variant.  It
# also covers SNTRUP653_PUBLIC_ZS_RUNTIME_MASK=1, the legacy run-time-masking
# arm that the shipped build never compiles.
#
# usage: bash ./check_build_variants.sh [out_dir]
# env:   VAR_INTERNAL_TESTS  internal selftest random cases (default 300)
#        VAR_CORE_TESTS      core equivalence cases        (default 2000)
#        VAR_HASH_TESTS      hash smoke cases              (default 200)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

OUT_DIR="${1:-$SCRIPT_DIR/results_variants}"
INTERNAL_TESTS="${VAR_INTERNAL_TESTS:-300}"
CORE_TESTS="${VAR_CORE_TESTS:-2000}"
HASH_TESTS="${VAR_HASH_TESTS:-200}"
SEED="${VAR_SEED:-1}"
CPU_PIN="${CPU_PIN:-}"

CFLAGS="-O3 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU_TARGET -mtune=$CPU_TARGET -fomit-frame-pointer -fno-lto"
REQUIRE_NEON="-DSNTRUP653_REQUIRE_AARCH64_NEON=1"
COMMON_DEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"

# name|extra definitions appended to COMMON_DEFS
VARIANTS=(
  "default|"
  "dense|-DSNTRUP653_PUBLIC_ZS_MODE=0"
  "rtmask|-DSNTRUP653_PUBLIC_ZS_RUNTIME_MASK=1"
  "dense_rtmask|-DSNTRUP653_PUBLIC_ZS_MODE=0 -DSNTRUP653_PUBLIC_ZS_RUNTIME_MASK=1"
  "noinplace|-DSNTRUP653_USE_INPLACE_BUFFERS=0"
  "low73_scalar|-DSNTRUP653_LOW73_NEON=0"
  "fold_scalar|-DSNTRUP653_FINAL_FOLD_NEON=0"
  "no_barrett|-DUSE_BARRETT_REDUCE=0"
  "all_fallbacks|-DSNTRUP653_USE_INPLACE_BUFFERS=0 -DSNTRUP653_PUBLIC_ZS_MODE=0 -DSNTRUP653_LOW73_NEON=0 -DSNTRUP653_FINAL_FOLD_NEON=0 -DUSE_BARRETT_REDUCE=0"
)

run_target() {
  if [ -n "$CPU_PIN" ]; then taskset -c "$CPU_PIN" "$@"; else "$@"; fi
}

CC="${CC:-}"
if [ -z "$CC" ] || ! command -v "$CC" >/dev/null 2>&1; then
  CC=gcc
fi
command -v "$CC" >/dev/null 2>&1 || { echo "error: no C compiler" >&2; exit 2; }

mkdir -p "$OUT_DIR/common"
LOG="$OUT_DIR/variants.log"
: > "$LOG"
: > "$OUT_DIR/results.txt"

echo "[common] baseline core and shared codecs" | tee -a "$LOG"
"$CC" $CFLAGS \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=baseline_crypto_core_multsntrup653 \
  -Ibaseline -c baseline/crypto_core_multsntrup653.c -o "$OUT_DIR/common/baseline_core.o" >> "$LOG" 2>&1
"$CC" $CFLAGS -Ibaseline -c baseline/crypto_encode_653xint16.c -o "$OUT_DIR/common/encode.o" >> "$LOG" 2>&1
"$CC" $CFLAGS -Ibaseline -c baseline/crypto_decode_653xint16.c -o "$OUT_DIR/common/decode.o" >> "$LOG" 2>&1

declare -a hashes=()
declare -a names=()
failed=0

for entry in "${VARIANTS[@]}"; do
  name="${entry%%|*}"
  defs="${entry#*|}"
  dir="$OUT_DIR/$name"
  rm -rf "$dir"
  mkdir -p "$dir"
  echo "[variant] $name defs='${defs:-<none>}'" | tee -a "$LOG"

  if ! "$CC" $CFLAGS $REQUIRE_NEON $defs \
      -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=optimized_crypto_core_multsntrup653 \
      -Ioptimized -c optimized/crypto_core_multsntrup653.c -o "$dir/optimized_core.o" >> "$LOG" 2>&1; then
    echo "  compile failed" | tee -a "$LOG"; failed=$((failed + 1)); continue
  fi
  if ! "$CC" $CFLAGS $REQUIRE_NEON $defs -DSNTRUP653_INTERNAL_TEST_HOOKS=1 \
      -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=internal_crypto_core_multsntrup653 \
      -Ioptimized shared/test_internal_construction.c optimized/crypto_core_multsntrup653.c \
      optimized/crypto_encode_653xint16.c optimized/crypto_decode_653xint16.c \
      -o "$dir/test_internal_construction" >> "$LOG" 2>&1; then
    echo "  compile(selftest) failed" | tee -a "$LOG"; failed=$((failed + 1)); continue
  fi
  if ! "$CC" $CFLAGS -Ibaseline -Ishared shared/test_core_equiv.c \
      "$OUT_DIR/common/baseline_core.o" "$dir/optimized_core.o" \
      "$OUT_DIR/common/encode.o" "$OUT_DIR/common/decode.o" \
      -o "$dir/test_core_equiv" >> "$LOG" 2>&1; then
    echo "  compile(equiv) failed" | tee -a "$LOG"; failed=$((failed + 1)); continue
  fi
  if ! "$CC" $CFLAGS -Ioptimized -Ishared shared/test_endtoend_oracle.c \
      "$OUT_DIR/common/baseline_core.o" "$dir/optimized_core.o" \
      "$OUT_DIR/common/encode.o" "$OUT_DIR/common/decode.o" \
      -o "$dir/test_endtoend_oracle" >> "$LOG" 2>&1; then
    echo "  compile(oracle) failed" | tee -a "$LOG"; failed=$((failed + 1)); continue
  fi
  if ! "$CC" $CFLAGS $REQUIRE_NEON $defs \
      -DBENCH_IMPL_TAG=variant-$name -DBENCH_BACKEND_TAG="$name" \
      shared/bench_core_mult.c shared/randombytes.c shared/sha2.c optimized/*.c \
      -Ioptimized -Ishared -o "$dir/bench_core_optimized" >> "$LOG" 2>&1; then
    echo "  compile(bench) failed" | tee -a "$LOG"; failed=$((failed + 1)); continue
  fi

  ok=1
  run_target "$dir/test_internal_construction" "$INTERNAL_TESTS" "$SEED" \
    > "$dir/internal.log" 2>&1 || ok=0
  [ "$ok" -eq 1 ] || echo "  selftest FAIL: $(tail -1 "$dir/internal.log")" | tee -a "$LOG"

  if [ "$ok" -eq 1 ]; then
    run_target "$dir/test_core_equiv" "$CORE_TESTS" "$SEED" > "$dir/equiv.log" 2>&1 || ok=0
    [ "$ok" -eq 1 ] || echo "  core equivalence FAIL: $(tail -1 "$dir/equiv.log")" | tee -a "$LOG"
  fi

  if [ "$ok" -eq 1 ]; then
    run_target "$dir/test_endtoend_oracle" fixed 0 "$SEED" > "$dir/oracle_fixed.log" 2>&1 || ok=0
    [ "$ok" -eq 1 ] || echo "  oracle(fixed) FAIL: $(tail -1 "$dir/oracle_fixed.log")" | tee -a "$LOG"
  fi

  if [ "$ok" -eq 1 ]; then
    run_target "$dir/test_endtoend_oracle" axes 0 "$SEED" > "$dir/oracle_axes.log" 2>&1 || ok=0
    [ "$ok" -eq 1 ] || echo "  oracle(axes) FAIL: $(tail -1 "$dir/oracle_axes.log")" | tee -a "$LOG"
  fi

  hash=""
  if [ "$ok" -eq 1 ]; then
    hash="$(run_target "$dir/bench_core_optimized" 1 1 "$HASH_TESTS" 0 "$SEED" 2>&1 \
      | tee "$dir/bench.log" | grep '^core_correctness_hash=' | sed 's/^core_correctness_hash=//' || true)"
    [ -n "$hash" ] || { ok=0; echo "  bench produced no hash" | tee -a "$LOG"; }
  fi

  if [ "$ok" -eq 0 ]; then
    failed=$((failed + 1))
    echo "variant=$name RESULT=FAIL" | tee -a "$OUT_DIR/results.txt"
    continue
  fi

  hashes+=("$hash")
  names+=("$name")
  echo "variant=$name RESULT=PASS core_hash=$hash" | tee -a "$OUT_DIR/results.txt"
done

if [ "${#hashes[@]}" -eq 0 ]; then
  echo "build_variants=FAIL every variant failed"
  exit 3
fi

unique="$(printf '%s\n' "${hashes[@]}" | sort -u | wc -l | tr -d ' ')"
{
  echo "variants_total=${#VARIANTS[@]}"
  echo "variants_passed=${#hashes[@]}"
  echo "variants_failed=$failed"
  echo "unique_hashes=$unique"
  printf '  %s\n' "${hashes[@]}" | sort -u
} >> "$OUT_DIR/results.txt"

if [ "$failed" -ne 0 ]; then
  echo "build_variants=FAIL failed=$failed (see $OUT_DIR/results.txt)"
  exit 4
fi
if [ "$unique" -ne 1 ]; then
  echo "build_variants=FAIL unique_hashes=$unique (a switch changes the output)"
  exit 5
fi
echo "build_variants=PASS variants=${#hashes[@]} unique_hashes=1 hash=${hashes[0]}"
