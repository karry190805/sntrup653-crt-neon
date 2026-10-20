#!/usr/bin/env bash
# 多项式乘法单次调用 A/B（论文表 1）。
# 包装 packages/sntrup653-crt-cortex-<PLATFORM>/run_polymul.sh。
#
# 用法：PLATFORM=a53 CPU_PIN=2 bash bench/run_polymul.sh [rounds] [loops] [tests] [warmup] [seed] [order]
# 默认：100 轮、每轮 2000 次调用、1000 次正确性测试、预热 200 次、
#       顺序 baseline,optimized,optimized,baseline
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="${PLATFORM:-a53}"
PKG="$ROOT/packages/sntrup653-crt-cortex-$PLATFORM"

if [ ! -d "$PKG" ]; then
  echo "error: unknown platform '$PLATFORM' (expected a53, a76 or x1c)" >&2
  exit 2
fi

exec bash "$PKG/run_polymul.sh" "$@"
