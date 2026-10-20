# VERIFICATION.md — what this package establishes, and what it does not

Three separate questions are often collapsed into one. They have different
answers here, and a paper is only safe if it keeps them apart.

| # | Claim | Kind of evidence | Status |
|---|---|---|---|
| P1 | Each step computes the quantity its formula claims, so the composed algorithm returns `A·B mod (q, x^653-x-1)` | paper derivation | in the manuscript, §3 |
| P2 | The NEON code really executes those steps | layered oracle comparison + exhaustive basis sweeps in the binary | **established here** |
| P3 | Replacing the core does not change any externally visible byte | byte-wise equivalence, KEM transcript, cross-arm hash | **established here** |

P1 ∧ P2 gives a correct implementation. P3 plus a trusted reference gives a
safe substitution. Neither alone gives both. Nothing in this package is a
formal proof, and nothing here is a side-channel proof.

## 1. Layered internal selftest (`test_internal_construction`)

`sntrup653_internal_selftest` compares every stage of the pipeline against an
independent scalar oracle and returns a distinct code on the first mismatch, so
a failure names the layer. Run it with:

```bash
bash ./build.sh && ./build/test_internal_construction 1000 1
```

| Code | Layer | What it pins down |
|---|---|---|
| 10 | constants | `ω^77 = 1`, `ω^7 ≠ 1`, `ω^11 ≠ 1`, `2·2⁻¹ = 1`, `77·77⁻¹ = 1` |
| 12, 13 | constants | `η_m² = λ_m` and `2η_m·(2η_m)⁻¹ = 1` for all 77 weights |
| 11 | schedule | run-time column list and pair mask reproduce the compile-time predicate on all 7×11 cells |
| 14, 15, 16, 17 | schedule | the constant expression `SNTRUP_GT_ACT`, the index table and `ACTIVE_COLS_PER_R` agree everywhere |
| 20, 21 | transform | unit impulse: forward equals the dense oracle, inverse returns the impulse |
| 30, 31 | transform | dense random forward and inverse |
| 32, 33 | transform | the production precondition: dense on the 41 public blocks, exact zeros on the padding |
| 42, 45 | product | 8-dimensional weighted product, NEON TMVP8 vs schoolbook, vs the production dispatcher |
| 43, 46 | product | the same with the output aliasing either input |
| 44 | product | **exhaustive**: 77 × 16 × 16 = 19 712 monomial pairs |
| 50 | fold | NEON fold vs scalar fold on random input |
| 62, 63 | product | the worst representative `q-1` on every lane |
| 70 | low part | **exhaustive**: 73 × 73 × 2 monomial pairs for the 73-coefficient branch |
| 71 | fold | **exhaustive**: the 1305 unit vectors `D = x^idx` |

Codes 44, 70 and 71 are not samples. The stage they cover is bilinear (44, 70) or
linear (71) over `F_q`, the monomials span the whole input space of that stage,
and the reducer those stages call has been verified separately and exhaustively
over all 65 536 signed 16-bit values. On that basis, exhaustive agreement on the
basis carries over to all inputs of the stage. This is a mathematical argument,
not an empirical one, and it is the strongest correctness statement in the
package.

## 2. First-principles oracle (`test_endtoend_oracle`)

Everything in §1 compares two implementations inside the same file. The oracle
removes that dependency: it computes `A·B mod (q, x^653-x-1)` from the
definition — integer convolution, fold by `x^653 = x+1`, centre — and compares
the result with the core output, coefficient by coefficient.

```bash
./build/test_endtoend_oracle all 20000 1     # 10 s: fixed + axes + 20000 random
./build/test_endtoend_oracle every 20000 1   # + the full 653 × 653 sweep
```

Its case sequence is shared with `oracle_reference.py`, a pure-Python model of
the same definition that shares no code with the C, and which cross-checks its
own fold against a second, completely different formulation
(`python3 oracle_reference.py --selftest`). The two agree on the 1971-case
aggregate hash, which `check_oracle_hash.py` enforces:

```
cases=1971 fnv1a64=c53ec27ecdbb56bf      (oracle_reference.py, independent)
cases_hash=c53ec27ecdbb56bf              (test_endtoend_oracle, from the core)
```

Because the map is `F_q`-bilinear, the axes and the full sweep are also complete
covers, so `ORACLE_MODE=every` gives a second exhaustive argument for the whole
composition rather than only for its stages.

## 3. Byte-wise equivalence and the transcript

```bash
bash ./run_correctness.sh 100000 1000 1
```

* `test_core_equiv` — baseline against optimized, byte for byte, on random and
  boundary inputs.
* `kem_transcript` — keygen / encaps / decaps, including the implicit-rejection
  path, for 1000 deterministic cases, byte for byte.

The KEM transcript hash is stable across all three boards and across
compilers, and `run_polymul.sh` recomputes the core output hash inside every A/B
run: a speedup is only reported when both arms produced the same bytes. A faster
wrong answer cannot be measured as a win.

## 4. Compiler invariance (`check_multicompiler.sh`)

The range arguments in `RANGE_AND_SECURITY.md` model the source schedule, not
the machine code, and a compiler is free to reassociate or vectorise. The cheap
empirical answer is to build the same core with GCC and Clang at each of
`-O0`, `-O1`, `-O2`, `-O3` and `-Os`, and require one correctness hash:

```bash
REQUIRE_ALL_COMPILERS=1 bash ./check_multicompiler.sh
# compiler=gcc RESULT=PASS combos=5 unique_hashes=1 hash=0x...
# compiler=clang RESULT=PASS combos=5 unique_hashes=1 hash=0x...
# multicompiler=PASS compilers=2 combos=10 unique_hashes=1 hash=0x...
```

This is coverage, not an equivalence proof. It is reported that way on purpose.
The same distinction applies to `-O3`-versus-`-O1` logical equivalence checking
of the kind a fully formal treatment would use; that is a paper-sized effort and
is not attempted here.

## 5. Switch invariance (`check_build_variants.sh`)

A switch wired to the wrong helper, or a fallback that has drifted away from the
vector path, is invisible to every other test because nothing else builds the
other arm. Nine variants are built and each runs all three correctness suites:

`default`, `dense`, `rtmask`, `dense_rtmask`, `noinplace`, `low73_scalar`,
`fold_scalar`, `no_barrett`, `all_fallbacks`.

All nine must produce the same correctness hash. `rtmask` in particular compiles
`SNTRUP653_PUBLIC_ZS_RUNTIME_MASK=1`, an arm the shipped build never selects.

## 6. Concurrency (`test_concurrent_firstcall`)

The transform tables are initialised lazily on the first call, guarded by an
atomic state word with release/acquire ordering. Two properties matter: a losing
thread must observe a fully initialised table set, and concurrent first calls
must return what the same calls return serially.

```bash
./build/test_concurrent_firstcall 32 1
./build/test_concurrent_firstcall_tsan 32 1     # built with -fsanitize=thread
```

The plain build checks the second property. The TSan build checks the first
deterministically, under `halt_on_error=0 exitcode=66`.

## 7. Stack budget (`check_stack_usage.py`)

`build.sh` has always passed `-fstack-usage` and nothing has ever read the
result. On a Cortex-A53 target the per-call frame of the core alone is around
18 KiB, so the budget is a real constraint rather than a formality:

```bash
python3 ./check_stack_usage.py 65536 build/*.su
# stack_usage=PASS max=... budget=65536 headroom=...
```

## 8. Test isolation (`check_selftest_isolation.py`)

The two exhaustive sweeps added in §1 sit inside the
`SNTRUP653_INTERNAL_TEST_HOOKS` conditionals; `build.sh` defines that macro for
exactly one binary. The claim that production code is untouched is checkable:
strip every block the macro controls, using preprocessor semantics and the
package's default switch settings, and hash what remains.

```bash
python3 ./check_selftest_isolation.py
# selftest_isolation=PASS production view unchanged (9749385416ba3e53)
```

The production view of the core is 1026 lines and hashes to
`9749385416ba3e53bf1fe81be01f5becb6a74ea723b60e100fcfd5910faabad4`. It is
identical to the production view of the package that shipped before the new
tests were added, which is what makes "the delivered arithmetic is unchanged" a
statement about bytes rather than about intent.

Every fresh board run records this digest together with the source manifest.
The digest is meaningful only with the stated preprocessing convention, so the
log records both the convention and the full value.

## 9. Boundaries

These are limitations, stated so that a reviewer does not have to find them.

1. **No formal verification.** There is no SMT, no range-overflow proof of the
   compiled artefacts and no machine-code logical equivalence check. The
   arguments in §1 and §2 are exhaustive over a basis, not machine-checked.
2. **The end-to-end oracle is one implementation, written twice.** The Python
   model and the C test are independent of each other and of `baseline/`, but
   both were written by the same authors from the same definition.
3. **Byte-wise equivalence is capped by the reference.** "Same as baseline" is
   exactly as strong as "PQClean round-3 clean C", which is well studied but is
   not itself a proof. The oracle in §2 is the answer to that, not §3.
4. **Compiler invariance is empirical.** Ten compiler/level combinations on one
   machine is coverage; it is not a guarantee about an unmeasured toolchain.
5. **The timing screen is not a proof.** It is a first-order Welch test
   (`NO_SIGNAL`, threshold 5, no multiple-comparison correction), not dudect. It
   has no power against higher-order effects, power, or EM leakage. `NO_SIGNAL`
   means "this test did not detect a signal".
6. **Sanitisers do not instrument inline assembly.** ASan and UBSan see the C,
   not the twenty or so inline AArch64 fragments that implement the constant
   time correction and sign mask.
7. **No known-answer test from the standard.** The package has no NIST `.rsp`
   vectors; the independent anchor is the definition-level oracle of §2.
8. **Two public loops remain.** The pruning loop structure and the padding
   pattern are public, and the test in §6 checks races on the initialiser rather
   than absence of secret-dependent memory access patterns.
