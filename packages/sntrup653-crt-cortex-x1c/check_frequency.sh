#!/usr/bin/env bash
set -euo pipefail
: "${CPU_PIN:?set CPU_PIN}"
policy="/sys/devices/system/cpu/cpu${CPU_PIN}/cpufreq"
governor="$(cat "$policy/scaling_governor")"
min="$(cat "$policy/scaling_min_freq")"
max="$(cat "$policy/scaling_max_freq")"
[ "$governor" = performance ] && [ "$min" = "$max" ] && [ "$min" -gt 0 ] || {
  echo "error: paper run requires performance governor and min=max>0; got $governor $min $max" >&2
  exit 2
}
echo "frequency_policy=PASS governor=$governor min_khz=$min max_khz=$max"
