#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
command -v sha256sum >/dev/null 2>&1 || { echo "error: sha256sum is required" >&2; exit 2; }

tmp="SOURCE_MANIFEST.sha256.tmp"
: > "$tmp"
while IFS= read -r file; do
  sha256sum "$file" >> "$tmp"
done < <(
  find baseline optimized shared -type f -print
  find . -maxdepth 1 -type f \
    ! -name 'SOURCE_MANIFEST.sha256' ! -name 'SOURCE_MANIFEST.sha256.tmp' \
    ! -name '*.zip' -print
  )
LC_ALL=C sort -k2 "$tmp" > SOURCE_MANIFEST.sha256
rm -f "$tmp"
echo "manifest=PASS file=SOURCE_MANIFEST.sha256"

