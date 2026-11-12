# sntrup653 CRT core — Cortex-X1C opt22 source package

Clean source package: PQClean Round-3 baseline, the optimized AArch64 core, and
every script needed to reproduce the opt22 validation campaign. No build tree,
no measurement output and no previous result archive is included; each run
creates its own results directory.

## opt22 selection

No source change from the current device version (2x pointwise + l2u2 + no live-range shrinkage). Every tested batch shape was at or below 1.0.

## Start here

`../RUN_ZH.md` in the package root explains the full command set, the exact
expected status lines and the interpretation limits. The platform-specific
change record is `OPT22_CHANGES_zh.md`.

## Invariants for this tree

```text
core_correctness_hash   = 0xfe69baced6245e47
math_fingerprint        = 0xa866aa344f934dca
optimized core sha256   = 489ebdb87039931c4840fd4861ea53d5caeb8b59cb76650d9a0c297314832003
production view sha256  = 489fd82ad813114b2e6b13e588cba3b06e47e85dd60948e12cb86de08ca5e1cf
```

## Quick run

```bash
# Cortex-X1C, pin CPU 7
CPU_PIN=7 OUT=$PWD/results bash ./run_opt22_campaign.sh
```

The campaign builds with `STRICT_SAME_FLAGS=1`, then runs the 100,000-case core
equivalence check, the 1,000-case KEM transcript comparison, paired polymul and
KEM A/B, real PMU counters, the security checks and the diagnostic stage tables.

## Layout

| Path | Contents |
|---|---|
| `baseline/` | untouched PQClean round-3 clean C implementation |
| `optimized/` | delivered core; only `crypto_core_multsntrup653.c` differs |
| `shared/` | benchmarks, correctness drivers, oracle, profiling driver |
| `check_*.sh`, `check_*.py` | verification scripts |
| `run_*.sh` | campaign steps and the one-command driver |
| `expected_*` | recorded hashes and fingerprints |
| `SOURCE_MANIFEST.sha256` | SHA-256 of every source and script in this tree |

The core is data-independent in control flow and table access by construction;
that is a source-level property, not a formal proof of the whole binary. See
`RANGE_AND_SECURITY.md` and `VERIFICATION.md` for the exact scope.
