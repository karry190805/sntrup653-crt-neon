#!/usr/bin/env bash
# 仓库自检：分块能否还原、固定测例与哈希序列是否与包内 oracle 一致。
# 不需要 ARM 板卡。用法：bash scripts/test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

PY="${PYTHON:-python3}"
command -v "$PY" >/dev/null 2>&1 || PY=python

echo "== split/join =="
"$PY" tools/join.py

echo "== testcases =="
"$PY" scripts/check_testcases.py
