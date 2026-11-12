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

The top-level layout follows the compact structure used by the reference
`CRTWithMonomial` repository: `CMakeLists.txt`, `src/`, `include/`, `bench/`,
and `scripts/` provide a short entry point for reviewers. The complete
platform-specific source and validation material remains under `packages/`.

On an AArch64 host, the top-level build can be started with:

```bash
./scripts/build.sh
```

The correctness and campaign commands are documented in the selected platform
package and can be launched with `./scripts/test.sh` on the target board.

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

PMU core cycles per polynomial-multiplication call (`perf stat -r 5`, 50,000 calls, same-platform A/B):

| Platform | Baseline | Optimized | Speedup |
|---|---:|---:|---:|
| Cortex-A53 | 567,937.2 | 45,859.5 | 12.384x |
| Cortex-A76 | 163,091.8 | 24,176.2 | 6.746x |
| Cortex-X1C | 144,464.7 | 12,329.5 | 11.717x |

End-to-end KEM total speedups (ABBA-paired, 2,000 pairs) are 1.050x (A76), 1.087x (A53), and 1.088x (X1C). The full evidence is in [`results/table3_pmu.csv`](results/table3_pmu.csv) (core), [`results/table4_kem.csv`](results/table4_kem.csv) (KEM) and [`results/table6_ablation.csv`](results/table6_ablation.csv).
These are measurements of the supplied builds on the listed cores, not a claim about all AArch64 processors.

## Verification

Run `bash ./run_correctness.sh` from a target-board package. The checks cover
internal self-tests, byte equivalence, independent oracle comparisons, KEM
transcripts, and compiler/build variants. PMU timings require the target
boards. The repository does not claim a formal side-channel proof or formal
verification of the complete binary.

## Version

The current release is the 2026-11-10 three-platform remeasurement. The repository records the validation limits and does not claim a formal side-channel proof.
