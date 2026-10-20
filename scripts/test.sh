#!/usr/bin/env bash
# 仓库自检（任何机器都能跑，不需要 ARM 板卡）：
#   1. src/ 分块视图去掉注释头后能否还原出源文件（SHA-256 一致）
#   2. scripts/testcases/ 的固定测例与交付包的纯 Python oracle 是否一致
#   3. 1971 个用例的 FNV-1a-64 哈希是否与 expected_oracle_hashes.txt 一致
#
# 用法：bash scripts/test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

PY="${PYTHON:-python3}"
command -v "$PY" >/dev/null 2>&1 || PY=python

echo "== split/join =="
"$PY" tools/join.py

echo "== testcases =="
"$PY" scripts/check_testcases.py
