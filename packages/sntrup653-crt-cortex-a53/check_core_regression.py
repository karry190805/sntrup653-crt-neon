#!/usr/bin/env python3
"""Catch the previously observed scalar sign-bit branches, not all leakage."""
import re
import sys
from pathlib import Path

for filename in sys.argv[1:]:
    text = Path(filename).read_text(encoding="utf-8")
    marker = "<PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653>:"
    if marker not in text:
        raise SystemExit("missing production core symbol: " + filename)
    body = text.split(marker, 1)[1].split("\n\n", 1)[0]
    bad = [line.strip() for line in body.splitlines()
           if re.search(r"\b(?:tbz|tbnz)\b.*#(?:31|63),", line)]
    if bad:
        raise SystemExit("sign_branch_regression=REVIEW_REQUIRED %s %s" % (filename, bad))
    print("known_sign_branch_regression=PASS " + filename)
print("This check does not prove absence of all secret-dependent control flow.")
