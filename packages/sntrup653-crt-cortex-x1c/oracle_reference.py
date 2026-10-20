#!/usr/bin/env python3
"""oracle_reference.py — the end-to-end oracle, written from the definition.

This file is deliberately independent of every C source in this package.  It
computes

    c = A * B mod (q, x^653 - x - 1)

straight from the mathematical definition, in pure Python:

    d[k] = sum_{i+j=k} f[i] * g[j]                       ordinary convolution
    c[r] = d[r] + [r >= 1]*d[652+r] + [r <= 651]*d[653+r]   x^653 = x + 1
    centre c[r] into (-q/2, q/2]

It then reproduces byte for byte the case sequence of
shared/test_endtoend_oracle.c (12 fixed cases followed by 3*653 axis cases)
and prints the FNV-1a-64 hash of the 1971 encoded results.  The C test prints
the same hash for its own outputs; check_oracle_hash.py compares the two.

Two independent implementations of the same definition agreeing on the same
case sequence is the point: if the core reproduces the hash and every case is
checked against the oracle, then the delivered core really does compute the
documented ring product, and it does so without trusting baseline/.

usage:
    python3 oracle_reference.py                 # print the hash
    python3 oracle_reference.py --write         # regenerate expected_oracle_hashes.txt
    python3 oracle_reference.py --dump 5        # show the first 5 expected outputs
"""
import sys

p = 653
q = 4621
w = 288
STRBYTES = 2 * p

FIXED_CASES = 12
AXIS_CASES = 3 * p
HASH_CASES = FIXED_CASES + AXIS_CASES

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


class Rng:
    """xorshift32, identical to shared/bench_core_mult.c and to the C oracle."""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF or 1

    def next(self):
        x = self.s
        x = (x ^ ((x << 13) & 0xFFFFFFFF)) & 0xFFFFFFFF
        x = (x ^ (x >> 17)) & 0xFFFFFFFF
        x = (x ^ ((x << 5) & 0xFFFFFFFF)) & 0xFFFFFFFF
        self.s = x
        return x


def oracle_product(f, g):
    """Return the 653 centred coefficients of A*B in F_q[x]/(x^653 - x - 1)."""
    d = [0] * (2 * p - 1)
    for i, fi in enumerate(f):
        if not fi:
            continue
        for j, gj in enumerate(g):
            if gj:
                d[i + j] += fi * gj
    out = []
    for r in range(p):
        acc = d[r]
        if r >= 1:
            acc += d[p - 1 + r]
        if r <= p - 2:
            acc += d[p + r]
        acc %= q
        if acc > q // 2:
            acc -= q
        out.append(acc)
    return out


def encode_le16(values):
    """Little-endian 16-bit encoding of a coefficient vector."""
    b = bytearray(2 * len(values))
    for i, v in enumerate(values):
        u = v & 0xFFFF
        b[2 * i] = u & 0xFF
        b[2 * i + 1] = (u >> 8) & 0xFF
    return bytes(b)


def fixed_case(cid):
    f = [0] * p
    g = [0] * p
    if cid == 0:
        pass
    elif cid == 1:
        for i in range(w):
            g[i] = 1
    elif cid == 2:
        f = [1] * p
    elif cid == 3:
        f = [1] * p
        for i in range(w):
            g[i] = 1
    elif cid == 4:
        f = [q // 2] * p
        for i in range(w):
            g[i] = 1
    elif cid == 5:
        f = [-(q // 2)] * p
        for i in range(w):
            g[i] = -1
    elif cid == 6:
        f[0] = 1
        g[0] = 1
    elif cid == 7:
        f[p - 1] = 1
        g[p - 1] = 1
    elif cid == 8:
        f[p - 1] = 1
        g[580] = 1
    elif cid == 9:
        f[p - 1] = 1
        g[579] = 1
    elif cid == 10:
        f = [-(q // 2) if (i & 1) else (q // 2) for i in range(p)]
        for i in range(w):
            g[i] = 1
    else:
        f[0] = 1
        g[0] = -1
    return f, g


def axis_case(cid):
    i = cid % p
    which = cid // p
    f = [0] * p
    g = [0] * p
    if which == 0:
        f[i] = 1
        g[0] = 1
    else:
        f[0] = 1
        g[i] = 1 if which == 1 else -1
    return f, g


def fnv1a64(h, data):
    for byte in data:
        h ^= byte
        h = (h * FNV_PRIME) & MASK64
    return h


def hash_stream():
    h = FNV_OFFSET
    dump = []
    for cid in range(FIXED_CASES):
        f, g = fixed_case(cid)
        blob = encode_le16(oracle_product(f, g))
        dump.append(("fixed", cid, blob))
        h = fnv1a64(h, blob)
    for cid in range(AXIS_CASES):
        f, g = axis_case(cid)
        blob = encode_le16(oracle_product(f, g))
        dump.append(("axis", cid, blob))
        h = fnv1a64(h, blob)
    return h, dump


def random_case(rng):
    f = [0] * p
    g = [0] * p
    pos = list(range(p))
    for i in range(p):
        f[i] = (rng.next() % q) - q // 2
    for i in range(w):
        j = i + (rng.next() % (p - i))
        pos[i], pos[j] = pos[j], pos[i]
    for i in range(w):
        g[pos[i]] = 1 if (rng.next() & 1) else -1
    return f, g


def main(argv):
    write = "--write" in argv
    dump_n = 0
    if "--dump" in argv:
        idx = argv.index("--dump")
        dump_n = int(argv[idx + 1]) if idx + 1 < len(argv) else 5
    if "--selftest" in argv:
        # Cross-check the fast path against a naive reimplementation on a
        # handful of tiny random cases.  Cheap guard against a typo in either
        # formulation of the fold.
        rng = Rng(12345)
        for _ in range(5):
            f = [(rng.next() % q) - q // 2 for _ in range(p)]
            g = [0] * p
            for _ in range(30):
                g[rng.next() % p] = 1 if (rng.next() & 1) else -1
            fast = oracle_product(f, g)
            slow = oracle_product_naive(f, g)
            assert fast == slow, "fast and naive fold disagree"
        print("oracle_reference selftest: PASS")

    h, dump = hash_stream()
    if dump_n:
        for kind, cid, blob in dump[:dump_n]:
            print("%s case %d: %s" % (kind, cid, blob.hex()))
    print("cases=%d hash=%016x" % (HASH_CASES, h))

    if write:
        lines = [
            "# expected_oracle_hashes.txt",
            "# Generated by oracle_reference.py, the independent pure-Python model",
            "# of the ring product.  Do not edit by hand: regenerate with",
            "#     python3 oracle_reference.py --write",
            "#",
            "# The C test shared/test_endtoend_oracle.c replays the same case",
            "# sequence (12 fixed + 3*653 axis cases) and must print the same",
            "# cases_hash; check_oracle_hash.py enforces it.",
            "cases=%d" % HASH_CASES,
            "fnv1a64=%016x" % h,
        ]
        with open("expected_oracle_hashes.txt", "w", encoding="ascii", newline="\n") as fh:
            fh.write("\n".join(lines) + "\n")
        print("wrote expected_oracle_hashes.txt")
    return 0


def oracle_product_naive(f, g):
    """Second, deliberately different formulation: fold by repeated reduction.

    x^i for i >= 653 is replaced by x^(i-652) + x^(i-653) until every exponent
    is below 653.  Slower, structurally unrelated to the closed form above,
    used only to cross-check oracle_product.
    """
    terms = {}
    for i, fi in enumerate(f):
        if not fi:
            continue
        for j, gj in enumerate(g):
            if gj:
                terms[i + j] = terms.get(i + j, 0) + fi * gj
    while True:
        top = [e for e in terms if e >= p]
        if not top:
            break
        e = max(top)
        coeff = terms.pop(e)
        if coeff:
            terms[e - (p - 1)] = terms.get(e - (p - 1), 0) + coeff
            terms[e - p] = terms.get(e - p, 0) + coeff
    out = []
    for r in range(p):
        acc = terms.get(r, 0) % q
        if acc > q // 2:
            acc -= q
        out.append(acc)
    return out


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
