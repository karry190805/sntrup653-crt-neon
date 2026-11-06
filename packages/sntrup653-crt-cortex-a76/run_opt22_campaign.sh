#!/usr/bin/env bash
# run_opt22_campaign.sh — final opt22 validation campaign for one platform.
#
# usage: CPU_PIN=<n> OUT=/abs/path/results bash ./run_opt22_campaign.sh
#
# Runs, in order:
#   1. frequency before
#   2. strict same-flags build
#   3. correctness 100k / polymul / KEM
#   4. real PMU counters
#   5. diagnostic stage table (gcc)
#   6. frequency after
#
# The scripts are expected to live in the same directory as this file.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

CPU_PIN="${CPU_PIN:?set CPU_PIN to the logical ${CPU_MODEL} CPU number}"
OUT="${OUT:-$SCRIPT_DIR/results/opt22_campaign}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build_opt22}"
CORRECTNESS_TESTS="${CORRECTNESS_TESTS:-100000}"
KEM_CASES="${KEM_CASES:-1000}"
POLYMUL_ROUNDS="${POLYMUL_ROUNDS:-100}"
POLYMUL_LOOPS="${POLYMUL_LOOPS:-2000}"
SEED="${SEED:-20260901}"
STAGE_DIR="${STAGE_DIR:-$OUT/stage}"

mkdir -p "$OUT" "$STAGE_DIR"
stamp="$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || date +%Y%m%dT%H%M%S)"
export CPU_PIN BUILD_DIR

# Export the switch so every nested build.sh call (run_correctness.sh invokes
# build.sh itself) keeps the baseline and optimized arms on one flag set.
export STRICT_SAME_FLAGS=1

freq_snapshot() {
  local dest="$1"
  {
    echo "timestamp_utc=$stamp"
    echo "platform=$CPU_MODEL pin=$CPU_PIN"
    for f in scaling_governor scaling_min_freq scaling_max_freq scaling_cur_freq; do
      echo "$f=$(cat "/sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/$f" 2>/dev/null || echo unavailable)"
    done
  } > "$dest"
}
freq_snapshot "$OUT/frequency_before.txt"

echo "[campaign] strict same-flags build"
STRICT_SAME_FLAGS=1 CPU_PIN="$CPU_PIN" BUILD_DIR="$BUILD_DIR" BUILD_TARGETS=all bash ./build.sh
cp "$BUILD_DIR/build_metadata.txt" "$OUT/build_metadata.txt"
cp "$BUILD_DIR/flags_baseline_optimized.txt" "$OUT/flags_baseline_optimized.txt"
cp "$BUILD_DIR/compiler_macros.txt" "$OUT/compiler_macros.txt" 2>/dev/null || true

echo "[campaign] correctness ($CORRECTNESS_TESTS core cases, $KEM_CASES KEM cases)"
RESULT_DIR="$OUT/correctness" CPU_PIN="$CPU_PIN" BUILD_DIR="$BUILD_DIR" \
  bash ./run_correctness.sh "$CORRECTNESS_TESTS" "$KEM_CASES" "$SEED"

echo "[campaign] polymul A/B"
RESULT_DIR="$OUT/polymul" CPU_PIN="$CPU_PIN" BUILD_DIR="$BUILD_DIR" \
  bash ./run_polymul.sh "$POLYMUL_ROUNDS" "$POLYMUL_LOOPS" 1000 200 "$SEED" baseline,optimized,optimized,baseline

echo "[campaign] KEM A/B"
RESULT_DIR="$OUT/kem" CPU_PIN="$CPU_PIN" BUILD_DIR="$BUILD_DIR" \
  bash ./run_kem.sh 1000 100 baseline,optimized,optimized,baseline "$SEED"

echo "[campaign] PMU counters"
PMU_OUT="$OUT/pmu" CPU_PIN="$CPU_PIN" BUILD_DIR="$BUILD_DIR" bash ./collect_pmu.sh

echo "[campaign] security: straight-line probes, sanitizers, timing screen"
SECURITY_DIR="$OUT/security" CPU_PIN="$CPU_PIN" BUILD_DIR="$BUILD_DIR" \
  bash ./run_security.sh

echo "[campaign] diagnostic stage profile (gcc)"
BUILD_DIR="$BUILD_DIR" bash ./build_stage_profile.sh
mkdir -p "$STAGE_DIR"
taskset -c "$CPU_PIN" "$BUILD_DIR/bench_stage_profile" 40 2000 "$SEED" "$STAGE_DIR/stage_profile_gcc.csv" \
  | tee "$STAGE_DIR/stage_profile_gcc.txt"

if command -v clang >/dev/null 2>&1; then
  echo "[campaign] diagnostic stage profile (clang)"
  CC=clang BUILD_DIR="$BUILD_DIR/clang" bash ./build_stage_profile.sh
  taskset -c "$CPU_PIN" "$BUILD_DIR/clang/bench_stage_profile" 40 2000 "$SEED" "$STAGE_DIR/stage_profile_clang.csv" \
    | tee "$STAGE_DIR/stage_profile_clang.txt"
else
  echo "[campaign] clang not present; stage_table_clang.md will record that"
fi

freq_snapshot "$OUT/frequency_after.txt"
if [ "${RUN_VALIDATION:-0}" = "1" ]; then
  echo "[campaign] validation rerun (contract, compiler matrix, variants, bit flips)"
  bash ./run_validation.sh "$OUT/validation"
fi

echo "campaign_out=$OUT"
