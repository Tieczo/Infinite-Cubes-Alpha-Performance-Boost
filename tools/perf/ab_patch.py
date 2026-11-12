# Test-only: A/B build for picture comparisons. Puts the old code of marked regions of
# GameControllerRender.cpp (from BASE_FILE) next to the new one, switchable at run time by g_abOld,
# and lets g_abFreeze stop the game time (update(0)). Usage: ab_patch.py <copy>/alpha BASE_FILE (build-measure.sh does it with AB_BASE=commit)
import sys
d, base_file = sys.argv[1], sys.argv[2]
R = d + '/src/cube/GameControllerRender.cpp'
new = open(R).read(); old = open(base_file).read()
# Regions: (start anchor, end anchor), both present in old and new; the code between is swapped.
regions = [
    # Your change's regions here, e.g. (from P2b, the stars):
    # ('    // The stars (x, y, z, size each) at night above water', '    // The sun: a textured quad 100 blocks along the light'),
]
assert regions, 'ab_patch.py: list the regions of your change first'
# AB_FROM=n: only the regions from n on (when the other regions no longer differ or the build carries prof.h).
import os
regions = regions[int(os.environ.get('AB_FROM', '0')):]
# Old code that must keep new bookkeeping valid when switched back and forth at run time (A/B only).
old_fixups = [
    ('            chunk.lightCount = 0;\n', '            chunk.lightCount = 0;\n            chunk.lightsWritten = 16;\n'),
]
def cut(s, a, b):
    i = s.index(a); assert s.count(a) == 1, a[:60]
    j = s.index(b, i)
    return s[i:j]
for a, b in regions:
    o = cut(old, a, b); n = cut(new, a, b)
    for fa, fb in old_fixups:
        if fa in o and 'lightsWritten' in new:
            o = o.replace(fa, fb)
    if o == n:
        print('gleich (nicht umgeschaltet):', a.strip()[:50]); continue
    # g_abOld 2 draws neither (control: the region must be visible in the picture).
    new = new.replace(n, '    if (g_abOld == 1) {\n' + o + '    } else if (g_abOld == 0) {\n' + n + '    }\n')
    print('A/B:', a.strip()[:50])
# Stopped time also stops the wall clock of the render pass (the stars over stunned creatures turn by it).
clock = 'gameTime()' if 'int startTime = gameTime();' in new else 'timeGetTime()'
a = '    int startTime = %s;\n' % clock
assert new.count(a) == 1
new = new.replace(a, '    static int abTime;\n    if (!g_abFreeze)\n        abTime = %s;\n    int startTime = abTime;\n' % clock)
new = '#include <vector>\nint g_abOld = 0, g_abFreeze = 0;\n' + new
open(R, 'w').write(new)
M = d + '/platform/linux/main.cpp'
m = open(M).read()
a = '    g_game->update(now - s_lastTime);\n'
assert m.count(a) == 1
m = m.replace(a, '    g_game->update(g_abFreeze ? 0 : now - s_lastTime);\n')
open(M, 'w').write(m)
