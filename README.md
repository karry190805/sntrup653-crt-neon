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

| Platform | Baseline | Optimized | Speedup |
|---|---:|---:|---:|
| Cortex-A53 | 567,917.2 | 45,412.7 | 12.506x |
| Cortex-A76 | 162,428.9 | 24,178.4 | 6.718x |
| Cortex-X1C | 144,419.5 | 12,329.9 | 11.713x |

End-to-end KEM total speedups are 1.0873x, 1.0503x, and 1.0867x respectively. The full 2026-11-09 evidence is in [`results/summary_20261109.csv`](results/summary_20261109.csv) and [`results/SUMMARY_20261109.md`](results/SUMMARY_20261109.md).
These are measurements of the supplied builds, not a claim about all AArch64
processors.

## Verification

Run `bash ./run_correctness.sh` from a target-board package. The checks cover
internal self-tests, byte equivalence, independent oracle comparisons, KEM
transcripts, and compiler/build variants. PMU timings require the target
boards. The repository does not claim a formal side-channel proof or formal
verification of the complete binary.

## Version

The current release is the 2026-11-09 three-platform remeasurement. The repository records the validation limits and does not claim a formal side-channel proof.
