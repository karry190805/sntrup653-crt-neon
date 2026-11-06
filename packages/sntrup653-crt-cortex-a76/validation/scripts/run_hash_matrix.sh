#!/usr/bin/env bash
# core_correctness_hash for every compiler/optimisation combination.
# usage: run_hash_matrix.sh <x1c|a53|a76> [out_dir]
set -euo pipefail
PLATFORM="${1:-x1c}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${2:-$ROOT/results/hash_matrix_$PLATFORM}"
SRC="$ROOT"
. "$ROOT/platform.conf"
CPU="$CPU_TARGET"
rm -rf "$OUT"; mkdir -p "$OUT"
BASE_CFLAGS="-O3 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU -mtune=$CPU -fomit-frame-pointer -fno-lto"
OPTDEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"
run_target() { "$@"; }

fail=0
for cc in gcc clang; do
  command -v "$cc" >/dev/null 2>&1 || { echo "$cc missing" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue; }
  for opt in -O0 -O1 -O2 -O3 -Os; do
    tag="${cc}${opt}"
    bin="$OUT/bench_${tag}"
    log="$OUT/${tag}.log"
    srcs=()
    for f in "$SRC"/optimized/*.c; do srcs+=("$f"); done
    if ! "$cc" $BASE_CFLAGS "$opt" $OPTDEFS -I"$SRC/optimized" -I"$SRC/shared" \
        -DBENCH_IMPL_TAG="$tag" -DBENCH_BACKEND_TAG="$tag" \
        "$SRC/shared/bench_core_mult.c" "$SRC/shared/randombytes.c" "$SRC/shared/sha2.c" \
        "${srcs[@]}" -o "$bin" > "$log" 2>&1; then
      echo "combo=$tag RESULT=FAIL build" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue
    fi
    out="$(run_target "$bin" 1 1 200 0 1 2>&1 | tee -a "$log" || true)"
    hash="$(printf '%s\n' "$out" | sed -n 's/^core_correctness_hash=//p' | head -1)"
    if [ -n "$hash" ]; then
      echo "combo=$tag RESULT=PASS core_correctness_hash=$hash" | tee -a "$OUT/results.txt"
    else
      echo "combo=$tag RESULT=FAIL run" | tee -a "$OUT/results.txt"; fail=$((fail+1))
    fi
  done
done
if [ "$fail" -ne 0 ]; then echo "hash_matrix=FAIL failed=$fail"; exit 1; fi
echo "hash_matrix=PASS combos=10"
