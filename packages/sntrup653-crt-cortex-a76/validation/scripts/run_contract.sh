#!/usr/bin/env bash
# Build and run the contract/range validation for one target.
# usage: run_contract.sh <x1c|a53|a76> [cases] [out_dir]
set -euo pipefail

PLATFORM="${1:-x1c}"
CASES="${2:-100000}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${3:-$ROOT/results/contract_$PLATFORM}"
SRC="$ROOT"
. "$ROOT/platform.conf"
CPU="$CPU_TARGET"
CC="${CC:-gcc}"
if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT" 2>/dev/null || true)" ]; then
  if [ "${ALLOW_OVERWRITE:-0}" = "1" ]; then
    rm -rf "$OUT"
  else
    OUT="${OUT}_$(date -u +%Y%m%dT%H%M%SZ)"
    echo "notice: existing output kept; writing to $OUT" >&2
  fi
fi
BUILD="$OUT/build"
mkdir -p "$BUILD"


CFLAGS="-O3 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU -mtune=$CPU -fomit-frame-pointer -fno-lto -DSNTRUP653_REQUIRE_AARCH64_NEON=1"
OPTFLAGS="-funroll-loops -fvect-cost-model=unlimited -fno-guess-branch-probability -fno-tree-slp-vectorize -falign-functions=32 -fmodulo-sched -flive-range-shrinkage"
OPTDEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"

echo "[contract] platform=$PLATFORM cases=$CASES cpu=$CPU cc=$CC" | tee "$OUT/run.log"
"$CC" --version | head -2 | tee -a "$OUT/run.log"
{
  echo "platform=$PLATFORM"
  echo "cpu=$CPU"
  echo "cases=$CASES"
  echo "cflags=$CFLAGS"
  echo "optflags=$OPTFLAGS"
  echo "optdefs=$OPTDEFS"
  echo "source=$SRC"
  echo "cc=$("$CC" --version | head -1)"
} > "$OUT/metadata.txt"

compile() { "$CC" "$@" >>"$OUT/run.log" 2>&1; }

compile $CFLAGS -I"$SRC/baseline" \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=baseline_mult \
  -c "$SRC/baseline/crypto_core_multsntrup653.c" -o "$BUILD/baseline_mult.o"
compile $CFLAGS $OPTFLAGS $OPTDEFS -I"$SRC/optimized" \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=optimized_mult \
  -c "$SRC/optimized/crypto_core_multsntrup653.c" -o "$BUILD/optimized_mult.o"
compile $CFLAGS -I"$SRC/baseline" \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_mult3sntrup653=baseline_mult3 \
  -c "$SRC/baseline/crypto_core_mult3sntrup653.c" -o "$BUILD/baseline_mult3.o"
compile $CFLAGS $OPTFLAGS $OPTDEFS -I"$SRC/optimized" \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_mult3sntrup653=optimized_mult3 \
  -c "$SRC/optimized/crypto_core_mult3sntrup653.c" -o "$BUILD/optimized_mult3.o"
compile $CFLAGS -I"$SRC/baseline" \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_scale3sntrup653=baseline_scale3 \
  -c "$SRC/baseline/crypto_core_scale3sntrup653.c" -o "$BUILD/baseline_scale3.o"
compile $CFLAGS $OPTFLAGS $OPTDEFS -I"$SRC/optimized" \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_scale3sntrup653=optimized_scale3 \
  -c "$SRC/optimized/crypto_core_scale3sntrup653.c" -o "$BUILD/optimized_scale3.o"
compile $CFLAGS -I"$SRC/baseline" -c "$SRC/baseline/crypto_encode_653xint16.c" -o "$BUILD/encode.o"
compile $CFLAGS -I"$SRC/baseline" -c "$SRC/baseline/crypto_decode_653xint16.c" -o "$BUILD/decode.o"
compile $CFLAGS -I"$SRC/baseline" -I"$SRC/shared" \
  "$ROOT/validation/src/test_contract.c" \
  "$BUILD/baseline_mult.o" "$BUILD/optimized_mult.o" \
  "$BUILD/baseline_mult3.o" "$BUILD/optimized_mult3.o" \
  "$BUILD/baseline_scale3.o" "$BUILD/optimized_scale3.o" \
  "$BUILD/encode.o" "$BUILD/decode.o" -o "$BUILD/test_contract"

"$BUILD/test_contract" "$CASES" 20261103 2>&1 | tee "$OUT/output.txt"
