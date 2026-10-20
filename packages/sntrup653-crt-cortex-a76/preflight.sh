#!/usr/bin/env bash
# preflight.sh — fail in two seconds, not after the manifest has run.
#
# The first A53 campaign left a 0-byte host_check.log and exit=126 because the
# zip had been packed without permission bits, so ./check_host.sh was not
# executable.  Everything upstream of that point had already succeeded, which
# made the failure look like a code problem.
#
# That failure mode is designed out rather than merely detected: every script
# in this package is launched through an explicit interpreter (`bash ./x.sh`,
# `python3 x.py`), so no execute bit is required anywhere.  The permission
# check below therefore reports a warning, not a problem, and is kept only
# because a root directory of non-executable scripts is a sign that the archive
# was unpacked oddly or landed on a filesystem without POSIX modes.
#
# What still fails fast:
#
#   * every external command the runners need is installed, objdump included
#   * the compiler exists and really targets AArch64
#   * CPU_PIN, if set, is a plausible core number
#   * the work tree is writable and the source manifest is present
#
# usage: bash ./preflight.sh            (CPU_PIN optional)
#        CPU_PIN=3 bash ./preflight.sh
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
. "$SCRIPT_DIR/platform.conf"

problems=0
warnings=0

note()  { printf '  %s\n' "$*"; }
fail()  { printf '  FAIL  %s\n' "$*"; problems=$((problems + 1)); }
warn()  { printf '  WARN  %s\n' "$*"; warnings=$((warnings + 1)); }
ok()    { printf '  ok    %s\n' "$*"; }

echo "preflight: sntrup653 CRT core package ($CPU_MODEL, part $CPU_PART)"
echo

echo "[1] script permissions (advisory)"
noexec=""
while IFS= read -r f; do
  [ -n "$f" ] || continue
  if [ ! -x "$f" ]; then noexec="$noexec $f"; fi
done < <(find . -maxdepth 1 -type f \( -name '*.sh' -o -name '*.py' \) -print | sort)
if [ -n "$noexec" ]; then
  warn "no execute bit:$noexec"
  note "this is not fatal: every script is launched as 'bash ./x.sh' or"
  note "'python3 x.py', so the campaign needs no execute bit at all."
  note "cause: the archive was unpacked without permission bits, or landed"
  note "       on a filesystem that cannot store them."
  note "cosmetic fix:  chmod +x ./*.sh ./*.py"
else
  ok "every .sh/.py in the package root is executable"
fi
echo

echo "[2] required commands"
for tool in bash python3 sha256sum taskset objdump mktemp cmp find sort awk sed grep uname chmod; do
  if command -v "$tool" >/dev/null 2>&1; then
    ok "$tool -> $(command -v "$tool")"
  else
    case "$tool" in
      objdump) fail "$tool is missing (install binutils); run_security.sh needs it" ;;
      taskset) fail "$tool is missing (install util-linux); the benchmark cannot pin a core" ;;
      python3) fail "$tool is missing; every summariser needs it" ;;
      *)       fail "$tool is missing" ;;
    esac
  fi
done
if command -v gcc >/dev/null 2>&1; then
  ok "gcc -> $(gcc -dumpversion 2>/dev/null || echo present)"
else
  fail "gcc is missing"
fi
if command -v clang >/dev/null 2>&1; then
  ok "clang -> $(clang --version 2>/dev/null | head -1)"
else
  fail "clang is missing; install it with: sudo apt-get update && sudo apt-get install -y clang"
fi
echo

echo "[3] host architecture and compiler target"
arch="$(uname -m 2>/dev/null || echo unknown)"
case "$arch" in
  aarch64|arm64) ok "uname -m = $arch" ;;
  *) warn "uname -m = $arch, not AArch64; use LOCAL_EMULATION=1 under QEMU or ALLOW_NON_TARGET=1 to override" ;;
esac
if command -v gcc >/dev/null 2>&1; then
  gcc_macros="$(printf '' | gcc -dM -E -x c - 2>/dev/null || true)"
  case "$gcc_macros" in
    *__aarch64__*) ok "default gcc targets AArch64" ;;
    *) fail "default gcc does not define __aarch64__; the core requires AArch64 NEON" ;;
  esac
fi
if command -v clang >/dev/null 2>&1; then
  clang_macros="$(printf '' | clang -dM -E -x c - 2>/dev/null || true)"
  case "$clang_macros" in
    *__aarch64__*) ok "default clang targets AArch64" ;;
    *) fail "default clang does not define __aarch64__; the core requires AArch64 NEON" ;;
  esac
fi
echo

echo "[4] core pinning"
if [ -n "${CPU_PIN:-}" ]; then
  case "$CPU_PIN" in
    *[!0-9]*|'') fail "CPU_PIN='$CPU_PIN' is not an unsigned integer" ;;
    *)
      if command -v taskset >/dev/null 2>&1 && taskset -c "$CPU_PIN" true >/dev/null 2>&1; then
        ok "CPU_PIN=$CPU_PIN is an available CPU"
      else
        fail "CPU_PIN=$CPU_PIN cannot be used by this process"
      fi
      ;;
  esac
else
  note "CPU_PIN is unset; run_all.sh will detect it, run_release.sh requires it"
fi
echo

echo "[5] work tree"
if [ -r SOURCE_MANIFEST.sha256 ]; then
  n="$(wc -l < SOURCE_MANIFEST.sha256 | tr -d ' ')"
  ok "SOURCE_MANIFEST.sha256 present ($n entries)"
else
  fail "SOURCE_MANIFEST.sha256 is missing; run bash ./generate_manifest.sh"
fi
for tree in baseline optimized shared; do
  if [ -d "$tree" ]; then ok "$tree/ present"; else fail "$tree/ is missing"; fi
done
if touch .preflight_write_test 2>/dev/null; then
  rm -f .preflight_write_test
  ok "working directory is writable"
else
  fail "working directory is not writable"
fi
if [ -r expected_oracle_hashes.txt ]; then
  ok "expected_oracle_hashes.txt present ($(grep -c . expected_oracle_hashes.txt) lines)"
else
  fail "expected_oracle_hashes.txt is missing; run python3 oracle_reference.py --write"
fi
echo

echo "[6] optional accelerators"
if command -v perf >/dev/null 2>&1; then
  ok "perf available (PMU counting still requires kernel permission)"
else
  note "perf not installed; the package does not require it"
fi
if [ -r /proc/sys/kernel/perf_event_paranoid ]; then
  note "perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid)"
fi
if [ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
  note "cpu0 governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
fi

echo
if [ "$problems" -ne 0 ]; then
  echo "preflight=FAIL problems=$problems warnings=$warnings"
  exit 1
fi
echo "preflight=PASS warnings=$warnings"
exit 0
