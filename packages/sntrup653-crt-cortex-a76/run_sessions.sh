#!/usr/bin/env bash
# run_sessions.sh — five independent paired sessions, alternating ABBA/BAAB.
# Produces the session-level table: the finest unit that can be treated as a
# replication, so the confidence interval is not built from raw rounds.
#
# usage: CPU_PIN=<core> bash ./run_sessions.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/protocol.conf"

export CPU_PIN="${CPU_PIN:?set the logical target CPU number explicitly}"
BASE_SEED="${BASE_SEED:-20260901}"
RESULTS_DIR="${RESULTS_DIR:-$SCRIPT_DIR/results}"
mkdir -p "$RESULTS_DIR"

for i in 1 2 3 4 5; do
  session="$(printf 'session_%02d' "$i")"
  [ ! -e "$RESULTS_DIR/$session" ] || {
    echo "error: $RESULTS_DIR/$session already exists" >&2
    exit 2
  }
done

for i in 1 2 3 4 5; do
  session="$(printf 'session_%02d' "$i")"
  seed=$((BASE_SEED + i - 1))
  if [ $((i % 2)) -eq 1 ]; then design=ABBA_FIRST; else design=BAAB_FIRST; fi
  CPU_PIN="$CPU_PIN" SESSION_ID="$(date -u +%Y%m%dT%H%M%SZ)" \
    SESSION_SEED="$seed" DESIGN="$design" RESULT_ROOT="$RESULTS_DIR/$session" \
    bash ./run_session.sh
done

python3 ./summarize.py sessions "$RESULTS_DIR/summary_sessions.csv" "$RESULTS_DIR"/session_* \
  | tee "$RESULTS_DIR/summary_sessions.txt"
