#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/platform.conf"

CPU_PIN="${CPU_PIN:-${1:-}}"
if [ -z "$CPU_PIN" ]; then
  echo "error: set CPU_PIN to the logical ${CPU_MODEL} CPU number" >&2
  exit 2
fi
case "$CPU_PIN" in *[!0-9]*|'') echo "error: invalid CPU_PIN" >&2; exit 2 ;; esac

cpu_freq="/sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq"
if [ ! -d "$cpu_freq" ]; then
  echo "error: cpufreq is unavailable for CPU $CPU_PIN" >&2
  exit 3
fi
policy="$(readlink -f "$cpu_freq" 2>/dev/null || printf '%s' "$cpu_freq")"
gov="$policy/scaling_governor"
avail="$policy/scaling_available_governors"
minf="$policy/scaling_min_freq"
maxf="$policy/scaling_max_freq"
hwmax="$policy/cpuinfo_max_freq"
STATE_FILE="${STATE_FILE:-results/cpu${CPU_PIN}_performance_state.txt}"
mkdir -p "$(dirname "$STATE_FILE")"
[ ! -e "$STATE_FILE" ] || { echo "error: preserve existing state file: $STATE_FILE" >&2; exit 2; }

old_gov="$(cat "$gov" 2>/dev/null || true)"
old_min="$(cat "$minf" 2>/dev/null || true)"
old_max="$(cat "$maxf" 2>/dev/null || true)"
{
  echo "policy=$policy"
  echo "governor=$old_gov"
  echo "min_freq=$old_min"
  echo "max_freq=$old_max"
} > "$STATE_FILE"

if [ ! -w "$gov" ]; then
  echo "error: $gov is not writable; run this optional script with sudo" >&2
  exit 4
fi
if [ -r "$avail" ] && ! grep -qw performance "$avail"; then
  echo "error: performance governor is not available in $avail" >&2
  exit 5
fi
echo performance > "$gov"

if [ "${LOCK_MAX_FREQ:-0}" = "1" ]; then
  target="$(cat "$hwmax" 2>/dev/null || cat "$maxf" 2>/dev/null || true)"
  if [ -z "$target" ] || [ ! -w "$minf" ]; then
    echo "error: cannot lock minimum frequency for policy $policy" >&2
    exit 6
  fi
  echo "$target" > "$maxf"
  echo "$target" > "$minf"
fi

echo "performance_policy_set=PASS cpu=$CPU_PIN policy=$policy state_file=$STATE_FILE lock_max=${LOCK_MAX_FREQ:-0}"
