#!/usr/bin/env bash
# Validation driver: native identity, contract, compiler matrix, variants,
# single-bit flips, and the sanitizer subset.
#
# usage: bash ./run_validation.sh [out_dir]
# env:
#   ALLOW_OVERWRITE=1  delete an existing non-empty out_dir instead of using a
#                      timestamped sibling directory
#   RUN_ASAN=0|1       override the platform default sanitizer-subset setting
set -uo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
. ./platform.conf

case "$CPU_MODEL" in
  Cortex-X1C) PLATFORM=X1C;;
  Cortex-A53) PLATFORM=A53;;
  Cortex-A76) PLATFORM=A76;;
  *) PLATFORM="$CPU_MODEL";;
esac

OUT="${1:-$ROOT/results/validation}"
if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT" 2>/dev/null || true)" ]; then
  if [ "${ALLOW_OVERWRITE:-0}" = "1" ]; then
    rm -rf "$OUT"
  else
    OUT="${OUT}_$(date -u +%Y%m%dT%H%M%SZ)"
    echo "notice: existing output kept; writing to $OUT" >&2
  fi
fi
mkdir -p "$OUT"
STEPS="$OUT/steps.txt"
FAILED="$OUT/FAILED.txt"
: > "$STEPS"
: > "$FAILED"
failed=0
notrun=0

run_step() {
  local name="$1"
  shift
  local rc=0
  echo "[validation] $name"
  "$@" > "$OUT/${name}.console.log" 2>&1 || rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "$name=PASS" >> "$STEPS"
  else
    echo "$name=FAIL rc=$rc" >> "$STEPS"
    echo "$name rc=$rc log=$OUT/${name}.console.log" >> "$FAILED"
    failed=1
  fi
  return "$rc"
}

# ---------------------------------------------------------------- native identity
host_rc=0
bash ./check_host.sh "$CPU_PIN" > "$OUT/host_check.txt" 2>&1 || host_rc=$?
cat "$OUT/host_check.txt"
if [ "$host_rc" -ne 0 ]; then
  echo "host_check=FAIL rc=$host_rc" >> "$STEPS"
  echo "host_check rc=$host_rc log=$OUT/host_check.txt" >> "$FAILED"
  failed=1
  EXECUTION=UNVERIFIED
else
  echo "host_check=PASS" >> "$STEPS"
  EXECUTION=native
fi

CASES="${VALIDATION_CASES:-100000}"
CORE_CASES="${VALIDATION_CORE_CASES:-1000}"
KEM_CASES="${VALIDATION_KEM_CASES:-50}"
FLIP_BATCHES="${VALIDATION_FLIP_BATCHES:-4}"
RUN_ASAN="${RUN_ASAN:-${ASAN_SUPPORTED:-1}}"

board_model="$(tr -d '\000' < /proc/device-tree/model 2>/dev/null || true)"
midr_file="/sys/devices/system/cpu/cpu${CPU_PIN}/regs/identification/midr_el1"
midr="$(tr '[:upper:]' '[:lower:]' < "$midr_file" 2>/dev/null | tr -d ' \n' || true)"
{
  echo "platform=$PLATFORM"
  echo "cpu_model=$CPU_MODEL"
  echo "cpu_target=$CPU_TARGET"
  echo "cpu_part=$CPU_PART"
  echo "cpu_pin=$CPU_PIN"
  echo "board_model=$board_model"
  echo "hostname=$(uname -n 2>/dev/null || true)"
  echo "kernel=$(uname -r 2>/dev/null || true)"
  echo "midr=${midr:-unknown}"
  echo "execution=$EXECUTION"
  echo "cases=$CASES"
  echo "core_cases=$CORE_CASES"
  echo "kem_cases=$KEM_CASES"
  echo "flip_batches=$FLIP_BATCHES"
  echo "run_asan=$RUN_ASAN"
  echo "has_asan_support=${ASAN_SUPPORTED:-1}"
  echo "compiler_matrix=gcc+clang,O0-O3/Os"
  echo "variants=10"
  echo "source_tree_sha256=$(bash ./source_tree_sha256.sh)"
  echo "core_sha256=$(sha256sum optimized/crypto_core_multsntrup653.c | cut -d' ' -f1)"
  echo "generated_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || true)"
} > "$OUT/metadata.txt"

# ---------------------------------------------------------------- required checks
run_step contract bash validation/scripts/run_contract.sh "$PLATFORM" "$CASES" "$OUT/contract"
run_step compiler_hash bash validation/scripts/run_hash_matrix.sh "$PLATFORM" "$OUT/compiler/hash"
run_step compiler_core bash validation/scripts/run_core_matrix.sh "$PLATFORM" "$CORE_CASES" "$OUT/compiler/core"
run_step compiler_math bash validation/scripts/run_math_matrix.sh "$PLATFORM" "$OUT/compiler/math"
run_step compiler_kem bash validation/scripts/run_kem_matrix.sh "$PLATFORM" "$KEM_CASES" "$OUT/compiler/kem"

run_step variants env VAR_CORE_TESTS=2000 VAR_INTERNAL_TESTS=300 VAR_HASH_TESTS=200 VAR_SEED=1 \
  bash ./check_build_variants.sh "$OUT/variants"
run_step variant_math env CC=gcc bash ./check_math_fingerprint.sh "$OUT/variant_math"

run_step single_bit_flip bash validation/scripts/run_single_bit_flip.sh "$PLATFORM" "$FLIP_BATCHES" "$OUT/single_bit_flip"

# ---------------------------------------------------------------- sanitizer subset
ASAN_STATUS=NOT_RUN
if [ "$RUN_ASAN" = "1" ]; then
  if run_step single_bit_flip_asan env SANFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
       bash validation/scripts/run_single_bit_flip.sh "$PLATFORM" 1 "$OUT/single_bit_flip_asan"; then
    ASAN_STATUS=PASS
  else
    ASAN_STATUS=FAIL
  fi
else
  echo "single_bit_flip_asan=NOT_RUN reason=RUN_ASAN=0" >> "$STEPS"
  echo "single_bit_flip_asan NOT_RUN: RUN_ASAN=0; the original campaign sanitizer result is separate and is not a result from this harness" >> "$FAILED"
  ASAN_STATUS=NOT_RUN
  notrun=1
fi

# ---------------------------------------------------------------- summarise
if ! python3 validation/scripts/summarize.py "$OUT" >> "$OUT/summarize.console.log" 2>&1; then
  echo "summarize=FAIL" >> "$STEPS"
  echo "summarize FAIL log=$OUT/summarize.console.log" >> "$FAILED"
  failed=1
fi

if [ "$failed" -ne 0 ]; then
  OVERALL=FAIL
elif [ "$ASAN_STATUS" != "PASS" ] || [ "$notrun" -ne 0 ]; then
  OVERALL=PARTIAL
else
  OVERALL=PASS
fi
{
  echo "validation=$OVERALL"
  echo "asan_subset=$ASAN_STATUS"
  echo "source_tree_sha256=$(bash ./source_tree_sha256.sh)"
} > "$OUT/status.txt"
cat "$OUT/status.txt"
echo "validation=$OVERALL out=$OUT"
[ "$OVERALL" = "FAIL" ] && exit 1
exit 0
