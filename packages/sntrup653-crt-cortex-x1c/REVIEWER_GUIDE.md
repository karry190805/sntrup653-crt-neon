# Reviewer reproduction guide — Cortex-X1C

This archive contains the PQClean Round-3 clean C implementation and the
optimized AArch64 core. Only `optimized/crypto_core_multsntrup653.c` replaces
the baseline arithmetic core; `BASELINE_DIFF.txt` and the source manifest make
that scope auditable.

Run the complete campaign with one command; apart from a possible sudo password,
no stage-by-stage confirmation is required:

```bash
sudo apt-get update && sudo apt-get install -y --no-install-recommends clang gcc binutils util-linux coreutils python3 unzip && sudo bash ./run_on_target.sh
```

The entry point verifies the AArch64 MIDR, selects a Cortex-X1C logical core,
locks its frequency, validates the source manifest, builds the programs, runs
correctness, compiler/option invariance, switch variants, security diagnostics,
paired performance sessions and ablation, restores the frequency policy, and
creates `sntrup653-crt-cortex-x1c-results-<timestamp>.zip`.

The decisive status lines include `compiler=gcc RESULT=PASS`,
`compiler=clang RESULT=PASS`, `multicompiler=PASS compilers=2 combos=10`,
`correctness=PASS`,
`build_variants=PASS`, `functional_security_checks=PASS`, `ablation=PASS`, and
`functional_and_benchmark_workflow=PASS`. A failed stage stops the campaign and
is recorded in `FAILED.txt`; `RUN_SUMMARY.txt` distinguishes requested stages
from stages actually completed.

The experiments provide reproducible test evidence, not formal verification or
a machine-code equivalence proof. `VERIFICATION.md` states the supported claims
and limitations.
