# sntrup653 CRT core — Cortex-A76 opt22 source package

Clean source package: PQClean Round-3 baseline, the optimized AArch64 core, and
every script needed to reproduce the opt22 validation campaign. No build tree,
no measurement output and no previous result archive is included; each run
creates its own results directory.

## opt22 selection

No source change from opt21-r2. Eight pointwise batch shapes and the tail-shape sweep were measured; the best candidate was +0.13%, below the 0.3% adoption threshold.

## Start here

`../RUN_ZH.md` in the package root explains the full command set, the exact
expected status lines and the interpretation limits. The platform-specific
change record is `OPT22_CHANGES_zh.md`.

## Invariants for this tree

```text
core_correctness_hash   = 0xfe69baced6245e47
math_fingerprint        = 0xa866aa344f934dca
optimized core sha256   = a2b4e171d6498a15a9c7ae2a1584545716916c32b9d2fef56579107231dc4b5e
production view sha256  = aaa4e009ea77c644f2187874cea1c70b18a6dbaf1536133a70df58c081cd8abc
```

## Quick run

```bash
# Cortex-A76, pin CPU 7
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
