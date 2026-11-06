#!/usr/bin/env bash
# Deterministic SHA-256 of the source/scripts/docs tree.
# Excludes build outputs, result directories, VCS data, and binary artifacts.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
find . -type f \
  ! -path './.git/*' \
  ! -path './build/*' ! -path './build_*/*' \
  ! -path './results/*' ! -path './results_*/*' \
  ! -name '*.o' ! -name '*.a' ! -name '*.so' \
  ! -name '*.bin' ! -name '*.log' \
  ! -name 'SHA256SUMS.txt' ! -name 'MANIFEST.sha256' \
  ! -name 'source_tree_sha256.txt' \
  -print0 \
  | LC_ALL=C sort -z \
  | xargs -0 sha256sum \
  | sha256sum \
  | awk '{print $1}'
