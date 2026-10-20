#!/usr/bin/env bash
# run_release.sh — the entire measurement campaign in one command.
#
#   preflight        permissions, dependencies, CPU_PIN sanity (fails fast)
#   manifest         sha256 manifest check
#   host             confirm CPU_PIN really is the core named in platform.conf
#   frequency        lock the frequency policy (needs sudo)
#   build            build every binary
#   correctness      functional equivalence plus the first-principles oracle
#   extended         multi-compiler hash sweep and switch-variant matrix
#   security         straight-line probes, sanitizers, timing screen (needs objdump)
#   polymul          polynomial core A/B          -> paper table 1
#   kem              per-phase and end-to-end A/B  -> paper table 2
#   sessions         five paired ABBA/BAAB runs    -> session-level detail for tables 1-2
#   ablation         one-factor-at-a-time optimization ablation
#
# usage:  CPU_PIN=<core> bash ./run_release.sh
# env:    QUICK=1                  short counts, extended and sessions skipped
#         SKIP="security sessions" skip the named stages
#         WITH_SECURITY=0          drop the security stage
#         WITH_EXTENDED=0          drop the multi-compiler / variant stages
#         WITH_ABLATION=0          drop the ablation stage
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/protocol.conf"

export CPU_PIN="${CPU_PIN:?set CPU_PIN to the logical core reported by lscpu}"
export CC="${CC:-gcc}"
for tool in "$CC" python3 sha256sum taskset sudo objdump; do
  command -v "$tool" >/dev/null || { echo "error: missing dependency $tool" >&2; exit 2; }
done

RUN_ROOT="$(mktemp -d "$SCRIPT_DIR/run_$(date -u +%Y%m%dT%H%M%SZ)_XXXXXX")"
export BUILD_DIR="$RUN_ROOT/build"
export SECURITY_DIR="$RUN_ROOT/security"
export RESULTS_DIR="$RUN_ROOT/results"
export STATE_FILE="$RUN_ROOT/performance_state.txt"
export USE_LTO=0 CROSS_BUILD=0 ALLOW_NON_TARGET=0 PORTABLE_SANITY=0
export SNTRUP653_USE_INPLACE_BUFFERS=1 SNTRUP653_PUBLIC_ZS_MODE=1
export SNTRUP653_LOW73_NEON=1 SNTRUP653_FINAL_FOLD_NEON=1
unset CFLAGS

STAGES="preflight manifest host frequency build correctness"
[ "${WITH_EXTENDED:-1}" = "1" ] && STAGES="$STAGES extended"
[ "${WITH_SECURITY:-1}" = "1" ] && STAGES="$STAGES security"
STAGES="$STAGES polymul kem sessions"
[ "${WITH_ABLATION:-1}" = "1" ] && STAGES="$STAGES ablation"
if [ "${QUICK:-0}" = "1" ]; then
  STAGES="${STAGES// sessions/}"
  STAGES="${STAGES// extended/}"
  STAGES="${STAGES// ablation/}"
  export POLYMUL_ROUNDS=20 POLYMUL_LOOPS=500 POLYMUL_HASH_TESTS=500
  export KEM_ROUNDS=200 KEM_WARMUP=50
  export CORE_EQUIV_TESTS=20000 KEM_TRANSCRIPT_CASES=200
  export ORACLE_CASES=2000 ORACLE_MODE=all
  export CORE_ROUNDS=100
fi
SKIP="${SKIP:-}"
run_stages=""
for candidate in $STAGES; do
  dropped=0
  for skipped in $SKIP; do
    [ "$candidate" = "$skipped" ] && dropped=1
  done
  [ "$dropped" -eq 0 ] && run_stages="$run_stages $candidate"
done
STAGES="${run_stages# }"

want() { case " $STAGES " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

STAGES_COMPLETED=""
run_stage() {
  local name="$1"
  shift
  if want "$name"; then
    echo
    echo "########## $name ##########"
    "$@"
    STAGES_COMPLETED="${STAGES_COMPLETED}${STAGES_COMPLETED:+ }$name"
  else
    echo
    echo "########## $name SKIPPED ##########"
  fi
}

# ---------------------------------------------------------------- stages
stage_preflight()   { bash ./preflight.sh | tee "$RUN_ROOT/preflight.log"; }
stage_manifest()    { sha256sum -c SOURCE_MANIFEST.sha256 | tee "$RUN_ROOT/manifest.log"; }
stage_host()        { bash ./check_host.sh | tee "$RUN_ROOT/host_check.log"; }
stage_build()       { bash ./build.sh | tee "$RUN_ROOT/build.log"; }
stage_extended() {
  mkdir -p "$RUN_ROOT/extended"
  CC_LIST="${MC_CC_LIST:-gcc clang}" OPT_LIST="${MC_OPT_LIST:--O0 -O1 -O2 -O3 -Os}" \
    HASH_TESTS="${MC_HASH_TESTS:-200}" REQUIRE_ALL_COMPILERS="${MC_REQUIRE_ALL:-1}" \
    bash ./check_multicompiler.sh "$RUN_ROOT/extended/multicompiler" \
    | tee "$RUN_ROOT/extended/multicompiler.log"
  VAR_INTERNAL_TESTS="${VAR_INTERNAL_TESTS:-300}" VAR_CORE_TESTS="${VAR_CORE_TESTS:-2000}" \
    VAR_HASH_TESTS="${VAR_HASH_TESTS:-200}" \
    bash ./check_build_variants.sh "$RUN_ROOT/extended/variants" \
    | tee "$RUN_ROOT/extended/variants.log"
}
stage_frequency() {
  sudo -v
  sudo env CPU_PIN="$CPU_PIN" STATE_FILE="$STATE_FILE" LOCK_MAX_FREQ=1 \
    bash ./set_performance.sh | tee "$RUN_ROOT/set_performance.log"
  bash ./check_frequency.sh | tee "$RUN_ROOT/frequency_before.txt"
}
stage_frequency_after() {
  bash ./check_frequency.sh | tee "$RUN_ROOT/frequency_after.txt"
  cmp "$RUN_ROOT/frequency_before.txt" "$RUN_ROOT/frequency_after.txt"
}
stage_correctness() {
  RESULT_DIR="$RUN_ROOT/correctness" \
    bash ./run_correctness.sh "${CORE_EQUIV_TESTS:-100000}" "${KEM_TRANSCRIPT_CASES:-1000}" "${BASE_SEED:-1}"
}
stage_security()    { bash ./run_security.sh | tee "$RUN_ROOT/security.log"; }
stage_polymul() {
  RESULT_DIR="$RUN_ROOT/results/polymul_ab" SKIP_PRECHECK=1 \
    bash ./run_polymul.sh "${POLYMUL_ROUNDS:-100}" "${POLYMUL_LOOPS:-2000}" "${POLYMUL_HASH_TESTS:-1000}" \
    "${POLYMUL_WARMUP:-200}" "${BASE_SEED:-1}"
}
stage_kem() {
  RESULT_DIR="$RUN_ROOT/results/kem_ab" SKIP_PRECHECK=1 \
    bash ./run_kem.sh "${KEM_ROUNDS:-1000}" "${KEM_WARMUP:-100}" \
    "baseline,optimized,optimized,baseline" "${BASE_SEED:-1}"
}
stage_sessions() {
  CPU_PIN="$CPU_PIN" RESULTS_DIR="$RUN_ROOT/results" BASE_SEED="${BASE_SEED:-20260901}" bash ./run_sessions.sh
}
stage_ablation() {
  CPU_PIN="$CPU_PIN" RESULT_DIR="$RUN_ROOT/results/ablation" \
    bash ./run_ablation.sh "${ABLATION_ROUNDS:-600}" "${ABLATION_WARMUP:-100}" "${BASE_SEED:-20260901}"
}

finish() {
  rc=$?
  trap - EXIT
  if [ -f "$STATE_FILE" ]; then
    sudo bash "$SCRIPT_DIR/restore_performance.sh" "$STATE_FILE" > "$RUN_ROOT/restore.log" 2>&1 || {
      echo "warning: restore the frequency policy manually:"
      echo "  sudo bash \"$SCRIPT_DIR/restore_performance.sh\" \"$STATE_FILE\""
      rc=1
    }
  fi
  {
    echo "run_root=$RUN_ROOT"
    echo "cpu_pin=$CPU_PIN"
    echo "stages_requested=$STAGES"
    echo "stages_completed=${STAGES_COMPLETED:-none}"
    [ -n "$SKIP" ] && echo "stages_skipped=$SKIP"
    echo "exit_code=$rc"
    echo
    echo "artifact                                  paper table"
    echo "$RUN_ROOT/results/polymul_ab/summary.txt   table 1  core speedup + CI"
    echo "$RUN_ROOT/results/kem_ab/summary.txt       table 2  keygen/encaps/decaps/total/e2e"
    echo "$RUN_ROOT/results/summary_sessions.txt     tables 1-2  session-level speedup + CI"
    echo "$RUN_ROOT/results/ablation/summary.txt      optimization ablation"
    echo "$RUN_ROOT/correctness/                     equivalence + oracle + TSan + stack evidence"
    echo "$RUN_ROOT/extended/multicompiler.log       compiler/optimisation hash sweep"
    echo "$RUN_ROOT/extended/variants.log            per-switch variant matrix"
    echo "$RUN_ROOT/security/STATUS.txt              security scope"
  } | tee "$RUN_ROOT/RUN_SUMMARY.txt"
  if [ "$rc" -ne 0 ]; then echo "release=FAIL exit=$rc" > "$RUN_ROOT/FAILED.txt"; fi
  if [ "$rc" -eq 0 ]; then
    {
      echo "functional_and_benchmark_workflow=PASS"
      echo "frequency_restore=PASS"
      if want extended; then
        echo "gcc_clang_invariance=PASS"
      fi
      echo "constant_time_proof=NOT_CLAIMED"
      echo "formal_verification=NOT_CLAIMED"
    } > "$RUN_ROOT/COMPLETE.txt"
  fi
  echo "All outputs: $RUN_ROOT"
  exit "$rc"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

run_stage preflight        stage_preflight
run_stage manifest         stage_manifest
run_stage host             stage_host
run_stage frequency        stage_frequency
run_stage build            stage_build
run_stage correctness      stage_correctness
run_stage extended         stage_extended
run_stage security         stage_security
run_stage polymul          stage_polymul
run_stage kem              stage_kem
run_stage sessions         stage_sessions
run_stage ablation         stage_ablation
if want frequency; then
  echo
  echo "########## frequency_after ##########"
  stage_frequency_after
fi
