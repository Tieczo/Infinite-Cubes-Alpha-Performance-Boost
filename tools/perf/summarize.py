# Test-only: summarises run.sh logs. For each log: FRAMETIMES of the measuring window and the mean of the PROF
# blocks that lie wholly inside it (the first block after MEASURE is skipped). Groups by label without the
# trailing -N (run number) and prints mean and range of the fps per group.
# Usage: summarize.py LOG... [--zones]
import sys, re, collections, statistics
args = [a for a in sys.argv[1:] if not a.startswith('--')]
zones = '--zones' in sys.argv
def parse(path):
    lines = open(path, errors='replace').read().splitlines()
    measured = False
    skip = True
    blocks = []
    cur = None
    ft = None
    hits = 0
    combat = False
    endpos = None
    for l in lines:
        m = re.match(r'CAMSTATE nachAnlauf yaw=\S+ pitch=\S+ x=([-\d.]+) y=([-\d.]+) z=([-\d.]+)', l)
        if m:
            endpos = tuple(float(v) for v in m.groups())
        if l.startswith('PLAYERHIT'):
            hits += 1
        if l.startswith('RUN ') and 'combat=1' in l:
            combat = True
        if l.startswith('MEASURE'):
            measured = True
            continue
        m = re.match(r'FRAMETIMES n=(\d+) mean=([\d.]+)ms fps=([\d.]+) p50=([\d.]+)ms p99=([\d.]+)ms p999=([\d.]+)ms max=([\d.]+)ms', l)
        if m:
            ft = dict(n=int(m.group(1)), fps=float(m.group(3)), p50=float(m.group(4)), p99=float(m.group(5)),
                      p999=float(m.group(6)), max=float(m.group(7)))
        if not l.startswith('PROF'):
            continue
        m = re.match(r'PROF frames=(\d+) fps=([\d.]+) frame=([\d.]+)ms creatures=(\d+)', l)
        if m:
            cur = None
            if measured:
                if skip:
                    skip = False
                    continue
                cur = dict(fps=float(m.group(2)), frame=float(m.group(3)), creatures=int(m.group(4)), z={}, c={})
                blocks.append(cur)
            continue
        if cur is None:
            continue
        m = re.match(r'PROF   (.+?)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)%\s+(\d+)', l)
        if m and not l.startswith('PROF   Bereich'):
            cur['z'][m.group(1).strip()] = (float(m.group(2)), float(m.group(3)), int(m.group(6)))
            continue
        m = re.match(r'PROF   # (.+?)\s+([\d.]+)$', l)
        if m:
            cur['c'][m.group(1).strip()] = float(m.group(2))
    return ft, blocks, (hits if combat else None), endpos
groups = collections.OrderedDict()
endpositions = collections.OrderedDict()
for p in args:
    label = re.sub(r'\.log$', '', p.split('/')[-1])
    g = re.sub(r'-\d+$', '', label)
    ft, blocks, hits, endpos = parse(p)
    groups.setdefault(g, []).append((label, ft, blocks, hits))
    if endpos:
        endpositions[label] = endpos
# Combat runs: the end of the walk must be the same spot (within 2 blocks of the median of all given runs).
bad = set()
if endpositions:
    med = [statistics.median(v[i] for v in endpositions.values()) for i in range(3)]
    for label, e in endpositions.items():
        d = sum((e[i] - med[i]) ** 2 for i in range(3)) ** 0.5
        print('Anlauf-Ende %-22s x=%.1f y=%.1f z=%.1f  Abstand zum Median %.1f Blöcke%s' % (label, e[0], e[1], e[2], d,
              '  -> UNGÜLTIG' if d > 2.0 else ''))
        if d > 2.0:
            bad.add(label)
print('%-26s %4s %8s %8s %8s %8s %8s %6s %9s %9s' % ('Gruppe/Lauf', 'Bl.', 'fps', 'p50 ms', 'p99 ms', 'max ms', 'Bild ms', 'Leb.', 'Pflanzen', 'sichtb.Ch'))
for g, runs in groups.items():
    fpss = []
    for label, ft, blocks, hits in runs:
        if hits == 0:
            print('%-26s  UNGÜLTIG: Kampf-Lauf ohne Treffer auf den Spieler (PLAYERHIT 0)' % label)
            continue
        if label in bad:
            print('%-26s  UNGÜLTIG: Anlauf endet an anderer Stelle' % label)
            continue
        if not blocks:
            print('%-26s  (keine Messblöcke)' % label)
            continue
        fps = ft['fps'] if ft else statistics.mean(b['fps'] for b in blocks)
        fpss.append(fps)
        cre = statistics.mean(b['creatures'] for b in blocks)
        pl = statistics.mean(b['c'].get('Pflanzen durchlaufen (Lichter/Sound)', 0) for b in blocks)
        vis = statistics.mean(b['c'].get('sichtbare Chunks', 0) for b in blocks)
        frame = statistics.mean(b['frame'] for b in blocks)
        print('%-26s %4d %8.1f %8s %8s %8s %8.3f %6.0f %9.0f %9.0f' % (label, len(blocks), fps,
              '%.3f' % ft['p50'] if ft else '-', '%.3f' % ft['p99'] if ft else '-', '%.1f' % ft['max'] if ft else '-',
              frame, cre, pl, vis))
    if fpss:
        print('%-26s      Mittel %.1f fps, Spannweite %.1f..%.1f (%d Läufe)' % ('  = ' + g, statistics.mean(fpss), min(fpss), max(fpss), len(fpss)))
    if zones:
        acc = collections.OrderedDict()
        nb = 0
        for label, ft, blocks, hits in runs:
            if hits == 0 or label in bad:
                continue
            for b in blocks:
                nb += 1
                for k, v in b['z'].items():
                    a = acc.setdefault(k, [0.0, 0.0, 0.0])
                    a[0] += v[0]; a[1] += v[1]; a[2] += v[2]
        for k, a in acc.items():
            print('      %-34s %7.3f ms  (D3D %6.3f, Draws %6.0f)' % (k, a[0] / nb, a[1] / nb, a[2] / nb))
