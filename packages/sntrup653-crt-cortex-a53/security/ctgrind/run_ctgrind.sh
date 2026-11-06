#!/usr/bin/env bash
# ctgrind-style Valgrind Memcheck run.  No root required: valgrind can be
# extracted from the Debian .deb into a local prefix.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
. ./platform.conf
OUT="${1:-$ROOT/results/ctgrind}"
CC="${CC:-gcc}"
VGROOT="${VGROOT:-$OUT/valgrind-root}"
mkdir -p "$OUT"
if [ ! -x "$VGROOT/usr/bin/valgrind" ]; then
  mkdir -p "$OUT/deb"
  (cd "$OUT/deb" && apt-get download valgrind >/dev/null 2>&1 || true)
  deb=$(ls "$OUT"/deb/valgrind_*.deb 2>/dev/null | head -1)
  [ -n "$deb" ] || { echo "valgrind deb not available" >&2; exit 2; }
  dpkg-deb -x "$deb" "$VGROOT"
fi
CFLAGS="-O2 -g -march=armv8-a -mno-outline-atomics -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU_TARGET -mtune=$CPU_TARGET -fomit-frame-pointer -fno-lto"
srcs=()
for f in optimized/*.c; do srcs+=("$f"); done
"$CC" $CFLAGS -Ioptimized -Ishared -I"$VGROOT/usr/include" \
  shared/randombytes.c shared/sha2.c "${srcs[@]}" security/ctgrind/ctgrind_kem.c \
  -o "$OUT/ctgrind_kem" > "$OUT/build.log" 2>&1
set +e
EXTRA=()
if [ -n "${LIBC6DBG:-}" ]; then EXTRA+=(--extra-debuginfo-path="$LIBC6DBG/usr/lib/debug"); fi
VALGRIND_LIB="$VGROOT/usr/libexec/valgrind" \
  timeout 900 "$VGROOT/usr/bin/valgrind" --tool=memcheck --error-exitcode=99 \
  --track-origins=yes --leak-check=no "${EXTRA[@]}" "$OUT/ctgrind_kem" > "$OUT/ctgrind.log" 2>&1
rc=$?
set -e
echo "ctgrind_rc=$rc" | tee "$OUT/result.txt"
grep -E 'Conditional jump|uninitialised|Use of uninitialised|ERROR SUMMARY' "$OUT/ctgrind.log" | tail -20 | tee -a "$OUT/result.txt"
