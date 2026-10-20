#!/usr/bin/env bash
# 单因素消融：逐个关掉 NEON 路径、公开补零模式、第一层特化等，看各自的贡献。
# 包装 packages/sntrup653-crt-cortex-<PLATFORM>/run_ablation.sh。
#
# 用法：PLATFORM=a53 CPU_PIN=2 bash bench/ablation.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="${PLATFORM:-a53}"
PKG="$ROOT/packages/sntrup653-crt-cortex-$PLATFORM"

if [ ! -d "$PKG" ]; then
  echo "error: unknown platform '$PLATFORM' (expected a53, a76 or x1c)" >&2
  exit 2
fi

exec bash "$PKG/run_ablation.sh" "$@"
