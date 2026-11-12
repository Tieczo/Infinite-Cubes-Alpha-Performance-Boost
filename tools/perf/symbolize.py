# Test-only: turns prof.h samples (S pc zone, M maps lines) into per-zone function and render-line tables.
# Usage: symbolize.py SAMPLES CUBE_BINARY [TOPN]
import sys, subprocess, bisect, collections, re, os
samples_path, cube_bin = sys.argv[1], sys.argv[2]
topn = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith('--') else 25
# --lines=NAME: self time by source line for the functions whose name contains NAME (default GameController::render)
line_func = 'GameController::render'
for a in sys.argv[3:]:
    if a.startswith('--lines='):
        line_func = a[8:]
ZONES = ["Rest", "GameCtrl::update", "World::update", "Chunk-Durchlauf", "Setup", "Lichter", "Wolken",
         "Landschaft", "Pflanzen nah", "Items/Objekte", "Lebewesen", "Partikel/Geschosse", "Wasser",
         "Fern/Blend", "UI", "Present"]
maps = []
samples = []
for line in open(samples_path):
    if line.startswith('M '):
        parts = line[2:].split()
        if len(parts) < 6:
            continue
        a, b = parts[0].split('-')
        maps.append((int(a, 16), int(b, 16), int(parts[2], 16), parts[5]))
    elif line.startswith('S '):
        _, pc, z = line.split()
        samples.append((int(pc, 16), int(z)))
maps.sort()
cube_real = os.path.realpath(cube_bin)
starts = [m[0] for m in maps]
segcache = {}
def segments(path):
    if path not in segcache:
        segs = []
        try:
            out = subprocess.run(['readelf', '-lW', path], capture_output=True, text=True).stdout
            for l in out.splitlines():
                l = l.split()
                if l and l[0] == 'LOAD':
                    segs.append((int(l[1], 16), int(l[2], 16), int(l[4], 16)))  # offset, vaddr, filesz
        except Exception:
            pass
        segcache[path] = segs
    return segcache[path]
def locate(pc):
    i = bisect.bisect_right(starts, pc) - 1
    if i < 0 or pc >= maps[i][1]:
        return None, None
    a, b, off, path = maps[i]
    if os.path.basename(path) == 'Cube':
        path = cube_real  # the test folder's Cube may have been replaced since; use the given build
    fo = pc - a + off
    for so, sv, sz in segments(path):
        if so <= fo < so + sz:
            return path, fo - so + sv
    return path, fo
symcache = {}
def symtab(path):
    if path not in symcache:
        syms = []
        for flags in (['-C', '--defined-only'], ['-C', '-D', '--defined-only']):
            out = subprocess.run(['nm'] + flags + [path], capture_output=True, text=True).stdout
            for l in out.splitlines():
                m = re.match(r'([0-9a-f]+) [tTwW] (.*)', l)
                if m:
                    syms.append((int(m.group(1), 16), m.group(2)))
            if syms:
                break
        syms.sort()
        symcache[path] = ([s[0] for s in syms], [s[1] for s in syms])
    return symcache[path]
def funcname(path, addr):
    addrs, names = symtab(path)
    i = bisect.bisect_right(addrs, addr) - 1
    if i < 0:
        return '?'
    return names[i]
total = len(samples)
by_zone = collections.Counter()
func_zone = collections.defaultdict(collections.Counter)
func_all = collections.Counter()
render_addrs = collections.Counter()
lib_all = collections.Counter()
for pc, z in samples:
    path, addr = locate(pc)
    by_zone[z] += 1
    if path is None:
        name = '[unbekannt]'
        lib = '?'
    else:
        lib = os.path.basename(path)
        name = funcname(path, addr)
        if name == '?':
            name = '[' + lib + ']'
        elif not os.path.basename(path).startswith('Cube'):
            name = name + '  [' + lib + ']'
        if os.path.basename(path) == 'Cube' and line_func in name:
            render_addrs[addr] += 1
    lib_all[lib] += 1
    func_zone[z][name] += 1
    func_all[name] += 1
def short(n, w=90):
    n = re.sub(r'\(.*\)', '()', n)
    return n if len(n) <= w else n[:w - 3] + '...'
print('Samples: %d (1 ms Hauptthread-CPU je Sample)' % total)
print('\n== Bibliotheken ==')
for lib, c in lib_all.most_common(10):
    print('  %5.1f%%  %s' % (100.0 * c / total, lib))
print('\n== Top-Funktionen gesamt ==')
for n, c in func_all.most_common(topn):
    print('  %5.1f%%  %s' % (100.0 * c / total, short(n)))
for z, zc in sorted(by_zone.items(), key=lambda x: -x[1]):
    if zc < total * 0.02:
        continue
    print('\n== Zone %s: %.1f%% ==' % (ZONES[z] if z < len(ZONES) else z, 100.0 * zc / total))
    for n, c in func_zone[z].most_common(12):
        print('  %5.1f%%  %s' % (100.0 * c / total, short(n)))
if render_addrs:
    addrs = list(render_addrs.keys())
    out = subprocess.run(['addr2line', '-e', cube_bin, '-i', '-C', '-f'] + ['%x' % a for a in addrs[:20000]],
                         capture_output=True, text=True).stdout.splitlines()
    # -i prints several (func, file:line) pairs per address; split by re-running per address is slow,
    # so use the single-line form for the outermost frame instead.
    out = subprocess.run(['addr2line', '-e', cube_bin] + ['%x' % a for a in addrs[:20000]],
                         capture_output=True, text=True).stdout.splitlines()
    lines = collections.Counter()
    for a, l in zip(addrs, out):
        lines[os.path.basename(l.split(' ')[0])] += render_addrs[a]
    print('\n== %s, Selbstzeit nach Quellzeile (innerste Zeile) ==' % line_func)
    for l, c in lines.most_common(30):
        print('  %5.1f%%  %s' % (100.0 * c / total, l))
