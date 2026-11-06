#!/usr/bin/env python3
"""Render a diagnostic stage table from bench_stage_profile CSV.

Usage: make_stage_table.py PLATFORM FREQ_MHZ CSV OUT.md [COMPILER]

cycles_per_call is derived from ns_per_call using the locked frequency; it is a
derived figure, not a PMU count, and the table is diagnostic because the marks
themselves add work.
"""
import csv
import sys

platform, freq_mhz, csv_path, out_path = sys.argv[1], float(sys.argv[2]), sys.argv[3], sys.argv[4]
compiler = sys.argv[5] if len(sys.argv) > 5 else 'gcc'

rows = {}
meta = {}
with open(csv_path, newline='') as f:
    lines = [l for l in f if not l.startswith('#')]
reader = csv.DictReader(lines)
for r in reader:
    stage = r['stage']
    if stage == 'sum_marked':
        meta['sum'] = float(r['ns_per_call'])
        continue
    rows[stage] = (float(r['ns_per_call']), float(r['pct_of_marked']))

order = [
    ('blockify_A', 'blockify A (decode + block layout)'),
    ('blockify_B', 'blockify B (key decode + block layout)'),
    ('fwd_dft11', 'forward DFT-11 (A+B first layer)'),
    ('fwd_dft7', 'forward DFT-7 (A+B second layer)'),
    ('pointwise', '77-point weighted pointwise product'),
    ('inv_dft11', 'inverse DFT-11 (first layer)'),
    ('inv_dft7', 'inverse DFT-7 (second layer)'),
    ('low73', 'low-73 recovery'),
    ('final_fold', 'modulo x^653-x-1 fold'),
    ('encode', 'output encode'),
]

total_ns = meta.get('sum', sum(v[0] for v in rows.values()))
total_cycles = total_ns * freq_mhz / 1000.0  # ns * MHz = 1e-3 cycles

out = []
out.append('# Diagnostic stage table — %s (%s)\n' % (platform, compiler))
out.append('> **Diagnostic, not a production measurement.** Every stage boundary '
           'carries a `clock_gettime(CLOCK_MONOTONIC_RAW)` call, so the rows include '
           'instrumentation overhead and must not be summed and quoted as the '
           'production cycle count. Use them for relative attribution only.\n')
out.append('| stage | ns/call | cycles/call (derived) | % of marked time |')
out.append('|---|---:|---:|---:|')
for key, label in order:
    if key not in rows:
        out.append('| %s | n/a | n/a | n/a |' % label)
        continue
    ns, pct = rows[key]
    out.append('| %s | %.1f | %d | %.2f%% |' % (label, ns, round(ns * freq_mhz / 1000.0), pct))
out.append('| **sum of marked stages** | **%.1f** | **%d** | **100.00%%** |'
           % (total_ns, round(total_cycles)))
out.append('')
out.append('Frequency used for the cycle column: **%.1f MHz** (locked). '
           'cycles = ns x MHz / 1000; this is a conversion, not a hardware counter.' % freq_mhz)
out.append('')
out.append('The `end` marker interval (call epilogue, typically <1%%) is included '
           'in the sum but not listed as a row, so the listed rows can add up to '
           'slightly less than 100%%.')
open(out_path, 'w').write('\n'.join(out) + '\n')
print('wrote', out_path)
