#!/usr/bin/env bash
# run_ablation.sh — one-factor-at-a-time ablation of the optimization switches.
#
# Every arm is the delivered core with exactly one switch turned off, plus the
# untouched reference core. Comparing arm against the delivered configuration
# isolates the contribution of that single switch, phase by phase.
#
#   all         delivered configuration (all switches on)   <- reference
#   baseline    reference PQClean core, no switches at all
#   no_inplace  SNTRUP653_USE_INPLACE_BUFFERS=0
#   no_zs       SNTRUP653_PUBLIC_ZS_MODE=0
#   no_low73    SNTRUP653_LOW73_NEON=0
#   no_fold     SNTRUP653_FINAL_FOLD_NEON=0
#   no_barrett  USE_BARRETT_REDUCE=0        (opt-in, see EXTRA_ARMS)
#
# Each arm runs twice, once in the forward epoch and once in the mirrored epoch,
# so a linear drift cannot be mistaken for an optimization effect. All arms use
# the same seed, so round i is the same input everywhere and the ratios are
# input-paired.
#
# usage: ./run_ablation.sh [rounds] [warmup] [seed] [order]
# env:   CPU_PIN, CORE_ROUNDS, CORE_LOOPS, EQUIV_TESTS, EXTRA_ARMS, RESULT_DIR
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

ROUNDS="${1:-${ABLATION_ROUNDS:-600}}"
WARMUP="${2:-${ABLATION_WARMUP:-100}}"
SEED="${3:-1}"
CPU_PIN="${CPU_PIN:-}"
CORE_ROUNDS="${CORE_ROUNDS:-200}"
CORE_LOOPS="${CORE_LOOPS:-1000}"
EQUIV_TESTS="${EQUIV_TESTS:-1000}"
EXTRA_ARMS="${EXTRA_ARMS:-}"
stamp="$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || date +%Y%m%dT%H%M%S)"
ABL_ROOT="${RESULT_DIR:-$SCRIPT_DIR/results/ablation_$stamp}"

run_pinned() {
  if [ -n "$CPU_PIN" ]; then
    taskset -c "$CPU_PIN" "$@"
  else
    "$@"
  fi
}

for value in "$ROUNDS" "$WARMUP" "$SEED" "$CORE_ROUNDS" "$CORE_LOOPS" "$EQUIV_TESTS"; do
  case "$value" in *[!0-9]*|'') echo "error: numeric arguments must be unsigned integers" >&2; exit 2 ;; esac
done
[ "$ROUNDS" -gt 0 ] && [ "$CORE_LOOPS" -gt 0 ] || { echo "error: ROUNDS and CORE_LOOPS must be positive" >&2; exit 2; }

# ---------------------------------------------------------------- arms
BUILD_ARMS=(no_inplace no_zs no_low73 no_fold)
case ",${EXTRA_ARMS}," in
  *,no_barrett,*) BUILD_ARMS+=(no_barrett) ;;
esac

if [ "$#" -ge 4 ]; then
  ORDER="$4"
else
  forward=""
  for arm in "${BUILD_ARMS[@]}"; do forward="${forward}${forward:+,}${arm}"; done
  backward=""
  for (( i=${#BUILD_ARMS[@]}-1; i>=0; i-- )); do backward="${backward}${backward:+,}${BUILD_ARMS[$i]}"; done
  ORDER="all,baseline,${forward},${backward},baseline,all"
fi

arm_build_env() {
  case "$1" in
    all)        ;;
    no_inplace) printf '%s\n' "SNTRUP653_USE_INPLACE_BUFFERS=0" ;;
    no_zs)      printf '%s\n' "SNTRUP653_PUBLIC_ZS_MODE=0" ;;
    no_low73)   printf '%s\n' "SNTRUP653_LOW73_NEON=0" ;;
    no_fold)    printf '%s\n' "SNTRUP653_FINAL_FOLD_NEON=0" ;;
    no_barrett) printf '%s\n' "USE_BARRETT_REDUCE=0" ;;
    *) return 1 ;;
  esac
}

arm_exe() {
  case "$1" in
    baseline) printf '%s' "$ABL_ROOT/build_all/bench_$2_baseline" ;;
    all)      printf '%s' "$ABL_ROOT/build_all/bench_$2_optimized" ;;
    *)        printf '%s' "$ABL_ROOT/build_$1/bench_$2_optimized" ;;
  esac
}

mkdir -p "$ABL_ROOT"
trap 'rc=$?; echo "ablation=FAIL rc=$rc result_dir=$ABL_ROOT order=$ORDER seed=$SEED" | tee -a "$ABL_ROOT/FAILED.txt" >/dev/null' ERR

echo "[build] delivered configuration"
env BUILD_DIR="$ABL_ROOT/build_all" BUILD_TARGETS=bench ./build.sh > "$ABL_ROOT/build_all.log" 2>&1
cp "$ABL_ROOT/build_all/build_metadata.txt" "$ABL_ROOT/metadata_all.txt"
{
  echo "arm=baseline"
  echo "note=reference PQClean core; the optimization switches do not apply"
  echo "core_sha256=$(sha256sum baseline/crypto_core_multsntrup653.c | cut -d' ' -f1)"
} > "$ABL_ROOT/metadata_baseline.txt"

for arm in "${BUILD_ARMS[@]}"; do
  echo "[build] $arm"
  override="$(arm_build_env "$arm")"
  # shellcheck disable=SC2086
  env $override BUILD_DIR="$ABL_ROOT/build_$arm" BUILD_TARGETS=bench ./build.sh \
    > "$ABL_ROOT/build_$arm.log" 2>&1
  cp "$ABL_ROOT/build_$arm/build_metadata.txt" "$ABL_ROOT/metadata_$arm.txt"
done

# ---------------------------------------------------------------- run
expected_hash=""
idx=0
IFS=',' read -r -a items <<< "$ORDER"
for item in "${items[@]}"; do
  arm="$(printf '%s' "$item" | tr -d '[:space:]')"
  exe_core="$(arm_exe "$arm" core)" || { echo "error: unknown arm '$arm'" >&2; exit 2; }
  exe_kem="$(arm_exe "$arm" kem)"
  [ -x "$exe_core" ] && [ -x "$exe_kem" ] || { echo "error: missing binary for arm '$arm'" >&2; exit 3; }
  idx=$((idx + 1))
  n="$(printf '%02d' "$idx")"

  core_log="$ABL_ROOT/core_${n}_${arm}.log"
  core_raw="$ABL_ROOT/core_${n}_${arm}_raw.csv"
  SNTRUP_BENCH_CSV="$core_raw" run_pinned "$exe_core" \
    "$CORE_ROUNDS" "$CORE_LOOPS" "$EQUIV_TESTS" "$CORE_LOOPS" "$SEED" | tee "$core_log"
  hash="$(sed -n 's/^core_correctness_hash=//p' "$core_log" | tail -1)"
  [ -n "$hash" ] || { echo "error: missing core_correctness_hash in $core_log" >&2; exit 4; }
  if [ -z "$expected_hash" ]; then expected_hash="$hash"; fi
  [ "$hash" = "$expected_hash" ] || {
    echo "error: arm '$arm' changed the core output; the ablation is invalid" >&2; exit 5; }

  kem_log="$ABL_ROOT/kem_${n}_${arm}.log"
  kem_raw="$ABL_ROOT/kem_${n}_${arm}_raw.csv"
  SNTRUP_BENCH_CSV="$kem_raw" run_pinned "$exe_kem" "$ROUNDS" "$WARMUP" "$SEED" | tee "$kem_log"
  grep -q 'KEM correctness: PASS' "$kem_log" || { echo "error: KEM mismatch in arm '$arm'" >&2; exit 6; }
  echo "[run $n] arm=$arm core_hash=$hash"
done

echo "[check] every arm produced core_correctness_hash=$expected_hash"

if command -v python3 >/dev/null 2>&1; then
  python3 ./summarize.py ablation "$ABL_ROOT/summary.csv" "$ABL_ROOT" | tee "$ABL_ROOT/summary.txt"
else
  echo "warning: python3 unavailable; raw CSV retained but summary not generated" >&2
fi
cp "$ABL_ROOT/build_all/build_metadata.txt" "$ABL_ROOT/build_metadata.txt" 2>/dev/null || true
echo "ablation=PASS result_dir=$ABL_ROOT core_correctness_hash=$expected_hash"
