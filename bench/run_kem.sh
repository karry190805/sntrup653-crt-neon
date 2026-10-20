#!/usr/bin/env bash
# KEM 分阶段与端到端 A/B（论文表 2）。
# 包装 packages/sntrup653-crt-cortex-<PLATFORM>/run_kem.sh。
#
# 用法：PLATFORM=a53 CPU_PIN=2 bash bench/run_kem.sh [rounds] [warmup] [seed] [order]
# 默认：1000 轮、预热 100 次、顺序 baseline,optimized,optimized,baseline
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="${PLATFORM:-a53}"
PKG="$ROOT/packages/sntrup653-crt-cortex-$PLATFORM"

if [ ! -d "$PKG" ]; then
  echo "error: unknown platform '$PLATFORM' (expected a53, a76 or x1c)" >&2
  exit 2
fi

exec bash "$PKG/run_kem.sh" "$@"
