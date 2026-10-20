#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/platform.conf"

CPU_PIN="${CPU_PIN:-${1:-}}"
if [ -z "$CPU_PIN" ]; then
  echo "error: set CPU_PIN to the logical ${CPU_MODEL} CPU number" >&2
  exit 2
fi
case "$CPU_PIN" in
  *[!0-9]*|'') echo "error: CPU_PIN must be an unsigned integer" >&2; exit 2 ;;
esac

arch="$(uname -m 2>/dev/null || true)"
case "$arch" in
  aarch64|arm64) ;;
  *)
    if [ "${ALLOW_NON_TARGET:-0}" != "1" ]; then
      echo "error: native AArch64 is required; uname -m reported '$arch'" >&2
      exit 3
    fi
    echo "warning: ALLOW_NON_TARGET=1; results are not ${CPU_MODEL} measurements" >&2
    ;;
esac

if ! command -v taskset >/dev/null 2>&1; then
  echo "error: taskset is required so the benchmark cannot migrate between cores" >&2
  exit 4
fi
if ! taskset -c "$CPU_PIN" true >/dev/null 2>&1; then
  echo "error: CPU_PIN=$CPU_PIN is not an available CPU for this process" >&2
  exit 4
fi

confirmed=0
evidence=""
midr_file="/sys/devices/system/cpu/cpu${CPU_PIN}/regs/identification/midr_el1"
if [ -r "$midr_file" ]; then
  midr="$(tr '[:upper:]' '[:lower:]' < "$midr_file" 2>/dev/null | tr -d ' \n')"
  case "$midr" in 0x*) ;; *) midr="0x$midr" ;; esac
  evidence="MIDR=$midr"
  if (( ((midr >> 4) & 0xfff) == CPU_PART && ((midr >> 24) & 0xff) == 0x41 )); then
    confirmed=1
  fi
  if [ "$confirmed" -eq 0 ] && [ "${ALLOW_NON_TARGET:-0}" != 1 ]; then
    echo "error: MIDR $midr does not match ARM $CPU_PART" >&2
    exit 5
  fi
fi

if [ "$confirmed" -eq 0 ] && command -v lscpu >/dev/null 2>&1; then
  model="$(lscpu -p=CPU,MODELNAME 2>/dev/null | awk -F, -v c="$CPU_PIN" '$1 == c {print $2; exit}' || true)"
  if [ -n "$model" ]; then
    evidence="${evidence}${evidence:+; }lscpu_model=$model"
    case "$model" in
      *${CPU_MODEL}*|*${CPU_TARGET}*) confirmed=1 ;;
    esac
  fi
fi

if [ "$confirmed" -eq 0 ]; then
  if [ "${ALLOW_NON_TARGET:-0}" != "1" ]; then
    echo "error: CPU $CPU_PIN could not be confirmed as ${CPU_MODEL} (CPU part $CPU_PART)" >&2
    echo "evidence: ${evidence:-none}; inspect env_report.sh output" >&2
    echo "ALLOW_NON_TARGET=1 is for portability debugging only, not paper results" >&2
    exit 5
  fi
  echo "warning: target core is unverified: ${evidence:-no per-core model data}" >&2
fi

echo "host_check=PASS arch=$arch cpu_pin=$CPU_PIN confirmed=$confirmed evidence='${evidence:-none}'"
