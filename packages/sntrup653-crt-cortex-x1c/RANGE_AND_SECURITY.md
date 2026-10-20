# Range and security scope, revision 20261016

## Interface

Only the Rq-by-small core is replaced. R3 is unchanged. This is not a general
dense-by-dense API. Input f consists of signed int16 coefficients normalized
to [-2310,2310]; each key byte is decoded to {-1,0,1} exactly as in the bundled
reference. The bounds do not require weight 288; benchmark inputs use weight 288.
Production uses all four optimization switches =1 and USE_BARRETT_REDUCE=1.

## Source-schedule bounds

Let M=4620 and O=4621*25000=115525000. Each transform layer reduces to [0,M]
before the next layer consumes it. Constants are also canonical.

| Stage | Intermediate bounds | Storage/output |
|---|---|---|
| Input freeze | abs(q18*x)<=1867776; abs(q27*x+67108864)<=205769694 | int32; int16 output [-2310,2310] |
| Block conversion | f centered or g ternary | canonical int32 [0,4620] |
| Pair sum/difference | S in [0,9240]; raw D in [-4620,4620], corrected D in [0,4620] | int32 |
| One even/odd product | <=42688800 / <=21344400 | int32 |
| 11-point E / D | <=213448620 / <=106722000 | int32 |
| 11-point E+D / E-D+O | <=320170620 / [8803000,328973620] | nonnegative int32 |
| 7-point E / D | <=128071020 / <=64033200 | int32 |
| 7-point E+D / E-D+O | <=192104220 / [51491800,243596020] | nonnegative int32 |
| Inverse normalization | <=M*M=21344400 | uint32 then reduction |
| Local input narrowing | [0,4620], already reduced | int16, no truncation |
| Local constant multiplication | <=M*M=21344400 | widening int32 |
| Local sum/difference | [0,9240] / [-4620,4620] | int16 then masked correction |
| TMVP8 | 8 products, each <=M*M | int32 <=170755200 |
| Local output narrowing | reduction first, [0,4620] | int16 |
| Low73 | abs(product)<=2310; at most 18 products per lane | int32 lane abs<=41580 |
| Low73 total | abs<=73*2310=168630 | int64 |
| Final folding | conservative [-9240,13860] | int32; centered int16 output |

The intermediate E-D also fits int32 before O is added. Pruning cannot exceed
these dense bounds. TMVP wrapped coefficients are reduced before accumulation.
The test-only schoolbook oracle uses int64 with bound
16*M*M+15*M*M*M < 2^63; this is not the production TMVP accumulator bound.
check_ranges.py verifies these inequalities and enumerates all 65536 input
representatives for Fq_freeze. This is a source model, not a machine-code proof.

Because a compiler may reassociate or vectorise regardless of what the source
model says, the model is complemented by an empirical build sweep:
check_multicompiler.sh builds the same core with gcc and clang at
-O0 -O1 -O2 -O3 -Os and requires one bit-identical correctness hash over ten
combinations. That is coverage, not a proof of the compiled artefacts, and it is
reported as coverage. A machine-code logical equivalence check of the kind a
fully formal treatment needs is not attempted.

The two end-to-end sweeps that exercise the folding bounds are exhaustive rather
than sampled: test_endtoend_oracle covers every monomial pair, and internal
selftest code 71 covers all 1305 unit vectors D = x^idx. The bounds in the table
are therefore reached, not merely argued.

For x<2^s and mu=floor(2^s/q), floor(x*mu/2^s) underestimates floor(x/q)
by at most one. Thus the remainder is in [0,2q); one correction suffices and
the retained second correction is harmless. Products use uint64 for the
32-bit reducer and unsigned __int128 for the 64-bit reducer.
Signed reduction uses unsigned magnitude `(u ^ mask)-mask`, defined even at
INT64_MIN, then masked selection of r and (q-r) mod q. Corrections and centering
use explicit AArch64 subs/csel or cmp/csel, with asr for sign extraction.

## Memory and initialization

The transform indexes blocks 0..76, lanes 0..15, and a temporary [7][11][16].
Public support is blocks 0..40, with 13 live coefficients in the last block.
The first layer completes into the temporary before output stores, supporting
in-place operation. TMVP loads both inputs before storing output.
Low73 reads f[0..k] and a reversed g segment inside [0,72]. Final-fold indices
are inside main[0..1231] and low[0..72], with end guards on vector loops.
Canaries, alias tests and sanitizers complement these source-level observations.

Tables use atomic states 0/1/2. A compare-exchange winner writes the tables then
release-stores 2; all readers acquire-load 2 before using the tables.
test_concurrent_firstcall is now built twice: the plain build checks that the
concurrent first calls equal a later serial replay, and the -fsanitize=thread
build checks the release/acquire ordering deterministically. The plain build
alone would only be a functional check; the TSan build is what rules out a data
race on the tables.
Async-signal safety and fork during initialization are not claimed.
Performance measurements are warm steady state, not cold-call latency.

The per-call frame of the core is about 18 KiB and is now budgeted rather than
merely recorded: check_stack_usage.py reads the -fstack-usage data that build.sh
has always produced and fails when the largest frame exceeds
STACK_BUDGET_BYTES (default 65536). This matters on a Cortex-A53 target, where
the benchmark binds several threads to one core.

## Validation limits

run_security.sh runs native and ASan/UBSan checks, including full-width reducer,
negative mask, raw-input, alias, low73 and concurrent-first-call tests.
Leak detection is disabled; ASan/UBSan does not instrument inline assembly.
The five-session benchmark separately performs 100000 core comparisons and
1000 KEM transcript cases per session.

Seven reducer probes must be branch/call-free in the compiled object.
Full core and KEM disassemblies are retained for manual secret-flow review.
Public loop, padding and initialization branches remain; an instruction search
alone is not a complete constant-time analysis.

The timing screen is an original raw first-order Welch t-test, NOT dudect.
Mode 0 compares a fixed valid input pair with random valid pairs.
Mode 1 randomizes f in both classes and compares fixed/random weight-288 g.
Each mode defaults to 200000 samples, balanced shuffled labels, and checks
max |t| at predetermined 10000-sample checkpoints. Threshold 5 produces ALERT
and stops the workflow. It is a screening rule, not a multiple-look-calibrated
confidence interval. Raw samples are saved. Do not rerun until a pass appears.
NO_SIGNAL is not a constant-time proof or higher-order/power/EM leakage evidence.

The timing harness links the production core object; the independently compiled
benchmark core is disassembled as well. Sanitized timing is never benchmark data.
Only the selected compiler is checked here; the multi-compiler and
switch-variant sweeps in check_multicompiler.sh and check_build_variants.sh
address correctness invariance, not timing. QEMU evidence is functional, not
native performance or leakage evidence.

In LOCAL_EMULATION mode the runner uses UBSan alone, because the local
ASan/QEMU runtime failed during startup before test execution. The native
runner still requires combined ASan/UBSan. A local UBSan pass is not an ASan pass.

## Naming in this release

`baseline/` is the untouched PQClean rounded-3 core; `optimized/` is the deliverable.
Only the two benchmark tables (polynomial core and KEM operations) are reported.
