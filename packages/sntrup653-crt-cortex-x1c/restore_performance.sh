#!/usr/bin/env bash
set -euo pipefail

STATE_FILE="${1:-}"
if [ -z "$STATE_FILE" ] || [ ! -r "$STATE_FILE" ]; then
  echo "usage: $0 results/cpuN_performance_state.txt" >&2
  exit 2
fi
policy="$(grep '^policy=' "$STATE_FILE" | head -1 | cut -d= -f2-)"
governor="$(grep '^governor=' "$STATE_FILE" | head -1 | cut -d= -f2-)"
min_freq="$(grep '^min_freq=' "$STATE_FILE" | head -1 | cut -d= -f2-)"
max_freq="$(grep '^max_freq=' "$STATE_FILE" | head -1 | cut -d= -f2-)"
case "$policy" in
  /sys/devices/system/cpu/*/cpufreq|/sys/devices/system/cpu/cpufreq/policy[0-9]*) ;;
  *) echo "error: refusing unexpected policy path '$policy'" >&2; exit 3 ;;
esac
[ -n "$min_freq" ] && echo "$min_freq" > "$policy/scaling_min_freq"
[ -n "$max_freq" ] && echo "$max_freq" > "$policy/scaling_max_freq"
[ -n "$governor" ] && echo "$governor" > "$policy/scaling_governor"
echo "performance_policy_restored=PASS policy=$policy"
