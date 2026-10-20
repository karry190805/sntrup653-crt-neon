#!/usr/bin/env bash
# run_all.sh — one command per board, no step-by-step confirmations.
#
# It
#   1. works out which board it is running on from the per-core MIDR values,
#      and refuses to continue if the board does not match platform.conf;
#   2. picks a core of the right type and exports CPU_PIN;
#   3. asks for sudo once (the frequency lock needs root) so that the rest of
#      the campaign never prompts again;
#   4. restores missing execute bits if the archive lost them (cosmetic only:
#      nothing here needs an execute bit), runs preflight.sh, then the whole
#      campaign through run_release.sh.
#
# usage:
#   bash ./run_all.sh                  # auto-detect the core, run everything
#   CPU_PIN=4 bash ./run_all.sh        # use an explicit logical core
#   QUICK=1 bash ./run_all.sh          # short counts; sessions/extended/ablation skipped
#   UNATTENDED=1 bash ./run_all.sh     # never prompt; needs passwordless sudo
#   SKIP="security" bash ./run_all.sh  # pass through to run_release.sh
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

note() { printf '%s\n' "$*"; }

echo "==================================================================="
echo " sntrup653 CRT core — automatic campaign"
echo " package platform: $CPU_MODEL  (ARM part $CPU_PART)"
echo "==================================================================="

# ------------------------------------------------------------------ 1. board
arch="$(uname -m 2>/dev/null || echo unknown)"
note "[1/4] host"
note "      uname -m = $arch"
if [ "$arch" != "aarch64" ] && [ "$arch" != "arm64" ]; then
  note "      this is not an AArch64 host."
  note "      The core requires AArch64 NEON and check_host.sh will refuse."
  note "      Set ALLOW_NON_TARGET=1 only for portability debugging."
fi

midr_root="/sys/devices/system/cpu"
declare -a match_cpus=()
declare -a match_caps=()
if [ -d "$midr_root" ]; then
  for cpu_dir in "$midr_root"/cpu[0-9]*; do
    [ -d "$cpu_dir" ] || continue
    cpu="${cpu_dir##*cpu}"
    online="$(cat "$cpu_dir/online" 2>/dev/null || echo 1)"
    [ "$online" = "1" ] || continue
    mf="$cpu_dir/regs/identification/midr_el1"
    [ -r "$mf" ] || continue
    midr="$(tr '[:upper:]' '[:lower:]' < "$mf" 2>/dev/null | tr -d ' \n')"
    case "$midr" in 0x*) ;; *) midr="0x$midr" ;; esac
    part="$(printf '0x%x' $(( (midr >> 4) & 0xfff )) 2>/dev/null || echo unknown)"
    if [ "$part" = "$CPU_PART" ]; then
      cap="$(cat "$cpu_dir/cpu_capacity" 2>/dev/null || echo 0)"
      match_cpus+=("$cpu")
      match_caps+=("$cap")
      note "      cpu$cpu  part=$part  capacity=$cap   <- matches $CPU_MODEL"
    else
      note "      cpu$cpu  part=$part  capacity=$(cat "$cpu_dir/cpu_capacity" 2>/dev/null || echo 0)"
    fi
  done
fi

if [ "${#match_cpus[@]}" -eq 0 ]; then
  if [ -n "${CPU_PIN:-}" ]; then
    note "      no core reported part $CPU_PART; falling back to CPU_PIN=$CPU_PIN"
  else
    note ""
    note "ERROR: no online core reports ARM part $CPU_PART."
    note "       Either this is not a $CPU_MODEL board, or the package was"
    note "       unpacked for the wrong platform.  Set CPU_PIN explicitly and"
    note "       ALLOW_NON_TARGET=1 to override on purpose."
    exit 2
  fi
else
  # Highest capacity wins; ties are broken towards the highest CPU number so
  # that cpu0, which carries most interrupt traffic, is not chosen.
  best_idx=0
  for i in "${!match_cpus[@]}"; do
    if [ "${match_caps[$i]}" -gt "${match_caps[$best_idx]}" ] ||
       { [ "${match_caps[$i]}" -eq "${match_caps[$best_idx]}" ] &&
         [ "${match_cpus[$i]}" -gt "${match_cpus[$best_idx]}" ]; }; then
      best_idx=$i
    fi
  done
  auto_pin="${match_cpus[$best_idx]}"
  if [ -n "${CPU_PIN:-}" ]; then
    note "      CPU_PIN=$CPU_PIN given explicitly; auto-detected cpu$auto_pin"
  else
    CPU_PIN="$auto_pin"
    note "      selected CPU_PIN=$CPU_PIN (capacity ${match_caps[$best_idx]})"
  fi
fi
export CPU_PIN

# ------------------------------------------------------------------ 2. tools
note ""
note "[2/4] prerequisites"
missing=""
for tool in bash python3 sha256sum taskset objdump sudo gcc mktemp cmp find sort awk sed grep uname; do
  command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if ! command -v clang >/dev/null 2>&1; then
  missing="$missing clang"
fi
if [ -n "$missing" ]; then
  note "      ERROR: missing commands:$missing"
  note "      Debian/Ubuntu:  sudo apt-get update && sudo apt-get install -y --no-install-recommends binutils util-linux coreutils gcc clang python3"
  note "      Fedora:         sudo dnf install binutils util-linux coreutils gcc clang python3"
  exit 2
fi
note "      every required command is present"
note "      clang present; the multi-compiler sweep will cover gcc and clang"

# ------------------------------------------------------------------ 3. sudo
note ""
note "[3/4] privileges"
if [ "${UNATTENDED:-0}" = "1" ]; then
  if sudo -n true 2>/dev/null; then
    note "      passwordless sudo confirmed"
  else
    note "      ERROR: UNATTENDED=1 was requested but sudo still needs a password."
    note "      Either drop UNATTENDED, or pre-authorise with: echo ... | sudo -S -v"
    exit 2
  fi
else
  note "      the frequency lock needs root; you will be asked once, now."
  if ! sudo -v; then
    note "      ERROR: sudo was refused; the campaign cannot lock the frequency."
    exit 2
  fi
  note "      sudo credentials cached; the rest of the run will not prompt"
fi

# ------------------------------------------------------------- 4. campaign
note ""
note "[4/4] preflight, then the full campaign"
note "      run root will be printed at the end"

# Best effort, cosmetic only.  No script in this package needs an execute bit:
# every one of them is launched as 'bash ./x.sh' or 'python3 x.py'.  But an
# archive that arrives with the modes stripped (Windows-made zip, exFAT stick,
# some unzip builds) is worth tidying up before an hour-long campaign starts,
# and it costs nothing to try.
stripped=0
for f in ./*.sh ./*.py; do
  [ -f "$f" ] || continue
  if [ ! -x "$f" ]; then stripped=1; break; fi
done
if [ "$stripped" = "1" ]; then
  if chmod +x ./*.sh ./*.py 2>/dev/null; then
    note "      execute bits were missing; restored (not required to run)"
  else
    note "      execute bits are missing and cannot be set here;"
    note "      the campaign still runs, scripts are launched via bash/python3"
  fi
fi
note ""

if ! bash ./preflight.sh; then
  note ""
  note "preflight refused to continue.  Fix the problems above and re-run."
  exit 1
fi

note ""
exec bash ./run_release.sh
