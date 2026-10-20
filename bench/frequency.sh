#!/usr/bin/env bash
# 锁定/还原 CPU 频率（governor=performance，可选锁到最高频）。
# 包装 packages/sntrup653-crt-cortex-<PLATFORM>/{set,restore}_performance.sh。
#
# 用法：
#   PLATFORM=a76 CPU_PIN=6 sudo -E bash bench/frequency.sh set
#   PLATFORM=a76 CPU_PIN=6 bash bench/frequency.sh restore
#   PLATFORM=a76 CPU_PIN=6 bash bench/frequency.sh status
#
# LOCK_MAX_FREQ=1 时同时把 scaling_min_freq 顶到 cpuinfo_max_freq。
# set 把原状态写到包内 results/cpu<N>_performance_state.txt，restore 读回；
# 也可以第二个参数显式给出状态文件路径。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="${PLATFORM:-a53}"
PKG="$ROOT/packages/sntrup653-crt-cortex-$PLATFORM"
ACTION="${1:-}"
STATE="${2:-}"

if [ ! -d "$PKG" ]; then
  echo "error: unknown platform '$PLATFORM' (expected a53, a76 or x1c)" >&2
  exit 2
fi

cd "$PKG"

case "$ACTION" in
  set)
    exec bash ./set_performance.sh
    ;;
  restore)
    : "${CPU_PIN:?set CPU_PIN}"
    exec bash ./restore_performance.sh "${STATE:-results/cpu${CPU_PIN}_performance_state.txt}"
    ;;
  status)
    exec bash ./check_frequency.sh
    ;;
  *)
    echo "usage: PLATFORM=<a53|a76|x1c> CPU_PIN=<n> bash bench/frequency.sh {set|restore|status} [state_file]" >&2
    exit 2
    ;;
esac
