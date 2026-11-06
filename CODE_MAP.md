# Code map

Read one platform package in this order:

1. `src/01_*.c` through `src/19_*.c`: reviewer-facing slices of the
   mathematical core, including
   constants, 77-point transform, local 16-dimensional product, low branch,
   CRT recovery, and fold-back.
2. `packages/<platform>/optimized/crypto_core_multsntrup653.c`: complete
   compilable production core.
3. `packages/<platform>/baseline/crypto_core_multsntrup653.c`: PQClean reference used as the
   replacement baseline.
4. `packages/<platform>/shared/`: correctness, oracle, KEM transcript, and timing drivers.
5. `run_correctness.sh` and `run_opt22_campaign.sh`: validation entry points.

The top-level `src/` files are a read-only view and are joined in lexical order
by `tools/join.py`; they are not independent translation units. The only
production source difference between `baseline/` and `optimized/` is
`crypto_core_multsntrup653.c`. The three optimized files share the same
mathematical construction and interface; they differ in pointwise loop
batching and compiler scheduling for the target cores. The package
`VERIFICATION.md` files record the source hashes and the scope of each check.
