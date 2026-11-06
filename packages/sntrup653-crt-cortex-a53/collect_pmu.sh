#!/usr/bin/env bash
# collect_pmu.sh — collect real hardware counters for both core builds.
#
# usage: PMU_OUT=/path/to/pmu CPU_PIN=7 bash ./collect_pmu.sh
#
# Writes $PMU_OUT/<platform>_<impl>.txt with the raw perf output, plus
# $PMU_OUT/SYSTEM.txt with the kernel/perf/paranoid/frequency state.  The
# platform tag comes from platform.conf (CPU_MODEL).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

CPU_PIN="${CPU_PIN:?set CPU_PIN to the logical ${CPU_MODEL} CPU number}"
PMU_OUT="${PMU_OUT:-$SCRIPT_DIR/results/pmu}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
REPEATS="${REPEATS:-5}"
BENCH_ARGS="${BENCH_ARGS:-1 50000 0 0 1}"

mkdir -p "$PMU_OUT"
tag="$(printf '%s' "$CPU_MODEL" | tr '[:upper:]' '[:lower:]')"

if ! command -v perf >/dev/null 2>&1; then
  echo "error: perf not installed" >&2
  exit 3
fi

{
  echo "platform=$CPU_MODEL part=$CPU_PART pin=$CPU_PIN"
  echo "kernel=$(uname -r)"
  echo "perf=$(perf --version 2>&1)"
  echo "perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo unavailable)"
  echo "governor=$(cat /sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
  echo "cur_freq_khz=$(cat /sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/scaling_cur_freq 2>/dev/null || echo unavailable)"
  echo "min_freq_khz=$(cat /sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/scaling_min_freq 2>/dev/null || echo unavailable)"
  echo "max_freq_khz=$(cat /sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq/scaling_max_freq 2>/dev/null || echo unavailable)"
  echo "--- perf list (core events) ---"
  perf list 2>/dev/null | grep -E 'cycles|instructions|branch-misses' || true
} > "$PMU_OUT/SYSTEM.txt"

for impl in baseline optimized; do
  exe="$BUILD_DIR/bench_core_$impl"
  [ -x "$exe" ] || { echo "error: $exe missing" >&2; exit 4; }
  out="$PMU_OUT/${tag}_${impl}.txt"
  echo "[pmu] $impl -> $out"
  {
    echo "# perf stat -e cycles:u,instructions:u,branch-misses:u -r $REPEATS -- taskset -c $CPU_PIN $exe $BENCH_ARGS"
    perf stat -e cycles:u,instructions:u,branch-misses:u -r "$REPEATS" -- \
      taskset -c "$CPU_PIN" "$exe" $BENCH_ARGS 2>&1 || true
  } > "$out"
done

echo "pmu_out=$PMU_OUT"
