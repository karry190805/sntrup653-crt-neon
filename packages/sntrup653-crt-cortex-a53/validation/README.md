# clean-v2 validation

This directory adds functional validation that is separate from the timed
campaign.  It never inserts counters into the performance binaries and it does
not change any measured cycle or millisecond value.

Run from the platform root:

```bash
bash ./run_validation.sh
```

Default output is `results/validation/`.  Useful overrides:

```bash
VALIDATION_CASES=100000 \
VALIDATION_CORE_CASES=1000 \
VALIDATION_KEM_CASES=50 \
VALIDATION_FLIP_BATCHES=4 \
bash ./run_validation.sh /path/to/out
```

What is generated:

- `contract/` — general-input contract and low-73 range validation;
- `compiler/hash`, `compiler/core`, `compiler/math`, `compiler/kem` —
  GCC/Clang x O0-O3/Os matrix;
- `variants/`, `variant_math/` — the current 10 build variants;
- `single_bit_flip/`, `single_bit_flip_asan/` — exhaustive ciphertext bit
  flips, baseline vs optimized;
- `analysis/` — separate source-level operation-count build (not used for timing);
- `summary/*.csv` and `validation_summary.md`.

Interface note: `crypto_core_multsntrup653` is Rq-by-small.  Its second
argument is byte-decoded to `{-1,0,1}`.  The KEM `3*v` step is
`crypto_core_scale3`, applied to the first argument.  The contract test covers
that path separately; it does not pretend that `{0,+-3}` is a valid second
argument.

The compiler matrix is coverage, not a formal machine-code equivalence check.
The single-bit test checks baseline/optimized error-path equality; it is not a
security proof.
