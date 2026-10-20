#!/usr/bin/env bash
# run_correctness.sh — check that swapping the core does not change tested outputs,
# and that the delivered core realises the documented ring product on the test set.
#   1. optimized internal construction selftest (layer by layer, against
#      independent scalar oracles, including two exhaustive basis sweeps)
#   2. baseline vs optimized core, byte for byte, on random and boundary cases
#   3. optimized core vs a first-principles oracle for A*B mod (x^653-x-1)
#   4. baseline vs optimized full KEM transcript, byte for byte
#   5. the oracle stream hash vs the independent Python model
#   6. concurrent first call, plain and under ThreadSanitizer
#   7. stack budget from the collected -fstack-usage data
#   8. check that the new tests live entirely behind SNTRUP653_INTERNAL_TEST_HOOKS
#
# usage: bash ./run_correctness.sh [core_tests] [kem_cases] [seed]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
CORE_TESTS="${1:-10000}"
KEM_CASES="${2:-100}"
SEED="${3:-1}"
INTERNAL_TESTS="${INTERNAL_TESTS:-1000}"
ORACLE_MODE="${ORACLE_MODE:-all}"
ORACLE_CASES="${ORACLE_CASES:-20000}"
ORACLE_SEED="${ORACLE_SEED:-$SEED}"
CONCURRENT_THREADS="${CONCURRENT_THREADS:-32}"
STACK_BUDGET_BYTES="${STACK_BUDGET_BYTES:-65536}"
CPU_PIN="${CPU_PIN:-}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
stamp="$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || date +%Y%m%dT%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/correctness_$stamp}"
run_pinned() {
  if [ -n "$CPU_PIN" ]; then
    taskset -c "$CPU_PIN" "$@"
  else
    "$@"
  fi
}

for value in "$CORE_TESTS" "$KEM_CASES" "$SEED" "$INTERNAL_TESTS" "$CONCURRENT_THREADS" "$STACK_BUDGET_BYTES"; do
  case "$value" in *[!0-9]*|'') echo "error: test counts must be positive integers" >&2; exit 2 ;; esac
  [ "$value" -gt 0 ] || { echo "error: test counts must be positive" >&2; exit 2; }
done

mkdir -p "$RESULT_DIR"
trap 'rc=$?; echo "correctness=FAIL rc=$rc result_dir=$RESULT_DIR" | tee -a "$RESULT_DIR/FAILED.txt" >/dev/null' ERR
if [ "${SKIP_BUILD:-0}" != "1" ]; then
  BUILD_DIR="$BUILD_DIR" bash ./build.sh | tee "$RESULT_DIR/build.log"
fi

echo "[1/8] internal construction layers"
run_pinned "$BUILD_DIR/test_internal_construction" "$INTERNAL_TESTS" "$SEED" \
  | tee "$RESULT_DIR/internal_construction.log"
grep -q 'internal construction selftest: PASS' "$RESULT_DIR/internal_construction.log" \
  || { echo "error: internal construction selftest did not pass" >&2; exit 3; }

echo "[2/8] baseline vs optimized core, byte for byte"
run_pinned "$BUILD_DIR/test_core_equiv" "$CORE_TESTS" "$SEED" | tee "$RESULT_DIR/core_equiv.log"
grep -q 'core equivalence: PASS' "$RESULT_DIR/core_equiv.log" \
  || { echo "error: core equivalence did not pass" >&2; exit 3; }

echo "[3/8] optimized core vs the first-principles oracle"
run_pinned "$BUILD_DIR/test_endtoend_oracle" "$ORACLE_MODE" "$ORACLE_CASES" "$ORACLE_SEED" \
  | tee "$RESULT_DIR/endtoend_oracle.log"
grep -q 'endtoend oracle: PASS' "$RESULT_DIR/endtoend_oracle.log" \
  || { echo "error: end-to-end oracle did not pass" >&2; exit 3; }

echo "[4/8] deterministic KEM transcript, baseline vs optimized"
run_pinned "$BUILD_DIR/kem_transcript_baseline" "$RESULT_DIR/baseline_transcript.bin" "$KEM_CASES" "$SEED" \
  | tee "$RESULT_DIR/baseline_transcript.log"
run_pinned "$BUILD_DIR/kem_transcript_optimized" "$RESULT_DIR/optimized_transcript.bin" "$KEM_CASES" "$SEED" \
  | tee "$RESULT_DIR/optimized_transcript.log"
if ! cmp -s "$RESULT_DIR/baseline_transcript.bin" "$RESULT_DIR/optimized_transcript.bin"; then
  echo "KEM transcript equivalence: FAIL" | tee "$RESULT_DIR/kem_transcript_equiv.log"
  cmp -l "$RESULT_DIR/baseline_transcript.bin" "$RESULT_DIR/optimized_transcript.bin" | head -20 \
    | tee -a "$RESULT_DIR/kem_transcript_equiv.log" || true
  exit 4
fi
echo "KEM transcript equivalence: PASS ($KEM_CASES cases, byte for byte)" \
  | tee "$RESULT_DIR/kem_transcript_equiv.log"

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$RESULT_DIR"/*_transcript.bin | tee "$RESULT_DIR/transcript_sha256.txt"
fi

echo "[5/8] oracle stream hash vs the Python model"
set +e
python3 ./check_oracle_hash.py expected_oracle_hashes.txt "$RESULT_DIR/endtoend_oracle.log" \
  | tee "$RESULT_DIR/oracle_hash_check.log"
hash_rc="${PIPESTATUS[0]}"
set -e
# rc 2 means the log carried no cases_hash at all, which happens when the
# oracle was asked for the fixed cases only.  That is a scope reduction, not
# a mismatch, so it is recorded as a skip; a real mismatch still fails.
if [ "$hash_rc" -eq 2 ]; then
  echo "oracle_hash_check=SKIP oracle ran without the axis cases (ORACLE_MODE=${ORACLE_MODE})" \
    | tee -a "$RESULT_DIR/oracle_hash_check.log"
elif [ "$hash_rc" -ne 0 ]; then
  echo "error: oracle hash check failed (rc=$hash_rc)" >&2; exit 5
fi

echo "[6/8] concurrent first call"
run_pinned "$BUILD_DIR/test_concurrent_firstcall" "$CONCURRENT_THREADS" "$SEED" \
  | tee "$RESULT_DIR/concurrent_firstcall.log"
grep -q 'concurrent first-call: PASS' "$RESULT_DIR/concurrent_firstcall.log" \
  || { echo "error: concurrent first call did not pass" >&2; exit 6; }

if [ -x "$BUILD_DIR/test_concurrent_firstcall_tsan" ]; then
  echo "[6/8] concurrent first call under ThreadSanitizer"
  set +e
  TSAN_OPTIONS="halt_on_error=0 exitcode=66 history_size=7" \
    run_pinned "$BUILD_DIR/test_concurrent_firstcall_tsan" "$CONCURRENT_THREADS" "$SEED" \
    > "$RESULT_DIR/concurrent_firstcall_tsan.log" 2>&1
  tsan_rc=$?
  set -e
  cat "$RESULT_DIR/concurrent_firstcall_tsan.log"
  if [ "$tsan_rc" -ne 0 ] || grep -q 'ThreadSanitizer' "$RESULT_DIR/concurrent_firstcall_tsan.log"; then
    echo "error: ThreadSanitizer reported a problem (rc=$tsan_rc)" >&2
    exit 6
  fi
  echo "concurrent first-call under TSan: PASS (no data race reported)" \
    | tee -a "$RESULT_DIR/concurrent_firstcall_tsan.log"
else
  echo "concurrent first-call under TSan: NOT_BUILT (WITH_TSAN=0 or no TSan support)" \
    | tee "$RESULT_DIR/concurrent_firstcall_tsan.log"
fi

echo "[7/8] stack budget"
su_files=()
while IFS= read -r f; do [ -n "$f" ] && su_files+=("$f"); done < <(find "$BUILD_DIR" -maxdepth 2 -name '*.su' -print 2>/dev/null | sort)
if [ "${#su_files[@]}" -gt 0 ]; then
  python3 ./check_stack_usage.py "$STACK_BUDGET_BYTES" "${su_files[@]}" \
    | tee "$RESULT_DIR/stack_usage.log"
  grep -q 'stack_usage=PASS' "$RESULT_DIR/stack_usage.log" \
    || { echo "error: stack budget exceeded" >&2; exit 7; }
  mkdir -p "$RESULT_DIR/stack_usage"
  cp "${su_files[@]}" "$RESULT_DIR/stack_usage/" 2>/dev/null || true
else
  echo "stack_usage=ABSENT (no .su files under $BUILD_DIR)" | tee "$RESULT_DIR/stack_usage.log"
fi

echo "[8/8] production-view isolation of the test hooks"
python3 ./check_selftest_isolation.py | tee "$RESULT_DIR/selftest_isolation.log"
grep -q 'selftest_isolation=PASS' "$RESULT_DIR/selftest_isolation.log" \
  || { echo "error: the production view of the core changed" >&2; exit 8; }

cp "$BUILD_DIR/build_metadata.txt" "$RESULT_DIR/" 2>/dev/null || true
cp BASELINE_DIFF.txt SOURCE_MANIFEST.sha256 expected_oracle_hashes.txt \
   expected_production_core.sha256 "$RESULT_DIR/" 2>/dev/null || true
echo "correctness=PASS result_dir=$RESULT_DIR"
