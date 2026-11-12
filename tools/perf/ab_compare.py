# Test-only: compares the pictures of run.sh ABSHOTS=1 (alt1, neu1, alt2, neu2 as PPM) and prints a summary:
# alt1/alt2 must be equal (time really stopped), then alt vs neu = the change. Writes diff.ppm (red =
# differing pixel) when alt and neu differ.
import sys, os
d = sys.argv[1]
def load(n):
    with open(os.path.join(d, n + '.ppm'), 'rb') as f:
        data = f.read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]
def cmp(a, b):
    wa, ha, pa = load(a); wb, hb, pb = load(b)
    if (wa, ha) != (wb, hb):
        return None
    n = 0; mx = 0; diff = bytearray(pa)
    for i in range(0, len(pa), 3):
        if pa[i:i+3] != pb[i:i+3]:
            n += 1; mx = max(mx, max(abs(pa[i+k] - pb[i+k]) for k in range(3)))
            diff[i:i+3] = b'\xff\x00\x00'
    return n, mx, wa * ha, diff, (wa, ha)
res = {}
for a, b in (('alt1', 'alt2'), ('neu1', 'neu2'), ('alt1', 'neu1'), ('alt2', 'neu2')):
    r = cmp(a, b); res[(a, b)] = r
    print('%s/%s: %s' % (a, b, 'Größe verschieden' if r is None else '%d von %d Pixeln verschieden, max. %d' % (r[0], r[2], r[1])))
r = res[('alt1', 'neu1')]
if r and r[0]:
    with open(os.path.join(d, 'diff.ppm'), 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % r[4]); f.write(bytes(r[3]))
static = res[('alt1', 'alt2')] and res[('alt1', 'alt2')][0] == 0
same = all(res[k] and res[k][0] == 0 for k in res)
ctl = cmp('alt1', 'leer') if os.path.exists(os.path.join(d, 'leer.ppm')) else None
if ctl is not None:
    print('Gegenprobe alt1/leer (ohne die Region): %d Pixel verschieden' % ctl[0])
    if ctl[0] == 0:
        print('ERGEBNIS: UNGÜLTIG – die geänderte Region ist im Bild nicht zu sehen'); sys.exit(0)
print('ERGEBNIS:', 'PIXELGLEICH' if same else ('Szene nicht still (alt1≠alt2)' if not static else 'ABWEICHUNG alt/neu'))
