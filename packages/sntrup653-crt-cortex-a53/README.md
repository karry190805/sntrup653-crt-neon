# sntrup653 CRT polynomial core — measurement package

One optimized file, `optimized/crypto_core_multsntrup653.c`. Every other PQClean
round-3 KEM file is byte-identical between `baseline/` and `optimized/`; `build.sh`
enforces that the two trees differ in exactly `crypto_core_multsntrup653.c`.

The compile-time switches, all on in the delivered configuration:

| Switch | Effect |
|---|---|
| `SNTRUP653_USE_INPLACE_BUFFERS` | time-domain arrays reused in place, no 2×77×16 staging copy |
| `SNTRUP653_PUBLIC_ZS_MODE` | first Good–Thomas layer specialised at build time, public zero blocks dropped |
| `SNTRUP653_LOW73_NEON` | 73-coefficient low part as a four-lane NEON dot product |
| `SNTRUP653_FINAL_FOLD_NEON` | four-lane NEON add/sub for the reduction modulo `x^653-x-1` |
| `USE_BARRETT_REDUCE` | Barrett reduction (1, default) instead of the `% q` fallback (0) |

`SNTRUP653_PUBLIC_ZS_RUNTIME_MASK` (default 0, off in the delivered build) selects
the legacy run-time-masking arm of the first layer; `check_build_variants.sh`
builds and tests it so it cannot rot unnoticed.

## Run

```bash
sudo apt-get update && sudo apt-get install -y clang
unzip -q sntrup653-crt-cortex-a53-gcc-clang.zip -d ~/sntrup
cd ~/sntrup/sntrup653-crt-cortex-a53
bash ./run_on_target.sh
```

`run_on_target.sh` is the entry point.  It calls `run_all.sh`, and once the
campaign finishes it packs the results into one archive next to the package
(`sntrup653-crt-cortex-a53-results-<stamp>.zip`, the build tree left out, set `INCLUDE_BUILD=1` to
keep it).  `--no-pack` stops after the campaign.  `bash ./run_all.sh` on its
own runs exactly the same campaign without the packing step.

The ZIP does not need a `chmod`. Every script is launched through an explicit interpreter
(`bash ./x.sh`, `python3 x.py`), so the campaign runs unchanged even if the
archive is unpacked without execute bits — which is what a zip built on Windows
does on Linux, and what any non-POSIX filesystem does. `run_all.sh` restores the
bits if it can, purely as a convenience.

`run_all.sh` identifies the board from the per-core MIDR values, refuses to
continue on the wrong board, picks a core of the right type, asks for sudo once
(the frequency lock needs root) and then runs the whole campaign. `CPU_PIN=4`
overrides the detected core, `QUICK=1` takes the short path, `UNATTENDED=1`
requires pre-authorised passwordless sudo.

The campaign itself is `run_release.sh`:

```
preflight → manifest → host → frequency lock → build → correctness → extended
          → security → polymul A/B → KEM A/B → five sessions → ablation
          → frequency restore
```

`SKIP="security sessions"` drops the named stages, `WITH_SECURITY=0` and
`WITH_EXTENDED=0` drop those two. Outputs land in a fresh
`run_<timestamp>_<random>/`; `RUN_SUMMARY.txt` maps each artifact to a table, and
the frequency policy is always restored on exit.

To run one experiment at a time (`CPU_PIN` must be exported):

```bash
bash ./preflight.sh                   # dependencies, CPU_PIN (permissions advisory)
bash ./run_correctness.sh 10000 100 1 # all eight correctness stages
bash ./check_multicompiler.sh         # gcc/clang × -O0..-Os, one hash
bash ./check_build_variants.sh        # every switch on and off, one hash
bash ./run_polymul.sh                 # core only               -> paper table 1
bash ./run_kem.sh                     # keygen/encaps/decaps/e2e -> paper table 2
bash ./run_sessions.sh                # five paired sessions    -> tables 1-2 detail
bash ./run_ablation.sh                # one-factor-at-a-time optimization ablation
```

## What each experiment answers

* `run_polymul.sh` — speedup of one core call, with the core output hash compared
  across arms so a faster wrong answer can never be reported as a win.
* `run_kem.sh` — the same A/B for `keygen`, `encaps`, `decaps`, the in-process sum
  of the three, and one wall-clock interval around the whole KEM sequence.
* `run_sessions.sh` — five paired ABBA/BAAB sessions; a session is the unit of
  replication, so the confidence interval is not built from raw rounds.
* `run_correctness.sh` — eight stages, in order: the layered internal selftest,
  byte-wise baseline equivalence, the first-principles oracle, the KEM
  transcript, the oracle hash against an independent Python model, the
  concurrent first call (plain and under ThreadSanitizer), the stack budget, and
  a check that the new tests stay behind `SNTRUP653_INTERNAL_TEST_HOOKS`.
* `check_multicompiler.sh` — the same core built by both GCC and Clang at
  `-O0 -O1 -O2 -O3 -Os`; all ten builds must produce one correctness hash.
* `check_build_variants.sh` — nine switch combinations, each running all three
  correctness suites, all required to produce one correctness hash.
* `run_ablation.sh` — the delivered configuration, the clean-C baseline and
  one-switch-disabled arms in mirrored order, with one shared correctness hash.

## Verification documents

| File | Contents |
|---|---|
| `VERIFICATION.md` | claim → evidence → command → limit, per verification layer |
| `RANGE_AND_SECURITY.md` | what the range model and the timing screen do and do not establish |
| `LOCAL_VALIDATION.md` | what was validated locally, and the change history of this package |

## Evidence generated by this package

The source archive contains no historical build tree or earlier board output.
Each invocation creates a fresh `run_<timestamp>_<random>/` directory, records
the exact source manifest, host, compiler, frequency state and stage status,
and packs that directory for return. This keeps evidence from different source
revisions or platforms from being mixed.

`summarize.py` is the single summarizer: `kem`, `polymul`, `sessions`. Speedups
are `mean(baseline) / mean(optimized)` (above 1 = the delivered core wins). Each
carries a block-bootstrap CI95 of the per-round paired ratio plus a two-sided
sign test. The verdict column reads

| verdict | meaning |
|---|---|
| `POSITIVE` | CI95 entirely above 1 — the delivered core is reliably faster |
| `NEGATIVE` | CI95 entirely below 1 — reliably slower |
| `NEUTRAL` | CI95 contains 1 — the data do not resolve the sign |

Rounds from different arms share one seed, so round *i* is the same KEM input in
every arm; the ratios are input-paired, not simultaneously paired.

## Requirements and limits

GCC and Clang with the `-mcpu` named in `platform.conf`, binutils,
python3, util-linux, sudo, and the ASan/UBSan/TSan runtimes. `preflight.sh`
checks all of this in about two seconds. `check_host.sh` refuses to continue
unless `CPU_PIN` really is the core named in `platform.conf` (MIDR check), and
`check_frequency.sh` requires the `performance` governor with `min == max`.

Native AArch64 measurement only. Cross builds and QEMU validate functionality,
never timing. The timing screen is a first-order Welch test, not dudect, and a
`NO_SIGNAL` line is not a constant-time proof. Read `RANGE_AND_SECURITY.md`
before quoting any security claim, and `LOCAL_VALIDATION.md` before quoting any
functionality claim.
