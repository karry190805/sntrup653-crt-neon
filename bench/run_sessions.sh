#!/usr/bin/env bash
# 五组 ABBA/BAAB 会话级复测，给出表 2 的会话聚合与会话级置信区间。
# 包装 packages/sntrup653-crt-cortex-<PLATFORM>/run_sessions.sh。
#
# 用法：PLATFORM=a53 CPU_PIN=2 bash bench/run_sessions.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="${PLATFORM:-a53}"
PKG="$ROOT/packages/sntrup653-crt-cortex-$PLATFORM"

if [ ! -d "$PKG" ]; then
  echo "error: unknown platform '$PLATFORM' (expected a53, a76 or x1c)" >&2
  exit 2
fi

exec bash "$PKG/run_sessions.sh" "$@"
