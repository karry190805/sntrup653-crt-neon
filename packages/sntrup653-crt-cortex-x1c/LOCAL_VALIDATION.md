# Local delivery validation

Date: 2026-09-20, re-packaged 2026-10-14. Host: Windows/WSL x86_64, not one of the
target boards. Toolchain: aarch64-linux-gnu-gcc 15.2.0; QEMU AArch64 10.2.1.

For all three target flags (cortex-a53, cortex-a76, cortex-x1c):

- Cross build of the eight standard binaries: PASS.
- Seven straight-line scalar helper assembly probes: PASS.
- Previously observed sign-bit branches in core/KEM benchmark core: absent.
- QEMU eight-thread first-call test: PASS.
- QEMU full-width reducer and exhaustive bounded tests: PASS.
- QEMU internal construction and negative mask test: PASS.
- QEMU 1000 raw-byte input, output canary, alias and low73 tests: PASS.
- The same security functional checks under UBSan: PASS.
- QEMU baseline-vs-optimized core: 10000 random cases plus fixed cases, PASS.
- QEMU baseline-vs-optimized KEM transcript: 20 cases, byte-identical.

An attempted combined ASan/UBSan executable failed during ASan runtime startup
under QEMU, before tests executed. This is NOT an ASan pass. Native ASan/UBSan
remains mandatory in `run_release.sh` and fails closed on error.

Native target timings, native timing leakage screens, native ASan, and a second
compiler have NOT been run here. Old performance numbers must not be attached to
this revision. Local logs and binaries are kept outside release archives. No
formal constant-time or universal memory-safety proof is claimed.

## What the 2026-10-14 re-packaging changed

Naming only, plus new measurement code. The polynomial core, the arithmetic, and
every security-relevant construct are unchanged.

- `official_clean/` → `baseline/`, `ours/` → `optimized/`, and the matching
  script, binary, tag and symbol renames.
- `summarize.py` now also reports the end-to-end interval and a per-metric
  verdict, and replaces `summarize_sessions.py`.
- `run_delivery.sh` → `run_release.sh`, `run_all.sh` → `run_session.sh`,
  `run_five_sessions.sh` → `run_sessions.sh`.
- `USE_BARRETT_REDUCE` became an explicit knob.

Nothing in this re-packaging was re-validated on hardware. The statements above
describe the 2026-09-20 core, which is unchanged byte for byte
(`optimized/crypto_core_multsntrup653.c`), and the new scripts are validated
only statically.

## What the 2026-10-16 verification revision changed

Test coverage only. The production arithmetic of the core is unchanged, and this
is checkable rather than merely asserted: `check_selftest_isolation.py` strips
every block gated by `SNTRUP653_INTERNAL_TEST_HOOKS` and hashes the remainder.
The production view is 1026 lines and hashes to
`9749385416ba3e53bf1fe81be01f5becb6a74ea723b60e100fcfd5910faabad4`, which is
identical to the production view of the 2026-10-14 revision. Consequently the
core correctness hash of every benchmark binary is unchanged as well, and
`check_build_variants.sh` re-derives that hash for nine build configurations.

Added:

- `shared/test_endtoend_oracle.c` and `oracle_reference.py` — the first-principles
  oracle for `A·B mod (q, x^653-x-1)`, written twice and independently, with
  `expected_oracle_hashes.txt` / `check_oracle_hash.py` enforcing agreement.
- `check_multicompiler.sh` — gcc and clang at `-O0 -O1 -O2 -O3 -Os`, one hash.
- `check_build_variants.sh` — nine switch combinations, one hash, including the
  `SNTRUP653_PUBLIC_ZS_RUNTIME_MASK=1` arm.
- `shared/test_concurrent_firstcall.c` — plain and ThreadSanitizer builds, so the
  lazy table initialisation is now checked for races instead of only for
  functional equality.
- `check_stack_usage.py` — the `-fstack-usage` data collected by `build.sh` is
  now actually read and budgeted.
- `check_selftest_isolation.py` — byte-level check that the new tests stay behind
  `SNTRUP653_INTERNAL_TEST_HOOKS`.
- `preflight.sh` — permissions, dependencies (including the previously missing
  `objdump`), architecture and CPU_PIN, in about two seconds.
- `run_all.sh` — board detection, core selection and one sudo prompt, then the
  entire campaign in one command.
- `VERIFICATION.md` — claim, evidence, command and limit for each layer.

Inside `sntrup653_internal_selftest`, still behind the test-hooks macro:

- code 70 — exhaustive 73 × 73 × 2 monomial-pair sweep for the low-73 branch,
  replacing a 1000-case random comparison;
- code 71 — exhaustive 1305 unit-vector sweep for the final fold, replacing a
  single random case.

Neither of the two new sweeps is reachable from a production binary. They run
only in `test_internal_construction`.

## What the 2026-10-16 packaging revision changed

Launch harness only. Again none of the arithmetic moves, and the same
`check_selftest_isolation.py` comparison applies unchanged.

Two campaigns died before reaching any measurement because the archive arrived
without execute bits: the first left a 0-byte `host_check.log` and `exit=126`,
the second stopped at `preflight=FAIL problems=1`. Both were archive metadata,
not code. The cause on Linux is that a zip written on Windows carries
`create_system = 0` (MS-DOS), and the version of `unzip` on the target board then
ignores the Unix mode stored in the upper half of `external_attr`, even though
that mode is `0100755`.

Rather than only detect it, the dependency is gone:

- every script launches its children through an explicit interpreter —
  `bash ./x.sh` for shell, `python3 x.py` for Python — so no execute bit is
  needed anywhere in the tree;
- `preflight.sh` reports missing execute bits as a warning, not a problem, and
  says why they are not required;
- `run_all.sh` restores the bits with `chmod +x` if it can, and says so, but the
  campaign does not depend on the outcome;
- the zips are rewritten with `create_system = 3` (Unix) so that Info-ZIP does
  restore the modes on the boards where it honours them;
- a `.tar.gz` of the identical tree is shipped alongside each zip, since tar
  carries modes without any metadata convention to get wrong.

