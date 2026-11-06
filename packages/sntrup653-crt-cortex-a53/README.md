# sntrup653 CRT core — Cortex-A53 opt22 source package

Clean source package: PQClean Round-3 baseline, the optimized AArch64 core, and
every script needed to reproduce the opt22 validation campaign. No build tree,
no measurement output and no previous result archive is included; each run
creates its own results directory.

## opt22 selection

Pointwise loop batched 2 at a time plus one explicit tail block (all 77 blocks covered exactly once), and `#pragma GCC unroll 2` on the two `gt_layer2_*` `cbase` loops.

## Start here

`../RUN_ZH.md` in the package root explains the full command set, the exact
expected status lines and the interpretation limits. The platform-specific
change record is `OPT22_CHANGES_zh.md`.

## Invariants for this tree

```text
core_correctness_hash   = 0xfe69baced6245e47
math_fingerprint        = 0xa866aa344f934dca
optimized core sha256   = 64f1c4dc051488e3ff7aed47df97a65c1d00bcd7ccc100ac650e020383258bf9
production view sha256  = f7c6f935f404bc4a19d3be4ab24591a503eaf89b2ba6ad63a3567b49c83d828b
```

## Quick run

```bash
# Cortex-A53, pin CPU 3
CPU_PIN=3 OUT=$PWD/results bash ./run_opt22_campaign.sh
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
