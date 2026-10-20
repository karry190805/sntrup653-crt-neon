#!/usr/bin/env python3
"""Executable arithmetic checks supporting RANGE_AND_SECURITY.md."""
Q = 4621
M = Q - 1
OFFSET = Q * 25000
I32 = (1 << 31) - 1

for n in (7, 11):
    pairs = (n - 1) // 2
    even = M + 2 * pairs * M * M
    odd = pairs * M * M
    assert OFFSET - odd >= 0
    assert max(even + odd, OFFSET + even) <= I32
    print(f"dft{n}: even<={even} odd<={odd} plus<={even+odd} "
          f"biased_minus=[{OFFSET-odd},{OFFSET+even}]")
assert 8 * M * M <= I32
assert 2 * M < (1 << 15)
assert 73 * (Q // 2) <= I32
assert 3 * M <= I32
assert 16 * M * M + 15 * M * M * M < (1 << 63)
assert (1 << 32) // Q == 929445
assert (1 << 64) // Q == 3991937691778738
for original in range(-(1 << 15), 1 << 15):
    t = 57 * original
    assert -(1 << 31) <= t <= I32
    x = original - Q * (t >> 18)
    t = 29045 * x + 67108864
    assert -(1 << 31) <= t <= I32
    x -= Q * (t >> 27)
    assert -Q // 2 < x <= Q // 2
    assert (x - original) % Q == 0
for r in range(7):
    cols = [s for s in range(11) if (22 * r + 56 * s) % 77 < 41]
    assert len(cols) <= 6
print(f"tmvp8_accumulator<={8*M*M}; low73_abs<={73*(Q//2)}")
print("range_model_checks=PASS (source schedule model, not a machine-code proof)")
