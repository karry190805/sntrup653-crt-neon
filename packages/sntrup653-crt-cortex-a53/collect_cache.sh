#!/usr/bin/env bash
# collect_cache.sh — native cache-reference / cache-miss batch for one platform.
#
# This is a separate native batch from the Table 3a PMU batch.  It uses the
# same strict-same-flags campaign binaries (build_opt22) and the same
# 50,000-call benchmark, but adds the cache events.  Cache counts are
# descriptive only: they are not used in a speedup or security claim and are
# not compared across microarchitectures.
#
# usage: CACHE_OUT=/path/to/cache CPU_PIN=7 bash ./collect_cache.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

CPU_PIN="${CPU_PIN:?set CPU_PIN to the logical ${CPU_MODEL} CPU number}"
CACHE_OUT="${CACHE_OUT:-$SCRIPT_DIR/results/cache}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build_opt22}"
REPEATS="${REPEATS:-5}"
BENCH_ARGS="${BENCH_ARGS:-1 50000 0 0 1}"

mkdir -p "$CACHE_OUT"
tag="$(printf '%s' "$CPU_MODEL" | tr '[:upper:]' '[:lower:]')"

if ! command -v perf >/dev/null 2>&1; then
  echo "error: perf not installed" >&2
  exit 3
fi

for impl in baseline optimized; do
  exe="$BUILD_DIR/bench_core_$impl"
  [ -x "$exe" ] || { echo "error: $exe missing" >&2; exit 4; }
done

gov="$(cat "/sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/scaling_governor" 2>/dev/null || echo unavailable)"
cur="$(cat "/sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/scaling_cur_freq" 2>/dev/null || echo unavailable)"
{
  echo "platform=$CPU_MODEL"
  echo "part=$CPU_PART"
  echo "pin=$CPU_PIN"
  echo "batch=native-cache-v4"
  echo "kernel=$(uname -r)"
  echo "arch=$(uname -m)"
  echo "host=$(uname -n)"
  echo "perf=$(perf --version 2>&1)"
  echo "perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo unavailable)"
  echo "governor=$gov"
  echo "cur_khz=$cur"
  echo "min_khz=$(cat "/sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/scaling_min_freq" 2>/dev/null || echo unavailable)"
  echo "max_khz=$(cat "/sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/scaling_max_freq" 2>/dev/null || echo unavailable)"
  echo "bench_args=$BENCH_ARGS"
  echo "repeats=$REPEATS"
  echo "binary_baseline=$(sha256sum "$BUILD_DIR/bench_core_baseline" | cut -d' ' -f1)"
  echo "binary_optimized=$(sha256sum "$BUILD_DIR/bench_core_optimized" | cut -d' ' -f1)"
} > "$CACHE_OUT/metadata.txt"

for impl in baseline optimized; do
  exe="$BUILD_DIR/bench_core_$impl"
  out="$CACHE_OUT/${impl}.txt"
  echo "[cache] $impl -> $out"
  {
    echo "# perf stat -e cycles:u,instructions:u,branch-misses:u,cache-references:u,cache-misses:u -r $REPEATS -- taskset -c $CPU_PIN $exe $BENCH_ARGS"
    perf stat -e cycles:u,instructions:u,branch-misses:u,cache-references:u,cache-misses:u \
      -r "$REPEATS" -- taskset -c "$CPU_PIN" "$exe" $BENCH_ARGS 2>&1 || true
  } > "$out"
done

echo "cache_out=$CACHE_OUT"
