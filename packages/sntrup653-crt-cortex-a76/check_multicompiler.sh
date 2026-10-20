#!/usr/bin/env bash
# check_multicompiler.sh — one source tree, many compilers, one answer.
#
# RANGE_AND_SECURITY.md states that the range reasoning behind the reducer is a
# model of the source schedule, not a machine-code proof.  A compiler is free to
# reassociate, vectorise or narrow an expression, so "the source is in range" is
# not by itself "the binary is in range".  The cheap empirical answer is to
# build the same core with every required compiler at every optimisation level
# and require the correctness hash to be bit-identical.
#
# This is coverage, not an equivalence proof; it is reported as such.
#
# usage: bash ./check_multicompiler.sh [out_dir]
# env:   CC_LIST   compilers to try        (default: "gcc clang")
#        OPT_LIST  optimisation levels     (default: "-O0 -O1 -O2 -O3 -Os")
#        HASH_TESTS  core hash smoke tests (default: 200)
#        REQUIRE_ALL_COMPILERS  fail if any requested compiler is absent (default: 1)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

OUT_DIR="${1:-$SCRIPT_DIR/results_multicompiler}"
CC_LIST="${CC_LIST:-gcc clang}"
OPT_LIST="${OPT_LIST:--O0 -O1 -O2 -O3 -Os}"
HASH_TESTS="${HASH_TESTS:-200}"
REQUIRE_ALL_COMPILERS="${REQUIRE_ALL_COMPILERS:-1}"
CPU_PIN="${CPU_PIN:-}"

BASE_CFLAGS="-std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU_TARGET -mtune=$CPU_TARGET -fomit-frame-pointer -fno-lto"
REQUIRE_NEON="-DSNTRUP653_REQUIRE_AARCH64_NEON=1"
OPTDEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1"

run_target() {
  if [ -n "$CPU_PIN" ]; then taskset -c "$CPU_PIN" "$@"; else "$@"; fi
}

mkdir -p "$OUT_DIR/bin"
: > "$OUT_DIR/multicompiler.log"
: > "$OUT_DIR/results.txt"
{
  echo "generated_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || true)"
  echo "cpu_target=$CPU_TARGET"
  echo "base_cflags=$BASE_CFLAGS"
  echo "hash_tests=$HASH_TESTS"
} > "$OUT_DIR/metadata.txt"

combos=0
missing=0
tested=0
declare -a hashes=()

for cc in $CC_LIST; do
  if ! command -v "$cc" >/dev/null 2>&1; then
    if [ "$REQUIRE_ALL_COMPILERS" = "1" ]; then
      echo "multicompiler=FAIL missing_compiler=$cc" | tee -a "$OUT_DIR/multicompiler.log"
      echo "install on Debian/Ubuntu: sudo apt-get update && sudo apt-get install -y clang" \
        | tee -a "$OUT_DIR/multicompiler.log"
      exit 2
    fi
    echo "skip: compiler '$cc' not installed" | tee -a "$OUT_DIR/multicompiler.log"
    missing=$((missing + 1))
    continue
  fi
  tested=$((tested + 1))
  declare -a cc_hashes=()
  for opt in $OPT_LIST; do
    tag="${cc}${opt}"
    bin="$OUT_DIR/bin/bench_$tag"
    echo "[build] $tag" | tee -a "$OUT_DIR/multicompiler.log"
    if ! "$cc" $BASE_CFLAGS "$opt" $REQUIRE_NEON $OPTDEFS \
        -DBENCH_IMPL_TAG=multicompiler -DBENCH_BACKEND_TAG="$tag" \
        shared/bench_core_mult.c shared/randombytes.c shared/sha2.c optimized/*.c \
        -Ioptimized -Ishared -o "$bin" >> "$OUT_DIR/multicompiler.log" 2>&1; then
      echo "FAIL $tag: compile failed (see multicompiler.log)" | tee -a "$OUT_DIR/multicompiler.log"
      exit 3
    fi
    line="$(run_target "$bin" 1 1 "$HASH_TESTS" 0 1 | tee -a "$OUT_DIR/multicompiler.log" | grep '^core_correctness_hash=' || true)"
    if [ -z "$line" ]; then
      echo "FAIL $tag: no core_correctness_hash in output" | tee -a "$OUT_DIR/multicompiler.log"
      exit 4
    fi
    hash="${line#core_correctness_hash=}"
    hashes+=("$hash")
    cc_hashes+=("$hash")
    combos=$((combos + 1))
    echo "combo=$tag hash=$hash" | tee -a "$OUT_DIR/results.txt"
  done
  cc_unique="$(printf '%s\n' "${cc_hashes[@]}" | sort -u | wc -l | tr -d ' ')"
  if [ "$cc_unique" -ne 1 ]; then
    echo "compiler=$cc RESULT=FAIL combos=${#cc_hashes[@]} unique_hashes=$cc_unique" \
      | tee -a "$OUT_DIR/multicompiler.log"
    exit 5
  fi
  echo "compiler=$cc RESULT=PASS combos=${#cc_hashes[@]} unique_hashes=1 hash=${cc_hashes[0]}" \
    | tee -a "$OUT_DIR/multicompiler.log"
done

if [ "$combos" -eq 0 ]; then
  echo "multicompiler=SKIP no compiler of { $CC_LIST } is installed"
  exit 0
fi

unique="$(printf '%s\n' "${hashes[@]}" | sort -u | wc -l | tr -d ' ')"
{
  echo "combos=$combos"
  echo "compilers_tested=$tested"
  echo "compilers_missing=$missing"
  echo "unique_hashes=$unique"
  echo "hashes:"
  printf '  %s\n' "${hashes[@]}" | sort -u
} | tee -a "$OUT_DIR/multicompiler.log"

if [ "$unique" -ne 1 ]; then
  echo "multicompiler=FAIL combos=$combos unique_hashes=$unique"
  exit 5
fi
echo "multicompiler=PASS compilers=$tested combos=$combos unique_hashes=1 hash=${hashes[0]}"
