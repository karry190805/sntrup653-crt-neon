#!/usr/bin/env bash
# Exhaustive ciphertext single-bit-flip comparison, baseline vs optimized.
# usage: run_single_bit_flip.sh <x1c|a53|a76> [batches] [out_dir]
set -euo pipefail
PLATFORM="${1:-x1c}"
BATCHES="${2:-2}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${3:-$ROOT/results/single_bit_flip_$PLATFORM}"
SRC="$ROOT"
. "$ROOT/platform.conf"
CPU="$CPU_TARGET"
rm -rf "$OUT"; mkdir -p "$OUT"
CC="${CC:-gcc}"
SANFLAGS="${SANFLAGS:-}"
BASE_CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU -mtune=$CPU -fomit-frame-pointer -fno-lto"
OPTDEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"
run_target() { "$@"; }

build_one() {
  local impl="$1" out="$2" defs="$3"
  local -a srcs=()
  local f
  for f in "$SRC/$impl"/*.c; do srcs+=("$f"); done
  "$CC" $BASE_CFLAGS $SANFLAGS $defs -I"$SRC/$impl" -I"$SRC/shared" \
    "$ROOT/validation/src/test_single_bit_flip.c" "$SRC/shared/randombytes.c" "$SRC/shared/sha2.c" \
    "${srcs[@]}" -o "$out" > "$OUT/${impl}.build.log" 2>&1
}

build_one baseline "$OUT/flip_baseline" ""
build_one optimized "$OUT/flip_optimized" "$OPTDEFS"

ASAN_OPTIONS=detect_leaks=0 run_target "$OUT/flip_baseline" "$OUT/baseline.bin" "$BATCHES" 20261103 > "$OUT/baseline.run.log" 2>&1
ASAN_OPTIONS=detect_leaks=0 run_target "$OUT/flip_optimized" "$OUT/optimized.bin" "$BATCHES" 20261103 > "$OUT/optimized.run.log" 2>&1
cat "$OUT/baseline.run.log" "$OUT/optimized.run.log" | tee "$OUT/summary.txt"

if cmp -s "$OUT/baseline.bin" "$OUT/optimized.bin"; then
  echo "single_bit_flip=PASS batches=$BATCHES transcript_identical=1" | tee -a "$OUT/summary.txt"
else
  echo "single_bit_flip=FAIL transcript_mismatch" | tee -a "$OUT/summary.txt"
  cmp -l "$OUT/baseline.bin" "$OUT/optimized.bin" | head -10 || true
  exit 1
fi
sha256sum "$OUT/baseline.bin" "$OUT/optimized.bin" | tee -a "$OUT/summary.txt"
