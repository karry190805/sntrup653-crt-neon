#!/usr/bin/env bash
# Statistical timing test for sntrup653 decapsulation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
. ./platform.conf
OUT="${1:-$ROOT/results/dudect}"
N="${2:-100000}"
CC="${CC:-gcc}"
mkdir -p "$OUT/obj"
CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=$CPU_TARGET -mtune=$CPU_TARGET -fomit-frame-pointer -fno-lto"
objs=()
for f in optimized/*.c; do
  o="$OUT/obj/$(basename "$f" .c).o"
  "$CC" $CFLAGS -Drandombytes=kem_randombytes -Ioptimized -Ishared -c "$f" -o "$o" >> "$OUT/build.log" 2>&1
  objs+=("$o")
done
"$CC" $CFLAGS -Drandombytes=kem_randombytes -Ishared -c shared/randombytes.c -o "$OUT/obj/randombytes.o" >> "$OUT/build.log" 2>&1
"$CC" $CFLAGS -Ishared -c shared/sha2.c -o "$OUT/obj/sha2.o" >> "$OUT/build.log" 2>&1
"$CC" $CFLAGS -Ioptimized -Ishared -Isecurity/dudect -c security/dudect/dudect_kem.c -o "$OUT/obj/dudect_kem.o" >> "$OUT/build.log" 2>&1
"$CC" $CFLAGS "${objs[@]}" "$OUT/obj/randombytes.o" "$OUT/obj/sha2.o" "$OUT/obj/dudect_kem.o" -lm -o "$OUT/dudect_kem" >> "$OUT/build.log" 2>&1
set +e
timeout 900 "$OUT/dudect_kem" "$N" > "$OUT/dudect.log" 2>&1
rc=$?
set -e
echo "rc=$rc" | tee "$OUT/result.txt"
tail -40 "$OUT/dudect.log" | tee -a "$OUT/result.txt"
