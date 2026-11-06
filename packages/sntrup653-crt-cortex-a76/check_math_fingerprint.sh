#!/usr/bin/env bash
# check_math_fingerprint.sh — freeze the mathematics across build variants.
#
# Every optimisation switch in this package chooses a code path; none of them
# may change p, q, w, the Good-Thomas index maps, the direct 7/11-point DFT
# tables, the packed Shoup tables, the radix-2/TMVP weights or the public
# pruning schedule.  shared/test_math_fingerprint.c hashes all of those from
# the initialised runtime tables and also checks that a fixed multiplication
# returns centred canonical representatives.
#
# This script builds that test for every switch combination used by
# check_build_variants.sh and for gcc -O0/-O1/-O2/-O3/-Os, and requires one
# fingerprint from all of them.
#
# usage: bash ./check_math_fingerprint.sh [out_dir]
# env:   CPU_PIN   logical core (optional)
#        CC        compiler (default gcc)
#        EXPECTED  expected fingerprint (default from expected_math_fingerprint.txt)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

OUT_DIR="${1:-$SCRIPT_DIR/results_math_fingerprint}"
CPU_PIN="${CPU_PIN:-}"
CC="${CC:-gcc}"
EXPECTED_FILE="$SCRIPT_DIR/expected_math_fingerprint.txt"

EXPECTED="${EXPECTED:-}"
if [ -z "$EXPECTED" ]; then
  EXPECTED="$(sed -n 's/^math_fingerprint=//p' "$EXPECTED_FILE" | head -1)"
fi
[ -n "$EXPECTED" ] || { echo "error: no expected fingerprint in $EXPECTED_FILE" >&2; exit 2; }

CFLAGS_BASE="-std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU_TARGET -mtune=$CPU_TARGET -fomit-frame-pointer -fno-lto"
OPT_CFLAGS="${OPT_CFLAGS:--funroll-loops -fvect-cost-model=unlimited -fno-guess-branch-probability -fno-tree-slp-vectorize -falign-functions=32 -fmodulo-sched -flive-range-shrinkage}"
COMMON_DEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"

# name|extra definitions
VARIANTS=(
  "default|"
  "dense|-DSNTRUP653_PUBLIC_ZS_MODE=0"
  "rtmask|-DSNTRUP653_PUBLIC_ZS_RUNTIME_MASK=1"
  "dense_rtmask|-DSNTRUP653_PUBLIC_ZS_MODE=0 -DSNTRUP653_PUBLIC_ZS_RUNTIME_MASK=1"
  "noinplace|-DSNTRUP653_USE_INPLACE_BUFFERS=0"
  "low73_scalar|-DSNTRUP653_LOW73_NEON=0"
  "fold_scalar|-DSNTRUP653_FINAL_FOLD_NEON=0"
  "no_barrett|-DUSE_BARRETT_REDUCE=0"
  "no_fast16|-DSNTRUP653_FAST16=0"
  "all_fallbacks|-DSNTRUP653_USE_INPLACE_BUFFERS=0 -DSNTRUP653_PUBLIC_ZS_MODE=0 -DSNTRUP653_LOW73_NEON=0 -DSNTRUP653_FINAL_FOLD_NEON=0 -DUSE_BARRETT_REDUCE=0"
)

run_target() {
  if [ -n "$CPU_PIN" ]; then taskset -c "$CPU_PIN" "$@"; else "$@"; fi
}

command -v "$CC" >/dev/null 2>&1 || { echo "error: compiler '$CC' not found" >&2; exit 3; }
mkdir -p "$OUT_DIR"
: > "$OUT_DIR/results.txt"
: > "$OUT_DIR/fingerprints.txt"

pass=0
fail=0
declare -a values=()

check_one() {
  local name="$1" defs="$2" opt="$3"
  local dir="$OUT_DIR/$name"
  local core="$dir/core.o"
  local bin="$dir/test_math_fingerprint"
  local log="$dir/run.log"
  local fp
  rm -rf "$dir"
  mkdir -p "$dir"
  if ! "$CC" $opt $CFLAGS_BASE $OPT_CFLAGS -DSNTRUP653_INTERNAL_TEST_HOOKS=1 \
      $COMMON_DEFS $defs -Ioptimized \
      -c optimized/crypto_core_multsntrup653.c -o "$core" > "$dir/compile.log" 2>&1; then
    echo "$name RESULT=FAIL compile_core" | tee -a "$OUT_DIR/results.txt"
    fail=$((fail + 1)); return
  fi
  if ! "$CC" $opt $CFLAGS_BASE $COMMON_DEFS $defs -Ioptimized -Ishared \
      shared/test_math_fingerprint.c optimized/crypto_encode_653xint16.c \
      optimized/crypto_decode_653xint16.c "$core" -o "$bin" >> "$dir/compile.log" 2>&1; then
    echo "$name RESULT=FAIL compile_test" | tee -a "$OUT_DIR/results.txt"
    fail=$((fail + 1)); return
  fi
  if ! run_target "$bin" "$EXPECTED" > "$log" 2>&1; then
    echo "$name RESULT=FAIL run" | tee -a "$OUT_DIR/results.txt"
    sed -n '1,5p' "$log" | sed 's/^/  /'
    fail=$((fail + 1)); return
  fi
  fp="$(sed -n 's/^math_fingerprint=//p' "$log" | head -1)"
  echo "$name fingerprint=$fp" >> "$OUT_DIR/fingerprints.txt"
  values+=("$fp")
  echo "$name RESULT=PASS fingerprint=$fp" | tee -a "$OUT_DIR/results.txt"
  pass=$((pass + 1))
}

for entry in "${VARIANTS[@]}"; do
  name="${entry%%|*}"
  defs="${entry#*|}"
  check_one "variant-$name" "$defs" "-O3"
done

for opt in -O0 -O1 -O2 -O3 -Os; do
  check_one "gcc-${opt#-}" "" "$opt"
done

unique="$(printf '%s\n' "${values[@]}" | sort -u | wc -l | tr -d ' ')"
{
  echo "checks_total=$((pass + fail))"
  echo "checks_passed=$pass"
  echo "checks_failed=$fail"
  echo "unique_fingerprints=$unique"
  printf '  %s\n' "${values[@]}" | sort -u
} >> "$OUT_DIR/results.txt"

if [ "$fail" -ne 0 ]; then
  echo "math_fingerprint=FAIL failed=$fail (see $OUT_DIR/results.txt)"
  exit 4
fi
if [ "$unique" -ne 1 ] || [ "${values[0]}" != "$EXPECTED" ]; then
  echo "math_fingerprint=FAIL unique=$unique expected=$EXPECTED (see $OUT_DIR/fingerprints.txt)"
  exit 5
fi
echo "math_fingerprint=PASS checks=$pass unique=1 value=${values[0]}"
