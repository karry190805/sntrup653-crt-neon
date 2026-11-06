#!/usr/bin/env python3
"""Create a pointwise-batch variant of a core tree.

Usage: make_pw_variant.py SRC DST BATCH [L2U2]

The generated loop is the only accepted shape:

    for (m = 0; m + B - 1 < BLOCKS; m += B) { B calls }
    <tail> explicit calls covering the remaining blocks

The generator simulates the main loop to find the first block it does not
cover, then emits one explicit call per remaining block.  Coverage is checked
structurally for every B, including B that do not divide BLOCKS-1.

Refuses to run if the source already looks modified in an unexpected way.
"""
import os
import re
import shutil
import subprocess
import sys

if len(sys.argv) < 4:
    sys.exit('usage: make_pw_variant.py SRC DST BATCH [L2U2]')

src, dst, batch = sys.argv[1], sys.argv[2], int(sys.argv[3])
l2u2 = int(sys.argv[4]) if len(sys.argv) > 4 else 0

if batch < 1 or batch > 77:
    sys.exit('batch out of range')

if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst)

core = os.path.join(dst, 'optimized', 'crypto_core_multsntrup653.c')
s = open(core).read()

MARKER = '/* opt22-pointwise-batch */'
if MARKER in s:
    sys.exit('source is already a generated pointwise variant (%s found)' % MARKER)

# Replace the whole pointwise region, from the first loop header through the
# explicit last-block call, with the provably complete main-loop + tail shape.
# The generic header accepts every pristine base shape: the A76/A53 bases use
# 4 calls per iteration + last block, the X1C device base uses 2 calls plus a
# remainder loop + last block.
pattern = re.compile(
    r'    for \(m = 0; m \+ \d+ < BLOCKS(?: - 1)?; m \+= \d+\) \{.*?'
    r'    weighted_mul16_scaled16\(A16\[BLOCKS - 1\], A16\[BLOCKS - 1\],\n'
    r'                            B16\[BLOCKS - 1\], BLOCKS - 1, 1\);\n',
    re.S)
matches = pattern.findall(s)
if len(matches) != 1:
    sys.exit('expected exactly one pointwise region, found %d' % len(matches))
base_calls = matches[0].count('weighted_mul16_scaled16(A16[')
if base_calls < 3:
    sys.exit('unexpected base pointwise shape: only %d calls' % base_calls)

body = ['    %s' % MARKER,
        '    for (m = 0; m + %d < BLOCKS; m += %d) {' % (batch - 1, batch)]
for k in range(batch):
    body.append('        weighted_mul16_scaled16(A16[m + %d], A16[m + %d], '
                'B16[m + %d], m + %d, 1);' % (k, k, k, k))
body.append('    }')

# Simulate the main loop exactly as the C compiler will execute it, then emit
# one explicit call per block the loop leaves over.  The simulation is checked
# against the loop condition rather than assumed.
blocks = 77
m = 0
while m + batch - 1 < blocks:
    m += batch
tail = blocks - m
if m % batch != 0:
    sys.exit('internal coverage error: main-loop stop %d is not a multiple of %d'
             % (m, batch))
if not (m + batch > blocks):
    sys.exit('internal coverage error: main loop did not terminate (m=%d B=%d)'
             % (m, batch))
if not (0 <= tail < batch):
    sys.exit('internal coverage error: tail=%d for batch=%d' % (tail, batch))
if m + tail != blocks:
    sys.exit('internal coverage error: %d + %d != %d' % (m, tail, blocks))
for k in range(tail):
    body.append('    weighted_mul16_scaled16(A16[m + %d], A16[m + %d], '
                'B16[m + %d], m + %d, 1);' % (k, k, k, k))
new_region = '\n'.join(body) + '\n'
s = pattern.sub(lambda _m: new_region, s, count=1)

# Structural check: the region must contain batch + tail calls.
if new_region.count('weighted_mul16_scaled16(') != batch + tail:
    sys.exit('generated call count mismatch')

# Independent coverage proof: re-parse the emitted text, not the numbers we
# just used to build it.  Expand every call it actually contains into the
# concrete block indices and require exactly one visit per block in 0..76.
hdr = re.search(r'for \(m = 0; m \+ (\d+) < BLOCKS; m \+= (\d+)\) \{', new_region)
if not hdr:
    sys.exit('emitted main loop header not found')
hdr_span, hdr_step = int(hdr.group(1)) + 1, int(hdr.group(2))
if hdr_span != batch or hdr_step != batch:
    sys.exit('emitted header %s/%s does not match batch %d'
             % (hdr_span, hdr_step, batch))
body_ks = [int(x) for x in re.findall(
    r'weighted_mul16_scaled16\(A16\[m \+ (\d+)\]', new_region)]
if body_ks[:batch] != list(range(batch)):
    sys.exit('emitted loop body indices are not 0..%d' % (batch - 1))
tail_ks = body_ks[batch:]
if tail_ks != list(range(tail)):
    sys.exit('emitted tail indices are not 0..%d' % (tail - 1))

m_sim = 0
while m_sim + hdr_span - 1 < blocks:
    m_sim += hdr_step
covered = []
for start in range(0, m_sim, hdr_step):
    covered.extend(start + k for k in range(hdr_span))
covered.extend(m_sim + k for k in tail_ks)
if sorted(covered) != list(range(blocks)) or len(set(covered)) != blocks:
    sys.exit('coverage proof failed for batch=%d: visits=%d unique=%d'
             % (batch, len(covered), len(set(covered))))

# The whole source must contain exactly batch + tail pointwise calls, and no
# leftover last-block call from the original region.
if s.count('weighted_mul16_scaled16(A16[') != batch + tail:
    sys.exit('source has %d pointwise call sites, expected %d'
             % (s.count('weighted_mul16_scaled16(A16['), batch + tail))
if 'weighted_mul16_scaled16(A16[BLOCKS - 1]' in s:
    sys.exit('leftover explicit last-block pointwise call')

if l2u2:
    old = ('    for (v = 0; v < 11; v++) {\n'
           '        for (cbase = 0; cbase < BS; cbase += 8) {')
    new = ('    for (v = 0; v < 11; v++) {\n'
           '#pragma GCC unroll 2\n'
           '        for (cbase = 0; cbase < BS; cbase += 8) {')
    if s.count(old) != 2:
        sys.exit('expected 2 layer-2 loops, found %d' % s.count(old))
    s = s.replace(old, new)

open(core, 'w').write(s)

here = os.path.dirname(os.path.abspath(__file__))
subprocess.check_call(['python3', os.path.join(here, 'add_stage_marks.py'), core])
subprocess.check_call(['python3', os.path.join(here, 'patch_strict_flags.py'),
                       os.path.join(dst, 'build.sh')])
for f in ('build_stage_profile.sh',):
    shutil.copy2(os.path.join(here, 'base_local', f), os.path.join(dst, f))
for f in ('shared/profile_core_stages.c', 'shared/stage_profile_ids.h'):
    shutil.copy2(os.path.join(here, 'base_local', f), os.path.join(dst, f))

print('made %s: batch=%d l2u2=%d calls=%d tail=%d (all 77 blocks covered)'
      % (dst, batch, l2u2, batch + tail, tail))
