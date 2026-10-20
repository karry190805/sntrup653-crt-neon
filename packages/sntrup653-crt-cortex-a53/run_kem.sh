#!/usr/bin/env bash
# run_kem.sh — paired A/B of the full KEM for every operation separately.
#
# Reports, per phase, speedup = mean(baseline) / mean(optimized):
#   keygen  encaps  decaps  total(three ops summed in-process)  e2e(one wall-clock
#   interval around keygen -> encaps -> decaps)
#
# usage: bash ./run_kem.sh [rounds] [warmup] [order] [seed]
#   order default: baseline,optimized,optimized,baseline  (interleaved, cancels drift)
# env:   CPU_PIN=<logical core>, BUILD_DIR, RESULT_DIR
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
ROUNDS="${1:-1000}"
WARMUP="${2:-100}"
ORDER="${3:-baseline,optimized,optimized,baseline}"
SEED="${4:-1}"
CPU_PIN="${CPU_PIN:-}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
tag="$(printf '%s' "$ORDER" | tr ',' '_' | tr -cd '[:alnum:]_')"
stamp="$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || date +%Y%m%dT%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/kem_${tag}_$stamp}"
run_pinned() {
  if [ -n "$CPU_PIN" ]; then
    taskset -c "$CPU_PIN" "$@"
  else
    "$@"
  fi
}

for value in "$ROUNDS" "$WARMUP" "$SEED"; do
  case "$value" in *[!0-9]*|'') echo "error: numeric arguments must be unsigned integers" >&2; exit 2 ;; esac
done
[ "$ROUNDS" -gt 0 ] || { echo "error: ROUNDS must be positive" >&2; exit 2; }

mkdir -p "$RESULT_DIR"
trap 'rc=$?; echo "kem_ab=FAIL rc=$rc result_dir=$RESULT_DIR order=$ORDER seed=$SEED" | tee -a "$RESULT_DIR/FAILED.txt" >/dev/null' ERR
if [ ! -x "$BUILD_DIR/bench_kem_baseline" ] || [ ! -x "$BUILD_DIR/bench_kem_optimized" ]; then
  BUILD_DIR="$BUILD_DIR" bash ./build.sh | tee "$RESULT_DIR/build.log"
else
  bash ./check_host.sh | tee "$RESULT_DIR/host_check.log"
fi

if [ "${SKIP_PRECHECK:-0}" != "1" ]; then
  RESULT_DIR="$RESULT_DIR/precheck" BUILD_DIR="$BUILD_DIR" SKIP_BUILD=1 \
    bash ./run_correctness.sh "${PRECHECK_CORE_TESTS:-1000}" "${PRECHECK_KEM_CASES:-10}" "$SEED"
fi

raw_files=()
idx=0
IFS=',' read -r -a order_items <<< "$ORDER"
for item in "${order_items[@]}"; do
  impl="$(printf '%s' "$item" | tr -d '[:space:]')"
  case "$impl" in baseline|optimized) ;; *) echo "error: ORDER contains '$impl'" >&2; exit 2 ;; esac
  idx=$((idx + 1))
  n="$(printf '%02d' "$idx")"
  log="$RESULT_DIR/${n}_${impl}.log"
  raw="$RESULT_DIR/${n}_${impl}_raw.csv"
  exe="$BUILD_DIR/bench_kem_baseline"
  [ "$impl" = "optimized" ] && exe="$BUILD_DIR/bench_kem_optimized"
  echo "[run $n] KEM $impl"
  SNTRUP_BENCH_CSV="$raw" run_pinned "$exe" "$ROUNDS" "$WARMUP" "$SEED" | tee "$log"
  grep -q 'KEM correctness: PASS' "$log" || { echo "error: KEM correctness failed for $impl" >&2; exit 3; }
  [ -s "$raw" ] || { echo "error: missing raw CSV $raw" >&2; exit 4; }
  raw_files+=("$raw")
done

if command -v python3 >/dev/null 2>&1; then
  python3 ./summarize.py kem "$RESULT_DIR/summary.csv" "${raw_files[@]}" | tee "$RESULT_DIR/summary.txt"
else
  echo "warning: python3 unavailable; raw CSV retained but summary not generated" >&2
fi
cp "$BUILD_DIR/build_metadata.txt" "$RESULT_DIR/" 2>/dev/null || true
echo "kem_ab=PASS result_dir=$RESULT_DIR"
