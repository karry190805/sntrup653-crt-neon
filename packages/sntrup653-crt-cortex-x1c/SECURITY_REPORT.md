# Security / power additions (v4)

## dudect

An ARM64 port of dudect's statistical engine (with a `cntvct_el0` timing
primitive) was added under `security/dudect/` and run natively on all three
boards.  Two classes are two independent valid `(sk, ct)` pairs; the target is
`crypto_kem_dec`.  The run used five chunks of 20,000 requested measurements.

| Platform | dudect state | max \|t\| | result |
|---|---|---:|---|
| A76 | `DUDECT_NO_LEAKAGE_EVIDENCE_YET` (1) | 1.98 | no evidence at this sample size |
| A53 | `DUDECT_NO_LEAKAGE_EVIDENCE_YET` (1) | 1.24 | no evidence at this sample size |
| X1C | `DUDECT_NO_LEAKAGE_EVIDENCE_YET` (1) | 1.66 | no evidence at this sample size |

This is a statistical screen: no leakage evidence at the stated sample size, not a proof.
Raw logs: `provenance/raw/security-v4/<platform>/dudect{,_result}.txt`.

## ctgrind

A ctgrind-style Valgrind harness is under `security/ctgrind/`
(`VALGRIND_MAKE_MEM_UNDEFINED` on `sk`, then `crypto_kem_dec`).  With
Valgrind and libc6-dbg installed as root, it ran natively on all three boards:

| Platform | Valgrind | error summary | result |
|---|---|---:|---|
| A76 | 3.19.0 | 0 errors from 0 contexts | 0 reported errors |
| A53 | 3.24.0 | 0 errors from 0 contexts | 0 reported errors |
| X1C | 3.24.0 | 0 errors from 0 contexts | 0 reported errors |

This is a Valgrind taint check: 0 reported errors, not a formal proof; it
does not establish constant-time behavior.  Raw logs:
`provenance/raw/security-v4/<platform>/ctgrind{,_result}.txt`.

## Power

X1C exposes a `system_power` hwmon node (`in0_input`, `curr1_input`,
`power1_input`).  Ten-second captures were recorded while idle and while the
optimized core was looped:

| State | mean voltage | mean current | mean system power |
|---|---:|---:|---:|
| idle | 8.670 V | 845 mA | 7.33 W |
| optimized core loop | 8.698 V | 1,194 mA | 10.39 W |

These are **whole-system** power figures, not core-only or EM measurements.
The A53/A76 Power CC meter was not attached to an accessible serial device
during this run, so no A53/A76 power curve is claimed.

## Not covered

- Power/EM **side-channel leakage analysis** is not covered.  X1C power is a
  whole-system logger value only and is not a side-channel result; A53/A76
  power was not measured; EM was not measured.
- dudect is a statistical screen with **no leakage evidence at the stated
  sample size**; it is not a proof.
- The ctgrind-style Valgrind run is **0 reported errors, not a formal proof**;
  it does not establish constant-time behavior.
- Whole-binary formal equivalence is `NOT_PROVEN`.
