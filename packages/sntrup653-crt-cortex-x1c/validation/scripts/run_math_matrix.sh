#!/usr/bin/env bash
# Math-fingerprint coverage for every compiler/optimisation combination.
# usage: run_math_matrix.sh <x1c|a53|a76> [out_dir]
set -euo pipefail
PLATFORM="${1:-x1c}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${2:-$ROOT/results/math_matrix_$PLATFORM}"
SRC="$ROOT"
. "$ROOT/platform.conf"
CPU="$CPU_TARGET"
EXPECTED="$(sed -n 's/^math_fingerprint=//p' "$SRC/expected_math_fingerprint.txt" | head -1)"
rm -rf "$OUT"; mkdir -p "$OUT"
BASE_CFLAGS="-std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU -mtune=$CPU -fomit-frame-pointer -fno-lto"
OPTDEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"
run_target() { "$@"; }

fail=0
for cc in gcc clang; do
  command -v "$cc" >/dev/null 2>&1 || { echo "$cc missing" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue; }
  for opt in -O0 -O1 -O2 -O3 -Os; do
    tag="${cc}${opt}"
    core="$OUT/core_${tag}.o"
    bin="$OUT/math_${tag}"
    log="$OUT/${tag}.log"
    if ! "$cc" $BASE_CFLAGS "$opt" $OPTDEFS -DSNTRUP653_INTERNAL_TEST_HOOKS=1 -I"$SRC/optimized" \
        -c "$SRC/optimized/crypto_core_multsntrup653.c" -o "$core" > "$log" 2>&1; then
      echo "combo=$tag RESULT=FAIL build_core" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue
    fi
    if ! "$cc" $BASE_CFLAGS "$opt" $OPTDEFS -I"$SRC/optimized" -I"$SRC/shared" \
        "$SRC/shared/test_math_fingerprint.c" "$SRC/optimized/crypto_encode_653xint16.c" \
        "$SRC/optimized/crypto_decode_653xint16.c" "$core" -o "$bin" >> "$log" 2>&1; then
      echo "combo=$tag RESULT=FAIL build_test" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue
    fi
    fp="$(run_target "$bin" "$EXPECTED" 2>&1 | sed -n 's/^math_fingerprint=//p' | head -1 || true)"
    if [ "$fp" = "$EXPECTED" ]; then
      echo "combo=$tag RESULT=PASS fingerprint=$fp" | tee -a "$OUT/results.txt"
    else
      echo "combo=$tag RESULT=FAIL fingerprint=${fp:-none} expected=$EXPECTED" | tee -a "$OUT/results.txt"; fail=$((fail+1))
    fi
  done
done
if [ "$fail" -ne 0 ]; then echo "math_matrix=FAIL failed=$fail"; exit 1; fi
echo "math_matrix=PASS combos=10 unique_fingerprint=$EXPECTED"
