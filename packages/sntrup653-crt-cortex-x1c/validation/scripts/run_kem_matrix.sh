#!/usr/bin/env bash
# KEM transcript coverage for every compiler/optimisation combination.
# usage: run_kem_matrix.sh <x1c|a53|a76> [cases] [out_dir]
set -euo pipefail

PLATFORM="${1:-x1c}"
CASES="${2:-50}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${3:-$ROOT/results/kem_matrix_$PLATFORM}"
SRC="$ROOT"
. "$ROOT/platform.conf"
CPU="$CPU_TARGET"
rm -rf "$OUT"
mkdir -p "$OUT"

BASE_CFLAGS="-std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU -mtune=$CPU -fomit-frame-pointer -fno-lto"
OPTDEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"

run_target() { "$@"; }

build_kem() {
  local impl="$1" out="$2" cc="$3" opt="$4"
  local inc="-I$SRC/$impl -I$SRC/shared"
  local -a srcs=()
  local f
  for f in "$SRC/$impl"/*.c; do
    srcs+=("$f")
  done
  "$cc" $BASE_CFLAGS "$opt" $([ "$impl" = optimized ] && echo "$OPTDEFS") $inc \
    "$SRC/shared/kem_transcript.c" "$SRC/shared/randombytes.c" "$SRC/shared/sha2.c" \
    "${srcs[@]}" -o "$out" > "$OUT/${impl}_${cc}_${opt#-}.build.log" 2>&1
}

# One baseline transcript per platform; the baseline source is compiler-independent.
BASE_BIN="$OUT/baseline_transcript"
build_kem baseline "$BASE_BIN" gcc -O2
run_target "$BASE_BIN" "$OUT/baseline.bin" "$CASES" 1 > "$OUT/baseline.run.log" 2>&1
BASE_SHA=$(sha256sum "$OUT/baseline.bin" | cut -d' ' -f1)
echo "baseline_sha256=$BASE_SHA" | tee "$OUT/results.txt"

fail=0
for cc in gcc clang; do
  command -v "$cc" >/dev/null 2>&1 || { echo "$cc missing" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue; }
  for opt in -O0 -O1 -O2 -O3 -Os; do
    tag="${cc}${opt}"
    bin="$OUT/kem_${tag}"
    tbin="$OUT/transcript_${tag}.bin"
    if ! build_kem optimized "$bin" "$cc" "$opt"; then
      echo "combo=$tag RESULT=FAIL build" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue
    fi
    if ! run_target "$bin" "$tbin" "$CASES" 1 > "$OUT/${tag}.run.log" 2>&1; then
      echo "combo=$tag RESULT=FAIL run" | tee -a "$OUT/results.txt"; fail=$((fail+1)); continue
    fi
    sha=$(sha256sum "$tbin" | cut -d' ' -f1)
    if [ "$sha" = "$BASE_SHA" ]; then
      echo "combo=$tag RESULT=PASS transcript_sha256=$sha" | tee -a "$OUT/results.txt"
    else
      echo "combo=$tag RESULT=FAIL transcript_sha256=$sha expected=$BASE_SHA" | tee -a "$OUT/results.txt"; fail=$((fail+1))
    fi
  done
done

if [ "$fail" -ne 0 ]; then
  echo "kem_matrix=FAIL failed=$fail"
  exit 1
fi
echo "kem_matrix=PASS combos=10 unique_transcript=1 sha256=$BASE_SHA"
