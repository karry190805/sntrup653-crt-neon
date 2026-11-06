#!/usr/bin/env bash
# build_stage_profile.sh — build the diagnostic per-stage profiler binary.
#
# This is NOT part of the delivered build.  It compiles the optimized core with
# -DSNTRUP653_STAGE_PROFILE=1, which turns the SNTRUP_STAGE_MARK() sites into
# calls to shared/profile_core_stages.c.  The marks are no-ops in every other
# build, so production binaries are unchanged.
#
# usage: CPU_PIN=<n> bash ./build_stage_profile.sh
# env:   CC, CFLAGS, OPT_CFLAGS, BUILD_DIR
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

CC="${CC:-gcc}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
CFLAGS="${CFLAGS:--O3 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -mcpu=${CPU_TARGET} -mtune=${CPU_TARGET} -fomit-frame-pointer -fno-lto}"
# The GCC tuning switches below are rejected by clang.  When CC is clang and the
# caller did not override OPT_CFLAGS, use the clang equivalents instead.  This
# only affects the diagnostic binary; delivered builds use build.sh.
if "$CC" --version 2>/dev/null | grep -qi clang; then
  OPT_CFLAGS="${OPT_CFLAGS:--funroll-loops -fvectorize -fslp-vectorize -falign-functions=32}"
else
  OPT_CFLAGS="${OPT_CFLAGS:--funroll-loops -fvect-cost-model=unlimited -fno-guess-branch-probability -fno-tree-slp-vectorize -falign-functions=32 -fmodulo-sched -flive-range-shrinkage}"
fi

SNTRUP653_USE_INPLACE_BUFFERS="${SNTRUP653_USE_INPLACE_BUFFERS:-1}"
SNTRUP653_PUBLIC_ZS_MODE="${SNTRUP653_PUBLIC_ZS_MODE:-1}"
SNTRUP653_LOW73_NEON="${SNTRUP653_LOW73_NEON:-1}"
SNTRUP653_FINAL_FOLD_NEON="${SNTRUP653_FINAL_FOLD_NEON:-1}"
USE_BARRETT_REDUCE="${USE_BARRETT_REDUCE:-1}"
REQUIRE_NEON="-DSNTRUP653_REQUIRE_AARCH64_NEON=1"
OPTIMIZED_DEFS="-DSNTRUP653_USE_INPLACE_BUFFERS=$SNTRUP653_USE_INPLACE_BUFFERS"
OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DSNTRUP653_PUBLIC_ZS_MODE=$SNTRUP653_PUBLIC_ZS_MODE"
OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DSNTRUP653_LOW73_NEON=$SNTRUP653_LOW73_NEON"
OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DSNTRUP653_FINAL_FOLD_NEON=$SNTRUP653_FINAL_FOLD_NEON"
OPTIMIZED_DEFS="$OPTIMIZED_DEFS -DUSE_BARRETT_REDUCE=$USE_BARRETT_REDUCE"

mkdir -p "$BUILD_DIR"

if ! grep -q 'SNTRUP653_STAGE_PROFILE' optimized/crypto_core_multsntrup653.c; then
  echo "error: core has no stage marks; run add_stage_marks.py first" >&2
  exit 4
fi

echo "[stage-profile] core object (diagnostic marks enabled)"
"$CC" $CFLAGS $OPT_CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS \
  -DSNTRUP653_STAGE_PROFILE=1 -Ioptimized -Ishared \
  -c optimized/crypto_core_multsntrup653.c -o "$BUILD_DIR/stage_profile_core.o"

echo "[stage-profile] driver"
"$CC" $CFLAGS $REQUIRE_NEON $OPTIMIZED_DEFS -Ioptimized -Ishared \
  shared/profile_core_stages.c \
  optimized/crypto_encode_653xint16.c optimized/crypto_decode_653xint16.c \
  "$BUILD_DIR/stage_profile_core.o" \
  -o "$BUILD_DIR/bench_stage_profile"

echo "built $BUILD_DIR/bench_stage_profile"
