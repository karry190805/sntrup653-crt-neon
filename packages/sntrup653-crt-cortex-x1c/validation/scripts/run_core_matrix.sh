#!/usr/bin/env bash
# Core equivalence for every compiler/optimisation combination.
# usage: run_core_matrix.sh <x1c|a53|a76> [cases] [out_dir]
set -euo pipefail
PLATFORM="${1:-x1c}"
CASES="${2:-1000}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${3:-$ROOT/results/core_matrix_$PLATFORM}"
SRC="$ROOT"
. "$ROOT/platform.conf"
CPU="$CPU_TARGET"
rm -rf "$OUT"; mkdir -p "$OUT"
CC="${CC:-gcc}"
BASE_CFLAGS="-std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU -mtune=$CPU -fomit-frame-pointer -fno-lto"
OPTDEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"
run_target() { "$@"; }

# shared baseline core/codecs
"$CC" $BASE_CFLAGS -I"$SRC/baseline" \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=baseline_core \
  -c "$SRC/baseline/crypto_core_multsntrup653.c" -o "$OUT/baseline_core.o" > "$OUT/baseline.build.log" 2>&1
"$CC" $BASE_CFLAGS -I"$SRC/baseline" -c "$SRC/baseline/crypto_encode_653xint16.c" -o "$OUT/encode.o" >> "$OUT/baseline.build.log" 2>&1
"$CC" $BASE_CFLAGS -I"$SRC/baseline" -c "$SRC/baseline/crypto_decode_653xint16.c" -o "$OUT/decode.o" >> "$OUT/baseline.build.log" 2>&1

fail=0
for cc in gcc clang; do
  command -v "$cc" >/dev/null 2>&1 || { echo "$cc missing" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue; }
  for opt in -O0 -O1 -O2 -O3 -Os; do
    tag="${cc}${opt}"
    core="$OUT/core_${tag}.o"
    bin="$OUT/equiv_${tag}"
    if ! "$cc" $BASE_CFLAGS "$opt" $OPTDEFS -I"$SRC/optimized" \
        -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=optimized_core \
        -c "$SRC/optimized/crypto_core_multsntrup653.c" -o "$core" > "$OUT/${tag}.build.log" 2>&1; then
      echo "combo=$tag RESULT=FAIL build" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue
    fi
    if ! "$cc" $BASE_CFLAGS -I"$SRC/baseline" -I"$SRC/shared" \
        -DBASELINE_CRYPTO_CORE_MULT=baseline_core \
        -DOPTIMIZED_CRYPTO_CORE_MULT=optimized_core \
        "$SRC/shared/test_core_equiv.c" "$OUT/baseline_core.o" "$core" \
        "$OUT/encode.o" "$OUT/decode.o" -o "$bin" >> "$OUT/${tag}.build.log" 2>&1; then
      echo "combo=$tag RESULT=FAIL link" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue
    fi
    if run_target "$bin" "$CASES" 1 > "$OUT/${tag}.run.log" 2>&1; then
      echo "combo=$tag RESULT=PASS cases=$CASES" | tee -a "$OUT/results.txt"
    else
      echo "combo=$tag RESULT=FAIL run" | tee -a "$OUT/results.txt"; fail=$((fail+1))
    fi
  done
done
if [ "$fail" -ne 0 ]; then echo "core_matrix=FAIL failed=$fail"; exit 1; fi
echo "core_matrix=PASS combos=10 cases=$CASES"
