#!/usr/bin/env bash
# run_session.sh — one complete paired measurement session.
#   manifest check -> host check -> frequency check -> build -> correctness ->
#   polymul A/B (both orders) -> KEM A/B (both orders, per phase and end to end)
# The mirrored orders let a linear drift cancel out within the session.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/protocol.conf"
CPU_PIN="${CPU_PIN:-}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
SESSION_ID="${SESSION_ID:-$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || date +%Y%m%dT%H%M%S)}"
SESSION_SEED="${SESSION_SEED:-1}"
DESIGN="${DESIGN:-ABBA_FIRST}"
RESULT_ROOT="${RESULT_ROOT:-$SCRIPT_DIR/results/session_$SESSION_ID}"

case "$DESIGN" in
  ABBA_FIRST) FIRST=baseline,optimized,optimized,baseline; SECOND=optimized,baseline,baseline,optimized ;;
  BAAB_FIRST) FIRST=optimized,baseline,baseline,optimized; SECOND=baseline,optimized,optimized,baseline ;;
  *) echo "error: DESIGN must be ABBA_FIRST or BAAB_FIRST" >&2; exit 2 ;;
esac

[ ! -e "$RESULT_ROOT" ] || { echo "error: result directory already exists: $RESULT_ROOT" >&2; exit 2; }
mkdir -p "$RESULT_ROOT"
trap 'rc=$?; echo "full_session=FAIL rc=$rc result_root=$RESULT_ROOT session_id=$SESSION_ID seed=$SESSION_SEED design=$DESIGN" | tee -a "$RESULT_ROOT/FAILED.txt" >/dev/null' ERR
sha256sum -c SOURCE_MANIFEST.sha256 | tee "$RESULT_ROOT/source_manifest_check.log"
bash ./check_host.sh | tee "$RESULT_ROOT/host_check.log"
bash ./check_frequency.sh | tee "$RESULT_ROOT/frequency_before.txt"
cp protocol.conf platform.conf "$RESULT_ROOT/"
bash ./env_report.sh "$RESULT_ROOT/env_before.txt" >/dev/null
BUILD_DIR="$BUILD_DIR" bash ./build.sh | tee "$RESULT_ROOT/build.log"

RESULT_DIR="$RESULT_ROOT/correctness" BUILD_DIR="$BUILD_DIR" SKIP_BUILD=1 \
  INTERNAL_TESTS="${INTERNAL_TESTS:-1000}" \
  bash ./run_correctness.sh "${CORE_EQUIV_TESTS:-100000}" "${KEM_TRANSCRIPT_CASES:-1000}" "$SESSION_SEED"

RESULT_DIR="$RESULT_ROOT/polymul_first" BUILD_DIR="$BUILD_DIR" SKIP_PRECHECK=1 \
  bash ./run_polymul.sh \
  "${POLYMUL_ROUNDS:-50}" "${POLYMUL_LOOPS:-4000}" "${POLYMUL_HASH_TESTS:-1000}" \
  "${POLYMUL_WARMUP:-200}" "$SESSION_SEED" "$FIRST"

RESULT_DIR="$RESULT_ROOT/polymul_second" BUILD_DIR="$BUILD_DIR" SKIP_PRECHECK=1 \
  bash ./run_polymul.sh \
  "${POLYMUL_ROUNDS:-50}" "${POLYMUL_LOOPS:-4000}" "${POLYMUL_HASH_TESTS:-1000}" \
  "${POLYMUL_WARMUP:-200}" "$SESSION_SEED" "$SECOND"

RESULT_DIR="$RESULT_ROOT/kem_first" BUILD_DIR="$BUILD_DIR" SKIP_PRECHECK=1 \
  bash ./run_kem.sh "${KEM_ROUNDS:-1000}" "${KEM_WARMUP:-100}" "$FIRST" "$SESSION_SEED"

RESULT_DIR="$RESULT_ROOT/kem_second" BUILD_DIR="$BUILD_DIR" SKIP_PRECHECK=1 \
  bash ./run_kem.sh "${KEM_ROUNDS:-1000}" "${KEM_WARMUP:-100}" "$SECOND" "$SESSION_SEED"

bash ./env_report.sh "$RESULT_ROOT/env_after.txt" >/dev/null
bash ./check_frequency.sh | tee "$RESULT_ROOT/frequency_after.txt"
cmp "$RESULT_ROOT/frequency_before.txt" "$RESULT_ROOT/frequency_after.txt"
cp BASELINE_DIFF.txt SOURCE_MANIFEST.sha256 "$RESULT_ROOT/"
cp "$BUILD_DIR/build_metadata.txt" "$BUILD_DIR/compiler_macros.txt" "$BUILD_DIR/binary_sha256.txt" "$RESULT_ROOT/"
mkdir -p "$RESULT_ROOT/stack_usage"
cp "$BUILD_DIR"/*.su "$RESULT_ROOT/stack_usage/" 2>/dev/null || true
{
  echo "functional_equivalence=PASS"
  echo "wallclock_session=PASS"
  echo "pmu=NOT_RUN"
  echo "session_id=$SESSION_ID"
  echo "session_seed=$SESSION_SEED"
  echo "design=$DESIGN"
  echo "result_root=$RESULT_ROOT"
} | tee "$RESULT_ROOT/FINAL_STATUS.txt"
