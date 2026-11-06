# sntrup653 CRT NEON

Final source and verification repository for the sntrup653 AArch64 NEON
polynomial multiplication study. It contains the PQClean Round-3 baseline,
the optimized core, target validation drivers, and paper result tables.

## Construction

The core uses the auxiliary modulus `Q(x)=x^73(x^1232-1)`, a 77-point outer
transform (`77=7·11`), and a 16-coefficient local `8+8` CRT product at each
frequency. The low 73-coefficient branch and fold-back modulo `x^653-x-1` are
explicit. The three packages share the algebra and API; pointwise loops are
scheduled for Cortex-A53, Cortex-A76, and Cortex-X1C separately.

## Source layout

* [`packages/sntrup653-crt-cortex-a53`](packages/sntrup653-crt-cortex-a53)
* [`packages/sntrup653-crt-cortex-a76`](packages/sntrup653-crt-cortex-a76)
* [`packages/sntrup653-crt-cortex-x1c`](packages/sntrup653-crt-cortex-x1c)
* [`CODE_MAP.md`](CODE_MAP.md) gives the production reading order.

In every package, `baseline/` is the unmodified PQClean clean-C reference and
`optimized/crypto_core_multsntrup653.c` is the replacement. The core contract
is the PQClean contract: the first input is an arbitrary polynomial in R_q;
the KEM call sites pass the ternary secret polynomial as the second input. The
separate `scale3` operation handles the `3v` case.

## Reported measurements

| Platform | Baseline | Optimized | Speedup |
|---|---:|---:|---:|
| Cortex-A53 | 567,916.9 | 45,757.4 | 12.411x |
| Cortex-A76 | 164,192.3 | 24,189.5 | 6.788x |
| Cortex-X1C | 144,355.4 | 12,331.0 | 11.707x |

End-to-end KEM total speedups are 1.087x, 1.048x, and 1.087x respectively.
These are measurements of the supplied builds, not a claim about all AArch64
processors.

## Verification

Run `bash ./run_correctness.sh` from a target-board package. The checks cover
internal self-tests, byte equivalence, independent oracle comparisons, KEM
transcripts, and compiler/build variants. PMU timings require the target
boards. The repository does not claim a formal side-channel proof or formal
verification of the complete binary.

## Version

The current release is tagged `v1.1.0`.
