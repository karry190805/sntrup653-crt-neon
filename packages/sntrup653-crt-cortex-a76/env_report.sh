#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/platform.conf"

OUT="${1:-results/env_report.txt}"
CPU_PIN="${CPU_PIN:-unset}"
CC="${CC:-gcc}"
mkdir -p "$(dirname "$OUT")"

{
  echo "${CPU_MODEL} experiment environment report"
  echo "generated_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || true)"
  echo "pwd=$(pwd)"
  echo "cpu_pin=$CPU_PIN"
  echo "cc=$CC"
  echo "cflags=${CFLAGS:-<package default>}"
  echo "use_lto=${USE_LTO:-0}"
  echo ""
  echo "[uname]"
  uname -a 2>/dev/null || true
  echo "[OS release]"
  cat /etc/os-release 2>/dev/null || true
  echo "[device model and compatible]"
  for f in /proc/device-tree/model /proc/device-tree/compatible; do
    if [ -r "$f" ]; then printf '%s=' "$f"; tr '\0' '\n' < "$f"; fi
  done
  echo ""
  echo "[compiler]"
  command -v "$CC" 2>/dev/null || true
  "$CC" --version 2>/dev/null | head -5 || true
  echo ""
  echo "[lscpu]"
  lscpu 2>/dev/null || true
  echo ""
  echo "[lscpu extended]"
  lscpu -e 2>/dev/null || true
  echo ""
  echo "[/proc/cpuinfo]"
  cat /proc/cpuinfo 2>/dev/null || true
  echo ""
  echo "[per-CPU MIDR, capacity, topology, cpufreq]"
  for cpu_dir in /sys/devices/system/cpu/cpu[0-9]*; do
    [ -d "$cpu_dir" ] || continue
    cpu="${cpu_dir##*cpu}"
    echo "cpu=$cpu"
    for rel in \
      online \
      regs/identification/midr_el1 \
      cpu_capacity \
      topology/core_id \
      topology/physical_package_id \
      cpufreq/scaling_driver \
      cpufreq/scaling_governor \
      cpufreq/scaling_cur_freq \
      cpufreq/scaling_min_freq \
      cpufreq/scaling_max_freq \
      cpufreq/cpuinfo_min_freq \
      cpufreq/cpuinfo_max_freq; do
      f="$cpu_dir/$rel"
      [ -r "$f" ] && echo "  $rel=$(cat "$f" 2>/dev/null || true)"
    done
  done
  echo ""
  echo "[thermal zones]"
  for zone in /sys/class/thermal/thermal_zone*; do
    [ -d "$zone" ] || continue
    echo "$zone type=$(cat "$zone/type" 2>/dev/null || true) temp=$(cat "$zone/temp" 2>/dev/null || true)"
  done
  echo ""
  echo "[load and memory]"
  cat /proc/loadavg 2>/dev/null || true
  cat /proc/meminfo 2>/dev/null | head -20 || true
  echo ""
  echo "[tool availability]"
  command -v taskset 2>/dev/null || true
  taskset --version 2>/dev/null | head -1 || true
  command -v python3 2>/dev/null || true
  command -v perf 2>/dev/null || true
  perf --version 2>/dev/null || true
  command -v sha256sum 2>/dev/null || true
  echo ""
  echo "[package manifest]"
  if [ -r SOURCE_MANIFEST.sha256 ]; then
    cat SOURCE_MANIFEST.sha256
  else
    echo "SOURCE_MANIFEST.sha256 not found"
  fi
} | tee "$OUT"

echo "environment_report=$OUT"
