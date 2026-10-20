#!/usr/bin/env bash
# run_security.sh — functional safety checks plus a first-order timing screen.
#   1. seven straight-line reducer probes (no branch, no call)
#   2. sign-branch regression search in the core and KEM disassembly
#   3. native and ASan/UBSan runs of the reducer / alias / low73 / first-call tests
#   4. Welch t-test timing screen on the production core object
#
# This does not prove constant time. See RANGE_AND_SECURITY.md.
set -euo pipefail
cd "$(dirname "$0")"
SCRIPT_DIR="$PWD"
. ./platform.conf
CC="${CC:-gcc}"
OBJDUMP="${OBJDUMP:-objdump}"
SECURITY_DIR="${SECURITY_DIR:-$SCRIPT_DIR/security_$(date -u +%Y%m%dT%H%M%SZ)}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
LOCAL_EMULATION="${LOCAL_EMULATION:-0}"
if [ "$LOCAL_EMULATION" = 1 ]; then
  : "${QEMU:?set QEMU}" "${QEMU_LD_PREFIX:?set QEMU_LD_PREFIX}"
else
  : "${CPU_PIN:?set CPU_PIN}"
  bash ./check_host.sh
fi
[ ! -e "$SECURITY_DIR" ] || { echo "error: preserve $SECURITY_DIR"; exit 2; }
mkdir -p "$SECURITY_DIR"
trap 'echo "security_checks=FAIL" > "$SECURITY_DIR/FAILED.txt"' ERR
run_target() {
  if [ "$LOCAL_EMULATION" = 1 ]; then "$QEMU" -L "$QEMU_LD_PREFIX" "$@"
  else taskset -c "$CPU_PIN" "$@"; fi
}
common=(-O3 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
        -Wall -Wextra "-mcpu=$CPU_TARGET" "-mtune=$CPU_TARGET"
        -fomit-frame-pointer -fno-lto -DUSE_BARRETT_REDUCE=1
        -DSNTRUP653_USE_INPLACE_BUFFERS=1 -DSNTRUP653_PUBLIC_ZS_MODE=1
        -DSNTRUP653_LOW73_NEON=1 -DSNTRUP653_FINAL_FOLD_NEON=1)
sha256sum -c SOURCE_MANIFEST.sha256 > "$SECURITY_DIR/source_manifest_check.log"
python3 check_ranges.py | tee "$SECURITY_DIR/ranges.log"
{
  "$CC" --version
  printf 'security_cflags='; printf '%q ' "${common[@]}"; printf '\n'
  echo "local_emulation=$LOCAL_EMULATION"
} > "$SECURITY_DIR/build_metadata.txt"
"$CC" "${common[@]}" -DPROBE_ONLY=1 -Ioptimized -c shared/security_probe.c -o "$SECURITY_DIR/probes.o"
"$OBJDUMP" -d "$SECURITY_DIR/probes.o" > "$SECURITY_DIR/probes.asm"
python3 check_ct_probes.py "$SECURITY_DIR/probes.asm" | tee "$SECURITY_DIR/probes_check.log"
for exe in bench_core_optimized bench_kem_optimized; do
  [ -x "$BUILD_DIR/$exe" ] || { echo "error: build first: $BUILD_DIR/$exe"; exit 2; }
  "$OBJDUMP" -d "$BUILD_DIR/$exe" > "$SECURITY_DIR/$exe.asm"
done
python3 check_core_regression.py "$SECURITY_DIR/bench_core_optimized.asm" \
  "$SECURITY_DIR/bench_kem_optimized.asm" | tee "$SECURITY_DIR/core_regression.log"
variants=(native sanitized)
if [ "$LOCAL_EMULATION" = 1 ]; then variants=(native ubsan); fi
for variant in "${variants[@]}"; do
  extra=()
  if [ "$variant" = sanitized ]; then
    extra=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all)
  elif [ "$variant" = ubsan ]; then
    extra=(-O1 -g -fno-omit-frame-pointer -fsanitize=undefined -fno-sanitize-recover=all)
  fi
  "$CC" "${common[@]}" "${extra[@]}" -Ibaseline \
    -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=baseline_crypto_core_multsntrup653 \
    -c baseline/crypto_core_multsntrup653.c -o "$SECURITY_DIR/baseline_$variant.o"
  "$CC" "${common[@]}" "${extra[@]}" -Ioptimized shared/security_probe.c \
    optimized/crypto_encode_653xint16.c optimized/crypto_decode_653xint16.c \
    "$SECURITY_DIR/baseline_$variant.o" -pthread -o "$SECURITY_DIR/check_$variant"
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    run_target "$SECURITY_DIR/check_$variant" 2>&1 | tee "$SECURITY_DIR/$variant.log"
done
"$CC" "${common[@]}" -Ioptimized shared/timing_leakage.c \
  "$BUILD_DIR/optimized_core.o" "$BUILD_DIR/encode.o" "$BUILD_DIR/decode.o" \
  -DPQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653=optimized_crypto_core_multsntrup653 \
  -lm -o "$SECURITY_DIR/timing_leakage"
if [ "$LOCAL_EMULATION" = 1 ]; then
  echo "timing_screen=NOT_RUN_UNDER_EMULATION" | tee "$SECURITY_DIR/timing_status.txt"
else
  bash ./env_report.sh "$SECURITY_DIR/env_before_timing.txt" >/dev/null
  for mode in 0 1; do
    run_target "$SECURITY_DIR/timing_leakage" "${TIMING_SAMPLES:-200000}" "$mode" \
      "$SECURITY_DIR/timing_mode${mode}.csv" | tee "$SECURITY_DIR/timing_mode${mode}.log"
  done
  bash ./env_report.sh "$SECURITY_DIR/env_after_timing.txt" >/dev/null
  echo "timing_screen=NO_SIGNAL" > "$SECURITY_DIR/timing_status.txt"
fi
sha256sum "$SECURITY_DIR"/check_* "$SECURITY_DIR"/probes.o \
  "$SECURITY_DIR"/timing_leakage > "$SECURITY_DIR/binary_sha256.txt"
{
  echo "functional_security_checks=PASS"
  if [ "$LOCAL_EMULATION" = 1 ]; then
    echo "ubsan=PASS"
    echo "asan=NOT_RUN_UNDER_EMULATION"
  else
    echo "asan_ubsan=PASS leak_detection=DISABLED"
  fi
  echo "straight_line_probes=PASS"
  cat "$SECURITY_DIR/timing_status.txt"
  echo "full_binary_constant_time=NOT_PROVEN"
  echo "second_compiler=NOT_RUN_IN_THIS_STAGE"
  echo "second_compiler_evidence=check_multicompiler.sh (run_release.sh extended stage)"
} > "$SECURITY_DIR/STATUS.txt"
echo "security_results=$SECURITY_DIR"
