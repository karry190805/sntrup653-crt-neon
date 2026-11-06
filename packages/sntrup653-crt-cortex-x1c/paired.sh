#!/usr/bin/env bash
# paired.sh A_EXE B_EXE OUTDIR [pairs]
# ABBA paired run of two core benchmarks on CPU_PIN (default 7).
#
# Every run is gated on core_correctness_hash: if the two arms disagree, the
# script prints HASH_MISMATCH and exits non-zero without reporting a ratio.
# Set EXPECT_HASH= to disable the reference check (both arms still must agree).
set -euo pipefail
A="$1"; B="$2"; out="$3"; pairs="${4:-8}"
pin="${CPU_PIN:-7}"
seed="${SEED:-20260901}"
rounds="${ROUNDS:-200}"
loops="${LOOPS:-2000}"
expect_hash="${EXPECT_HASH:-0xfe69baced6245e47}"
rm -rf "$out"; mkdir -p "$out"
: > "$out/paired.txt"
: > "$out/hashes.txt"
for i in $(seq 1 "$pairs"); do
  for order in "A B" "B A"; do
    for side in $order; do
      if [ "$side" = A ]; then exe="$A"; tag="A_${i}$( [ "$order" = "A B" ] && echo a || echo b )"; else exe="$B"; tag="B_${i}$( [ "$order" = "A B" ] && echo a || echo b )"; fi
      taskset -c "$pin" "$exe" "$rounds" "$loops" 1000 200 "$seed" >"$out/${tag}.log" 2>&1
      hash="$(awk -F= '/^core_correctness_hash=/{print $2}' "$out/${tag}.log")"
      if [ -z "$hash" ]; then
        echo "MISSING_HASH: $tag produced no core_correctness_hash" >&2
        exit 3
      fi
      printf '%s %s %s\n' "$side" "$tag" "$hash" >> "$out/hashes.txt"
      awk -v n="$side" -v t="$tag" '/mean_ms:/ {for(i=1;i<=NF;i++) if($i=="mean_ms:") {gsub(",","",$(i+1)); print n, t, $(i+1)}}' "$out/${tag}.log" >> "$out/paired.txt"
    done
  done
done
python3 - "$out/paired.txt" "$out/hashes.txt" "$expect_hash" <<'PY'
import sys, statistics, math
rows=[l.split() for l in open(sys.argv[1]) if l.strip()]
hrows=[l.split() for l in open(sys.argv[2]) if l.strip()]
expect=sys.argv[3].strip()
hA={r[2] for r in hrows if r[0]=='A'}
hB={r[2] for r in hrows if r[0]=='B'}
print('hash_A=%s hash_B=%s' % (','.join(sorted(hA)), ','.join(sorted(hB))))
if hA != hB:
    print('HASH_MISMATCH: arms compute different results; speedup is not reported')
    sys.exit(4)
if expect and hA != {expect}:
    print('HASH_UNEXPECTED: got %s expected %s; speedup is not reported'
          % (','.join(sorted(hA)), expect))
    sys.exit(5)
d={'A':[],'B':[]}
for side,tag,ms in rows: d[side].append(float(ms))
for k in d: print('%s n=%d mean=%.9f median=%.9f min=%.9f max=%.9f'%(k,len(d[k]),statistics.mean(d[k]),statistics.median(d[k]),min(d[k]),max(d[k])))
ratios=[]
by={}
for side,tag,ms in rows:
    base=tag[2:]
    by.setdefault(base,{})[side]=float(ms)
for base,v in by.items():
    if 'A' in v and 'B' in v:
        ratios.append(v['A']/v['B'])
print('pairs=%d'%len(ratios))
if ratios:
    m=statistics.mean(ratios); sd=statistics.stdev(ratios) if len(ratios)>1 else 0.0
    ci=1.96*sd/math.sqrt(len(ratios)) if len(ratios)>1 else 0.0
    wins=sum(1 for r in ratios if r>1)
    print('ratio B/A (B faster if >1) = %.6f  ci95=[%.6f, %.6f]  wins=%d/%d'%(m,m-ci,m+ci,wins,len(ratios)))
PY
