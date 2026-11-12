# Test-only: compares the WORLDHASH lines (run.sh WORLDHASH=1) of several logs: for every zone loaded in at
# least two of them, all hashes must be equal. Usage: worldhash_compare.py LOG...
import sys, re, collections
zones = collections.defaultdict(dict)  # (x, y) -> {log: hash}
for p in sys.argv[1:]:
    name = p.split('/')[-1]
    for l in open(p, errors='replace'):
        m = re.match(r'WORLDHASH (-?\d+) (-?\d+) ([0-9a-f]{16})', l)
        if m:
            zones[(int(m.group(1)), int(m.group(2)))][name] = m.group(3)
compared = differing = 0
for z, per in sorted(zones.items()):
    if len(per) < 2:
        continue
    compared += 1
    if len(set(per.values())) > 1:
        differing += 1
        groups = collections.defaultdict(list)
        for n, h in per.items():
            groups[h].append(n)
        print('Zone %d,%d unterschiedlich: %s' % (z[0], z[1], '; '.join('%s: %s' % (h, ', '.join(sorted(n))) for h, n in groups.items())))
print('Logs %d, Zonen verglichen %d (in mindestens 2 Logs), abweichend %d' % (len(sys.argv) - 1, compared, differing))
sys.exit(1 if differing else 0)
