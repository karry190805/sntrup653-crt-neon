# sntrup653 CRT core — Cortex-A53 opt22 source package

Clean source package: PQClean Round-3 baseline, the optimized AArch64 core, and
every script needed to reproduce the opt22 validation campaign. No build tree,
no measurement output and no previous result archive is included; each run
creates its own results directory.

## opt22 selection

Pointwise loop batched 2 at a time plus one explicit tail block (all 77 blocks covered exactly once), and `#pragma GCC unroll 2` on the two `gt_layer2_*` `cbase` loops.

## Start here

`VERIFICATION.md` explains the validation scope and interpretation limits.
The full command set is documented by the `run_*.sh` scripts in this package;
the platform-specific change record is `OPT22_CHANGES_zh.md`.

## Invariants for this tree

```text
core_correctness_hash   = 0xfe69baced6245e47
math_fingerprint        = 0xa866aa344f934dca
optimized core sha256   = e0d3e22e9738ba44ce8a04af2ca04729ace59a735982f765ae5e1b9eedff50fe
production view sha256  = 788663c83e79081d27d90571745fc857da8622495708ed9872d5c67a1255e30a
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
