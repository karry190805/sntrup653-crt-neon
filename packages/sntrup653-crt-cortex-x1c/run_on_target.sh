#!/usr/bin/env bash
# run_on_target.sh - single entry point: detect the board, run the campaign,
# and pack the results for return.
#
#   bash ./run_on_target.sh              detect the core, full campaign, auto-pack
#   bash ./run_on_target.sh --no-pack    campaign only
#
# Everything else is passed straight through to run_all.sh:
#   CPU_PIN=4   a core other than the detected one
#   QUICK=1     short counts; extended, sessions and ablation skipped
#   UNATTENDED=1  never prompt (needs pre-authorised passwordless sudo)
#   SKIP="security sessions ablation"
#
# run_all.sh does the board detection, the dependency check, the one sudo
# prompt and the preflight; this wrapper only adds the packing step, so
# `bash ./run_all.sh` on its own remains exactly equivalent minus the archive.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

pack=1
for arg in "$@"; do
  case "$arg" in
    --no-pack) pack=0 ;;
    -h|--help)
      sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "run_on_target: unknown argument '$arg' (try --help)" >&2; exit 2 ;;
  esac
done

echo "==================================================================="
echo " sntrup653 CRT core - target campaign"
echo "==================================================================="
echo

rc=0
bash ./run_all.sh || rc=$?

if [ "$pack" = "1" ]; then
  echo
  echo "########## pack_results ##########"
  if ! python3 ./pack_results.py; then
    echo "run_on_target: the campaign finished but the archive was not written." >&2
    [ "$rc" -eq 0 ] && rc=1
  fi
fi

echo
if [ "$rc" -eq 0 ]; then
  echo "target_campaign=PASS"
else
  echo "target_campaign=FAIL rc=$rc"
fi
exit "$rc"
