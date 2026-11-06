#!/usr/bin/env bash
# Capture X1C system_power hwmon while a command runs.
# usage: power_x1c.sh OUT.csv SECONDS [command...]
set -euo pipefail
OUT="$1"; SECS="$2"; shift 2
HWM=""
for d in /sys/class/hwmon/hwmon*; do
  [ "$(cat "$d/name" 2>/dev/null)" = "system_power" ] && HWM="$d" && break
done
[ -n "$HWM" ] || { echo "system_power hwmon not found" >&2; exit 2; }
echo "timestamp_ns,voltage_mV,current_mA,power_uW" > "$OUT"
end=$(( $(date +%s%N) + SECS * 1000000000 ))
if [ "$#" -gt 0 ]; then "$@" >/dev/null 2>&1 & pid=$!; fi
while [ "$(date +%s%N)" -lt "$end" ]; do
  printf '%s,%s,%s,%s\n' "$(date +%s%N)" \
    "$(cat "$HWM/in0_input")" "$(cat "$HWM/curr1_input")" "$(cat "$HWM/power1_input")" >> "$OUT"
  sleep 0.02
done
if [ -n "${pid:-}" ]; then
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
fi
python3 - "$OUT" <<'PY'
import csv,sys,statistics
r=list(csv.DictReader(open(sys.argv[1])))
for k in ('voltage_mV','current_mA','power_uW'):
    v=[float(x[k]) for x in r]
    print(f'{k}: n={len(v)} mean={statistics.mean(v):.3f} median={statistics.median(v):.3f} max={max(v):.3f}')
PY
